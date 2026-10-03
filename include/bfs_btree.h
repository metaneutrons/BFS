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
    /* Return caller-owned, immediately reclaimable storage, including unwritten
     * or partially written abort scratch. Older/shared nodes must be deferred
     * by their owner instead; the allocator cannot infer ownership from bytes. */
    bfs_err_t (*dealloc)(struct bfs_allocator *a, bfs_blk_t blk);
    /* Explains a BFS_BLK_NULL allocation result. Optional allocators default to
     * BFS_ERR_NOSPC when this callback is absent. */
    /* Invoked by allocator_failure in btree.c. */
    // cppcheck-suppress unusedStructMember
    bfs_err_t (*error)(struct bfs_allocator *a);
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

    /* Opt in only when key_compare is pure and defines a stable order for bytes.
     * Invalidate the BIO cache before changing comparator semantics. Default
     * false retains full structural validation for arbitrary comparators. */
    bool cache_key_order;
} bfs_btree_ops_t;

/* ── Engine limits ─────────────────────────────────────────── */

/* Maximum B+tree height the engine traverses; a tree deeper than this is
 * treated as corrupt (the descent loops abort with BFS_ERR_CORRUPT). */
#define BFS_BTREE_MAX_DEPTH 32

/* Worst-case number of OLD-transaction node blocks a single insert or delete
 * hands to the deferred-free sink. Delete dominates: along the root-to-leaf
 * path it COWs and at most merges/borrows one extra node per level (≤ 2·depth),
 * plus the leaf rebalance and the root collapse. Callers reserve this much
 * free-queue headroom per tree mutation so the in-COW defer() never overflows. */
#define BFS_BTREE_MAX_OP_FREES (2u * BFS_BTREE_MAX_DEPTH + 1u)

/* ── B+tree handle ─────────────────────────────────────────── */

/* ── Deferred-free sink ────────────────────────────────────── */

/* Metadata blocks that B-tree mutations allocated in the live transaction.
 * No committed superblock or snapshot references them, so they are the only
 * nodes the engine may rewrite in place. The on-disk txn_id alone is not proof
 * of ownership: a damaged image can carry it on committed nodes. Entries are
 * valid only while txn_id equals the live transaction; a different id clears
 * the set. If the set cannot grow, a block is simply not registered and keeps
 * the copy-on-write path. The members are used in btree.c and fs.c. */
typedef struct bfs_btree_owned {
    // cppcheck-suppress unusedStructMember
    bfs_blk_t *slots;     /* open addressing; BFS_BLK_NULL empty, UINT32_MAX removed */
    // cppcheck-suppress unusedStructMember
    uint32_t   capacity;  /* power of two, or 0 before first use */
    // cppcheck-suppress unusedStructMember
    uint32_t   used;      /* occupied plus removed slots */
    // cppcheck-suppress unusedStructMember
    uint64_t   txn_id;    /* transaction the entries belong to */
    /* Keep every change copy-on-write, e.g. to qualify the reserve and
     * metadata-stock paths that only run under copy-on-write churn. */
    // cppcheck-suppress unusedStructMember
    bool       disabled;
    /* Owner's sticky recovery error, set when an in-place rewrite fails. */
    // cppcheck-suppress unusedStructMember
    bfs_err_t *recovery_state;
} bfs_btree_owned_t;

/* Forget every entry, e.g. after reloading the committed state. */
void bfs_btree_owned_reset(bfs_btree_owned_t *owned);
/* Release the set's memory. */
void bfs_btree_owned_destroy(bfs_btree_owned_t *owned);

/* During COW the engine frees blocks that belonged to an older transaction;
 * they can only return to the allocator after the current transaction commits,
 * so they are handed to this sink (the filesystem's pending-free queue). This
 * keeps the generic B+tree engine free of any bfs_fs_t knowledge. A zeroed sink
 * (defer == NULL) is a standalone tree with no deferral. */
typedef struct {
    void *ctx;
    /* Queue blk for post-commit free; returns BFS_OK, or BFS_ERR_AGAIN if full.
     * Must NOT sync — it is called in the middle of a COW. */
    bfs_err_t (*defer)(void *ctx, bfs_blk_t blk);
    /* Free slots in the queue right now (for all-or-nothing batch sizing). */
    uint32_t (*headroom)(void *ctx);
    /* Optional memory-only growth before a mutation; must NOT commit or drain. */
    /* Invoked by bfs_extent_truncate in extent.c. */
    // cppcheck-suppress unusedStructMember
    bfs_err_t (*reserve)(void *ctx, uint32_t slots);
    /* Fixed capacity when reserve is absent; baseline capacity otherwise. */
    uint32_t capacity;
    /* Live-transaction node ownership; NULL keeps every change copy-on-write.
     * Set by bfs_fs_free_sink in fs.c, read in btree.c. */
    // cppcheck-suppress unusedStructMember
    bfs_btree_owned_t *owned;
} bfs_free_sink_t;

typedef struct bfs_btree {
    bfs_bio_t       *bio;
    bfs_allocator_t *alloc;
    const bfs_btree_ops_t *ops;
    bfs_blk_t        root;      /* root block number (0 = empty tree) */
    uint32_t          height;    /* tree height (0 = empty, 1 = root is leaf) */
    const uint64_t  *txn_id_ptr; /* pointer to current transaction id */
    uint64_t          txn_id_fallback; /* used if txn_id_ptr is NULL */

    /* Where COW'd old blocks go for deferred free (zeroed = standalone tree). */
    bfs_free_sink_t   free_sink;

    /* Sticky ownership error from reclamation or a failed composite rollback.
     * Callers must recover or abandon instead of publishing uncertain mappings. */
    bfs_err_t         free_sink_err;
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

/* Give an empty tree a root leaf holding count sorted entries, in one
 * mutation: on failure the tree stays empty. */
bfs_err_t bfs_btree_create_root_leaf(bfs_btree_t *tree, const void *keys,
                                     const void *vals, uint32_t count);

/* Replace a height-one tree with a complete, sorted leaf in one COW step. */
bfs_err_t bfs_btree_replace_root_leaf(bfs_btree_t *tree, const void *keys,
                                      const void *vals, uint32_t count);

/* As bfs_btree_replace_root_leaf, but a root the live transaction owns is
 * rewritten in place, so the root block may stay the same. *published is set
 * once the replacement is committed to the tree, even if the call then
 * reports a latched publication or reclamation error. */
bfs_err_t bfs_btree_rewrite_root_leaf(bfs_btree_t *tree, const void *keys,
                                      const void *vals, uint32_t count,
                                      bool *published);

/* Read the transaction id of a valid height-one root leaf. */
bfs_err_t bfs_btree_root_leaf_txn_id(bfs_btree_t *tree, uint64_t *txn_id_out);

/* Replace the expected current root leaf without reclaiming it. On success,
 * ownership of the old root block passes to the caller. */
bfs_err_t bfs_btree_replace_owned_root_leaf(bfs_btree_t *tree,
                                            bfs_blk_t expected_root,
                                            const void *keys, const void *vals,
                                            uint32_t count);

/* Maximum entries that fit in one leaf, or zero for an invalid tree. */
uint32_t bfs_btree_leaf_capacity(const bfs_btree_t *tree);

/* Delete a key. Returns BFS_OK or BFS_ERR_NOTFOUND.
 * (Implemented in Task 5) */
bfs_err_t bfs_btree_delete(bfs_btree_t *tree, const void *key);

/* Update a key's value in-place with COW. Single traversal.
 * Returns BFS_OK or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_btree_update(bfs_btree_t *tree, const void *key, const void *new_val);

/* Replace a stored key with a byte-distinct key that compares equal. This is
 * used for case-only directory renames: ordering is unchanged, but the stored
 * display spelling changes. */
bfs_err_t bfs_btree_rekey_equal(bfs_btree_t *tree, const void *old_key,
                                const void *new_key);

/* Scan keys >= start_key. Calls cb for each key/value pair. Traversal follows
 * parent/child links, not the on-disk right_sibling legacy leaf hint. If
 * start_key is NULL, scanning starts from the beginning. */
bfs_err_t bfs_btree_scan(bfs_btree_t *tree, const void *start_key,
                           bfs_scan_cb cb, void *ctx);

/* Search for the largest key <= search_key.
 * Returns BFS_OK if found, BFS_ERR_NOTFOUND if tree is empty or all keys > search_key.
 * On success, copies the found key to key_out and value to val_out. */
bfs_err_t bfs_btree_search_floor(bfs_btree_t *tree, const void *key,
                                    void *key_out, void *val_out);

/* Locate the first key >= key without copying resident nodes. BFS_OK: it is
 * copied to key_out. BFS_ERR_AGAIN: the leaf that would hold it has none;
 * every greater key is >= key_out, that leaf's exclusive upper bound.
 * BFS_ERR_NOTFOUND: no key >= key exists. */
bfs_err_t bfs_btree_lower_bound(bfs_btree_t *tree, const void *key, void *key_out);


/* Walk all node blocks in the tree. Calls cb(blk, ctx) for each. Returns the
 * first node-read/structural error, or BFS_OK. Refcounting callers must check. */
typedef void (*bfs_node_walk_cb)(bfs_blk_t blk, void *ctx);
bfs_err_t bfs_btree_walk_nodes(bfs_btree_t *tree, bfs_node_walk_cb cb, void *ctx);

/* One pass over the tree that reads each node once: entry_cb (optional) sees
 * every leaf entry in key order and may stop the walk by returning false;
 * node_cb (optional) sees each node block after its subtree. Errors as for
 * bfs_btree_walk_nodes; a stop by entry_cb is not an error. */
bfs_err_t bfs_btree_walk(bfs_btree_t *tree, bfs_node_walk_cb node_cb,
                         bfs_scan_cb entry_cb, void *ctx);

/* Compaction, build-and-swap half: re-pack the tree into fresh dense blocks and
 * swap tree->root to them, WITHOUT freeing the old nodes. Sets *old_root_out to
 * the pre-swap root; if it stays equal to tree->root, no compaction happened
 * (already dense, or empty). The caller must commit the swap and then free the
 * old nodes post-commit (bfs_btree_free_block) — a whole-old-tree free mid-COW
 * could overflow the deferred-free queue, so it must not happen here. fs trees
 * go through bfs_fs_compact_tree, which wires both halves correctly. */
bfs_err_t bfs_btree_compact_build_swap(bfs_btree_t *tree, bfs_blk_t *old_root_out);

/* Read node block `blk` and free it (defers it if it belongs to an older
 * transaction, deallocs immediately if current). Lets the fs-level compaction
 * free an old tree's nodes one at a time, draining the queue between them. A
 * read/alloc failure leaves the block unfreed rather than crashing. */
bfs_err_t bfs_btree_free_block(bfs_btree_t *tree, bfs_blk_t blk);

#endif /* BFS_BTREE_H */
