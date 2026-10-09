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

/* Extent mapping of one file. It is either empty, one inline extent at
 * logical block 0 stored in the inode, or a B+tree rooted at tree.root; never
 * both. Mapping changes convert an inline extent into a tree when needed. */
typedef struct {
    bfs_btree_t tree;
    bfs_freespace_t *fs;  /* for allocating/freeing data blocks */
    bool data_checksums;   /* compute/verify per-extent data CRC32 */
    // cppcheck-suppress unusedStructMember
    bfs_blk_t inline_start; /* host order; meaningful when inline_length != 0 */
    uint32_t inline_length;
    // cppcheck-suppress unusedStructMember
    uint32_t inline_crc;
} bfs_extent_tree_t;

/* Initialize an extent tree for a file.
 * root = BFS_BLK_NULL for a new (empty) file. */
bfs_err_t bfs_extent_init(bfs_extent_tree_t *et, bfs_bio_t *bio,
                      bfs_freespace_t *fs, bfs_blk_t root, uint64_t txn_id);

/* Initialize from the extent fields of an inode (tree root or inline extent). */
bfs_err_t bfs_extent_open(bfs_extent_tree_t *et, bfs_bio_t *bio,
                          bfs_freespace_t *fs, const bfs_inode_t *inode,
                          uint64_t txn_id);

/* Store the mapping into an inode's extent fields; other flags are kept. */
void bfs_extent_store(const bfs_extent_tree_t *et, bfs_inode_t *inode);

/* True when the inode's extent fields describe exactly this mapping. */
bool bfs_extent_matches(const bfs_extent_tree_t *et, const bfs_inode_t *inode);

static inline bool bfs_extent_is_inline(const bfs_extent_tree_t *et) {
    return et->inline_length != 0;
}

/* Look up the physical block for a file-relative block offset.
 * Returns BFS_OK and sets *disk_block, BFS_ERR_NOTFOUND for a hole, or the
 * underlying tree/corruption error. */
bfs_err_t bfs_extent_lookup(bfs_extent_tree_t *et, uint32_t file_block,
                              bfs_blk_t *disk_block);

/* As bfs_extent_lookup, and the number of contiguous blocks mapped from
 * file_block to the end of its extent (at least 1). */
bfs_err_t bfs_extent_lookup_run(bfs_extent_tree_t *et, uint32_t file_block,
                                bfs_blk_t *disk_block, uint32_t *run_blocks);

/* Append 'count' blocks to the end of the file (at file_block offset).
 * Allocates physical blocks from the free space allocator.
 * Returns BFS_OK or error. Sets *disk_block_out to the first allocated block. */
bfs_err_t bfs_extent_append(bfs_extent_tree_t *et, uint32_t file_block,
                              uint32_t count, bfs_blk_t *disk_block_out);

/* Insert one already allocated and initialized physical block. Ownership passes
 * to the extent tree only when this call succeeds. */
bfs_err_t bfs_extent_map_block(bfs_extent_tree_t *et, uint32_t file_block,
                               bfs_blk_t disk_block, uint32_t crc);

/* Insert an already allocated, initialized contiguous run. The on-disk extent
 * format has only one data CRC per record, so this is restricted to volumes
 * without data checksums. Ownership passes on success; after a failed insert,
 * free_sink_err must be checked before the caller reclaims the run. */
bfs_err_t bfs_extent_map_run(bfs_extent_tree_t *et, uint32_t file_block,
                             bfs_blk_t disk_block, uint32_t count);

/* Truncate at from_block, shortening an extent that straddles the boundary and
 * freeing all following extents. The batch form returns BFS_ERR_AGAIN when the
 * caller must commit/drain deferred frees before retrying. max_ops must be > 0. */
bfs_err_t bfs_extent_truncate(bfs_extent_tree_t *et, uint32_t from_block);
bfs_err_t bfs_extent_truncate_batch(bfs_extent_tree_t *et, uint32_t from_block,
                                     uint32_t max_ops);

/* Get the current root block of the extent tree; zero while the mapping is
 * empty or inline. */
static inline bfs_blk_t bfs_extent_root(const bfs_extent_tree_t *et) {
    return et->tree.root;
}

/* Look up full extent value for a file block (for CRC verification).
 * Returns BFS_OK and fills val_out, BFS_ERR_NOTFOUND for a hole, or the
 * underlying tree/corruption error. */
bfs_err_t bfs_extent_lookup_val(bfs_extent_tree_t *et, uint32_t file_block,
                                  bfs_extent_val_t *val_out);

/* Update the data_crc32 field of an extent entry. */
bfs_err_t bfs_extent_update_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                  uint32_t crc);

/* Remap one logical file block to a new physical block. If the logical block
 * lives inside a multi-block extent, the extent is split around that block. */
bfs_err_t bfs_extent_remap_block(bfs_extent_tree_t *et, uint32_t file_block,
                                   bfs_blk_t new_disk_block,
                                   bfs_blk_t *old_disk_block_out);

/* Remap one block and store its checksum in the same metadata update. */
bfs_err_t bfs_extent_remap_block_crc(bfs_extent_tree_t *et, uint32_t file_block,
                                     bfs_blk_t new_disk_block, uint32_t crc,
                                     bfs_blk_t *old_disk_block_out);

/* Walk every extent-tree node block (node_cb) and every data block (block_cb)
 * mapped by the inode. Either cb may be NULL; both receive (block, ctx). The
 * extent decode and length bounds-check live here. Returns the first error. */
bfs_err_t bfs_extent_walk(bfs_bio_t *bio, bfs_freespace_t *fsp, uint64_t txn_id,
                          const bfs_inode_t *inode, bfs_node_walk_cb node_cb,
                          bfs_node_walk_cb block_cb, void *ctx);

#endif /* BFS_EXTENT_H */
