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

/* ── B+tree ops for free space tree ────────────────────────── */

static int blk_compare(const void *a, const void *b)
{
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t free_ops = {
    .key_compare = blk_compare,
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
        if (fs->emergency_count && *fs->emergency_count > 0) {
            uint32_t idx = bfs_be32(*fs->emergency_count) - 1;
            bfs_blk_t blk = bfs_be32(fs->emergency_pool[idx]);
            *fs->emergency_count = bfs_be32(idx);
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

    return bfs_btree_init(&fs->tree, bio, &fs->iface, &free_ops, free_tree_root, txn_id);
}

/* ── Add free extent ───────────────────────────────────────── */

bfs_err_t bfs_freespace_add(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count)
{
    /* Bootstrap: if reserve is empty and tree is empty, seed the reserve
     * from the extent being added so the B+tree can allocate nodes. */
    if (fs->reserve_count == 0 && fs->tree.root == BFS_BLK_NULL) {
        while (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE && count > 0) {
            fs->reserve[fs->reserve_count++] = start++;
            count--;
        }
        if (count == 0) {
            fs->total_free += fs->reserve_count;
            return BFS_OK;
        }
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
    uint32_t start = bfs_be32(*(const uint32_t *)key);
    uint32_t len = bfs_be32(*(const uint32_t *)val);

    if (len >= sc->need) {
        sc->found_start = start;
        sc->found_len = len;
        return false; /* stop scanning */
    }
    return true;
}

/* ── Allocate ──────────────────────────────────────────────── */

bfs_blk_t bfs_freespace_alloc(bfs_freespace_t *fs, uint32_t count)
{
    if (count == 0 || fs->total_free < count)
        return BFS_BLK_NULL;

    fs->in_alloc = true;

    /* First-fit scan starting from roving pointer */
    alloc_scan_ctx_t sc = { .need = count, .found_start = BFS_BLK_NULL, .found_len = 0 };

    uint32_t start_key = bfs_be32(fs->roving);
    bfs_btree_scan(&fs->tree, &start_key, alloc_scan_cb, &sc);

    /* If not found, wrap around from beginning */
    if (sc.found_start == BFS_BLK_NULL && fs->roving > 0) {
        bfs_btree_scan(&fs->tree, NULL, alloc_scan_cb, &sc);
    }

    if (sc.found_start == BFS_BLK_NULL) {
        fs->in_alloc = false;
        return BFS_BLK_NULL;
    }

    /* Remove the old extent */
    uint32_t old_key = bfs_be32(sc.found_start);
    bfs_btree_delete(&fs->tree, &old_key);

    /* If extent is larger than needed, re-insert the remainder */
    bfs_blk_t result = sc.found_start;
    if (sc.found_len > count) {
        uint32_t rem_start = bfs_be32(sc.found_start + count);
        uint32_t rem_len = bfs_be32(sc.found_len - count);
        bfs_btree_insert(&fs->tree, &rem_start, &rem_len);
    }

    fs->total_free -= count;
    fs->roving = result + count;
    fs->in_alloc = false;

    /* Refill reserve after top-level operation */
    bfs_freespace_refill_reserve(fs);

    return result;
}

/* ── Free ──────────────────────────────────────────────────── */

bfs_err_t bfs_freespace_free(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count)
{
    if (count == 0) return BFS_OK;

    fs->in_alloc = true;

    bfs_blk_t merge_start = start;
    uint32_t merge_len = count;

    /* Right merge: check if there's an extent starting at (start + count) */
    {
        uint32_t right_key = bfs_be32(start + count);
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

    fs->total_free += count;
    fs->in_alloc = false;

    bfs_freespace_refill_reserve(fs);

    return err;
}

/* ── Reserve pool management ───────────────────────────────── */

bfs_err_t bfs_freespace_refill_reserve(bfs_freespace_t *fs)
{
    while (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE && fs->total_free > 0) {
        fs->in_alloc = true;

        /* Find any free extent using floor search from max key */
        uint32_t max_key = bfs_be32(UINT32_MAX);
        uint32_t found_key, found_len;
        if (bfs_btree_search_floor(&fs->tree, &max_key, &found_key, &found_len) != BFS_OK) {
            fs->in_alloc = false;
            break;
        }

        uint32_t blk_start = bfs_be32(found_key);
        uint32_t blk_len = bfs_be32(found_len);

        /* Remove the extent */
        bfs_btree_delete(&fs->tree, &found_key);

        /* Tree operations may have freed blocks back to reserve.
         * Re-check capacity before adding. */
        if (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE) {
            fs->reserve[fs->reserve_count++] = blk_start;
            if (blk_len > 1) {
                uint32_t rem_key = bfs_be32(blk_start + 1);
                uint32_t rem_val = bfs_be32(blk_len - 1);
                bfs_btree_insert(&fs->tree, &rem_key, &rem_val);
            }
            fs->total_free--;
        } else {
            /* Reserve full from freed COW blocks — put extent back and stop */
            bfs_btree_insert(&fs->tree, &found_key, &found_len);
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
