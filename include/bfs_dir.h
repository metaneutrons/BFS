/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Directory B+tree
 *
 * Single global directory tree. Key is composite:
 *   (parent_id:u32, name_hash:u32, name_len:u8, name:up to 255 bytes)
 * Value is inode number (u32).
 *
 * Case-insensitive lookup using Amiga international character folding.
 * FNV-1a hash of the case-folded name for fast comparison.
 */

#ifndef BFS_DIR_H
#define BFS_DIR_H

#include "bfs_btree.h"
#include "bfs_alloc.h"
#include "bfs_ondisk.h"

/* Directory entry value (stored in B+tree) */
typedef struct BFS_PACKED {
    uint32_t inode_nr;    /* big-endian */
    uint32_t entry_type;  /* BFS_INODE_FILE/DIR/etc, big-endian */
} bfs_dir_val_t;

_Static_assert(sizeof(bfs_dir_val_t) == 8, "dir_val size");

/* Directory tree handle */
typedef struct {
    bfs_btree_t tree;
} bfs_dir_tree_t;

/* Scan callback: called for each entry in a directory */
typedef bool (*bfs_dir_scan_cb)(const char *name, uint8_t name_len,
                                  uint32_t inode_nr, uint32_t entry_type,
                                  void *ctx);

/* Initialize the directory tree. root = BFS_BLK_NULL for empty. */
bfs_err_t bfs_dir_init(bfs_dir_tree_t *dt, bfs_bio_t *bio,
                   bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id);

/* Look up a name in a directory. Case-insensitive.
 * Returns BFS_OK and fills inode_nr, or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_dir_lookup(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t *inode_nr_out, uint32_t *type_out);

/* Insert a directory entry. Returns BFS_ERR_EXISTS if name already exists. */
bfs_err_t bfs_dir_insert(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t inode_nr, uint32_t entry_type);

/* Remove a directory entry. */
bfs_err_t bfs_dir_remove(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len);

/* Scan all entries in a directory (for ExAll/ExNext). */
bfs_err_t bfs_dir_scan(bfs_dir_tree_t *dt, uint32_t parent_id,
                         bfs_dir_scan_cb cb, void *ctx);

/* FNV-1a hash of case-folded name (exposed for testing) */
uint32_t bfs_dir_name_hash(const char *name, uint8_t len);

/* Case-fold a character (Amiga international) */
uint8_t bfs_intl_toupper(uint8_t c);

#endif /* BFS_DIR_H */
