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

static const bfs_btree_ops_t extent_ops = {
    .key_compare = bfs_cmp_be32,
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

/* ── Single-block remap ───────────────────────────────────── */

static bfs_err_t extent_insert_raw(bfs_extent_tree_t *et, uint32_t file_block,
                                   bfs_blk_t disk_block, uint32_t length,
                                   uint32_t crc)
{
    uint32_t key = bfs_be32(file_block);
    bfs_extent_val_t val = {
        .disk_block = bfs_be32(disk_block),
        .length = bfs_be32(length),
        .data_crc32 = bfs_be32(crc),
    };
    return bfs_btree_insert(&et->tree, &key, &val);
}

bfs_err_t bfs_extent_remap_block(bfs_extent_tree_t *et, uint32_t file_block,
                                   bfs_blk_t new_disk_block,
                                   bfs_blk_t *old_disk_block_out)
{
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    uint32_t search_key = bfs_be32(file_block);
    uint32_t found_key;
    bfs_extent_val_t found_val;
    bfs_err_t err = bfs_btree_search_floor(&et->tree, &search_key,
                                           &found_key, &found_val);
    if (err != BFS_OK) return err;

    uint32_t fb = bfs_be32(found_key);
    uint32_t len = bfs_be32(found_val.length);
    bfs_blk_t disk = bfs_be32(found_val.disk_block);
    if (file_block < fb || file_block - fb >= len)
        return BFS_ERR_NOTFOUND;

    uint32_t offset = file_block - fb;
    bfs_blk_t old_disk = disk + offset;
    if (old_disk_block_out) *old_disk_block_out = old_disk;
    if (old_disk == new_disk_block)
        return BFS_OK;

    err = bfs_btree_delete(&et->tree, &found_key);
    if (err != BFS_OK) return err;

    uint32_t left_len = offset;
    uint32_t right_len = len - offset - 1;
    uint32_t inserted_left = 0, inserted_mid = 0;

    if (left_len > 0) {
        err = extent_insert_raw(et, fb, disk, left_len, 0);
        if (err != BFS_OK) goto rollback;
        inserted_left = 1;
    }

    err = extent_insert_raw(et, file_block, new_disk_block, 1, 0);
    if (err != BFS_OK) goto rollback;
    inserted_mid = 1;

    if (right_len > 0) {
        err = extent_insert_raw(et, file_block + 1, old_disk + 1, right_len, 0);
        if (err != BFS_OK) goto rollback;
    }

    return BFS_OK;

rollback:
    if (inserted_mid) {
        uint32_t key = bfs_be32(file_block);
        bfs_btree_delete(&et->tree, &key);
    }
    if (inserted_left) {
        uint32_t key = bfs_be32(fb);
        bfs_btree_delete(&et->tree, &key);
    }
    bfs_btree_insert(&et->tree, &found_key, &found_val);
    return err;
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
    uint32_t fb = bfs_load_be32(key);
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
            uint32_t len = tc.lens[i];
            bfs_blk_t dblk = tc.dblks[i];
            /* Reject a corrupt/implausible extent read from disk before its
             * length drives the free loop below. Legitimate extents are tiny
             * (the writer appends one block at a time); a length that exceeds
             * the pending_frees capacity or runs past the device can only be
             * corruption, and must never index past the fixed pending_frees[]. */
            if (len == 0 || len > BFS_PENDING_FREES_MAX ||
                dblk >= et->tree.bio->block_count ||
                len > et->tree.bio->block_count - dblk) {
                return BFS_ERR_CORRUPT;
            }
            /* Overflow-safe headroom check — the old pending_count + len form
             * wraps when len is large and would defeat the guard. */
            if (fs_ctx && len > BFS_PENDING_FREES_MAX - fs_ctx->pending_count) {
                return BFS_ERR_AGAIN;
            }
            uint32_t key = bfs_be32(tc.keys[i]);
            bfs_btree_delete(&et->tree, &key);
            /* Don't return to free tree now (would cause COW recursion).
             * Instead, stash in pending_frees for reclaim during sync. */
            if (fs_ctx) {
                for (uint32_t b = 0; b < len; b++)
                    fs_ctx->pending_frees[fs_ctx->pending_count++] = dblk + b;
            } else {
                bfs_freespace_free(et->fs, dblk, len);
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
