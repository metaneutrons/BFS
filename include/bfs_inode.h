/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode B+tree
 *
 * Persists file/directory metadata (size, extent_root, timestamps).
 * Key = inode_nr (uint32_t), Value = bfs_inode_t (44 bytes).
 */

#ifndef BFS_INODE_H
#define BFS_INODE_H

#include "bfs_btree.h"
#include "bfs_ondisk.h"

bfs_err_t bfs_inode_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id);

bfs_err_t bfs_inode_read(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out);

bfs_err_t bfs_inode_write(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode);

bfs_err_t bfs_inode_delete(bfs_btree_t *tree, uint32_t ino);

#endif /* BFS_INODE_H */
