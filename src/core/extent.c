/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Per-file extent tree
 *
 * B+tree key: uint32_t file_block (big-endian)
 * B+tree value: bfs_extent_val_t {disk_block, length} (8 bytes, big-endian)
 *
 * Each entry maps a range of file blocks: file_block .. file_block+length-1
 * maps to disk_block .. disk_block+length-1.
 */

#include "bfs_extent.h"
#include "bfs_fs.h"
#include <string.h>

/* ── B+tree ops ────────────────────────────────────────────── */

static int extent_key_cmp(const void *a, const void *b)
{
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t extent_ops = {
    .key_compare = extent_key_cmp,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_extent_val_t),
};

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_extent_init(bfs_extent_tree_t *et, bfs_bio_t *bio,
                      bfs_freespace_t *fs, bfs_blk_t root, uint64_t txn_id)
{
    et->fs = fs;
    et->data_checksums = false;
    return bfs_btree_init(&et->tree, bio, bfs_freespace_allocator(fs),
                    &extent_ops, root, txn_id);
}

/* ── Lookup ────────────────────────────────────────────────── */

bfs_err_t bfs_extent_lookup(bfs_extent_tree_t *et, uint32_t file_block,
                              bfs_blk_t *disk_block)
{
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    uint32_t key = bfs_be32(file_block);
    uint32_t found_key;
    bfs_extent_val_t found_val;

    if (bfs_btree_search_floor(&et->tree, &key, &found_key, &found_val) == BFS_OK) {
        uint32_t fb = bfs_be32(found_key);
        uint32_t len = bfs_be32(found_val.length);
        if (file_block < fb + len) {
            *disk_block = bfs_be32(found_val.disk_block) + (file_block - fb);
            return BFS_OK;
        }
    }
    return BFS_ERR_NOTFOUND;
}

/* ── Full value lookup (for CRC verification) ──────────────── */

bfs_err_t bfs_extent_lookup_val(bfs_extent_tree_t *et, uint32_t file_block,
                                  bfs_extent_val_t *val_out)
{
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    uint32_t key = bfs_be32(file_block);
    return bfs_btree_search(&et->tree, &key, val_out);
}

/* ── CRC update (delete + re-insert with new CRC) ─────────── */

bfs_err_t bfs_extent_update_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                  uint32_t crc)
{
    uint32_t key = bfs_be32(file_block);
    bfs_extent_val_t val;
    bfs_err_t err = bfs_btree_search(&et->tree, &key, &val);
    if (err != BFS_OK) return err;
    val.data_crc32 = bfs_be32(crc);
    return bfs_btree_update(&et->tree, &key, &val);
}

/* ── Append ────────────────────────────────────────────────── */

bfs_err_t bfs_extent_append(bfs_extent_tree_t *et, uint32_t file_block,
                              uint32_t count, bfs_blk_t *disk_block_out)
{
    /* Allocate physical blocks */
    bfs_blk_t dblk = bfs_freespace_alloc(et->fs, count);
    if (dblk == BFS_BLK_NULL) return BFS_ERR_NOSPC;

    /* Insert extent entry */
    uint32_t key = bfs_be32(file_block);
    bfs_extent_val_t val = {
        .disk_block = bfs_be32(dblk),
        .length = bfs_be32(count),
    };

    bfs_err_t err = bfs_btree_insert(&et->tree, &key, &val);
    if (err != BFS_OK) {
        bfs_freespace_free(et->fs, dblk, count);
        return err;
    }

    if (disk_block_out) *disk_block_out = dblk;
    return BFS_OK;
}

/* ── Truncate ──────────────────────────────────────────────── */

/* Collect extents to delete */
typedef struct {
    uint32_t from;
    uint32_t keys[256];
    bfs_blk_t dblks[256];
    uint32_t lens[256];
    uint32_t count;
} trunc_ctx_t;

static bool trunc_cb(const void *key, const void *val, void *ctx)
{
    trunc_ctx_t *tc = (trunc_ctx_t *)ctx;
    uint32_t fb = bfs_be32(*(const uint32_t *)key);
    const bfs_extent_val_t *ev = (const bfs_extent_val_t *)val;

    if (fb >= tc->from && tc->count < 256) {
        tc->keys[tc->count] = fb;
        tc->dblks[tc->count] = bfs_be32(ev->disk_block);
        tc->lens[tc->count] = bfs_be32(ev->length);
        tc->count++;
    }
    return tc->count < 256;
}

/* Truncate at most max_ops extents. Returns BFS_OK when done, or
 * BFS_ERR_AGAIN if more work remains (caller should sync and retry). */
bfs_err_t bfs_extent_truncate_batch(bfs_extent_tree_t *et, uint32_t from_block,
                                     uint32_t max_ops)
{
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_OK;

    uint32_t ops_done = 0;
    uint32_t start_key = bfs_be32(from_block);
    trunc_ctx_t tc;

    do {
        tc.from = from_block;
        tc.count = 0;
        bfs_btree_scan(&et->tree, &start_key, trunc_cb, &tc);
        for (uint32_t i = 0; i < tc.count && ops_done < max_ops; i++) {
            bfs_fs_t *fs_ctx = (bfs_fs_t *)et->tree.fs_ctx;
            if (fs_ctx && fs_ctx->pending_count + tc.lens[i] > BFS_PENDING_FREES_MAX) {
                return BFS_ERR_AGAIN;
            }
            uint32_t key = bfs_be32(tc.keys[i]);
            bfs_btree_delete(&et->tree, &key);
            /* Don't return to free tree now (would cause COW recursion).
             * Instead, stash in pending_frees for reclaim during sync. */
            if (fs_ctx) {
                for (uint32_t b = 0; b < tc.lens[i]; b++)
                    fs_ctx->pending_frees[fs_ctx->pending_count++] = tc.dblks[i] + b;
            } else {
                bfs_freespace_free(et->fs, tc.dblks[i], tc.lens[i]);
            }
            ops_done++;
        }
        if (ops_done >= max_ops && tc.count > 0)
            return BFS_ERR_AGAIN;
    } while (tc.count > 0);

    return BFS_OK;
}

bfs_err_t bfs_extent_truncate(bfs_extent_tree_t *et, uint32_t from_block)
{
    /* Unbatched: process all at once (used when plenty of space) */
    return bfs_extent_truncate_batch(et, from_block, UINT32_MAX);
}
