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
#include <stdlib.h>
#include <string.h>
#ifdef BFS_PERF_PROBE
#include "../amiga/perf_probe.h"
#endif

#define BFS_ALLOC_RESERVE_MIN 16u
#define BFS_ALLOC_RESERVE_DEPTH_MARGIN 4u
#define BFS_ALLOC_METADATA_REUSE_WARMUP 8u

/* A reserve must cover a free-space-tree COW path, its possible ancestor
 * splits, and a small amount of compound-operation headroom. Replenishing to
 * a fixed 96 blocks made every small transaction move dozens of unused blocks
 * through the free-space tree at commit time. Scale the reserve with the
 * actual tree height while retaining the fixed array's hard upper bound. */
static uint32_t reserve_refill_target(const bfs_freespace_t *fs)
{
    uint32_t height = fs->tree.height;
    uint32_t target;

    if (height > (BFS_ALLOC_RESERVE_SIZE - BFS_ALLOC_RESERVE_MIN) /
                BFS_ALLOC_RESERVE_DEPTH_MARGIN)
        return BFS_ALLOC_RESERVE_SIZE;
    target = BFS_ALLOC_RESERVE_MIN +
             height * BFS_ALLOC_RESERVE_DEPTH_MARGIN;
    return target > BFS_ALLOC_RESERVE_SIZE ? BFS_ALLOC_RESERVE_SIZE : target;
}

/* ── B+tree ops for free space tree ────────────────────────── */

static const bfs_btree_ops_t free_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = true,
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

static bool reserve_block_is_emergency(const bfs_freespace_t *fs, bfs_blk_t blk);
static bfs_err_t validate_free_stock_range(const bfs_freespace_t *fs,
                                            bfs_blk_t start, uint32_t count,
                                            uint32_t ignore_reserve);
static bfs_err_t validate_free_tree_absence(bfs_freespace_t *fs,
                                            bfs_blk_t start, uint32_t count);

static bfs_err_t allocator_owner_error(const bfs_freespace_t *fs)
{
    if (fs->readonly_state && *fs->readonly_state) return BFS_ERR_UNSUPPORTED;
    if (fs->recovery_state && *fs->recovery_state != BFS_OK)
        return *fs->recovery_state;
    return BFS_OK;
}

static bool metadata_context_enabled(const bfs_freespace_t *fs)
{
    return fs->tree.bio && fs->mounted_state && *fs->mounted_state &&
           fs->snapshot_state && !*fs->snapshot_state && fs->sb &&
           fs->committed_sb && fs->tree.height == 1 &&
           fs->tree.root != BFS_BLK_NULL &&
           !(bfs_be32(fs->sb->options) & BFS_OPT_SNAPSHOTS) &&
           bfs_btree_txn_id(&fs->tree) == bfs_be64(fs->sb->txn_id) &&
           bfs_be64(fs->sb->txn_id) > bfs_be64(fs->committed_sb->txn_id);
}

static bool metadata_reuse_enabled(const bfs_freespace_t *fs)
{
    return metadata_context_enabled(fs) &&
           fs->metadata_reuse_txn == bfs_btree_txn_id(&fs->tree) &&
           fs->metadata_requests >= BFS_ALLOC_METADATA_REUSE_WARMUP;
}

static bool metadata_block_protected(const bfs_freespace_t *fs, bfs_blk_t blk)
{
    return blk < bfs_data_start_block(fs->tree.bio->block_size) ||
           blk >= fs->tree.bio->block_count || blk == fs->tree.root ||
           blk == bfs_be32(fs->committed_sb->free_tree_root) ||
           blk == bfs_be32(fs->committed_sb->dir_tree_root) ||
           blk == bfs_be32(fs->committed_sb->inode_tree_root) ||
           blk == bfs_be32(fs->committed_sb->refcount_tree_root) ||
           blk == bfs_be32(fs->committed_sb->snapshot_tree_root);
}

static bfs_err_t validate_metadata_stock(const bfs_freespace_t *fs)
{
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE ||
        bfs_be32(fs->sb->emergency_count) > BFS_EMERGENCY_POOL_SIZE)
        return BFS_ERR_CORRUPT;
    for (uint32_t i = 0; i < fs->reserve_count; i++) {
        bfs_blk_t blk = fs->reserve[i];
        if (metadata_block_protected(fs, blk)) return BFS_ERR_CORRUPT;
        bfs_err_t err = validate_free_stock_range(fs, blk, 1, i);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

/* Only a suffix spare may circulate through top-level metadata COW. Keeping
 * the entire existing prefix target avoids spending recursive COW headroom. */
static bfs_err_t take_metadata_spare(bfs_freespace_t *fs, bfs_blk_t *out)
{
    *out = BFS_BLK_NULL;
    if (!metadata_reuse_enabled(fs)) return BFS_ERR_UNSUPPORTED;
    uint32_t floor = reserve_refill_target(fs);
    if (fs->reserve_count <= floor) return BFS_ERR_UNSUPPORTED;
    bfs_err_t err = validate_metadata_stock(fs);
    if (err != BFS_OK) return err;
    for (uint32_t i = fs->reserve_count; i > floor; i--) {
        uint32_t index = i - 1;
        bfs_blk_t blk = fs->reserve[index];
        if (reserve_block_is_emergency(fs, blk)) continue;
        err = validate_free_tree_absence(fs, blk, 1);
        if (err != BFS_OK) return err;
        fs->reserve_count--;
        fs->reserve[index] = fs->reserve[fs->reserve_count];
        *out = blk;
        return BFS_OK;
    }
    return BFS_ERR_UNSUPPORTED;
}

static bfs_err_t stash_metadata_spare(bfs_freespace_t *fs, bfs_blk_t blk)
{
    if (!metadata_reuse_enabled(fs)) return BFS_ERR_UNSUPPORTED;
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) return BFS_ERR_CORRUPT;
    if (fs->reserve_count == BFS_ALLOC_RESERVE_SIZE ||
        reserve_block_is_emergency(fs, blk))
        return BFS_ERR_UNSUPPORTED;
    bfs_err_t err = validate_metadata_stock(fs);
    if (err != BFS_OK) return err;
    if (metadata_block_protected(fs, blk)) return BFS_ERR_CORRUPT;
    err = validate_free_stock_range(fs, blk, 1, UINT32_MAX);
    if (err == BFS_OK) err = validate_free_tree_absence(fs, blk, 1);
    if (err != BFS_OK) return err;
    fs->reserve[fs->reserve_count++] = blk;
    return BFS_OK;
}

static bfs_blk_t iface_alloc(bfs_allocator_t *a)
{
    if (!a || !a->ctx) return BFS_BLK_NULL;
    bfs_freespace_t *fs = (bfs_freespace_t *)a->ctx;
    bfs_err_t owner_err = allocator_owner_error(fs);
    if (owner_err != BFS_OK) {
        fs->last_error = owner_err;
        return BFS_BLK_NULL;
    }

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

    /* A suffix spare helps sustained COW, but changing short transactions'
     * allocation layout increased their settlement traffic in qualification.
     * Count only mounted top-level metadata requests, saturate at the bounded
     * warmup, and start over at every live transaction ID. */
    if (metadata_context_enabled(fs)) {
        uint64_t live = bfs_btree_txn_id(&fs->tree);
        if (fs->metadata_reuse_txn != live) {
            fs->metadata_reuse_txn = live;
            fs->metadata_requests = 0;
        }
        if (fs->metadata_requests < BFS_ALLOC_METADATA_REUSE_WARMUP)
            fs->metadata_requests++;
    }
    bfs_blk_t spare;
    bfs_err_t err = take_metadata_spare(fs, &spare);
    if (err == BFS_OK) {
        fs->last_error = BFS_OK;
        return spare;
    }
    if (err != BFS_ERR_UNSUPPORTED) {
        fs->last_error = err;
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

    bfs_err_t err = stash_metadata_spare(fs, blk);
    if (err != BFS_ERR_UNSUPPORTED) {
        fs->last_error = err;
        return err;
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
        if (seed_count > reserve_refill_target(fs))
            seed_count = reserve_refill_target(fs);
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

/* Take a tail run from the highest-key free extent with one COW mutation.
 * Reserving adjacent blocks one by one needlessly rewrites the same tree path. */
static bfs_err_t alloc_tail_from_highest(bfs_freespace_t *fs, uint32_t requested,
                                         bfs_blk_t *result_out, uint32_t *taken_out)
{
    if (requested == 0) return BFS_ERR_INVAL;
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
    uint32_t taken = blk_len < requested ? blk_len : requested;
    if (fs->total_free < taken) return BFS_ERR_CORRUPT;

    bfs_blk_t result = blk_start + blk_len - taken;
    // cppcheck-suppress knownConditionTrueFalse
    if (blk_len == taken) {
        err = bfs_btree_delete(&fs->tree, &found_key);
    } else {
        uint32_t new_len = bfs_be32(blk_len - taken);
        err = bfs_btree_update(&fs->tree, &found_key, &new_len);
    }
    if (err != BFS_OK) return err;

    fs->total_free -= taken;
    fs->roving = result + 1;
    *result_out = result;
    *taken_out = taken;
    return BFS_OK;
}

/* ── Allocate ──────────────────────────────────────────────── */

static bfs_err_t alloc_partial_root_leaf(bfs_freespace_t *fs,
                                          bfs_blk_t start, uint32_t length,
                                          uint32_t count);

bfs_blk_t bfs_freespace_alloc(bfs_freespace_t *fs, uint32_t count)
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_counters.freespace_alloc_calls++;
#endif
    if (!fs || !fs->tree.bio || count == 0) return BFS_BLK_NULL;
    fs->last_error = BFS_OK;
    bfs_err_t owner_err = allocator_owner_error(fs);
    if (owner_err != BFS_OK) {
        fs->last_error = owner_err;
        return BFS_BLK_NULL;
    }
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
        uint32_t taken = 0;
#ifdef BFS_PERF_PROBE
        ULONG previous_phase = bfs_perf_probe_free_tree_phase_enter(
            BFS_PERF_FREE_TREE_PHASE_ALLOCATION_BODY);
#endif
        bfs_err_t one_err = alloc_tail_from_highest(fs, 1, &result, &taken);
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_free_tree_phase_leave(previous_phase);
#endif
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

#ifdef BFS_PERF_PROBE
    ULONG previous_phase = bfs_perf_probe_free_tree_phase_enter(
        BFS_PERF_FREE_TREE_PHASE_ALLOCATION_BODY);
#endif
    bfs_err_t err;
    if (count > 1 && sc.found_len > count && fs->tree.height == 1) {
        err = alloc_partial_root_leaf(fs, sc.found_start, sc.found_len, count);
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_free_tree_phase_leave(previous_phase);
#endif
        fs->in_alloc = false;
        fs->last_error = err;
        return err == BFS_OK ? sc.found_start : BFS_BLK_NULL;
    }

    /* Exact fits and deeper trees retain the existing delete/reinsert path. */
    uint32_t old_key = bfs_be32(sc.found_start);
    uint32_t old_len = bfs_be32(sc.found_len);
    err = bfs_btree_delete(&fs->tree, &old_key);
    if (err != BFS_OK) {
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_free_tree_phase_leave(previous_phase);
#endif
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
#ifdef BFS_PERF_PROBE
            bfs_perf_probe_free_tree_phase_leave(previous_phase);
#endif
            fs->in_alloc = false;
            fs->last_error = rollback_err == BFS_OK ? err : rollback_err;
            return BFS_BLK_NULL;
        }
    }
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_free_tree_phase_leave(previous_phase);
#endif

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

static bfs_err_t validate_free_stock_range(const bfs_freespace_t *fs,
                                            bfs_blk_t start, uint32_t count,
                                            uint32_t ignore_reserve)
{
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) return BFS_ERR_CORRUPT;
    bfs_blk_t end = start + count;
    for (uint32_t i = 0; i < fs->reserve_count; i++) {
        if (i != ignore_reserve && fs->reserve[i] >= start && fs->reserve[i] < end)
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
    return BFS_OK;
}

static bfs_err_t validate_free_tree_absence(bfs_freespace_t *fs,
                                            bfs_blk_t start, uint32_t count)
{
    uint32_t pred_search = bfs_be32(start), pred_key = 0, pred_len_be = 0;
    bfs_err_t err = bfs_btree_search_floor(&fs->tree, &pred_search,
                                           &pred_key, &pred_len_be);
    if (err != BFS_OK && err != BFS_ERR_NOTFOUND) return err;
    if (err == BFS_OK) {
        uint32_t pk = bfs_be32(pred_key), pl = bfs_be32(pred_len_be);
        if (pl == 0 || pk == BFS_BLK_NULL || pk >= fs->tree.bio->block_count ||
            pl > fs->tree.bio->block_count - pk)
            return BFS_ERR_CORRUPT;
        if (pk <= start && pl > start - pk) return BFS_ERR_EXISTS;
    }
    overlap_scan_ctx_t overlap = { .end = start + count, .overlap = false };
    uint32_t key = bfs_be32(start);
    err = bfs_btree_scan(&fs->tree, &key, overlap_scan_cb, &overlap);
    if (err != BFS_OK) return err;
    return overlap.overlap ? BFS_ERR_EXISTS : BFS_OK;
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
    bfs_err_t stock_err = validate_free_stock_range(fs, start, count, UINT32_MAX);
    if (stock_err != BFS_OK) return stock_err;
    if (count > UINT32_MAX - fs->total_free) return BFS_ERR_CORRUPT;

    bfs_err_t refill_err = bfs_freespace_refill_reserve(fs);
    if (refill_err != BFS_OK) {
        fs->last_error = refill_err;
        return refill_err;
    }
    fs->in_alloc = true;

    bfs_err_t err = validate_free_tree_absence(fs, start, count);
    if (err != BFS_OK) goto fail;

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

typedef struct {
    uint32_t *keys;
    uint32_t *lengths;
    uint32_t count;
    uint32_t capacity;
    bfs_blk_t block_count;
    bfs_err_t error;
} free_leaf_entries_t;

static bool collect_free_leaf_entry(const void *key, const void *val, void *ctx)
{
    free_leaf_entries_t *entries = (free_leaf_entries_t *)ctx;
    uint32_t start = bfs_load_be32(key);
    uint32_t length = bfs_load_be32(val);
    if (entries->count == entries->capacity || start == BFS_BLK_NULL ||
        start >= entries->block_count || length == 0 ||
        length > entries->block_count - start) {
        entries->error = BFS_ERR_CORRUPT;
        return false;
    }
    entries->keys[entries->count] = start;
    entries->lengths[entries->count++] = length;
    return true;
}

static void encode_free_leaf_entries(free_leaf_entries_t *entries)
{
    for (uint32_t i = 0; i < entries->count; i++) {
        entries->keys[i] = bfs_be32(entries->keys[i]);
        entries->lengths[i] = bfs_be32(entries->lengths[i]);
    }
}

/* Shift the selected first-fit extent without a transient delete/reinsert.
 * The normal B-tree API retains the old root's established retirement path. */
static bfs_err_t alloc_partial_root_leaf(bfs_freespace_t *fs,
                                          bfs_blk_t start, uint32_t length,
                                          uint32_t count)
{
    if (fs->tree.height != 1) return BFS_ERR_CORRUPT;
    uint32_t capacity = bfs_btree_leaf_capacity(&fs->tree);
    if (capacity == 0 || fs->total_free < count) return BFS_ERR_CORRUPT;
    if ((uint64_t)capacity * 2u * sizeof(uint32_t) > SIZE_MAX)
        return BFS_ERR_NOMEM;
    uint32_t *storage = malloc((size_t)capacity * 2u * sizeof(*storage));
    if (!storage) return BFS_ERR_NOMEM;
    free_leaf_entries_t entries = {
        .keys = storage, .lengths = storage + capacity,
        .capacity = capacity, .block_count = fs->tree.bio->block_count,
        .error = BFS_OK,
    };
    bfs_err_t err = bfs_btree_scan(&fs->tree, NULL, collect_free_leaf_entry,
                                   &entries);
    if (err != BFS_OK) goto done;
    if (entries.error != BFS_OK) { err = entries.error; goto done; }
    uint32_t selected = entries.count;
    for (uint32_t i = 0; i < entries.count; i++) {
        if (i > 0 && entries.keys[i] <
            entries.keys[i - 1] + entries.lengths[i - 1]) {
            err = BFS_ERR_CORRUPT;
            goto done;
        }
        if (entries.keys[i] == start) selected = i;
    }
    if (selected == entries.count || entries.lengths[selected] != length ||
        length <= count) {
        err = BFS_ERR_CORRUPT;
        goto done;
    }
    entries.keys[selected] += count;
    entries.lengths[selected] -= count;
    encode_free_leaf_entries(&entries);
    bfs_blk_t old_root = fs->tree.root;
    err = bfs_btree_replace_root_leaf(&fs->tree, entries.keys, entries.lengths,
                                      entries.count);
    bool changed = fs->tree.root != old_root;
    if (changed) {
        fs->total_free -= count;
        fs->roving = start + count;
    }
    if (err != BFS_OK && (changed || fs->tree.free_sink_err != BFS_OK) &&
        fs->recovery_state && *fs->recovery_state == BFS_OK) {
        /* The interval may already be removed or scratch ownership uncertain.
         * A raw extent caller must not let another B-tree clear this failure
         * and subsequently publish the working filesystem state. */
        *fs->recovery_state = err;
    }
done:
    free(storage);
    return err;
}

static bfs_err_t append_free_leaf_entry(free_leaf_entries_t *entries,
                                        uint32_t start, uint32_t length)
{
    if (entries->count > 0) {
        uint32_t prev = entries->count - 1;
        uint64_t end = (uint64_t)entries->keys[prev] + entries->lengths[prev];
        if (start < end) return BFS_ERR_EXISTS;
        if (start == end) {
            if (length > UINT32_MAX - entries->lengths[prev])
                return BFS_ERR_CORRUPT;
            entries->lengths[prev] += length;
            return BFS_OK;
        }
    }
    if (entries->count == entries->capacity) return BFS_ERR_UNSUPPORTED;
    entries->keys[entries->count] = start;
    entries->lengths[entries->count++] = length;
    return BFS_OK;
}

static bfs_err_t validate_sorted_free_blocks(const bfs_freespace_t *fs,
                                             const bfs_blk_t *blocks,
                                             uint32_t count)
{
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE ||
        count > UINT32_MAX - fs->total_free)
        return BFS_ERR_CORRUPT;
    for (uint32_t i = 0; i < count; i++) {
        bfs_blk_t blk = blocks[i];
        if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count ||
            (i > 0 && blk <= blocks[i - 1]))
            return BFS_ERR_CORRUPT;
        for (uint32_t j = 0; j < fs->reserve_count; j++)
            if (fs->reserve[j] == blk) return BFS_ERR_EXISTS;
        if (!fs->sb) continue;
        for (uint32_t j = 0; j < BFS_EMERGENCY_POOL_SIZE; j++)
            if (bfs_be32(fs->sb->emergency_pool[j]) == blk)
                return BFS_ERR_UNSUPPORTED;
    }
    return BFS_OK;
}

static bfs_err_t merge_sorted_free_blocks(free_leaf_entries_t *old,
                                         free_leaf_entries_t *next,
                                         const bfs_blk_t *blocks,
                                         uint32_t count)
{
    uint32_t old_index = 0, block_index = 0;
    while (old_index < old->count || block_index < count) {
        uint32_t start, length;
        if (old_index < old->count &&
            (block_index == count || old->keys[old_index] < blocks[block_index])) {
            start = old->keys[old_index];
            length = old->lengths[old_index++];
        } else {
            start = blocks[block_index++];
            length = 1;
        }
        bfs_err_t err = append_free_leaf_entry(next, start, length);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

static bfs_err_t validate_free_leaf_pool_separation(const bfs_freespace_t *fs,
                                                    const free_leaf_entries_t *old)
{
    uint32_t active = bfs_be32(fs->sb->emergency_count);
    bool inactive_alias = false;
    for (uint32_t i = 0; i < old->count; i++) {
        if (fs->tree.root >= old->keys[i] &&
            fs->tree.root - old->keys[i] < old->lengths[i])
            return BFS_ERR_CORRUPT;
        for (uint32_t j = 0; j < fs->reserve_count; j++) {
            bfs_blk_t blk = fs->reserve[j];
            if (blk >= old->keys[i] && blk - old->keys[i] < old->lengths[i])
                return BFS_ERR_CORRUPT;
        }
        for (uint32_t j = 0; j < BFS_EMERGENCY_POOL_SIZE; j++) {
            bfs_blk_t blk = bfs_be32(fs->sb->emergency_pool[j]);
            if (blk >= old->keys[i] && blk - old->keys[i] < old->lengths[i]) {
                /* Ordinary range reclamation may legitimately put an inactive
                 * pool-origin block in the free tree. Keep the legacy path
                 * rather than transferring emergency ownership in that state. */
                if (j < active) return BFS_ERR_CORRUPT;
                inactive_alias = true;
            }
        }
    }
    return inactive_alias ? BFS_ERR_UNSUPPORTED : BFS_OK;
}

static bfs_err_t replace_free_root_leaf(bfs_freespace_t *fs,
                                        free_leaf_entries_t *entries,
                                        uint32_t count, bool take_current_root)
{
    encode_free_leaf_entries(entries);
    bfs_blk_t old_root = fs->tree.root;
    fs->in_alloc = true;
    bfs_err_t err = take_current_root
        ? bfs_btree_replace_owned_root_leaf(&fs->tree, old_root, entries->keys,
                                            entries->lengths, entries->count)
        : bfs_btree_replace_root_leaf(&fs->tree, entries->keys,
                                       entries->lengths, entries->count);
    fs->in_alloc = false;
    if (fs->tree.root != old_root) fs->total_free += count;
    fs->last_error = err;
    return err;
}

/* Reclaim all post-publication blocks with one root-leaf COW where possible.
 * The old root remains intact on allocation/write failure; deeper or overly
 * fragmented trees keep the ordinary, range-by-range reclamation path. */
static bfs_err_t free_sorted_blocks(bfs_freespace_t *fs,
                                    const bfs_blk_t *blocks,
                                    uint32_t count, bool take_current_root)
{
    if (!fs || !fs->tree.bio || !blocks || count == 0)
        return BFS_ERR_INVAL;
    if (fs->tree.height != 1 || fs->tree.root == BFS_BLK_NULL)
        return BFS_ERR_UNSUPPORTED;
    bfs_err_t err = validate_sorted_free_blocks(fs, blocks, count);
    if (err != BFS_OK) return err;

    err = bfs_freespace_refill_reserve(fs);
    if (err != BFS_OK) return err;
    if (fs->tree.height != 1) return BFS_ERR_UNSUPPORTED;
    uint32_t capacity = bfs_btree_leaf_capacity(&fs->tree);
    if (capacity == 0)
        return BFS_ERR_CORRUPT;
    /* A valid leaf fits in one block (at most BFS_MAX_BLOCK_SIZE bytes),
     * so four uint32_t arrays cannot overflow size_t on supported targets. */
    uint32_t *memory = malloc((size_t)capacity * 4u * sizeof(uint32_t));
    if (!memory) return BFS_ERR_NOMEM;
    free_leaf_entries_t old = {
        .keys = memory, .lengths = memory + capacity,
        .count = 0, .capacity = capacity,
        .block_count = fs->tree.bio->block_count, .error = BFS_OK,
    };
    free_leaf_entries_t next = {
        .keys = memory + 2u * capacity,
        .lengths = memory + 3u * capacity,
        .count = 0, .capacity = capacity,
        .block_count = fs->tree.bio->block_count, .error = BFS_OK,
    };
    err = bfs_btree_scan(&fs->tree, NULL, collect_free_leaf_entry, &old);
    if (err != BFS_OK || old.error != BFS_OK) {
        free(memory);
        return err != BFS_OK ? err : old.error;
    }

    if (take_current_root) err = validate_free_leaf_pool_separation(fs, &old);
    if (err == BFS_OK) err = merge_sorted_free_blocks(&old, &next, blocks, count);
    if (err == BFS_OK)
        err = replace_free_root_leaf(fs, &next, count, take_current_root);
    free(memory);
    return err;
}

bfs_err_t bfs_freespace_free_sorted_blocks(bfs_freespace_t *fs,
                                           const bfs_blk_t *blocks,
                                           uint32_t count)
{
    return free_sorted_blocks(fs, blocks, count, false);
}

/* ── Reserve pool management ───────────────────────────────── */

bfs_err_t bfs_freespace_refill_reserve(bfs_freespace_t *fs)
{
    if (!fs || !fs->tree.bio || fs->reserve_count > BFS_ALLOC_RESERVE_SIZE)
        return BFS_ERR_INVAL;
    if (fs->global_reserve == UINT32_MAX)
        return BFS_OK;
    uint32_t target = reserve_refill_target(fs);
    while (fs->reserve_count < target &&
           fs->total_free > fs->global_reserve + 1) {
        uint32_t wanted = target - fs->reserve_count;
        uint32_t surplus = fs->total_free - fs->global_reserve - 1;
        if (wanted > surplus) wanted = surplus;
        fs->in_alloc = true;

        bfs_blk_t start = BFS_BLK_NULL;
        uint32_t taken = 0;
#ifdef BFS_PERF_PROBE
        ULONG previous_phase = bfs_perf_probe_free_tree_phase_enter(
            BFS_PERF_FREE_TREE_PHASE_RESERVE_REFILL);
#endif
        bfs_err_t err = alloc_tail_from_highest(fs, wanted, &start, &taken);
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_free_tree_phase_leave(previous_phase);
#endif
        if (err != BFS_OK) {
            fs->in_alloc = false;
            return err == BFS_ERR_NOTFOUND ? BFS_ERR_CORRUPT : err;
        }

        if (taken > BFS_ALLOC_RESERVE_SIZE - fs->reserve_count) {
            fs->in_alloc = false;
            return BFS_ERR_CORRUPT;
        }
        /* Preserve the old descending order and LIFO allocation behavior. */
        for (uint32_t i = 0; i < taken; i++)
            fs->reserve[fs->reserve_count++] = start + taken - 1 - i;

        fs->in_alloc = false;
    }
    return BFS_OK;
}

/* Return any unused reserve-pool blocks to the free tree (called at transaction
 * commit so the reserve doesn't permanently hold space). Temporarily lifts
 * global_reserve so these frees aren't themselves blocked by the reserve floor. */
static bool reserve_block_is_emergency(const bfs_freespace_t *fs, bfs_blk_t blk)
{
    if (!fs->sb) return false;
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        if (bfs_be32(fs->sb->emergency_pool[i]) == blk) return true;
    }
    return false;
}

static void sort_reserve_blocks(bfs_blk_t *blocks, uint32_t count)
{
    for (uint32_t i = 1; i < count; i++) {
        bfs_blk_t value = blocks[i];
        uint32_t j = i;
        while (j > 0 && blocks[j - 1] > value) {
            blocks[j] = blocks[j - 1];
            j--;
        }
        blocks[j] = value;
    }
}

typedef struct {
    bfs_blk_t original[BFS_ALLOC_RESERVE_SIZE];
    bfs_blk_t retained[BFS_ALLOC_RESERVE_SIZE];
    bfs_blk_t blocks[BFS_ALLOC_RESERVE_SIZE + 1];
    uint32_t emergency_pool[BFS_EMERGENCY_POOL_SIZE];
    uint32_t original_count;
    uint32_t retained_count;
    uint32_t block_count;
    uint32_t emergency_count;
    bfs_blk_t old_root;
    bool scratch_from_pool;
    bool root_is_emergency;
} reserve_root_fold_t;

static bfs_err_t validate_fold_pools(const bfs_freespace_t *fs)
{
    uint32_t ec = bfs_be32(fs->sb->emergency_count);
    if (ec > BFS_EMERGENCY_POOL_SIZE) return BFS_ERR_CORRUPT;
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        bfs_blk_t blk = bfs_be32(fs->sb->emergency_pool[i]);
        if (blk == BFS_BLK_NULL && i >= ec) continue;
        if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count ||
            (i < ec && blk == fs->tree.root)) return BFS_ERR_CORRUPT;
        for (uint32_t j = 0; j < i; j++)
            if (bfs_be32(fs->sb->emergency_pool[j]) == blk)
                return BFS_ERR_CORRUPT;
    }
    for (uint32_t i = 0; i < fs->reserve_count; i++) {
        bfs_blk_t blk = fs->reserve[i];
        if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count ||
            blk == fs->tree.root) return BFS_ERR_CORRUPT;
        for (uint32_t j = 0; j < i; j++)
            if (fs->reserve[j] == blk) return BFS_ERR_CORRUPT;
        for (uint32_t j = 0; j < ec; j++)
            if (bfs_be32(fs->sb->emergency_pool[j]) == blk)
                return BFS_ERR_CORRUPT;
    }
    return BFS_OK;
}

static bfs_err_t prepare_reserve_root_fold(const bfs_freespace_t *fs,
                                           reserve_root_fold_t *batch)
{
    bfs_err_t err = validate_fold_pools(fs);
    if (err != BFS_OK) return err;
    batch->original_count = fs->reserve_count;
    batch->old_root = fs->tree.root;
    batch->emergency_count = bfs_be32(fs->sb->emergency_count);
    memcpy(batch->original, fs->reserve,
           batch->original_count * sizeof(*batch->original));
    memcpy(batch->emergency_pool, fs->sb->emergency_pool,
           sizeof(batch->emergency_pool));
    for (uint32_t i = 0; i < batch->original_count; i++) {
        bfs_blk_t blk = batch->original[i];
        if (reserve_block_is_emergency(fs, blk))
            batch->retained[batch->retained_count++] = blk;
        else
            batch->blocks[batch->block_count++] = blk;
    }
    if (batch->block_count == 0) return BFS_ERR_UNSUPPORTED;
    if (batch->retained_count == 0) {
        if (batch->emergency_count == 0) return BFS_ERR_UNSUPPORTED;
        batch->retained[batch->retained_count++] = bfs_be32(
            batch->emergency_pool[batch->emergency_count - 1]);
        batch->scratch_from_pool = true;
    }
    batch->root_is_emergency = reserve_block_is_emergency(fs, batch->old_root);
    if (!batch->root_is_emergency)
        batch->blocks[batch->block_count++] = batch->old_root;
    sort_reserve_blocks(batch->blocks, batch->block_count);
    return BFS_OK;
}

static bfs_err_t restore_failed_reserve_root_fold(bfs_freespace_t *fs,
                                                  const reserve_root_fold_t *batch,
                                                  bfs_err_t err)
{
    uint32_t expected_ec = batch->emergency_count - batch->scratch_from_pool;
    if (fs->tree.root != batch->old_root ||
        fs->reserve_count != batch->retained_count ||
        memcmp(fs->reserve, batch->retained,
               batch->retained_count * sizeof(*batch->retained)) != 0 ||
        bfs_be32(fs->sb->emergency_count) != expected_ec ||
        memcmp(fs->sb->emergency_pool, batch->emergency_pool,
               sizeof(batch->emergency_pool)) != 0)
        return err == BFS_ERR_UNSUPPORTED ? BFS_ERR_CORRUPT : err;
    memcpy(fs->reserve, batch->original,
           batch->original_count * sizeof(*batch->original));
    fs->reserve_count = batch->original_count;
    fs->sb->emergency_count = bfs_be32(batch->emergency_count);
    return err;
}

static bfs_err_t return_fold_emergency_block(bfs_freespace_t *fs, bfs_blk_t blk)
{
    bool handled;
    bfs_err_t err = return_to_emergency_pool(fs, blk, &handled);
    if (err == BFS_OK && handled) return BFS_OK;
    /* After a swap, retain the unreturned owner's block rather than restoring
     * candidates already represented by the new leaf. */
    if (fs->reserve_count < BFS_ALLOC_RESERVE_SIZE)
        fs->reserve[fs->reserve_count++] = blk;
    return err != BFS_OK ? err : BFS_ERR_CORRUPT;
}

static bfs_err_t apply_reserve_root_fold(bfs_freespace_t *fs,
                                         const reserve_root_fold_t *batch)
{
    memcpy(fs->reserve, batch->retained,
           batch->retained_count * sizeof(*batch->retained));
    fs->reserve_count = batch->retained_count;
    if (batch->scratch_from_pool)
        fs->sb->emergency_count = bfs_be32(batch->emergency_count - 1);
#ifdef BFS_PERF_PROBE
    ULONG previous_phase = bfs_perf_probe_free_tree_phase_enter(
        BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN);
    ULONG node_writes_before =
        bfs_perf_probe_counters.free_tree_reserve_return_node_writes;
#endif
    bfs_err_t err = free_sorted_blocks(fs, batch->blocks, batch->block_count, true);
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_free_tree_phase_leave(previous_phase);
    if (err == BFS_OK)
        bfs_perf_probe_reserve_return_batch(batch->block_count,
            bfs_perf_probe_counters.free_tree_reserve_return_node_writes -
                node_writes_before);
#endif
    if (err != BFS_OK) return restore_failed_reserve_root_fold(fs, batch, err);
    if (batch->root_is_emergency) {
        err = return_fold_emergency_block(fs, batch->old_root);
        if (err != BFS_OK) return err;
    }
    while (fs->reserve_count > 0) {
        bfs_blk_t blk = fs->reserve[--fs->reserve_count];
        err = return_fold_emergency_block(fs, blk);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

/* The root is unshared and unpublished only under both proofs below. Its
 * ownership can then be folded into the new leaf/pool without another COW. */
static bfs_err_t try_reserve_root_fold(bfs_freespace_t *fs, bool *applied)
{
    *applied = false;
    if (!fs->sb || !fs->committed_sb || fs->reserve_count == 0 ||
        fs->tree.height != 1 || fs->tree.root == BFS_BLK_NULL)
        return BFS_OK;
    uint64_t root_txn;
    bfs_err_t err = bfs_btree_root_leaf_txn_id(&fs->tree, &root_txn);
    if (err != BFS_OK) return err == BFS_ERR_UNSUPPORTED ? BFS_OK : err;
    uint64_t live_txn = bfs_btree_txn_id(&fs->tree);
    if (root_txn > live_txn) return BFS_ERR_CORRUPT;
    if (root_txn != live_txn ||
        fs->tree.root == bfs_be32(fs->committed_sb->free_tree_root))
        return BFS_OK;
    reserve_root_fold_t batch = {0};
    err = prepare_reserve_root_fold(fs, &batch);
    if (err == BFS_ERR_UNSUPPORTED) return BFS_OK;
    if (err != BFS_OK) return err;
    err = apply_reserve_root_fold(fs, &batch);
    if (err == BFS_ERR_UNSUPPORTED) return BFS_OK;
    if (err == BFS_OK) *applied = true;
    return err;
}

typedef struct {
    bfs_blk_t original[BFS_ALLOC_RESERVE_SIZE];
    bfs_blk_t retained[BFS_ALLOC_RESERVE_SIZE];
    bfs_blk_t blocks[BFS_ALLOC_RESERVE_SIZE];
    uint32_t original_count;
    uint32_t retained_count;
    uint32_t block_count;
} reserve_leaf_batch_t;

static bool prepare_reserve_leaf_batch(const bfs_freespace_t *fs,
                                       reserve_leaf_batch_t *batch,
                                       bool allow_mixed)
{
    batch->original_count = fs->reserve_count;
    memcpy(batch->original, fs->reserve,
           batch->original_count * sizeof(*batch->original));
    for (uint32_t i = 0; i < batch->original_count; i++) {
        bfs_blk_t blk = batch->original[i];
        if (reserve_block_is_emergency(fs, blk)) {
            if (!allow_mixed) {
#ifdef BFS_PERF_PROBE
                bfs_perf_probe_counters.free_tree_reserve_return_skip_emergency++;
#endif
                return false;
            }
            batch->retained[batch->retained_count++] = blk;
        } else
            batch->blocks[batch->block_count++] = blk;
    }
    if (batch->block_count < 3) {
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_counters.free_tree_reserve_return_skip_emergency++;
#endif
        return false;
    }
    /* Emergency slots stay out of the Free-Tree. Keep one ordinary block
     * behind them as the private root's COW scratch allocation. */
    batch->retained[batch->retained_count++] =
        batch->blocks[--batch->block_count];
    sort_reserve_blocks(batch->blocks, batch->block_count);
    return true;
}

static bool reserve_leaf_batch_shape_available(const bfs_freespace_t *fs)
{
    if (fs->tree.height != 1 || fs->tree.root == BFS_BLK_NULL) {
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_counters.free_tree_reserve_return_skip_shape++;
#endif
        return false;
    }
    if (fs->reserve_count < 3) {
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_counters.free_tree_reserve_return_skip_small++;
#endif
        return false;
    }
    return true;
}

static bfs_err_t apply_reserve_leaf_batch(bfs_freespace_t *fs,
                                          const reserve_leaf_batch_t *batch)
{
    memcpy(fs->reserve, batch->retained,
           batch->retained_count * sizeof(*batch->retained));
    fs->reserve_count = batch->retained_count;
#ifdef BFS_PERF_PROBE
    ULONG previous_phase = bfs_perf_probe_free_tree_phase_enter(
        BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN);
    ULONG node_writes_before =
        bfs_perf_probe_counters.free_tree_reserve_return_node_writes;
#endif
    bfs_err_t err = bfs_freespace_free_sorted_blocks(fs, batch->blocks,
                                                      batch->block_count);
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_free_tree_phase_leave(previous_phase);
    if (err == BFS_OK) {
        ULONG node_writes =
            bfs_perf_probe_counters.free_tree_reserve_return_node_writes -
            node_writes_before;
        bfs_perf_probe_reserve_return_batch(batch->block_count, node_writes);
    }
#endif
    return err;
}

static bfs_err_t restore_failed_reserve_leaf_batch(bfs_freespace_t *fs,
                                                   const reserve_leaf_batch_t *batch,
                                                   bfs_blk_t old_root,
                                                   bfs_err_t err)
{
    /* A failed write leaves the old root intact and mutation_abort returns
     * the replacement block to the reserve. Do not conceal a partial swap. */
    if (fs->tree.root != old_root || fs->reserve_count != batch->retained_count ||
        memcmp(fs->reserve, batch->retained,
               batch->retained_count * sizeof(*batch->retained)) != 0)
        return err == BFS_ERR_UNSUPPORTED ? BFS_ERR_CORRUPT : err;
    memcpy(fs->reserve, batch->original,
           batch->original_count * sizeof(*batch->original));
    fs->reserve_count = batch->original_count;
#ifdef BFS_PERF_PROBE
    if (err == BFS_ERR_UNSUPPORTED)
        bfs_perf_probe_counters.free_tree_reserve_return_skip_capacity++;
#endif
    return err == BFS_ERR_UNSUPPORTED ? BFS_OK : err;
}

/* Return ordinary reserve blocks with one private root-leaf replacement.
 * Emergency-slot blocks retain their existing single-block return path. */
static bfs_err_t return_reserve_root_leaf_batch(bfs_freespace_t *fs,
                                                bool allow_mixed)
{
    if (!reserve_leaf_batch_shape_available(fs)) return BFS_OK;
    reserve_leaf_batch_t batch = {0};
    if (!prepare_reserve_leaf_batch(fs, &batch, allow_mixed)) return BFS_OK;
    bfs_blk_t old_root = fs->tree.root;
    bfs_err_t err = apply_reserve_leaf_batch(fs, &batch);
    return err == BFS_OK ? BFS_OK
        : restore_failed_reserve_leaf_batch(fs, &batch, old_root, err);
}

static bfs_err_t return_reserve(bfs_freespace_t *fs, bool allow_mixed)
{
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_reserve_return_call();
#endif
    if (!fs || !fs->tree.bio || fs->reserve_count > BFS_ALLOC_RESERVE_SIZE)
        return BFS_ERR_INVAL;
    uint32_t saved_global_reserve = fs->global_reserve;
    fs->global_reserve = UINT32_MAX;
    bool folded;
    bfs_err_t batch_err = try_reserve_root_fold(fs, &folded);
    if (batch_err == BFS_OK && !folded)
        batch_err = return_reserve_root_leaf_batch(fs, allow_mixed);
    if (batch_err != BFS_OK) {
        fs->global_reserve = saved_global_reserve;
        fs->last_error = batch_err;
        return batch_err;
    }
    while (fs->reserve_count > 0) {
        bfs_blk_t start = fs->reserve[fs->reserve_count - 1];
        uint32_t run = 1;
        /* Keep enough scratch blocks for a free-tree COW mutation. A run may
         * not include emergency-pool blocks, which must be returned singly. */
        uint32_t scratch = 4u * fs->tree.height + 8u;
        uint32_t limit = fs->reserve_count > scratch
            ? fs->reserve_count - scratch : 1;
        if (!reserve_block_is_emergency(fs, start)) {
            while (run < limit && run < fs->tree.bio->block_count - start &&
                   fs->reserve[fs->reserve_count - 1 - run] == start + run &&
                   !reserve_block_is_emergency(fs, start + run))
                run++;
        }
        fs->reserve_count -= run;
#ifdef BFS_PERF_PROBE
        ULONG previous_phase = bfs_perf_probe_free_tree_phase_enter(
            BFS_PERF_FREE_TREE_PHASE_RESERVE_RETURN);
        ULONG node_writes_before =
            bfs_perf_probe_counters.free_tree_reserve_return_node_writes;
#endif
        bfs_err_t err = bfs_freespace_free(fs, start, run);
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_free_tree_phase_leave(previous_phase);
        if (err == BFS_OK) {
            ULONG node_writes =
                bfs_perf_probe_counters.free_tree_reserve_return_node_writes -
                node_writes_before;
            bfs_perf_probe_reserve_return_run(run, node_writes);
        }
#endif
        if (err != BFS_OK) {
            if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE ||
                run > BFS_ALLOC_RESERVE_SIZE - fs->reserve_count) {
                err = BFS_ERR_CORRUPT;
            } else {
                for (uint32_t i = run; i > 0; i--)
                    fs->reserve[fs->reserve_count++] = start + i - 1;
            }
            fs->global_reserve = saved_global_reserve;
            fs->last_error = err;
            return err;
        }
    }
    fs->global_reserve = saved_global_reserve;
    return BFS_OK;
}

bfs_err_t bfs_freespace_return_reserve(bfs_freespace_t *fs)
{
    return return_reserve(fs, true);
}

bfs_err_t bfs_freespace_settle_reserve(bfs_freespace_t *fs)
{
    return return_reserve(fs, false);
}

/* ── Accessor ──────────────────────────────────────────────── */

bfs_allocator_t *bfs_freespace_allocator(bfs_freespace_t *fs)
{
    return fs ? &fs->iface : NULL;
}
