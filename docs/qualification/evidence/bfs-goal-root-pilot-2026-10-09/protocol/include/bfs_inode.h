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

#endif /* BFS_INODE_H */
