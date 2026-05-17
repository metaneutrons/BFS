/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Transaction manager
 *
 * COW transaction model:
 *   txn_begin  → snapshot superblock, start tracking changes
 *   (mutations → all COW'd, tree roots updated in working copy)
 *   txn_commit → write new superblock (atomic commit point)
 *   txn_abort  → discard working copy, revert to snapshot
 *
 * Crash recovery:
 *   On mount, read both superblocks, pick highest valid txn_id.
 *   No log replay needed — the COW model ensures the committed
 *   state is always consistent.
 */

#ifndef BFS_TXN_H
#define BFS_TXN_H

#include "bfs_superblock.h"
#include "bfs_btree.h"

typedef struct {
    bfs_bio_t *bio;
    bfs_superblock_t sb;       /* snapshot at txn_begin */
    bfs_superblock_t sb_new;   /* working copy, updated during txn */
    bool active;
} bfs_txn_t;

/* Begin a transaction: snapshot current superblock state */
bfs_err_t bfs_txn_begin(bfs_txn_t *txn, bfs_bio_t *bio);

/* Update tree roots in the working superblock copy */
void bfs_txn_set_dir_root(bfs_txn_t *txn, bfs_blk_t root);
void bfs_txn_set_extent_root(bfs_txn_t *txn, bfs_blk_t root);
void bfs_txn_set_free_root(bfs_txn_t *txn, bfs_blk_t root);
void bfs_txn_set_free_blocks(bfs_txn_t *txn, uint32_t count);
void bfs_txn_set_inode_root(bfs_txn_t *txn, bfs_blk_t root);

/* Commit: write updated superblock with new tree roots */
bfs_err_t bfs_txn_commit(bfs_txn_t *txn);

/* Abort: discard changes */
void bfs_txn_abort(bfs_txn_t *txn);

/* Get current txn_id */
uint64_t bfs_txn_id(const bfs_txn_t *txn);

#endif /* BFS_TXN_H */
