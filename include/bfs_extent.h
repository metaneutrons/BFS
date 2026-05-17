/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Per-file extent tree
 *
 * Maps file-relative block offsets to physical disk blocks.
 * Uses the generic B+tree with key = file_block (uint32_t),
 * value = {disk_block, length} (two uint32_t).
 *
 */

#ifndef BFS_EXTENT_H
#define BFS_EXTENT_H

#include "bfs_btree.h"
#include "bfs_alloc.h"

/* Extent value stored in the B+tree (12 bytes) */
typedef struct {
    uint32_t disk_block;  /* physical block number (big-endian) */
    uint32_t length;      /* number of contiguous blocks (big-endian) */
    uint32_t data_crc32;  /* CRC32 of data (big-endian, 0 if checksums disabled) */
} bfs_extent_val_t;

_Static_assert(sizeof(bfs_extent_val_t) == 12, "extent_val size");

/* Extent tree handle — wraps a B+tree for a single file */
typedef struct {
    bfs_btree_t tree;
    bfs_freespace_t *fs;  /* for allocating/freeing data blocks */
    bool data_checksums;   /* compute/verify per-extent data CRC32 */
} bfs_extent_tree_t;

/* Initialize an extent tree for a file.
 * root = BFS_BLK_NULL for a new (empty) file. */
bfs_err_t bfs_extent_init(bfs_extent_tree_t *et, bfs_bio_t *bio,
                      bfs_freespace_t *fs, bfs_blk_t root, uint64_t txn_id);

/* Look up the physical block for a file-relative block offset.
 * Returns BFS_OK and sets *disk_block, or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_extent_lookup(bfs_extent_tree_t *et, uint32_t file_block,
                              bfs_blk_t *disk_block);

/* Append 'count' blocks to the end of the file (at file_block offset).
 * Allocates physical blocks from the free space allocator.
 * Returns BFS_OK or error. Sets *disk_block_out to the first allocated block. */
bfs_err_t bfs_extent_append(bfs_extent_tree_t *et, uint32_t file_block,
                              uint32_t count, bfs_blk_t *disk_block_out);

/* Truncate: free all extents with file_block >= from_block.
 * Returns freed data blocks to the free space allocator. */
bfs_err_t bfs_extent_truncate(bfs_extent_tree_t *et, uint32_t from_block);
bfs_err_t bfs_extent_truncate_batch(bfs_extent_tree_t *et, uint32_t from_block,
                                     uint32_t max_ops);

/* Get the current root block of the extent tree (for storing in inode). */
static inline bfs_blk_t bfs_extent_root(const bfs_extent_tree_t *et) {
    return et->tree.root;
}

/* Look up full extent value for a file block (for CRC verification).
 * Returns BFS_OK and fills val_out, or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_extent_lookup_val(bfs_extent_tree_t *et, uint32_t file_block,
                                  bfs_extent_val_t *val_out);

/* Update the data_crc32 field of an extent entry. */
bfs_err_t bfs_extent_update_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                  uint32_t crc);

#endif /* BFS_EXTENT_H */
