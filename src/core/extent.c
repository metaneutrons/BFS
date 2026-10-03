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
#ifdef BFS_PERF_PROBE
#include "../amiga/perf_probe.h"
#endif

/* ── B+tree ops ────────────────────────────────────────────── */

static const bfs_btree_ops_t extent_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_extent_val_t),
    .cache_key_order = true,
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
        uint64_t backup_offset = bfs_sb_backup_offset(et->fs->sb);
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

static void extent_clear_inline(bfs_extent_tree_t *et)
{
    et->inline_start = BFS_BLK_NULL;
    et->inline_length = 0;
    et->inline_crc = 0;
}

/* An inline extent read from disk is untrusted until its range is checked
 * against the device and the reserved regions, like a tree record. */
static bool inline_valid(const bfs_extent_tree_t *et)
{
    return et->tree.root == BFS_BLK_NULL &&
           extent_range_valid(et, et->inline_start, et->inline_length) &&
           (!et->data_checksums || et->inline_length == 1);
}

/* Replace the inline extent by a tree holding the given sorted records. The
 * tree is complete or absent: a failed build leaves the inline extent. */
static bfs_err_t extent_convert(bfs_extent_tree_t *et, const uint32_t *keys,
                                const bfs_extent_val_t *vals, uint32_t count)
{
    bfs_err_t err = bfs_btree_create_root_leaf(&et->tree, keys, vals, count);
    /* A latched reclamation error can follow a published root. */
    if (et->tree.root != BFS_BLK_NULL) extent_clear_inline(et);
    return err;
}

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_extent_init(bfs_extent_tree_t *et, bfs_bio_t *bio,
                      bfs_freespace_t *fs, bfs_blk_t root, uint64_t txn_id)
{
    if (!et || !bio || !fs) return BFS_ERR_INVAL;
    et->fs = fs;
    et->data_checksums = false;
    extent_clear_inline(et);
    return bfs_btree_init(&et->tree, bio, bfs_freespace_allocator(fs),
                    &extent_ops, root, txn_id);
}

bfs_err_t bfs_extent_open(bfs_extent_tree_t *et, bfs_bio_t *bio,
                          bfs_freespace_t *fs, const bfs_inode_t *inode,
                          uint64_t txn_id)
{
    if (!et || !inode) return BFS_ERR_INVAL;
    bfs_blk_t root = bfs_be32(inode->extent_root);
    if (!(bfs_be32(inode->flags) & BFS_INODE_FLAG_INLINE_EXTENT)) {
        if (inode->inline_length != 0 || inode->inline_crc32 != 0)
            return BFS_ERR_CORRUPT;
        return bfs_extent_init(et, bio, fs, root, txn_id);
    }
    bfs_err_t err = bfs_extent_init(et, bio, fs, BFS_BLK_NULL, txn_id);
    if (err != BFS_OK) return err;
    et->inline_start = root;
    et->inline_length = bfs_be32(inode->inline_length);
    et->inline_crc = bfs_be32(inode->inline_crc32);
    return inline_valid(et) ? BFS_OK : BFS_ERR_CORRUPT;
}

void bfs_extent_store(const bfs_extent_tree_t *et, bfs_inode_t *inode)
{
    uint32_t flags = bfs_be32(inode->flags) & ~BFS_INODE_FLAG_INLINE_EXTENT;
    if (et->inline_length != 0) {
        flags |= BFS_INODE_FLAG_INLINE_EXTENT;
        inode->extent_root = bfs_be32(et->inline_start);
        inode->inline_length = bfs_be32(et->inline_length);
        inode->inline_crc32 = bfs_be32(et->inline_crc);
    } else {
        inode->extent_root = bfs_be32(et->tree.root);
        inode->inline_length = 0;
        inode->inline_crc32 = 0;
    }
    inode->flags = bfs_be32(flags);
}

bool bfs_extent_matches(const bfs_extent_tree_t *et, const bfs_inode_t *inode)
{
    bfs_inode_t stored = *inode;
    bfs_extent_store(et, &stored);
    return stored.extent_root == inode->extent_root &&
           stored.flags == inode->flags &&
           stored.inline_length == inode->inline_length &&
           stored.inline_crc32 == inode->inline_crc32;
}

/* ── Lookup ────────────────────────────────────────────────── */

bfs_err_t bfs_extent_lookup(bfs_extent_tree_t *et, uint32_t file_block,
                              bfs_blk_t *disk_block)
{
    uint32_t run;
    return bfs_extent_lookup_run(et, file_block, disk_block, &run);
}

bfs_err_t bfs_extent_lookup_run(bfs_extent_tree_t *et, uint32_t file_block,
                                bfs_blk_t *disk_block, uint32_t *run_blocks)
{
    if (!et || !disk_block || !run_blocks) return BFS_ERR_INVAL;
    if (et->inline_length != 0) {
        if (!inline_valid(et)) return BFS_ERR_CORRUPT;
        if (file_block >= et->inline_length) return BFS_ERR_NOTFOUND;
        *disk_block = et->inline_start + file_block;
        *run_blocks = et->inline_length - file_block;
        return BFS_OK;
    }
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
    *run_blocks = len - (file_block - fb);
    return BFS_OK;
}

/* ── Full value lookup (for CRC verification) ──────────────── */

bfs_err_t bfs_extent_lookup_val(bfs_extent_tree_t *et, uint32_t file_block,
                                  bfs_extent_val_t *val_out)
{
    if (!et || !val_out) return BFS_ERR_INVAL;
    if (et->inline_length != 0) {
        if (!inline_valid(et)) return BFS_ERR_CORRUPT;
        val_out->disk_block = bfs_be32(et->inline_start);
        val_out->length = bfs_be32(et->inline_length);
        val_out->data_crc32 = bfs_be32(et->inline_crc);
        return file_block < et->inline_length ? BFS_OK : BFS_ERR_NOTFOUND;
    }
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
    if (et->inline_length != 0) {
        /* As in the tree, the CRC belongs to the record starting here. */
        if (!inline_valid(et)) return BFS_ERR_CORRUPT;
        if (file_block != 0) return BFS_ERR_NOTFOUND;
        et->inline_crc = crc;
        return BFS_OK;
    }
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

/* Map a new run. The first run at block 0 of an empty file is stored inline;
 * a contiguous continuation extends it; anything else needs a tree. */
static bfs_err_t extent_map(bfs_extent_tree_t *et, uint32_t file_block,
                            bfs_blk_t disk_block, uint32_t count, uint32_t crc)
{
    if (et->inline_length != 0) {
        if (!inline_valid(et)) return BFS_ERR_CORRUPT;
        uint32_t length = et->inline_length;
        if (file_block < length) return BFS_ERR_EXISTS;
        if (!et->data_checksums && file_block == length &&
            disk_block == et->inline_start + length &&
            count <= UINT32_MAX - length) {
            et->inline_length = length + count;
            return BFS_OK;
        }
        const uint32_t keys[2] = { 0, bfs_be32(file_block) };
        const bfs_extent_val_t vals[2] = {
            { bfs_be32(et->inline_start), bfs_be32(length), bfs_be32(et->inline_crc) },
            { bfs_be32(disk_block), bfs_be32(count), bfs_be32(crc) },
        };
        return extent_convert(et, keys, vals, 2);
    }
    if (et->tree.root == BFS_BLK_NULL && file_block == 0) {
        et->inline_start = disk_block;
        et->inline_length = count;
        et->inline_crc = crc;
        return BFS_OK;
    }
    return extent_insert_raw(et, file_block, disk_block, count, crc);
}

bfs_err_t bfs_extent_map_block(bfs_extent_tree_t *et, uint32_t file_block,
                               bfs_blk_t disk_block, uint32_t crc)
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.extent_map_calls++;
#endif
    if (!et || !extent_range_valid(et, disk_block, 1))
        return BFS_ERR_INVAL;
    return extent_map(et, file_block, disk_block, 1, crc);
}

bfs_err_t bfs_extent_map_run(bfs_extent_tree_t *et, uint32_t file_block,
                             bfs_blk_t disk_block, uint32_t count)
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.extent_map_calls++;
#endif
    if (!et || et->data_checksums || count == 0 ||
        count - 1 > UINT32_MAX - file_block ||
        !extent_range_valid(et, disk_block, count))
        return BFS_ERR_INVAL;
    return extent_map(et, file_block, disk_block, count, 0);
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

/* Remap one block of the inline extent. A one-block extent stays inline;
 * otherwise the extent splits around the block into a tree. */
static bfs_err_t inline_remap_block(bfs_extent_tree_t *et, uint32_t file_block,
                                    bfs_blk_t new_disk_block, uint32_t crc,
                                    bfs_blk_t *old_disk_block_out)
{
    if (!inline_valid(et)) return BFS_ERR_CORRUPT;
    uint32_t length = et->inline_length;
    if (file_block >= length) return BFS_ERR_NOTFOUND;
    bfs_blk_t old_disk = et->inline_start + file_block;
    if (old_disk_block_out) *old_disk_block_out = old_disk;
    if (old_disk == new_disk_block)
        return bfs_extent_update_crc(et, file_block, crc);
    if (length == 1) {
        et->inline_start = new_disk_block;
        et->inline_crc = crc;
        return BFS_OK;
    }

    uint32_t keys[3];
    bfs_extent_val_t vals[3];
    uint32_t count = 0;
    if (file_block > 0) {
        keys[count] = 0;
        vals[count] = (bfs_extent_val_t){ bfs_be32(et->inline_start),
                                          bfs_be32(file_block), 0 };
        count++;
    }
    keys[count] = bfs_be32(file_block);
    vals[count] = (bfs_extent_val_t){ bfs_be32(new_disk_block), bfs_be32(1),
                                      bfs_be32(crc) };
    count++;
    if (file_block + 1 < length) {
        keys[count] = bfs_be32(file_block + 1);
        vals[count] = (bfs_extent_val_t){ bfs_be32(old_disk + 1),
                                          bfs_be32(length - file_block - 1), 0 };
        count++;
    }
    return extent_convert(et, keys, vals, count);
}

bfs_err_t bfs_extent_remap_block_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                     bfs_blk_t new_disk_block, uint32_t crc,
                                     bfs_blk_t *old_disk_block_out)
{
    if (!et || !extent_range_valid(et, new_disk_block, 1))
        return BFS_ERR_INVAL;
    if (et->inline_length != 0)
        return inline_remap_block(et, file_block, new_disk_block, crc,
                                  old_disk_block_out);
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

    bfs_err_t err = extent_map(et, file_block, dblk, count, 0);
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

/* The combined walk passes one context to both callbacks. */
typedef struct {
    extent_walk_ctx_t *ec;
    bfs_node_walk_cb node_cb;
    void *ctx;
} walk_ctx_both_t;

static bool extent_walk_both_block_cb(const void *key, const void *val, void *c)
{
    return extent_walk_block_cb(key, val, ((walk_ctx_both_t *)c)->ec);
}

static void extent_walk_node_cb(bfs_blk_t blk, void *c)
{
    walk_ctx_both_t *both = (walk_ctx_both_t *)c;
    both->node_cb(blk, both->ctx);
}

bfs_err_t bfs_extent_walk(bfs_bio_t *bio, bfs_freespace_t *fsp, uint64_t txn_id,
                          const bfs_inode_t *inode, bfs_node_walk_cb node_cb,
                          bfs_node_walk_cb block_cb, void *ctx)
{
    bfs_extent_tree_t et;
    bfs_err_t err = bfs_extent_open(&et, bio, fsp, inode, txn_id);
    if (err != BFS_OK) return err;
    if (et.inline_length != 0) {
        for (uint32_t i = 0; block_cb && i < et.inline_length; i++)
            block_cb(et.inline_start + i, ctx);
        return BFS_OK;
    }
    if (et.tree.root == BFS_BLK_NULL) return BFS_OK;

    /* One pass reads each node once; blocks and nodes may interleave. */
    extent_walk_ctx_t ec = { &et, block_cb, ctx, BFS_OK };
    if (!block_cb) return node_cb ? bfs_btree_walk_nodes(&et.tree, node_cb, ctx) : BFS_OK;
    if (!node_cb) {
        err = bfs_btree_scan(&et.tree, NULL, extent_walk_block_cb, &ec);
        return err != BFS_OK ? err : ec.err;
    }
    walk_ctx_both_t both = { .ec = &ec, .node_cb = node_cb, .ctx = ctx };
    err = bfs_btree_walk(&et.tree, extent_walk_node_cb, extent_walk_both_block_cb, &both);
    return err != BFS_OK ? err : ec.err;
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
    if (et->inline_length != 0) {
        if (!inline_valid(et)) return BFS_ERR_CORRUPT;
        if (from_block >= et->inline_length) return BFS_OK;
        bfs_blk_t start = et->inline_start + from_block;
        uint32_t release = et->inline_length - from_block;
        bfs_err_t err = extent_release_preflight(et, release);
        if (err != BFS_OK) return err;
        if (from_block == 0) extent_clear_inline(et);
        else et->inline_length = from_block;
        return extent_release_blocks(et, start, release);
    }
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
