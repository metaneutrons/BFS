/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block reference counting (for snapshot support)
 *
 * Tracks how many trees reference each block. Blocks with refcount > 1
 * are shared between the live filesystem and one or more snapshots.
 * COW must decrement refcount instead of freeing shared blocks.
 *
 * The refcount tree is only created when the first snapshot is taken.
 * When refcount_tree_root == 0, all blocks are implicitly refcount=1
 * and the fast path (no refcount checks) is used.
 *
 * Key: uint32_t block_number (big-endian)
 * Val: uint32_t refcount (big-endian)
 *
 * Blocks NOT in the tree have implicit refcount=1 (unshared).
 * Blocks in the tree with refcount=1 are transitional (will be removed on next dec).
 */

#ifndef BFS_REFCOUNT_H
#define BFS_REFCOUNT_H

#include "bfs_btree.h"

typedef struct {
    bfs_btree_t tree;
} bfs_refcount_t;

/* Initialize refcount tree. root=BFS_BLK_NULL if no snapshots exist. */
bfs_err_t bfs_refcount_init(bfs_refcount_t *rc, bfs_bio_t *bio,
                             bfs_allocator_t *alloc, bfs_blk_t root,
                             uint64_t txn_id);

/* Increment refcount for a block. If not in tree, inserts with refcount=2. */
bfs_err_t bfs_refcount_inc(bfs_refcount_t *rc, bfs_blk_t blk);

/* Decrement refcount. Returns true via *freed if refcount reached 0 (caller should free). */
bfs_err_t bfs_refcount_dec(bfs_refcount_t *rc, bfs_blk_t blk, bool *freed);

/* Get current refcount (1 if not in tree). */
uint32_t bfs_refcount_get(bfs_refcount_t *rc, bfs_blk_t blk);

#endif /* BFS_REFCOUNT_H */
