/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Free space allocator (B+tree based)
 *
 * The free space tree stores extents keyed by starting block number.
 * Key = uint32_t block_nr (big-endian), Value = uint32_t length (big-endian).
 *
 * Self-hosting strategy:
 *   The B+tree COW path calls alloc->alloc() for new node blocks.
 *   When in_alloc is true (we're already inside an allocation),
 *   we serve from the reserve pool to avoid infinite recursion.
 *   After each top-level alloc/free, we refill the reserve.
 */

#include "bfs_alloc.h"
#include <string.h>

#define BFS_ALLOC_RESERVE_REFILL_TARGET (BFS_ALLOC_RESERVE_SIZE * 3 / 4)

/* ── B+tree ops for free space tree ────────────────────────── */

static const bfs_btree_ops_t free_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
};

/* ── Single-block allocator interface (for B+tree COW) ─────── */

static bfs_blk_t iface_alloc(bfs_allocator_t *a)
{
    bfs_freespace_t *fs = (bfs_freespace_t *)a->ctx;

    /* If we're inside an allocation, use the reserve pool */
    if (fs->in_alloc) {
        if (fs->reserve_count > 0)
            return fs->reserve[--fs->reserve_count];
        /* Last resort: emergency pool (breaks COW recursion) */
        if (fs->sb && bfs_be32(fs->sb->emergency_count) > 0) {
            uint32_t idx = bfs_be32(fs->sb->emergency_count) - 1;
            bfs_blk_t blk = bfs_be32(fs->sb->emergency_pool[idx]);
            fs->sb->emergency_count = bfs_be32(idx);
            return blk;
        }
        return BFS_BLK_NULL;
    }

    return bfs_freespace_alloc(fs, 1);
}

static void iface_free(bfs_allocator_t *a, bfs_blk_t blk)
{
    bfs_freespace_t *fs = (bfs_freespace_t *)a->ctx;

    /* If we're inside an allocation, stash in reserve */
    if (fs->in_alloc) {
        if (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE)
            fs->reserve[fs->reserve_count++] = blk;
        return;
    }

    bfs_freespace_free(fs, blk, 1);
}

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_freespace_init(bfs_freespace_t *fs, bfs_bio_t *bio,
                         bfs_blk_t free_tree_root, uint64_t txn_id)
{
    memset(fs, 0, sizeof(*fs));
    fs->iface.alloc = iface_alloc;
    fs->iface.dealloc = iface_free;
    fs->iface.ctx = fs;
    fs->roving = 0;
    fs->total_free = 0;
    fs->in_alloc = false;
    fs->reserve_count = 0;

    bfs_err_t err = bfs_btree_init(&fs->tree, bio, &fs->iface, &free_ops,
                                   free_tree_root, txn_id);
    if (err == BFS_OK)
        fs->tree.txn_id_ptr = &fs->tree.txn_id_fallback;
    return err;
}

/* ── Add free extent ───────────────────────────────────────── */

bfs_err_t bfs_freespace_add(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count)
{
    /* Bootstrap: if reserve is empty and tree is empty, seed the reserve
     * from the extent being added so the B+tree can allocate nodes. */
    if (fs->reserve_count == 0 && fs->tree.root == BFS_BLK_NULL) {
        fs->reserve[fs->reserve_count++] = start++;
        count--;
        if (count == 0)
            return BFS_OK;
    }

    fs->in_alloc = true;
    uint32_t key = bfs_be32(start);
    uint32_t val = bfs_be32(count);
    bfs_err_t err = bfs_btree_insert(&fs->tree, &key, &val);
    fs->in_alloc = false;
    if (err == BFS_OK)
        fs->total_free += count;
    return err;
}

/* ── Scan callback for first-fit allocation ────────────────── */

typedef struct {
    uint32_t need;
    bfs_blk_t found_start;
    uint32_t found_len;
} alloc_scan_ctx_t;

static bool alloc_scan_cb(const void *key, const void *val, void *ctx)
{
    alloc_scan_ctx_t *sc = (alloc_scan_ctx_t *)ctx;
    uint32_t start = bfs_load_be32(key);
    uint32_t len = bfs_load_be32(val);

    if (len >= sc->need) {
        sc->found_start = start;
        sc->found_len = len;
        return false; /* stop scanning */
    }
    return true;
}

static bfs_blk_t alloc_one_from_largest(bfs_freespace_t *fs)
{
    uint32_t max_key = bfs_be32(UINT32_MAX);
    uint32_t found_key, found_len;
    if (bfs_btree_search_floor(&fs->tree, &max_key, &found_key, &found_len) != BFS_OK)
        return BFS_BLK_NULL;

    uint32_t blk_start = bfs_be32(found_key);
    uint32_t blk_len = bfs_be32(found_len);
    if (blk_len == 0)
        return BFS_BLK_NULL;

    bfs_blk_t result = blk_start + blk_len - 1;
    bfs_err_t err;
    if (blk_len == 1) {
        err = bfs_btree_delete(&fs->tree, &found_key);
    } else {
        uint32_t new_len = bfs_be32(blk_len - 1);
        err = bfs_btree_update(&fs->tree, &found_key, &new_len);
    }
    if (err != BFS_OK)
        return BFS_BLK_NULL;

    fs->total_free--;
    fs->roving = result + 1;
    return result;
}

/* ── Allocate ──────────────────────────────────────────────── */

bfs_blk_t bfs_freespace_alloc(bfs_freespace_t *fs, uint32_t count)
{
    if (count == 0 || fs->total_free < count)
        return BFS_BLK_NULL;

    fs->in_alloc = true;

    if (count == 1) {
        bfs_blk_t result = alloc_one_from_largest(fs);
        if (result != BFS_BLK_NULL) {
            fs->in_alloc = false;
            bfs_freespace_refill_reserve(fs);
            return result;
        }
    }

    /* First-fit scan starting from roving pointer */
    alloc_scan_ctx_t sc = { .need = count, .found_start = BFS_BLK_NULL, .found_len = 0 };

    uint32_t start_key = bfs_be32(fs->roving);
    bfs_err_t scan_err = bfs_btree_scan(&fs->tree, &start_key, alloc_scan_cb, &sc);
    if (scan_err != BFS_OK) {
        fs->in_alloc = false;
        return BFS_BLK_NULL;
    }

    /* If not found, wrap around from beginning */
    if (sc.found_start == BFS_BLK_NULL && fs->roving > 0) {
        scan_err = bfs_btree_scan(&fs->tree, NULL, alloc_scan_cb, &sc);
        if (scan_err != BFS_OK) {
            fs->in_alloc = false;
            return BFS_BLK_NULL;
        }
    }

    if (sc.found_start == BFS_BLK_NULL) {
        if (count == 1 && fs->reserve_count > 0) {
            bfs_blk_t result = fs->reserve[--fs->reserve_count];
            if (fs->total_free > 0)
                fs->total_free--;
            fs->in_alloc = false;
            return result;
        }
        fs->in_alloc = false;
        return BFS_BLK_NULL;
    }

    /* Remove the old extent */
    uint32_t old_key = bfs_be32(sc.found_start);
    uint32_t old_len = bfs_be32(sc.found_len);
    bfs_err_t err = bfs_btree_delete(&fs->tree, &old_key);
    if (err != BFS_OK) {
        fs->in_alloc = false;
        return BFS_BLK_NULL;
    }

    /* If extent is larger than needed, re-insert the remainder */
    bfs_blk_t result = sc.found_start;
    if (sc.found_len > count) {
        uint32_t rem_start = bfs_be32(sc.found_start + count);
        uint32_t rem_len = bfs_be32(sc.found_len - count);
        err = bfs_btree_insert(&fs->tree, &rem_start, &rem_len);
        if (err != BFS_OK) {
            bfs_btree_insert(&fs->tree, &old_key, &old_len);
            fs->in_alloc = false;
            return BFS_BLK_NULL;
        }
    }

    fs->total_free -= count;
    fs->roving = result + count;
    fs->in_alloc = false;

    /* Refill reserve after top-level operation */
    bfs_freespace_refill_reserve(fs);

    return result;
}

/* ── Free ──────────────────────────────────────────────────── */

typedef struct {
    bfs_blk_t end;
    bool overlap;
} overlap_scan_ctx_t;

static bool overlap_scan_cb(const void *key, const void *val, void *ctx)
{
    (void)val;
    overlap_scan_ctx_t *oc = (overlap_scan_ctx_t *)ctx;
    oc->overlap = bfs_load_be32(key) < oc->end;
    return false;
}

static bfs_err_t return_to_emergency_pool(bfs_freespace_t *fs, bfs_blk_t blk,
                                          bool *handled)
{
    *handled = false;
    if (!fs->sb)
        return BFS_OK;

    uint32_t ec = bfs_be32(fs->sb->emergency_count);
    uint32_t found_idx = BFS_EMERGENCY_POOL_SIZE;
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        if (bfs_be32(fs->sb->emergency_pool[i]) == blk) {
            if (i < ec) {
                *handled = true;
                return BFS_OK;
            }
            found_idx = i;
            break;
        }
    }

    if (found_idx < BFS_EMERGENCY_POOL_SIZE && ec < BFS_EMERGENCY_POOL_SIZE) {
        uint32_t displaced = fs->sb->emergency_pool[ec];
        fs->sb->emergency_pool[ec] = bfs_be32(blk);
        fs->sb->emergency_pool[found_idx] = displaced;
        fs->sb->emergency_count = bfs_be32(ec + 1);
        *handled = true;
    }
    return BFS_OK;
}

bfs_err_t bfs_freespace_free(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count)
{
    if (count == 0) return BFS_OK;
    bfs_blk_t end = start + count;
    if (end < start) return BFS_ERR_INVAL;

    if (count == 1) {
        bool handled;
        bfs_err_t err = return_to_emergency_pool(fs, start, &handled);
        if (handled || err != BFS_OK)
            return err;
    }

    fs->in_alloc = true;

    bfs_blk_t merge_start = start;
    uint32_t merge_len = count;

    if (start > 0) {
        uint32_t pred_search = bfs_be32(start);
        uint32_t pred_key, pred_len;
        if (bfs_btree_search_floor(&fs->tree, &pred_search, &pred_key, &pred_len) == BFS_OK) {
            uint32_t pk = bfs_be32(pred_key);
            uint32_t pl = bfs_be32(pred_len);
            if (pk <= start && pk + pl > start) {
                fs->in_alloc = false;
                return BFS_ERR_EXISTS;
            }
        }
    }

    overlap_scan_ctx_t oc = { .end = end, .overlap = false };
    uint32_t overlap_key = bfs_be32(start);
    bfs_err_t scan_err = bfs_btree_scan(&fs->tree, &overlap_key, overlap_scan_cb, &oc);
    if (scan_err != BFS_OK) {
        fs->in_alloc = false;
        return scan_err;
    }
    if (oc.overlap) {
        fs->in_alloc = false;
        return BFS_ERR_EXISTS;
    }

    /* Right merge: check if there's an extent starting at (start + count) */
    {
        uint32_t right_key = bfs_be32(end);
        uint32_t right_len;
        if (bfs_btree_search(&fs->tree, &right_key, &right_len) == BFS_OK) {
            merge_len += bfs_be32(right_len);
            bfs_btree_delete(&fs->tree, &right_key);
        }
    }

    /* Left merge: find the largest key < merge_start using floor search */
    if (merge_start > 0) {
        uint32_t search_key = bfs_be32(merge_start - 1);
        uint32_t pred_key;
        uint32_t pred_len;
        if (bfs_btree_search_floor(&fs->tree, &search_key, &pred_key, &pred_len) == BFS_OK) {
            uint32_t pk = bfs_be32(pred_key);
            uint32_t pl = bfs_be32(pred_len);
            if (pk + pl == merge_start) {
                /* Left neighbor ends exactly at our start — merge */
                uint32_t del_key = pred_key; /* already big-endian */
                bfs_btree_delete(&fs->tree, &del_key);
                merge_start = pk;
                merge_len += pl;
            }
        }
    }

    /* Insert the (possibly merged) extent */
    uint32_t ins_key = bfs_be32(merge_start);
    uint32_t ins_val = bfs_be32(merge_len);
    bfs_err_t err = bfs_btree_insert(&fs->tree, &ins_key, &ins_val);

    if (err == BFS_OK)
        fs->total_free += count;
    fs->in_alloc = false;

    if (err == BFS_OK)
        bfs_freespace_refill_reserve(fs);

    return err;
}

/* ── Reserve pool management ───────────────────────────────── */

bfs_err_t bfs_freespace_refill_reserve(bfs_freespace_t *fs)
{
    if (fs->global_reserve == UINT32_MAX)
        return BFS_OK;
    while (fs->reserve_count < BFS_ALLOC_RESERVE_REFILL_TARGET &&
           fs->total_free > fs->global_reserve + 1) {
        fs->in_alloc = true;

        bfs_blk_t blk = alloc_one_from_largest(fs);
        if (blk == BFS_BLK_NULL) {
            fs->in_alloc = false;
            break;
        }

        if (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE) {
            fs->reserve[fs->reserve_count++] = blk;
        } else {
            bfs_freespace_free(fs, blk, 1);
            fs->in_alloc = false;
            break;
        }

        fs->in_alloc = false;
    }
    return BFS_OK;
}

/* ── Accessor ──────────────────────────────────────────────── */

bfs_allocator_t *bfs_freespace_allocator(bfs_freespace_t *fs)
{
    return &fs->iface;
}
