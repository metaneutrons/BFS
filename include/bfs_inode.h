/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode B+tree
 *
 * Persists file/directory metadata (size, extent mapping, flags, timestamps).
 * Key = inode_nr (uint32_t), Value = bfs_inode_t (56 bytes).
 */

#ifndef BFS_INODE_H
#define BFS_INODE_H

#include "bfs_btree.h"
#include "bfs_ondisk.h"

/* Host-order timestamp; on-disk encoding remains in the common inode code. */
typedef struct {
    uint16_t days;
    uint16_t mins;
    uint16_t ticks;
} bfs_inode_stamp_t;

/* Non-failing clock sampler. Fully initialize stamp; do not reenter BFS or
 * mutate its BIO/cache. Called under the filesystem's exclusive operation lock. */
typedef void (*bfs_inode_stamp_fn)(void *context, bfs_inode_stamp_t *stamp);

/* Set modification fields, and optionally creation fields, in a private inode.
 * Does not change any other field or perform I/O. Both pointers must be valid. */
void bfs_inode_apply_stamp(bfs_inode_t *inode, const bfs_inode_stamp_t *stamp,
                           bool creation);

bfs_err_t bfs_inode_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id);

bfs_err_t bfs_inode_read(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out);

/* Read count inodes whose numbers do not descend. Neighbours in one leaf
 * share a single view of it instead of one search each. results[i] is what
 * bfs_inode_read
 * would return for inos[i]. Returns BFS_ERR_INVAL for descending numbers and
 * an error only if the batch cannot start; otherwise BFS_OK. */
bfs_err_t bfs_inode_read_sorted(bfs_btree_t *tree, const uint32_t *inos, uint32_t count,
                                bfs_inode_t *out, bfs_err_t *results);

/* Structural checks shared by every reader: identity, type, flags, and the
 * extent fields. The link-count state is checked by the read functions. */
bool bfs_inode_valid(const bfs_btree_t *tree, uint32_t ino,
                     const bfs_inode_t *inode);

/* Read/write a zero-link inode retained solely for an open POSIX handle. Such
 * inodes are never reachable through the namespace and are reclaimed on the
 * next writable mount if their last handle was lost in a crash. */
bfs_err_t bfs_inode_read_unlinked(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out);

bfs_err_t bfs_inode_write(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode);

bfs_err_t bfs_inode_write_unlinked(bfs_btree_t *tree, uint32_t ino,
                                   const bfs_inode_t *inode);

bfs_err_t bfs_inode_delete(bfs_btree_t *tree, uint32_t ino);

/*
 * Inode write-back within a transaction (docs/plans/bfs-inode-write-back-v1.md).
 *
 * A tree with an attached table holds published inodes until the next
 * commit. The first publication of an inode in a transaction writes the tree,
 * so the path to its leaf belongs to the transaction and writing the copy
 * later needs no deferred-free headroom; later publications of that inode in
 * the same transaction only replace the pending copy. Every read returns the
 * pending copy, every other write or delete of the inode replaces it in the
 * tree and then drops it, and bfs_inode_flush_pending writes the pending
 * copies before the transaction commits. Snapshot views have no table.
 */
#define BFS_INODE_PENDING_SLOTS 8

typedef struct {
    uint32_t    ino;     /* 0: free */
    bool        dirty;   /* newer than the tree */
    uint64_t    txn_id;  /* transaction of the tree write that made it writable */
    bfs_inode_t inode;
} bfs_inode_pending_slot_t;

typedef struct bfs_inode_pending {
    uint32_t used;    /* occupied slots; reads skip the table when 0 */
    uint32_t version; /* changes with every change of a slot */
    bfs_inode_pending_slot_t slot[BFS_INODE_PENDING_SLOTS];
} bfs_inode_pending_t;

/* Attach an emptied table to a live inode tree. */
void bfs_inode_pending_attach(bfs_btree_t *tree, bfs_inode_pending_t *pending);

/* Drop every pending copy without writing it, as uncommitted tree changes
 * are dropped when the committed state is reloaded. */
void bfs_inode_pending_discard(bfs_btree_t *tree);

/* Publish a linked inode. Without a table, or when the inode was not yet
 * written in this transaction, this is bfs_inode_write. */
bfs_err_t bfs_inode_publish(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode);

/* A value that changes whenever a read of the tree's inodes could return
 * something else through the table: with the tree's root and generation it
 * tells a reader whether an inode it read is still current. 0 without a
 * table. */
uint32_t bfs_inode_pending_version(const bfs_btree_t *tree);

/* The pending copy of ino, or NULL. Valid until the next call that changes
 * the tree or the table; for readers that walk the tree directly. */
const bfs_inode_t *bfs_inode_pending_peek(const bfs_btree_t *tree, uint32_t ino);

/* Write every pending copy into the tree and empty the table. Required
 * before the tree's root is committed or its nodes are walked directly. On
 * error the table is emptied too; the transaction must not be committed. */
bfs_err_t bfs_inode_flush_pending(bfs_btree_t *tree);

#endif /* BFS_INODE_H */
