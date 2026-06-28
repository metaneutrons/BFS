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
    uint32_t total_free;            /* total free blocks (accounting) */
    uint32_t global_reserve;        /* blocks reserved for metadata (not data) */
    bool in_alloc;                  /* recursion guard */

    /* Emergency pool: last-resort blocks when reserve is empty during COW.
     * Points to the live superblock within the transaction manager. */
    bfs_superblock_t *sb;
} bfs_freespace_t;

/* Initialize the free space allocator. free_tree_root is the root of
 * an existing free space B+tree (from superblock), or BFS_BLK_NULL
 * for a freshly formatted volume. */
bfs_err_t bfs_freespace_init(bfs_freespace_t *fs, bfs_bio_t *bio,
                         bfs_blk_t free_tree_root, uint64_t txn_id);

/* Add a free extent to the tree (used during format or free_blocks). */
bfs_err_t bfs_freespace_add(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count);

/* Allocate count contiguous blocks. Returns the starting block number,
 * or BFS_BLK_NULL on failure. Uses first-fit with roving pointer. */
bfs_blk_t bfs_freespace_alloc(bfs_freespace_t *fs, uint32_t count);

/* Free count blocks starting at start. Merges with adjacent free extents. */
bfs_err_t bfs_freespace_free(bfs_freespace_t *fs, bfs_blk_t start, uint32_t count);

/* Get the bfs_allocator_t interface (for B+tree COW use) */
bfs_allocator_t *bfs_freespace_allocator(bfs_freespace_t *fs);

/* Refill the reserve pool from the free space tree */
bfs_err_t bfs_freespace_refill_reserve(bfs_freespace_t *fs);

#endif /* BFS_ALLOC_H */
