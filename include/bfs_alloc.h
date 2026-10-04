/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Free space allocator (B+tree based)
 *
 * Manages free space as a B+tree of (block_nr → length) extents.
 * Provides both multi-block allocation and the single-block
 * bfs_allocator_t interface used by B+tree COW.
 *
 * Self-hosting: the free space tree uses itself for node allocation.
 * A small reserve pool prevents infinite recursion during splits.
 */

#ifndef BFS_ALLOC_H
#define BFS_ALLOC_H

#include "bfs_btree.h"
#include "bfs_ondisk.h"

#define BFS_ALLOC_RESERVE_SIZE 128 /* pre-allocated reserve blocks */

typedef struct bfs_freespace {
    bfs_btree_t tree;              /* the free space B+tree */
    bfs_allocator_t iface;         /* single-block allocator interface */
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    uint32_t reserve_count;
    bfs_blk_t roving;              /* roving allocation hint */
    bfs_blk_t data_roving;         /* first-fit hint for file data, ascending */
    uint32_t total_free;            /* total free blocks (accounting) */
    uint32_t global_reserve;        /* blocks reserved for metadata (not data) */
    bool in_alloc;                  /* recursion guard */
    bool allocation_frozen;         /* sealed leaf: no reuse until SB sync */
    /* Read by iface_error in alloc.c, outside header-only analysis. */
    // cppcheck-suppress unusedStructMember
    bfs_err_t last_error;           /* reason the last allocation returned NULL */

    /* Emergency pool: last-resort blocks when reserve is empty during COW.
     * Points to the live superblock within the transaction manager. */
    bfs_superblock_t *sb;
    const bfs_superblock_t *committed_sb; /* last published roots, if mounted */
    /* Wired only by mounted filesystem setup; format/standalone remain NULL. */
    const bool *mounted_state;
    const bool *snapshot_state; /* includes in-progress snapshot creation */
    const bool *readonly_state;
    bfs_err_t *recovery_state; /* sticky mounted-owner ownership error */
    uint64_t metadata_reuse_txn; /* volatile warmup; never persisted */
    uint32_t metadata_requests;
} bfs_freespace_t;

/* Initialize the free space allocator. free_tree_root is the root of
 * an existing free space B+tree (from superblock), or BFS_BLK_NULL
 * for a freshly formatted volume. */
bfs_err_t bfs_freespace_init(bfs_freespace_t *fs, bfs_bio_t *bio,
                         bfs_blk_t free_tree_root, uint64_t txn_id);

/* Add a free extent to the tree (used during format or free_blocks). */
bfs_err_t bfs_freespace_add(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count);

/* Allocate count contiguous blocks. Returns the starting block number,
 * or BFS_BLK_NULL on failure. Uses first-fit with roving pointer. Mounted
 * read-only/recovery-latched owners reject allocation before any mutation.
 * A nonzero tree.free_sink_err means ownership is uncertain: standalone
 * callers must abandon/recover rather than publish or retry that live state. */
bfs_blk_t bfs_freespace_alloc(bfs_freespace_t *fs, uint32_t count);

/* Allocate count contiguous blocks for file data. If goal is not
 * BFS_BLK_NULL and [goal, goal + count) is free, that range is taken, so a
 * file that grows block by block stays contiguous. Otherwise the first fit at
 * or after the data roving pointer is taken. File data never takes the tail
 * of the highest extent, where single metadata blocks are allocated, so the
 * two do not interleave. Failure reporting is as for bfs_freespace_alloc. */
bfs_blk_t bfs_freespace_alloc_data(bfs_freespace_t *fs, uint32_t count,
                                   bfs_blk_t goal);

/* True if blk is one of the superblock's emergency pool blocks, active or
 * not. Freeing such a block alone returns it to the pool. */
bool bfs_freespace_pool_block(const bfs_freespace_t *fs, bfs_blk_t blk);

/* Free count blocks starting at start. Merges with adjacent free extents. */
bfs_err_t bfs_freespace_free(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count);

/* Atomically reclaim sorted, distinct single blocks when the free tree has a
 * single leaf. Returns BFS_ERR_UNSUPPORTED without reclaiming any input if the
 * bounded leaf fast path cannot represent the result; callers then use the
 * ordinary range-free path. Intended for post-publication pending frees. */
bfs_err_t bfs_freespace_free_sorted_blocks(bfs_freespace_t *fs,
                                           const bfs_blk_t *blocks,
                                           uint32_t count);

/* Get the bfs_allocator_t interface (for B+tree COW use) */
bfs_allocator_t *bfs_freespace_allocator(bfs_freespace_t *fs);

/* Refill the reserve pool from the free space tree */
bfs_err_t bfs_freespace_refill_reserve(bfs_freespace_t *fs);

/* Return unused reserve blocks to the free tree (called at transaction commit). */
bfs_err_t bfs_freespace_return_reserve(bfs_freespace_t *fs);

/* Post-publication settlement retains the established emergency-slot return
 * path: a mixed batch can keep retiring one ordinary root indefinitely. */
bfs_err_t bfs_freespace_settle_reserve(bfs_freespace_t *fs);

struct bfs_fs;
/* Mounted commit only, under the filesystem write lock. BFS_OK/false declines
 * before staging; every other error forbids ordinary fallback. On true, pending
 * ownership remains recorded and allocation is frozen until successful SB sync.
 * The caller must latch any error and abandon/recover that working state. */
bfs_err_t bfs_freespace_seal_commit(struct bfs_fs *owner, bool *sealed);

#endif /* BFS_ALLOC_H */
