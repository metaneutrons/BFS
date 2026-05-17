/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — B+tree engine
 *
 * Generic B+tree stored in disk blocks. Used for:
 *   - Directory index (key=dirkey, value=inode_nr)
 *   - Extent tree (key=file_block, value=extent)
 *   - Free space tree (key=block_nr, value=length)
 *
 * The tree is parameterized by a btree_ops vtable that defines
 * key comparison, key/value sizes, and serialization.
 *
 * All mutations are copy-on-write: modified nodes are written to
 * new blocks, and the old blocks are freed only after the transaction
 * commits (superblock write).
 */

#ifndef BFS_BTREE_H
#define BFS_BTREE_H

#include "bfs_types.h"
#include "bfs_bio.h"
#include "bfs_ondisk.h"

/* ── Block allocator interface ─────────────────────────────── */

/* Allocate/free a single block. Used by the B+tree for COW.
 * The bootstrap allocator is a simple bump allocator;
 * later replaced by the free-space-tree allocator. */
typedef struct bfs_allocator {
    bfs_blk_t (*alloc)(struct bfs_allocator *a);
    void (*dealloc)(struct bfs_allocator *a, bfs_blk_t blk);
    void *ctx;
} bfs_allocator_t;

/* ── B+tree operations vtable ──────────────────────────────── */

#define BFS_MAX_KEY_SIZE 512  /* Must accommodate largest key (DIR_KEY_SIZE=264) */

typedef struct bfs_btree_ops {
    /* Compare two keys. Returns <0, 0, >0. */
    int (*key_compare)(const void *a, const void *b);

    /* Fixed size of a key in bytes (as stored in node) */
    uint32_t key_size;

    /* Fixed size of a value in bytes (leaf only) */
    uint32_t val_size;
} bfs_btree_ops_t;

/* ── B+tree handle ─────────────────────────────────────────── */

typedef struct bfs_btree {
    bfs_bio_t       *bio;
    bfs_allocator_t *alloc;
    const bfs_btree_ops_t *ops;
    bfs_blk_t        root;      /* root block number (0 = empty tree) */
    uint32_t          height;    /* tree height (0 = empty, 1 = root is leaf) */
    const uint64_t  *txn_id_ptr; /* pointer to current transaction id */
    uint64_t          txn_id_fallback; /* used if txn_id_ptr is NULL */

    /* Back-pointer to bfs_fs_t for pending-frees tracking (NULL if standalone) */
    void             *fs_ctx;
} bfs_btree_t;

static inline uint64_t bfs_btree_txn_id(const bfs_btree_t *tree)
{
    return tree->txn_id_ptr ? *tree->txn_id_ptr : tree->txn_id_fallback;
}

/* ── Scan callback ─────────────────────────────────────────── */

/* Return false to stop scanning */
typedef bool (*bfs_scan_cb)(const void *key, const void *val, void *ctx);

/* ── Public API ────────────────────────────────────────────── */

/* Initialize a tree handle. root=0 means empty tree. */
bfs_err_t bfs_btree_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, const bfs_btree_ops_t *ops,
                     bfs_blk_t root, uint64_t txn_id);

/* Search for a key. Returns BFS_OK and copies value to val_out,
 * or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_btree_search(bfs_btree_t *tree, const void *key, void *val_out);

/* Insert a key/value pair. Returns BFS_OK, BFS_ERR_EXISTS, or error.
 * Updates tree->root if the root splits. */
bfs_err_t bfs_btree_insert(bfs_btree_t *tree, const void *key, const void *val);

/* Delete a key. Returns BFS_OK or BFS_ERR_NOTFOUND.
 * (Implemented in Task 5) */
bfs_err_t bfs_btree_delete(bfs_btree_t *tree, const void *key);

/* Update a key's value in-place with COW. Single traversal.
 * Returns BFS_OK or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_btree_update(bfs_btree_t *tree, const void *key, const void *new_val);

/* Scan keys >= start_key. Calls cb for each key/value pair.
 * If start_key is NULL, scans from the beginning. */
bfs_err_t bfs_btree_scan(bfs_btree_t *tree, const void *start_key,
                           bfs_scan_cb cb, void *ctx);

/* Search for the largest key <= search_key.
 * Returns BFS_OK if found, BFS_ERR_NOTFOUND if tree is empty or all keys > search_key.
 * On success, copies the found key to key_out and value to val_out. */
bfs_err_t bfs_btree_search_floor(bfs_btree_t *tree, const void *key,
                                    void *key_out, void *val_out);


/* Walk all node blocks in the tree (for fsck). Calls cb(blk, ctx) for each. */
typedef void (*bfs_node_walk_cb)(bfs_blk_t blk, void *ctx);
void bfs_btree_walk_nodes(bfs_btree_t *tree, bfs_node_walk_cb cb, void *ctx);

/* Online compaction: re-packs tree into new contiguous blocks.
 * Returns BFS_OK and updates tree->root on success.
 * Old blocks are leaked into pending_frees for reclamation. */
bfs_err_t bfs_btree_compact(bfs_btree_t *tree);

#endif /* BFS_BTREE_H */
