/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Per-file extent tree
 *
 * B+tree key: uint32_t file_block (big-endian)
 * B+tree value: bfs_extent_val_t {disk_block, length, data_crc32}
 *
 * Each entry maps a range of file blocks: file_block .. file_block+length-1
 * maps to disk_block .. disk_block+length-1.
 */

#include "bfs_extent.h"
#include <string.h>

/* ── B+tree ops ────────────────────────────────────────────── */

static const bfs_btree_ops_t extent_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_extent_val_t),
};

static bool extent_range_valid(const bfs_extent_tree_t *et, bfs_blk_t disk,
                               uint32_t len)
{
    if (!et || !et->tree.bio || !et->fs || len == 0)
        return false;
    bfs_bio_t *bio = et->tree.bio;
    bfs_blk_t first = bfs_data_start_block(bio->block_size);
    if (disk < first || disk >= bio->block_count ||
        len > bio->block_count - disk)
        return false;

    bfs_blk_t end = disk + len;
    if (et->fs->sb) {
        uint64_t backup_offset =
            ((uint64_t)bfs_be32(et->fs->sb->sb_backup_offset_hi) << 32) |
            bfs_be32(et->fs->sb->sb_backup_offset_lo);
        bfs_blk_t backup = (bfs_blk_t)(backup_offset / bio->block_size);
        if (backup >= disk && backup < end)
            return false;

        uint32_t emergency_count = bfs_be32(et->fs->sb->emergency_count);
        if (emergency_count > BFS_EMERGENCY_POOL_SIZE)
            return false;
        for (uint32_t i = 0; i < emergency_count; i++) {
            bfs_blk_t blk = bfs_be32(et->fs->sb->emergency_pool[i]);
            if (blk >= disk && blk < end)
                return false;
        }
    }
    if (et->fs->reserve_count > BFS_ALLOC_RESERVE_SIZE)
        return false;
    for (uint32_t i = 0; i < et->fs->reserve_count; i++) {
        bfs_blk_t blk = et->fs->reserve[i];
        if (blk >= disk && blk < end)
            return false;
    }
    return true;
}

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_extent_init(bfs_extent_tree_t *et, bfs_bio_t *bio,
                      bfs_freespace_t *fs, bfs_blk_t root, uint64_t txn_id)
{
    if (!et || !bio || !fs) return BFS_ERR_INVAL;
    et->fs = fs;
    et->data_checksums = false;
    return bfs_btree_init(&et->tree, bio, bfs_freespace_allocator(fs),
                    &extent_ops, root, txn_id);
}

/* ── Lookup ────────────────────────────────────────────────── */

bfs_err_t bfs_extent_lookup(bfs_extent_tree_t *et, uint32_t file_block,
                              bfs_blk_t *disk_block)
{
    if (!et || !disk_block) return BFS_ERR_INVAL;
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    uint32_t key = bfs_be32(file_block);
    uint32_t found_key;
    bfs_extent_val_t found_val;

    bfs_err_t err = bfs_btree_search_floor(&et->tree, &key, &found_key, &found_val);
    if (err != BFS_OK) return err;

    uint32_t fb = bfs_be32(found_key);
    uint32_t len = bfs_be32(found_val.length);
    bfs_blk_t disk = bfs_be32(found_val.disk_block);
    if (!extent_range_valid(et, disk, len))
        return BFS_ERR_CORRUPT;
    if (file_block < fb || file_block - fb >= len) return BFS_ERR_NOTFOUND;
    *disk_block = disk + (file_block - fb);
    return BFS_OK;
}

/* ── Full value lookup (for CRC verification) ──────────────── */

bfs_err_t bfs_extent_lookup_val(bfs_extent_tree_t *et, uint32_t file_block,
                                  bfs_extent_val_t *val_out)
{
    if (!et || !val_out) return BFS_ERR_INVAL;
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;
    uint32_t key = bfs_be32(file_block);
    uint32_t found_key;
    bfs_err_t err = bfs_btree_search_floor(&et->tree, &key, &found_key, val_out);
    if (err != BFS_OK) return err;
    uint32_t fb = bfs_be32(found_key);
    uint32_t len = bfs_be32(val_out->length);
    bfs_blk_t disk = bfs_be32(val_out->disk_block);
    if (!extent_range_valid(et, disk, len))
        return BFS_ERR_CORRUPT;
    return (file_block >= fb && file_block - fb < len) ? BFS_OK : BFS_ERR_NOTFOUND;
}

/* ── CRC update (delete + re-insert with new CRC) ─────────── */

bfs_err_t bfs_extent_update_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                  uint32_t crc)
{
    if (!et || !et->tree.bio) return BFS_ERR_INVAL;
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

bfs_err_t bfs_extent_map_block(bfs_extent_tree_t *et, uint32_t file_block,
                               bfs_blk_t disk_block, uint32_t crc)
{
    if (!et || !extent_range_valid(et, disk_block, 1))
        return BFS_ERR_INVAL;
    return extent_insert_raw(et, file_block, disk_block, 1, crc);
}

static bfs_err_t extent_rollback_remap(bfs_extent_tree_t *et, uint32_t file_block,
                                      uint32_t found_key,
                                      const bfs_extent_val_t *found_val,
                                      bool inserted_left, bool inserted_mid)
{
    bfs_err_t result = BFS_OK;
    if (inserted_mid) {
        uint32_t key = bfs_be32(file_block);
        result = bfs_btree_delete(&et->tree, &key);
    }
    if (inserted_left) {
        bfs_err_t err = bfs_btree_delete(&et->tree, &found_key);
        if (result == BFS_OK) result = err;
    }
    bfs_err_t err = bfs_btree_insert(&et->tree, &found_key, found_val);
    if (result == BFS_OK) result = err;
    /* Tell the caller that neither the old nor new mapping has proven ownership. */
    if (result != BFS_OK) et->tree.free_sink_err = result;
    return result;
}

bfs_err_t bfs_extent_remap_block_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                     bfs_blk_t new_disk_block, uint32_t crc,
                                     bfs_blk_t *old_disk_block_out)
{
    if (!et || !extent_range_valid(et, new_disk_block, 1))
        return BFS_ERR_INVAL;
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
    if (!extent_range_valid(et, disk, len))
        return BFS_ERR_CORRUPT;
    if (file_block < fb || file_block - fb >= len)
        return BFS_ERR_NOTFOUND;

    uint32_t offset = file_block - fb;
    bfs_blk_t old_disk = disk + offset;
    if (old_disk_block_out) *old_disk_block_out = old_disk;
    if (old_disk == new_disk_block)
        return bfs_extent_update_crc(et, file_block, crc);

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

    err = extent_insert_raw(et, file_block, new_disk_block, 1, crc);
    if (err != BFS_OK) goto rollback;
    inserted_mid = 1;

    if (right_len > 0) {
        err = extent_insert_raw(et, file_block + 1, old_disk + 1, right_len, 0);
        if (err != BFS_OK) goto rollback;
    }

    return BFS_OK;

rollback: {
        bfs_err_t rollback_err = extent_rollback_remap(et, file_block, found_key,
            &found_val, inserted_left != 0, inserted_mid != 0);
        return rollback_err == BFS_OK ? err : rollback_err;
    }
}

bfs_err_t bfs_extent_remap_block(bfs_extent_tree_t *et, uint32_t file_block,
                                  bfs_blk_t new_disk_block,
                                  bfs_blk_t *old_disk_block_out)
{
    return bfs_extent_remap_block_crc(et, file_block, new_disk_block, 0,
                                      old_disk_block_out);
}

/* ── Append ────────────────────────────────────────────────── */

bfs_err_t bfs_extent_append(bfs_extent_tree_t *et, uint32_t file_block,
                              uint32_t count, bfs_blk_t *disk_block_out)
{
    if (!et || !et->fs || !et->tree.bio || count == 0 ||
        count - 1 > UINT32_MAX - file_block)
        return BFS_ERR_INVAL;
    if (et->data_checksums && count != 1) return BFS_ERR_INVAL;
    /* Allocate physical blocks */
    bfs_blk_t dblk = bfs_freespace_alloc(et->fs, count);
    if (dblk == BFS_BLK_NULL)
        return et->fs->last_error == BFS_OK ? BFS_ERR_NOSPC : et->fs->last_error;

    /* Insert extent entry */
    uint32_t key = bfs_be32(file_block);
    bfs_extent_val_t val = {
        .disk_block = bfs_be32(dblk),
        .length = bfs_be32(count),
    };

    bfs_err_t err = bfs_btree_insert(&et->tree, &key, &val);
    if (err != BFS_OK) {
        bfs_err_t cleanup_err = bfs_freespace_free(et->fs, dblk, count);
        return cleanup_err == BFS_OK ? err : cleanup_err;
    }

    if (disk_block_out) *disk_block_out = dblk;
    return BFS_OK;
}

/* ── Whole-tree walk (delete / snapshot refcount share this) ─ */

typedef struct {
    bfs_extent_tree_t *tree;
    bfs_node_walk_cb cb;
    void *ctx;
    bfs_err_t err;
} extent_walk_ctx_t;

static bool extent_walk_block_cb(const void *key, const void *val, void *c)
{
    extent_walk_ctx_t *ec = (extent_walk_ctx_t *)c;
    (void)key;
    const bfs_extent_val_t *ev = (const bfs_extent_val_t *)val;
    bfs_blk_t disk = bfs_be32(ev->disk_block);
    uint32_t len = bfs_be32(ev->length);
    /* Same untrusted-length guard as bfs_extent_truncate_batch. */
    if (!extent_range_valid(ec->tree, disk, len)) {
        ec->err = BFS_ERR_CORRUPT;
        return false;
    }
    for (uint32_t i = 0; i < len; i++)
        ec->cb(disk + i, ec->ctx);
    return true;
}

bfs_err_t bfs_extent_walk(bfs_bio_t *bio, bfs_freespace_t *fsp, uint64_t txn_id,
                          bfs_blk_t root, bfs_node_walk_cb node_cb,
                          bfs_node_walk_cb block_cb, void *ctx)
{
    if (root == BFS_BLK_NULL) return BFS_OK;
    bfs_extent_tree_t et;
    bfs_err_t err = bfs_extent_init(&et, bio, fsp, root, txn_id);
    if (err != BFS_OK) return err;

    if (block_cb) {
        extent_walk_ctx_t ec = { &et, block_cb, ctx, BFS_OK };
        err = bfs_btree_scan(&et.tree, NULL, extent_walk_block_cb, &ec);
        if (err == BFS_OK) err = ec.err;
        if (err != BFS_OK) return err;
    }
    if (node_cb) {
        err = bfs_btree_walk_nodes(&et.tree, node_cb, ctx);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

/* ── Truncate ──────────────────────────────────────────────── */

/* Collect extents to delete */
typedef struct {
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

    if (tc->count < 256) {
        tc->keys[tc->count] = fb;
        tc->dblks[tc->count] = bfs_be32(ev->disk_block);
        tc->lens[tc->count] = bfs_be32(ev->length);
        tc->count++;
    }
    return tc->count < 256;
}

static bfs_err_t extent_release_preflight(bfs_extent_tree_t *et, uint32_t count)
{
    bfs_free_sink_t *sink = &et->tree.free_sink;
    if (!sink->defer)
        return et->fs ? BFS_OK : BFS_ERR_INVAL;
    if (!sink->headroom || sink->capacity == 0)
        return BFS_ERR_INVAL;

    uint64_t need = (uint64_t)count + BFS_BTREE_MAX_OP_FREES;
    if (need > UINT32_MAX)
        return BFS_ERR_NOSPC;
    if (sink->reserve) {
        bfs_err_t err = sink->reserve(sink->ctx, (uint32_t)need);
        if (err != BFS_OK) return err;
    } else if (need > sink->capacity) {
        return BFS_ERR_NOSPC;
    }
    if (need > sink->headroom(sink->ctx))
        return BFS_ERR_AGAIN;
    return BFS_OK;
}

static bfs_err_t extent_release_blocks(bfs_extent_tree_t *et, bfs_blk_t start,
                                       uint32_t count)
{
    bfs_free_sink_t *sink = &et->tree.free_sink;
    if (!sink->defer)
        return bfs_freespace_free(et->fs, start, count);

    for (uint32_t i = 0; i < count; i++) {
        bfs_err_t err = sink->defer(sink->ctx, start + i);
        if (err != BFS_OK)
            return err;
    }
    return BFS_OK;
}

/* Truncate at most max_ops extents. Returns BFS_OK when done, or
 * BFS_ERR_AGAIN if more work remains (caller should sync and retry). */
bfs_err_t bfs_extent_truncate_batch(bfs_extent_tree_t *et, uint32_t from_block,
                                     uint32_t max_ops)
{
    if (!et || !et->tree.bio || max_ops == 0)
        return BFS_ERR_INVAL;
    if (et->tree.root == BFS_BLK_NULL)
        return BFS_OK;

    uint32_t ops_done = 0;
    uint32_t start_key = bfs_be32(from_block);
    trunc_ctx_t tc;

    /* If the cut lands inside a multi-block extent, retain its prefix and
     * release only the suffix before scanning entries that start at the cut. */
    uint32_t found_key;
    bfs_extent_val_t found_val;
    bfs_err_t err = bfs_btree_search_floor(&et->tree, &start_key, &found_key,
                                           &found_val);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND)
        return err;
    if (err == BFS_OK) {
        uint32_t fb = bfs_be32(found_key);
        uint32_t len = bfs_be32(found_val.length);
        bfs_blk_t dblk = bfs_be32(found_val.disk_block);
        if (!extent_range_valid(et, dblk, len))
            return BFS_ERR_CORRUPT;
        if (fb < from_block && from_block - fb < len) {
            uint32_t keep = from_block - fb;
            uint32_t release = len - keep;
            err = extent_release_preflight(et, release);
            if (err != BFS_OK)
                return err;
            found_val.length = bfs_be32(keep);
            err = bfs_btree_update(&et->tree, &found_key, &found_val);
            if (err != BFS_OK)
                return err;
            err = extent_release_blocks(et, dblk + keep, release);
            if (err != BFS_OK)
                return err;
            ops_done++;
            if (ops_done >= max_ops)
                return BFS_ERR_AGAIN;
        }
    }

    do {
        tc.count = 0;
        err = bfs_btree_scan(&et->tree, &start_key, trunc_cb, &tc);
        if (err != BFS_OK)
            return err;
        for (uint32_t i = 0; i < tc.count && ops_done < max_ops; i++) {
            uint32_t len = tc.lens[i];
            bfs_blk_t dblk = tc.dblks[i];
            /* Reject a corrupt/implausible extent read from disk before its
             * length drives the free loop below. Legitimate extents are tiny
             * (the writer appends one block at a time); a length past the device,
             * or so long that it plus one delete's worst-case node frees could
             * never fit the deferred-free queue (so a retry could never help),
             * can only be corruption. */
            if (!extent_range_valid(et, dblk, len)) {
                return BFS_ERR_CORRUPT;
            }
            /* All-or-nothing per extent: the btree_delete below COWs up to
             * BFS_BTREE_MAX_OP_FREES old extent-tree nodes, then this extent's
             * `len` data blocks are deferred — all into the same queue. If that
             * whole batch won't fit right now, sync and retry rather than overflow
             * the queue with the (un-pre-checked) node frees. */
            err = extent_release_preflight(et, len);
            if (err != BFS_OK)
                return err;
            uint32_t key = bfs_be32(tc.keys[i]);
            err = bfs_btree_delete(&et->tree, &key);
            if (err != BFS_OK)
                return err;
            /* Don't return to the free tree now (would cause COW recursion);
             * defer to the post-commit reclaim queue. */
            err = extent_release_blocks(et, dblk, len);
            if (err != BFS_OK)
                return err;
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
