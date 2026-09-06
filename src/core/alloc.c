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
 *   Refill before top-level alloc/free so maintenance cannot turn a completed
 *   operation into an ambiguous error return.
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

/* ── Single-block allocator interface (for B+tree COW) ─────── */

static bfs_blk_t iface_alloc(bfs_allocator_t *a)
{
    if (!a || !a->ctx) return BFS_BLK_NULL;
    bfs_freespace_t *fs = (bfs_freespace_t *)a->ctx;

    /* If we're inside an allocation, use the reserve pool */
    if (fs->in_alloc) {
        if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) {
            fs->last_error = BFS_ERR_CORRUPT;
            return BFS_BLK_NULL;
        }
        if (fs->reserve_count > 0) {
            bfs_blk_t blk = fs->reserve[fs->reserve_count - 1];
            if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count) {
                fs->last_error = BFS_ERR_CORRUPT;
                return BFS_BLK_NULL;
            }
            fs->reserve_count--;
            return blk;
        }
        /* Last resort: emergency pool (breaks COW recursion) */
        if (fs->sb) {
            uint32_t count = bfs_be32(fs->sb->emergency_count);
            if (count > BFS_EMERGENCY_POOL_SIZE) {
                fs->last_error = BFS_ERR_CORRUPT;
                return BFS_BLK_NULL;
            }
            if (count == 0) goto no_space;
            uint32_t idx = count - 1;
            bfs_blk_t blk = bfs_be32(fs->sb->emergency_pool[idx]);
            if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count) {
                fs->last_error = BFS_ERR_CORRUPT;
                return BFS_BLK_NULL;
            }
            fs->sb->emergency_count = bfs_be32(idx);
            return blk;
        }
no_space:
        fs->last_error = BFS_ERR_NOSPC;
        return BFS_BLK_NULL;
    }

    return bfs_freespace_alloc(fs, 1);
}

static bfs_err_t iface_free(bfs_allocator_t *a, bfs_blk_t blk)
{
    if (!a || !a->ctx) return BFS_ERR_INVAL;
    bfs_freespace_t *fs = (bfs_freespace_t *)a->ctx;
    if (!fs->tree.bio || blk == BFS_BLK_NULL ||
        blk >= fs->tree.bio->block_count)
        return BFS_ERR_INVAL;

    /* If we're inside an allocation, stash in reserve */
    if (fs->in_alloc) {
        if (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE) {
            fs->reserve[fs->reserve_count++] = blk;
            return BFS_OK;
        }
        return BFS_ERR_NOSPC;
    }

    return bfs_freespace_free(fs, blk, 1);
}

static bfs_err_t iface_error(bfs_allocator_t *a)
{
    if (!a || !a->ctx) return BFS_ERR_INVAL;
    bfs_freespace_t *fs = (bfs_freespace_t *)a->ctx;
    return fs->last_error == BFS_OK ? BFS_ERR_NOSPC : fs->last_error;
}

/* ── Init ──────────────────────────────────────────────────── */

bfs_err_t bfs_freespace_init(bfs_freespace_t *fs, bfs_bio_t *bio,
                         bfs_blk_t free_tree_root, uint64_t txn_id)
{
    if (!fs || !bio) return BFS_ERR_INVAL;
    memset(fs, 0, sizeof(*fs));
    fs->iface.alloc = iface_alloc;
    fs->iface.dealloc = iface_free;
    fs->iface.error = iface_error;
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
    if (!fs || !fs->tree.bio || count == 0 || start == BFS_BLK_NULL ||
        start >= fs->tree.bio->block_count ||
        count > fs->tree.bio->block_count - start)
        return BFS_ERR_INVAL;
    if (count > UINT32_MAX - fs->total_free) return BFS_ERR_CORRUPT;
    fs->last_error = BFS_OK;

    bfs_blk_t end = start + count;
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE)
        return BFS_ERR_CORRUPT;
    for (uint32_t i = 0; i < fs->reserve_count; i++) {
        if (fs->reserve[i] >= start && fs->reserve[i] < end)
            return BFS_ERR_EXISTS;
    }
    if (fs->sb) {
        uint32_t emergency_count = bfs_be32(fs->sb->emergency_count);
        if (emergency_count > BFS_EMERGENCY_POOL_SIZE)
            return BFS_ERR_CORRUPT;
        for (uint32_t i = 0; i < emergency_count; i++) {
            bfs_blk_t blk = bfs_be32(fs->sb->emergency_pool[i]);
            if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count)
                return BFS_ERR_CORRUPT;
            if (blk >= start && blk < end)
                return BFS_ERR_EXISTS;
        }
    }

    uint32_t pred_key = 0, pred_len_be = 0;
    uint32_t pred_search = bfs_be32(start);
    bfs_err_t err = bfs_btree_search_floor(&fs->tree, &pred_search,
                                           &pred_key, &pred_len_be);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND) return err;
    if (err == BFS_OK) {
        bfs_blk_t pred = bfs_be32(pred_key);
        uint32_t pred_len = bfs_be32(pred_len_be);
        if (pred_len == 0 || pred == BFS_BLK_NULL ||
            pred >= fs->tree.bio->block_count ||
            pred_len > fs->tree.bio->block_count - pred)
            return BFS_ERR_CORRUPT;
        if (pred <= start && pred_len > start - pred)
            return BFS_ERR_EXISTS;
    }
    overlap_scan_ctx_t overlap = { .end = end, .overlap = false };
    uint32_t overlap_key = bfs_be32(start);
    err = bfs_btree_scan(&fs->tree, &overlap_key, overlap_scan_cb, &overlap);
    if (err != BFS_OK) return err;
    if (overlap.overlap) return BFS_ERR_EXISTS;

    /* Bootstrap enough reserve blocks for the free-space tree's own COW
     * journal. A single seed block is consumed by the initial root and leaves
     * no block with which to refill the reserve once COW is fully atomic. */
    if (fs->reserve_count == 0 && fs->tree.root == BFS_BLK_NULL) {
        uint32_t seed_count = count;
        if (seed_count > BFS_ALLOC_RESERVE_REFILL_TARGET)
            seed_count = BFS_ALLOC_RESERVE_REFILL_TARGET;
        for (uint32_t i = 0; i < seed_count; i++)
            fs->reserve[fs->reserve_count++] = start++;
        count -= seed_count;
        if (count == 0)
            return BFS_OK;
    }

    fs->in_alloc = true;
    uint32_t key = bfs_be32(start);
    uint32_t val = bfs_be32(count);
    err = bfs_btree_insert(&fs->tree, &key, &val);
    fs->in_alloc = false;
    if (err == BFS_OK) {
        fs->total_free += count;
    } else {
        fs->last_error = err;
    }
    return err;
}

/* ── Scan callback for first-fit allocation ────────────────── */

typedef struct {
    uint32_t need;
    bfs_blk_t found_start;
    uint32_t found_len;
    bfs_blk_t block_count;
    bfs_err_t err;
} alloc_scan_ctx_t;

static bool alloc_scan_cb(const void *key, const void *val, void *ctx)
{
    alloc_scan_ctx_t *sc = (alloc_scan_ctx_t *)ctx;
    uint32_t start = bfs_load_be32(key);
    uint32_t len = bfs_load_be32(val);

    if (len == 0 || start == BFS_BLK_NULL || start >= sc->block_count ||
        len > sc->block_count - start) {
        sc->err = BFS_ERR_CORRUPT;
        return false;
    }

    if (len >= sc->need) {
        sc->found_start = start;
        sc->found_len = len;
        return false; /* stop scanning */
    }
    return true;
}

static bfs_err_t alloc_one_from_largest(bfs_freespace_t *fs, bfs_blk_t *result_out)
{
    uint32_t max_key = bfs_be32(UINT32_MAX);
    uint32_t found_key, found_len;
    bfs_err_t err = bfs_btree_search_floor(&fs->tree, &max_key, &found_key,
                                           &found_len);
    if (err != BFS_OK) return err;

    uint32_t blk_start = bfs_be32(found_key);
    uint32_t blk_len = bfs_be32(found_len);
    if (blk_len == 0 || blk_start == BFS_BLK_NULL ||
        blk_start >= fs->tree.bio->block_count ||
        blk_len > fs->tree.bio->block_count - blk_start)
        return BFS_ERR_CORRUPT;
    if (fs->total_free == 0) return BFS_ERR_CORRUPT;

    bfs_blk_t result = blk_start + blk_len - 1;
    if (blk_len == 1) {
        err = bfs_btree_delete(&fs->tree, &found_key);
    } else {
        uint32_t new_len = bfs_be32(blk_len - 1);
        err = bfs_btree_update(&fs->tree, &found_key, &new_len);
    }
    if (err != BFS_OK) return err;

    fs->total_free--;
    fs->roving = result + 1;
    *result_out = result;
    return BFS_OK;
}

/* ── Allocate ──────────────────────────────────────────────── */

bfs_blk_t bfs_freespace_alloc(bfs_freespace_t *fs, uint32_t count)
{
    if (!fs || !fs->tree.bio || count == 0) return BFS_BLK_NULL;
    fs->last_error = BFS_OK;
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) {
        fs->last_error = BFS_ERR_CORRUPT;
        return BFS_BLK_NULL;
    }
    bfs_blk_t saved_roving = fs->roving;
    bfs_err_t refill_err = bfs_freespace_refill_reserve(fs);
    fs->roving = saved_roving;
    if (refill_err != BFS_OK) {
        fs->last_error = refill_err;
        return BFS_BLK_NULL;
    }
    uint64_t available = (uint64_t)fs->total_free + fs->reserve_count;
    if (available < count) {
        fs->last_error = BFS_ERR_NOSPC;
        return BFS_BLK_NULL;
    }
    if (fs->total_free < count) {
        if (count == 1 && fs->reserve_count > 0) {
            bfs_blk_t blk = fs->reserve[fs->reserve_count - 1];
            if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count) {
                fs->last_error = BFS_ERR_CORRUPT;
                return BFS_BLK_NULL;
            }
            fs->reserve_count--;
            return blk;
        }
        fs->last_error = BFS_ERR_NOSPC;
        return BFS_BLK_NULL;
    }

    fs->in_alloc = true;

    if (count == 1) {
        bfs_blk_t result = BFS_BLK_NULL;
        bfs_err_t one_err = alloc_one_from_largest(fs, &result);
        if (one_err == BFS_OK) {
            fs->in_alloc = false;
            fs->last_error = BFS_OK;
            return result;
        }
        if (one_err != BFS_ERR_NOTFOUND) {
            fs->last_error = one_err;
            fs->in_alloc = false;
            return BFS_BLK_NULL;
        }
    }

    /* First-fit scan starting from roving pointer */
    alloc_scan_ctx_t sc = {
        .need = count,
        .found_start = BFS_BLK_NULL,
        .found_len = 0,
        .block_count = fs->tree.bio->block_count,
        .err = BFS_OK,
    };

    uint32_t start_key = bfs_be32(fs->roving);
    bfs_err_t scan_err = bfs_btree_scan(&fs->tree, &start_key, alloc_scan_cb, &sc);
    if (scan_err != BFS_OK) {
        fs->in_alloc = false;
        fs->last_error = scan_err;
        return BFS_BLK_NULL;
    }
    if (sc.err != BFS_OK) {
        fs->in_alloc = false;
        fs->last_error = sc.err;
        return BFS_BLK_NULL;
    }

    /* If not found, wrap around from beginning */
    if (sc.found_start == BFS_BLK_NULL && fs->roving > 0) {
        scan_err = bfs_btree_scan(&fs->tree, NULL, alloc_scan_cb, &sc);
        if (scan_err != BFS_OK) {
            fs->in_alloc = false;
            fs->last_error = scan_err;
            return BFS_BLK_NULL;
        }
        if (sc.err != BFS_OK) {
            fs->in_alloc = false;
            fs->last_error = sc.err;
            return BFS_BLK_NULL;
        }
    }

    if (sc.found_start == BFS_BLK_NULL) {
        if (count == 1 && fs->reserve_count > 0) {
            bfs_blk_t result = fs->reserve[--fs->reserve_count];
            fs->in_alloc = false;
            return result;
        }
        fs->in_alloc = false;
        fs->last_error = BFS_ERR_NOSPC;
        return BFS_BLK_NULL;
    }

    /* Remove the old extent */
    uint32_t old_key = bfs_be32(sc.found_start);
    uint32_t old_len = bfs_be32(sc.found_len);
    bfs_err_t err = bfs_btree_delete(&fs->tree, &old_key);
    if (err != BFS_OK) {
        fs->in_alloc = false;
        fs->last_error = err;
        return BFS_BLK_NULL;
    }

    /* If extent is larger than needed, re-insert the remainder */
    bfs_blk_t result = sc.found_start;
    if (sc.found_len > count) {
        uint32_t rem_start = bfs_be32(sc.found_start + count);
        uint32_t rem_len = bfs_be32(sc.found_len - count);
        err = bfs_btree_insert(&fs->tree, &rem_start, &rem_len);
        if (err != BFS_OK) {
            bfs_err_t rollback_err = bfs_btree_insert(&fs->tree, &old_key, &old_len);
            fs->in_alloc = false;
            fs->last_error = rollback_err == BFS_OK ? err : rollback_err;
            return BFS_BLK_NULL;
        }
    }

    fs->total_free -= count;
    fs->roving = result + count;
    fs->in_alloc = false;

    fs->last_error = BFS_OK;

    return result;
}

/* ── Free ──────────────────────────────────────────────────── */

static bfs_err_t return_to_emergency_pool(bfs_freespace_t *fs, bfs_blk_t blk,
                                          bool *handled)
{
    *handled = false;
    if (!fs->sb)
        return BFS_OK;

    uint32_t ec = bfs_be32(fs->sb->emergency_count);
    if (ec > BFS_EMERGENCY_POOL_SIZE) return BFS_ERR_CORRUPT;
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
    if (!fs || !fs->tree.bio || count == 0 || start == BFS_BLK_NULL ||
        start >= fs->tree.bio->block_count ||
        count > fs->tree.bio->block_count - start)
        return BFS_ERR_INVAL;
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) return BFS_ERR_CORRUPT;
    fs->last_error = BFS_OK;
    bfs_blk_t end = start + count;

    if (count == 1) {
        bool handled;
        bfs_err_t err = return_to_emergency_pool(fs, start, &handled);
        if (handled || err != BFS_OK)
            return err;
    }

    /* Reserve and active emergency-pool blocks are free but deliberately absent
     * from the free tree. Reject ranges that would add either a second time. */
    for (uint32_t i = 0; i < fs->reserve_count; i++) {
        if (fs->reserve[i] >= start && fs->reserve[i] < end)
            return BFS_ERR_EXISTS;
    }
    if (fs->sb) {
        uint32_t ec = bfs_be32(fs->sb->emergency_count);
        if (ec > BFS_EMERGENCY_POOL_SIZE) return BFS_ERR_CORRUPT;
        for (uint32_t i = 0; i < ec; i++) {
            bfs_blk_t blk = bfs_be32(fs->sb->emergency_pool[i]);
            if (blk >= start && blk < end) return BFS_ERR_EXISTS;
        }
    }
    if (count > UINT32_MAX - fs->total_free) return BFS_ERR_CORRUPT;

    bfs_err_t refill_err = bfs_freespace_refill_reserve(fs);
    if (refill_err != BFS_OK) {
        fs->last_error = refill_err;
        return refill_err;
    }
    fs->in_alloc = true;

    uint32_t pred_search = bfs_be32(start);
    uint32_t pred_key = 0, pred_len_be = 0;
    bfs_err_t err = bfs_btree_search_floor(&fs->tree, &pred_search,
                                           &pred_key, &pred_len_be);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND) goto fail;
    if (err == BFS_OK) {
        uint32_t pk = bfs_be32(pred_key);
        uint32_t pl = bfs_be32(pred_len_be);
        if (pl == 0 || pk == BFS_BLK_NULL || pk >= fs->tree.bio->block_count ||
            pl > fs->tree.bio->block_count - pk) {
            err = BFS_ERR_CORRUPT;
            goto fail;
        }
        if (pk <= start && pl > start - pk) {
            err = BFS_ERR_EXISTS;
            goto fail;
        }
    }

    overlap_scan_ctx_t oc = { .end = end, .overlap = false };
    uint32_t overlap_key = bfs_be32(start);
    bfs_err_t scan_err = bfs_btree_scan(&fs->tree, &overlap_key, overlap_scan_cb, &oc);
    if (scan_err != BFS_OK) {
        err = scan_err;
        goto fail;
    }
    if (oc.overlap) {
        err = BFS_ERR_EXISTS;
        goto fail;
    }

    bool have_left = false;
    uint32_t left_key = 0, left_len = 0;
    if (start > 0) {
        uint32_t left_search = bfs_be32(start - 1);
        uint32_t left_len_be;
        err = bfs_btree_search_floor(&fs->tree, &left_search, &left_key,
                                     &left_len_be);
        if (err != BFS_OK && err != BFS_ERR_NOTFOUND) goto fail;
        if (err == BFS_OK) {
            uint32_t lk = bfs_be32(left_key);
            left_len = bfs_be32(left_len_be);
            if (left_len == 0 || lk == BFS_BLK_NULL ||
                lk >= fs->tree.bio->block_count ||
                left_len > fs->tree.bio->block_count - lk) {
                err = BFS_ERR_CORRUPT;
                goto fail;
            }
            have_left = left_len == start - lk;
        }
    }

    bool have_right = false;
    uint32_t right_key = bfs_be32(end), right_len_be = 0, right_len = 0;
    err = bfs_btree_search(&fs->tree, &right_key, &right_len_be);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND) goto fail;
    if (err == BFS_OK) {
        right_len = bfs_be32(right_len_be);
        if (right_len == 0 || end >= fs->tree.bio->block_count ||
            right_len > fs->tree.bio->block_count - end) {
            err = BFS_ERR_CORRUPT;
            goto fail;
        }
        have_right = true;
    }

    uint64_t merged_len = (have_left ? (uint64_t)left_len : 0) + count +
                          (have_right ? (uint64_t)right_len : 0);
    if (merged_len > UINT32_MAX) {
        err = BFS_ERR_CORRUPT;
        goto fail;
    }

    if (have_left && have_right) {
        err = bfs_btree_delete(&fs->tree, &right_key);
        if (err != BFS_OK) goto fail;
        uint32_t merged_be = bfs_be32((uint32_t)merged_len);
        err = bfs_btree_update(&fs->tree, &left_key, &merged_be);
        if (err != BFS_OK) {
            bfs_err_t rollback_err = bfs_btree_insert(&fs->tree, &right_key,
                                                      &right_len_be);
            if (rollback_err != BFS_OK) err = rollback_err;
            goto fail;
        }
    } else if (have_left) {
        uint32_t merged_be = bfs_be32((uint32_t)merged_len);
        err = bfs_btree_update(&fs->tree, &left_key, &merged_be);
        if (err != BFS_OK) goto fail;
    } else if (have_right) {
        err = bfs_btree_delete(&fs->tree, &right_key);
        if (err != BFS_OK) goto fail;
        uint32_t ins_key = bfs_be32(start);
        uint32_t merged_be = bfs_be32((uint32_t)merged_len);
        err = bfs_btree_insert(&fs->tree, &ins_key, &merged_be);
        if (err != BFS_OK) {
            bfs_err_t rollback_err = bfs_btree_insert(&fs->tree, &right_key,
                                                      &right_len_be);
            if (rollback_err != BFS_OK) err = rollback_err;
            goto fail;
        }
    } else {
        uint32_t ins_key = bfs_be32(start);
        uint32_t ins_val = bfs_be32(count);
        err = bfs_btree_insert(&fs->tree, &ins_key, &ins_val);
        if (err != BFS_OK) goto fail;
    }

    fs->in_alloc = false;
    fs->total_free += count;
    fs->last_error = BFS_OK;
    return BFS_OK;

fail:
    fs->last_error = err;
    fs->in_alloc = false;
    return err;
}

/* ── Reserve pool management ───────────────────────────────── */

bfs_err_t bfs_freespace_refill_reserve(bfs_freespace_t *fs)
{
    if (!fs || !fs->tree.bio || fs->reserve_count > BFS_ALLOC_RESERVE_SIZE)
        return BFS_ERR_INVAL;
    if (fs->global_reserve == UINT32_MAX)
        return BFS_OK;
    while (fs->reserve_count < BFS_ALLOC_RESERVE_REFILL_TARGET &&
           fs->total_free > fs->global_reserve + 1) {
        fs->in_alloc = true;

        bfs_blk_t blk = BFS_BLK_NULL;
        bfs_err_t err = alloc_one_from_largest(fs, &blk);
        if (err != BFS_OK) {
            fs->in_alloc = false;
            return err == BFS_ERR_NOTFOUND ? BFS_ERR_CORRUPT : err;
        }

        if (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE) {
            fs->reserve[fs->reserve_count++] = blk;
        } else {
            fs->in_alloc = false;
            return BFS_ERR_CORRUPT;
        }

        fs->in_alloc = false;
    }
    return BFS_OK;
}

/* Return any unused reserve-pool blocks to the free tree (called at transaction
 * commit so the reserve doesn't permanently hold space). Temporarily lifts
 * global_reserve so these frees aren't themselves blocked by the reserve floor. */
bfs_err_t bfs_freespace_return_reserve(bfs_freespace_t *fs)
{
    if (!fs || fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) return BFS_ERR_INVAL;
    uint32_t saved_global_reserve = fs->global_reserve;
    fs->global_reserve = UINT32_MAX;
    while (fs->reserve_count > 0) {
        bfs_blk_t blk = fs->reserve[--fs->reserve_count];
        bfs_err_t err = bfs_freespace_free(fs, blk, 1);
        if (err != BFS_OK) {
            fs->reserve[fs->reserve_count++] = blk;
            fs->global_reserve = saved_global_reserve;
            fs->last_error = err;
            return err;
        }
    }
    fs->global_reserve = saved_global_reserve;
    return BFS_OK;
}

/* ── Accessor ──────────────────────────────────────────────── */

bfs_allocator_t *bfs_freespace_allocator(bfs_freespace_t *fs)
{
    return fs ? &fs->iface : NULL;
}
