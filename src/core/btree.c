/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — B+tree engine
 *
 * Node layout (within a single disk block):
 *
 *   [bfs_btnode_hdr_t]  (28 bytes)
 *   [key0][key1]...[keyN-1]
 *   -- leaf:     [val0][val1]...[valN-1]
 *   -- internal: [child0][child1]...[childN]  (N+1 child pointers, uint32_t each)
 *
 * Internal nodes: key[i] is the separator. child[i] contains keys < key[i],
 *                 child[i+1] contains keys >= key[i].
 *                 So N keys → N+1 children.
 *
 * Leaf nodes: key[i]/val[i] are the actual data. right_sibling links leaves.
 *
 * COW: on modification, allocate a new block, copy+modify, update parent.
 *      Old blocks are freed after transaction commit.
 */

#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_crc32.h"
#include "bfs_fs.h"
#include <string.h>
#include <stdlib.h>

#define MAX_TREE_DEPTH 32

static uint8_t *alloc_buf(const bfs_btree_t *tree) { return malloc(tree->bio->block_size); }

/* Node layout/capacity/CRC accessors live in bfs_btree_internal.h — shared with
 * the invariant test (tests/test_invariants.c) so it validates the real layout,
 * not a hand-kept copy. */

/* ── Node I/O ──────────────────────────────────────────────── */

static bfs_err_t node_read(const bfs_btree_t *tree, bfs_blk_t blk, uint8_t *buf)
{
    bfs_err_t err = bfs_bio_read(tree->bio, blk, buf);
    if (err != BFS_OK) return err;

    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    if (bfs_be32(hdr->magic) != BFS_NODE_MAGIC)
        return BFS_ERR_CORRUPT;
    if (bfs_be32(hdr->crc32) != node_compute_crc(tree, buf))
        return BFS_ERR_CORRUPT;

    /* Validate structural header fields read from disk before any accessor uses
     * num_keys to index into the fixed-size block buffer. The CRC only catches
     * accidental bit-rot, not a deliberately-consistent corrupt node crafted on
     * untrusted media. */
    {
        uint16_t level = bfs_be16(hdr->level);
        uint32_t nkeys = bfs_be32(hdr->num_keys);
        uint32_t max_keys = (level == BFS_BTNODE_LEAF)
                            ? leaf_max_keys(tree) : internal_max_keys(tree);
        if (level > MAX_TREE_DEPTH || nkeys > max_keys)
            return BFS_ERR_CORRUPT;
    }
    return BFS_OK;
}

static bfs_err_t node_write(const bfs_btree_t *tree, bfs_blk_t blk, uint8_t *buf)
{
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->magic = bfs_be32(BFS_NODE_MAGIC);
    hdr->txn_id = bfs_be64(bfs_btree_txn_id(tree));
    hdr->crc32 = 0;
    hdr->crc32 = bfs_be32(node_compute_crc(tree, buf));
    return bfs_bio_write(tree->bio, blk, buf);
}

static void node_init(const bfs_btree_t *tree, uint8_t *buf, uint16_t level)
{
    memset(buf, 0, tree->bio->block_size);
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->magic = bfs_be32(BFS_NODE_MAGIC);
    hdr->level = bfs_be16(level);
    hdr->num_keys = 0;
    hdr->right_sibling = 0;
}

/* hdr_of / num_keys / node_level / is_leaf are in bfs_btree_internal.h. */

/* ── Binary search within a node ───────────────────────────── */

static uint32_t node_search(const bfs_btree_t *tree, uint8_t *buf,
                            const void *search_key, bool *found)
{
    uint32_t lo = 0, hi = num_keys(buf);
    *found = false;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = tree->ops->key_compare(node_key(tree, buf, mid), search_key);
        if (cmp < 0)
            lo = mid + 1;
        else if (cmp > 0)
            hi = mid;
        else {
            *found = true;
            return mid;
        }
    }
    return lo;
}

/* ── Public: init and search ───────────────────────────────── */

bfs_err_t bfs_btree_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, const bfs_btree_ops_t *ops,
                     bfs_blk_t root, uint64_t txn_id)
{
    tree->bio = bio;
    tree->alloc = alloc;
    tree->ops = ops;
    tree->root = root;
    tree->height = 0;
    tree->txn_id_ptr = NULL;
    tree->txn_id_fallback = txn_id;
    tree->fs_ctx = NULL;

    if (root != BFS_BLK_NULL) {
        uint8_t *buf = malloc(bio->block_size);
        if (!buf) return BFS_ERR_NOMEM;
        bfs_err_t err = node_read(tree, root, buf);
        if (err != BFS_OK) { free(buf); return err; }
        tree->height = node_level(buf) + 1;
        free(buf);
    }
    return BFS_OK;
}

bfs_err_t bfs_btree_search(bfs_btree_t *tree, const void *key, void *val_out)
{
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_blk_t blk = tree->root;
    uint32_t depth = 0;

    while (1) {
        if (depth++ > MAX_TREE_DEPTH) { free(buf); return BFS_ERR_CORRUPT; }
        bfs_err_t err = node_read(tree, blk, buf);
        if (err != BFS_OK) { free(buf); return err; }

        bool found;
        uint32_t idx = node_search(tree, buf, key, &found);

        if (is_leaf(buf)) {
            if (!found) { free(buf); return BFS_ERR_NOTFOUND; }
            memcpy(val_out, leaf_val(tree, buf, idx), tree->ops->val_size);
            free(buf);
            return BFS_OK;
        }
        /* Internal: child[idx] has keys < key[idx], child[idx+1] has keys >= key[idx] */
        blk = get_child(tree, buf, found ? idx + 1 : idx);
    }
}

/* ── Insert helpers ────────────────────────────────────────── */

static bfs_err_t cow_node(bfs_btree_t *tree, bfs_blk_t old_blk, uint8_t *buf, bfs_blk_t *out_blk);

/* Centralized node deallocation. Blocks from the current transaction are 
 * freed immediately; older blocks are queued for post-commit reclamation. */
static void btree_free_node(bfs_btree_t *tree, bfs_blk_t blk, const uint8_t *buf)
{
    if (blk == BFS_BLK_NULL) return;
    const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
    uint64_t block_txn = bfs_be64(hdr->txn_id);

    if (tree->fs_ctx) {
        bfs_fs_t *fs = (bfs_fs_t *)tree->fs_ctx;
        if (block_txn >= bfs_btree_txn_id(tree)) {
            tree->alloc->dealloc(tree->alloc, blk);
        } else if (fs->pending_count < BFS_PENDING_FREES_MAX) {
            fs->pending_frees[fs->pending_count++] = blk;
        }
    } else if (block_txn >= bfs_btree_txn_id(tree)) {
        tree->alloc->dealloc(tree->alloc, blk);
    }
}

static bfs_err_t cow_node(bfs_btree_t *tree, bfs_blk_t old_blk, uint8_t *buf, bfs_blk_t *out_blk)
{
    if (old_blk != BFS_BLK_NULL && tree->txn_id_ptr != NULL) {
        const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
        if (bfs_be64(hdr->txn_id) >= bfs_btree_txn_id(tree)) {
            /* Same transaction owns the block: rewrite in place. */
            bfs_err_t err = node_write(tree, old_blk, buf);
            if (err == BFS_OK) *out_blk = old_blk;
            return err;
        }
    }

    bfs_blk_t new_blk = tree->alloc->alloc(tree->alloc);
    if (new_blk == BFS_BLK_NULL) return BFS_ERR_NOSPC;

    if (old_blk != BFS_BLK_NULL) {
        btree_free_node(tree, old_blk, buf);
    }

    bfs_err_t err = node_write(tree, new_blk, buf);
    if (err != BFS_OK) {
        /* Allocated but never written or referenced — return it to the allocator
         * instead of leaking it, and propagate the real error (I/O, not NOSPC). */
        tree->alloc->dealloc(tree->alloc, new_blk);
        return err;
    }
    *out_blk = new_blk;
    return BFS_OK;
}

/* Insert key/val into a leaf at position idx. Caller must ensure there's room. */
static void leaf_insert_at(const bfs_btree_t *tree, uint8_t *buf,
                           uint32_t idx, const void *key, const void *val)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;
    uint32_t vs = tree->ops->val_size;

    for (uint32_t i = n; i > idx; i--) {
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i - 1), ks);
        memcpy(leaf_val(tree, buf, i), leaf_val(tree, buf, i - 1), vs);
    }
    memcpy(node_key(tree, buf, idx), key, ks);
    memcpy(leaf_val(tree, buf, idx), val, vs);
    hdr_of(buf)->num_keys = bfs_be32(n + 1);
}

/* Insert key + right_child into an internal node at position idx.
 * Caller must ensure there's room. */
static void internal_insert_at(const bfs_btree_t *tree, uint8_t *buf,
                               uint32_t idx, const void *key, bfs_blk_t right_child)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;

    for (uint32_t i = n; i > idx; i--)
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i - 1), ks);
    memcpy(node_key(tree, buf, idx), key, ks);

    for (uint32_t i = n + 1; i > idx + 1; i--)
        set_child(tree, buf, i, get_child(tree, buf, i - 1));
    set_child(tree, buf, idx + 1, right_child);

    hdr_of(buf)->num_keys = bfs_be32(n + 1);
}

typedef struct {
    bool did_split;
    uint8_t median_key[BFS_MAX_KEY_SIZE];
    bfs_blk_t new_right;
} split_result_t;

/* Split a full leaf. Left keeps first half, right gets second half.
 * Median key (first key of right) is returned for parent insertion. */
static bfs_err_t leaf_split(bfs_btree_t *tree, uint8_t *buf, split_result_t *result)
{
    uint32_t n = num_keys(buf);
    uint32_t mid = n / 2;
    uint32_t ks = tree->ops->key_size;
    uint32_t vs = tree->ops->val_size;

    uint8_t *right_buf = alloc_buf(tree);
    if (!right_buf) return BFS_ERR_NOMEM;
    node_init(tree, right_buf, BFS_BTNODE_LEAF);

    uint32_t right_count = n - mid;
    for (uint32_t i = 0; i < right_count; i++) {
        memcpy(node_key(tree, right_buf, i), node_key(tree, buf, mid + i), ks);
        memcpy(leaf_val(tree, right_buf, i), leaf_val(tree, buf, mid + i), vs);
    }
    hdr_of(right_buf)->num_keys = bfs_be32(right_count);
    hdr_of(right_buf)->right_sibling = hdr_of(buf)->right_sibling;

    hdr_of(buf)->num_keys = bfs_be32(mid);

    bfs_blk_t right_blk = tree->alloc->alloc(tree->alloc);
    if (right_blk == BFS_BLK_NULL) { free(right_buf); return BFS_ERR_NOSPC; }

    hdr_of(buf)->right_sibling = bfs_be32(right_blk);

    if (node_write(tree, right_blk, right_buf) != BFS_OK) {
        free(right_buf);
        return BFS_ERR_IO;
    }

    memcpy(result->median_key, node_key(tree, right_buf, 0), ks);
    result->new_right = right_blk;
    result->did_split = true;
    free(right_buf);
    return BFS_OK;
}

/* Split a full internal node. Median key is promoted (not kept in either child). */
static bfs_err_t internal_split(bfs_btree_t *tree, uint8_t *buf, split_result_t *result)
{
    uint32_t n = num_keys(buf);
    uint32_t mid = n / 2;
    uint32_t ks = tree->ops->key_size;

    uint8_t *right_buf = alloc_buf(tree);
    if (!right_buf) return BFS_ERR_NOMEM;
    node_init(tree, right_buf, node_level(buf));

    memcpy(result->median_key, node_key(tree, buf, mid), ks);

    uint32_t right_count = n - mid - 1;
    for (uint32_t i = 0; i < right_count; i++)
        memcpy(node_key(tree, right_buf, i), node_key(tree, buf, mid + 1 + i), ks);
    hdr_of(right_buf)->num_keys = bfs_be32(right_count);

    for (uint32_t i = 0; i <= right_count; i++)
        set_child(tree, right_buf, i, get_child(tree, buf, mid + 1 + i));

    hdr_of(buf)->num_keys = bfs_be32(mid);

    bfs_blk_t right_blk = tree->alloc->alloc(tree->alloc);
    if (right_blk == BFS_BLK_NULL) { free(right_buf); return BFS_ERR_NOSPC; }

    if (node_write(tree, right_blk, right_buf) != BFS_OK) {
        free(right_buf);
        return BFS_ERR_IO;
    }

    result->new_right = right_blk;
    result->did_split = true;
    free(right_buf);
    return BFS_OK;
}

/* ── Iterative top-down insert ─────────────────────────────── */

typedef struct {
    bfs_blk_t blk;
    uint32_t child_idx;
} path_entry_t;

bfs_err_t bfs_btree_insert(bfs_btree_t *tree, const void *key, const void *val)
{
    /* Empty tree: create a root leaf */
    if (tree->root == BFS_BLK_NULL) {
        uint8_t *buf = alloc_buf(tree);
        if (!buf) return BFS_ERR_NOMEM;
        node_init(tree, buf, BFS_BTNODE_LEAF);
        leaf_insert_at(tree, buf, 0, key, val);

        bfs_blk_t blk = tree->alloc->alloc(tree->alloc);
        if (blk == BFS_BLK_NULL) { free(buf); return BFS_ERR_NOSPC; }
        bfs_err_t err = node_write(tree, blk, buf);
        free(buf);
        if (err != BFS_OK) return err;
        tree->root = blk;
        tree->height = 1;
        return BFS_OK;
    }

    const uint32_t bs = tree->bio->block_size;
    bfs_err_t rc = BFS_OK;

    /* Descend to leaf, recording path */
    path_entry_t path[MAX_TREE_DEPTH];
    uint32_t alloc_depth = (tree->height > 0 ? tree->height : 2) + 1;
    uint8_t *node_bufs = malloc((size_t)alloc_depth * bs);
    if (!node_bufs) return BFS_ERR_NOMEM;
    #define NBUF(d) (node_bufs + (d) * bs)
    int depth = 0;

    bfs_blk_t blk = tree->root;
    while (1) {
        if (depth >= MAX_TREE_DEPTH) { rc = BFS_ERR_CORRUPT; goto insert_cleanup; }
        bfs_err_t err = node_read(tree, blk, NBUF(depth));
        if (err != BFS_OK) { rc = err; goto insert_cleanup; }
        path[depth].blk = blk;

        if (is_leaf(NBUF(depth)))
            break;

        bool found;
        uint32_t idx = node_search(tree, NBUF(depth), key, &found);
        path[depth].child_idx = found ? idx + 1 : idx;
        blk = get_child(tree, NBUF(depth), path[depth].child_idx);
        depth++;
    }

    /* Check for duplicate in leaf */
    uint8_t *leaf = NBUF(depth);
    bool found;
    uint32_t idx = node_search(tree, leaf, key, &found);
    if (found) { rc = BFS_ERR_EXISTS; goto insert_cleanup; }

    /* Split the leaf FIRST if it's full, then insert into the correct half */
    split_result_t split = { .did_split = false };

    if (num_keys(leaf) >= leaf_max_keys(tree)) {
        bfs_err_t err = leaf_split(tree, leaf, &split);
        if (err != BFS_OK) { rc = err; goto insert_cleanup; }

        /* Determine which half the new key goes into */
        if (tree->ops->key_compare(key, split.median_key) >= 0) {
            /* Key goes into the right (new) node — read it, insert there */
            uint8_t *right_buf = alloc_buf(tree);
            if (!right_buf) { rc = BFS_ERR_NOMEM; goto insert_cleanup; }
            err = node_read(tree, split.new_right, right_buf);
            if (err != BFS_OK) { free(right_buf); rc = err; goto insert_cleanup; }

            bool f2;
            uint32_t idx2 = node_search(tree, right_buf, key, &f2);
            if (f2) { free(right_buf); rc = BFS_ERR_EXISTS; goto insert_cleanup; }
            leaf_insert_at(tree, right_buf, idx2, key, val);

            /* Re-write the right node */
            err = node_write(tree, split.new_right, right_buf);
            free(right_buf);
            if (err != BFS_OK) { rc = err; goto insert_cleanup; }
        } else {
            /* Key goes into the left (current) node */
            bool f2;
            uint32_t idx2 = node_search(tree, leaf, key, &f2);
            leaf_insert_at(tree, leaf, idx2, key, val);
        }
    } else {
        /* Room available — just insert */
        leaf_insert_at(tree, leaf, idx, key, val);
    }

    /* COW the leaf */
    bfs_blk_t new_blk;
    rc = cow_node(tree, path[depth].blk, leaf, &new_blk);
    if (rc != BFS_OK) goto insert_cleanup;

    /* Walk back up the path */
    for (int d = depth - 1; d >= 0; d--) {
        uint8_t *node = NBUF(d);
        uint32_t ci = path[d].child_idx;

        set_child(tree, node, ci, new_blk);

        if (split.did_split) {
            if (num_keys(node) >= internal_max_keys(tree)) {
                /* Split the internal node FIRST, then insert into correct half */
                split_result_t parent_split;
                bfs_err_t err = internal_split(tree, node, &parent_split);
                if (err != BFS_OK) { rc = err; goto insert_cleanup; }

                /* Determine which half gets the new key */
                if (tree->ops->key_compare(split.median_key, parent_split.median_key) >= 0) {
                    /* Insert into right half */
                    uint8_t *right_buf = alloc_buf(tree);
                    if (!right_buf) { rc = BFS_ERR_NOMEM; goto insert_cleanup; }
                    err = node_read(tree, parent_split.new_right, right_buf);
                    if (err != BFS_OK) { free(right_buf); rc = err; goto insert_cleanup; }
                    bool f;
                    uint32_t ri = node_search(tree, right_buf, split.median_key, &f);
                    internal_insert_at(tree, right_buf, ri, split.median_key, split.new_right);
                    err = node_write(tree, parent_split.new_right, right_buf);
                    free(right_buf);
                    if (err != BFS_OK) { rc = err; goto insert_cleanup; }
                } else {
                    /* Insert into left half (current node) */
                    bool f;
                    uint32_t li = node_search(tree, node, split.median_key, &f);
                    internal_insert_at(tree, node, li, split.median_key, split.new_right);
                }
                split = parent_split;
            } else {
                internal_insert_at(tree, node, ci, split.median_key, split.new_right);
                split.did_split = false;
            }
        }

        rc = cow_node(tree, path[d].blk, node, &new_blk);
        if (rc != BFS_OK) goto insert_cleanup;
    }

    tree->root = new_blk;

    /* If the root split, create a new root */
    if (split.did_split) {
        uint8_t *root_buf = alloc_buf(tree);
        if (!root_buf) { rc = BFS_ERR_NOSPC; goto insert_cleanup; }
        node_init(tree, root_buf, node_level(NBUF(0)) + 1);
        set_child(tree, root_buf, 0, new_blk);
        memcpy(node_key(tree, root_buf, 0), split.median_key, tree->ops->key_size);
        set_child(tree, root_buf, 1, split.new_right);
        hdr_of(root_buf)->num_keys = bfs_be32(1);

        bfs_blk_t root_blk = tree->alloc->alloc(tree->alloc);
        if (root_blk == BFS_BLK_NULL) { free(root_buf); rc = BFS_ERR_NOSPC; goto insert_cleanup; }
        bfs_err_t err = node_write(tree, root_blk, root_buf);
        free(root_buf);
        if (err != BFS_OK) { rc = err; goto insert_cleanup; }
        tree->root = root_blk;
        tree->height++;
    }

insert_cleanup:
    free(node_bufs);
    #undef NBUF
    return rc;
}

/* ── Update (single-traversal value modification with COW) ── */

bfs_err_t bfs_btree_update(bfs_btree_t *tree, const void *key, const void *new_val)
{
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    const uint32_t bs = tree->bio->block_size;
    uint32_t depth = tree->height > 0 ? tree->height : 2;
    uint8_t *node_bufs = malloc((size_t)depth * bs);
    if (!node_bufs) return BFS_ERR_NOMEM;
    #define UBUF(d) (node_bufs + (d) * bs)

    path_entry_t path[MAX_TREE_DEPTH];
    int d = 0;
    bfs_blk_t blk = tree->root;

    /* Descend to leaf */
    while (1) {
        if (d >= MAX_TREE_DEPTH) { free(node_bufs); return BFS_ERR_CORRUPT; }
        bfs_err_t err = node_read(tree, blk, UBUF(d));
        if (err != BFS_OK) { free(node_bufs); return err; }
        path[d].blk = blk;
        if (is_leaf(UBUF(d))) break;
        bool found;
        uint32_t idx = node_search(tree, UBUF(d), key, &found);
        path[d].child_idx = found ? idx + 1 : idx;
        blk = get_child(tree, UBUF(d), path[d].child_idx);
        d++;
    }

    /* Find key in leaf and update value */
    bool found;
    uint32_t idx = node_search(tree, UBUF(d), key, &found);
    if (!found) { free(node_bufs); return BFS_ERR_NOTFOUND; }
    memcpy(leaf_val(tree, UBUF(d), idx), new_val, tree->ops->val_size);

    /* COW back up */
    bfs_blk_t new_blk;
    bfs_err_t cerr = cow_node(tree, path[d].blk, UBUF(d), &new_blk);
    if (cerr != BFS_OK) { free(node_bufs); return cerr; }
    for (int i = d - 1; i >= 0; i--) {
        set_child(tree, UBUF(i), path[i].child_idx, new_blk);
        cerr = cow_node(tree, path[i].blk, UBUF(i), &new_blk);
        if (cerr != BFS_OK) { free(node_bufs); return cerr; }
    }
    tree->root = new_blk;

    free(node_bufs);
    #undef UBUF
    return BFS_OK;
}

/* ── Scan ──────────────────────────────────────────────────── */

bfs_err_t bfs_btree_scan(bfs_btree_t *tree, const void *start_key,
                           bfs_scan_cb cb, void *ctx)
{
    if (tree->root == BFS_BLK_NULL)
        return BFS_OK;

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;

    /* First: descend to the starting leaf */
    bfs_blk_t blk = tree->root;
    uint32_t descend_depth = 0;
    while (1) {
        if (descend_depth++ > MAX_TREE_DEPTH) { free(buf); return BFS_ERR_CORRUPT; }
        bfs_err_t err = node_read(tree, blk, buf);
        if (err != BFS_OK) { free(buf); return err; }
        if (is_leaf(buf)) break;
        if (start_key) {
            bool found;
            uint32_t idx = node_search(tree, buf, start_key, &found);
            blk = get_child(tree, buf, found ? idx + 1 : idx);
        } else {
            blk = get_child(tree, buf, 0);
        }
    }

    /* Process first leaf */
    uint32_t start_idx = 0;
    if (start_key) {
        bool found;
        start_idx = node_search(tree, buf, start_key, &found);
    }

    uint32_t n = num_keys(buf);
    for (uint32_t i = start_idx; i < n; i++) {
        if (!cb(node_key(tree, buf, i), leaf_val(tree, buf, i), ctx)) {
            free(buf); return BFS_OK;
        }
    }

    if (n == 0) { free(buf); return BFS_OK; }

    uint8_t *last = malloc(tree->ops->key_size);
    if (!last) { free(buf); return BFS_ERR_NOMEM; }
    memcpy(last, node_key(tree, buf, n - 1), tree->ops->key_size);

    while (1) {
        bfs_blk_t path_blks[MAX_TREE_DEPTH];
        uint32_t path_idx[MAX_TREE_DEPTH];
        int depth = 0;

        bfs_blk_t cur = tree->root;
        bool reached_leaf = false;

        while (1) {
            if (depth > MAX_TREE_DEPTH) { free(last); free(buf); return BFS_ERR_CORRUPT; }
            bfs_err_t err = node_read(tree, cur, buf);
            if (err != BFS_OK) { free(last); free(buf); return err; }

            if (is_leaf(buf)) {
                reached_leaf = true;
                break;
            }

            bool found;
            uint32_t idx = node_search(tree, buf, last, &found);
            uint32_t ci = found ? idx + 1 : idx;

            if (depth < MAX_TREE_DEPTH) {
                path_blks[depth] = cur;
                path_idx[depth] = ci;
                depth++;
            }
            cur = get_child(tree, buf, ci);
        }

        if (!reached_leaf) break;

        /* In the leaf, find first key > last */
        bool found;
        uint32_t idx = node_search(tree, buf, last, &found);
        if (found) idx++;

        if (idx < num_keys(buf)) {
            n = num_keys(buf);
            for (uint32_t i = idx; i < n; i++) {
                if (!cb(node_key(tree, buf, i), leaf_val(tree, buf, i), ctx)) {
                    free(last); free(buf); return BFS_OK;
                }
            }
            memcpy(last, node_key(tree, buf, n - 1), tree->ops->key_size);
            continue;
        }

        /* Backtrack: try next child at deepest node with unvisited children */
        bool found_next = false;
        for (int d = depth - 1; d >= 0; d--) {
            bfs_err_t err = node_read(tree, path_blks[d], buf);
            if (err != BFS_OK) break;
            uint32_t next_ci = path_idx[d] + 1;
            if (next_ci <= num_keys(buf)) {
                cur = get_child(tree, buf, next_ci);
                while (1) {
                    err = node_read(tree, cur, buf);
                    if (err != BFS_OK) break;
                    if (is_leaf(buf)) break;
                    cur = get_child(tree, buf, 0);
                }
                if (num_keys(buf) > 0) {
                    n = num_keys(buf);
                    for (uint32_t i = 0; i < n; i++) {
                        if (!cb(node_key(tree, buf, i), leaf_val(tree, buf, i), ctx)) {
                            free(last); free(buf); return BFS_OK;
                        }
                    }
                    memcpy(last, node_key(tree, buf, n - 1), tree->ops->key_size);
                    found_next = true;
                }
                break;
            }
        }
        if (!found_next) break;
    }

    free(last);
    free(buf);
    return BFS_OK;
}


/* ── Floor search ──────────────────────────────────────────── */

/* Find the rightmost key in the subtree rooted at blk. */
static bfs_err_t rightmost_in_subtree(const bfs_btree_t *tree, bfs_blk_t blk,
                                       void *key_out, void *val_out)
{
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    uint32_t depth = 0;
    while (1) {
        if (depth++ > MAX_TREE_DEPTH) { free(buf); return BFS_ERR_CORRUPT; }
        bfs_err_t err = node_read(tree, blk, buf);
        if (err != BFS_OK) { free(buf); return err; }
        uint32_t n = num_keys(buf);
        if (n == 0) { free(buf); return BFS_ERR_NOTFOUND; }
        if (is_leaf(buf)) {
            memcpy(key_out, node_key(tree, buf, n - 1), tree->ops->key_size);
            memcpy(val_out, leaf_val(tree, buf, n - 1), tree->ops->val_size);
            free(buf);
            return BFS_OK;
        }
        blk = get_child(tree, buf, n); /* rightmost child */
    }
}

bfs_err_t bfs_btree_search_floor(bfs_btree_t *tree, const void *key,
                                    void *key_out, void *val_out)
{
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    bfs_blk_t blk = tree->root;
    uint32_t depth = 0;

    /* Track the last internal node where we descended right (idx > 0).
     * If the leaf has no key <= search_key, the predecessor is the
     * rightmost key in child[turn_idx - 1] of that node. */
    bfs_blk_t turn_blk = BFS_BLK_NULL;
    uint32_t turn_child_idx = 0; /* child index we came from (the left sibling has the predecessor) */

    while (1) {
        if (depth++ > MAX_TREE_DEPTH) { free(buf); return BFS_ERR_CORRUPT; }
        bfs_err_t err = node_read(tree, blk, buf);
        if (err != BFS_OK) { free(buf); return err; }

        bool found;
        uint32_t idx = node_search(tree, buf, key, &found);

        if (is_leaf(buf)) {
            if (found) {
                memcpy(key_out, node_key(tree, buf, idx), tree->ops->key_size);
                memcpy(val_out, leaf_val(tree, buf, idx), tree->ops->val_size);
                free(buf);
                return BFS_OK;
            }
            if (idx > 0) {
                memcpy(key_out, node_key(tree, buf, idx - 1), tree->ops->key_size);
                memcpy(val_out, leaf_val(tree, buf, idx - 1), tree->ops->val_size);
                free(buf);
                return BFS_OK;
            }
            /* idx == 0: all keys in this leaf > search_key.
             * The predecessor is the rightmost key in the left subtree
             * at the last right-turn point. */
            if (turn_blk == BFS_BLK_NULL) {
                free(buf);
                return BFS_ERR_NOTFOUND;
            }
            /* Re-read the turn node and descend into child[turn_child_idx - 1] */
            err = node_read(tree, turn_blk, buf);
            if (err != BFS_OK) { free(buf); return err; }
            bfs_blk_t left = get_child(tree, buf, turn_child_idx - 1);
            free(buf);
            return rightmost_in_subtree(tree, left, key_out, val_out);
        }

        /* Internal node: descend */
        uint32_t child_idx = found ? idx + 1 : idx;
        if (child_idx > 0) {
            turn_blk = blk;
            turn_child_idx = child_idx;
        }
        blk = get_child(tree, buf, child_idx);
    }
}

/* ── Delete ─────────────────────────────────────────────────── */

/* Remove key at index idx from a leaf node */
static void leaf_remove_at(const bfs_btree_t *tree, uint8_t *buf, uint32_t idx)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;
    uint32_t vs = tree->ops->val_size;

    for (uint32_t i = idx; i < n - 1; i++) {
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i + 1), ks);
        memcpy(leaf_val(tree, buf, i), leaf_val(tree, buf, i + 1), vs);
    }
    hdr_of(buf)->num_keys = bfs_be32(n - 1);
}

/* Remove key at index idx and child at idx+1 from an internal node */
static void internal_remove_at(const bfs_btree_t *tree, uint8_t *buf, uint32_t idx)
{
    uint32_t n = num_keys(buf);
    uint32_t ks = tree->ops->key_size;

    for (uint32_t i = idx; i < n - 1; i++)
        memcpy(node_key(tree, buf, i), node_key(tree, buf, i + 1), ks);
    for (uint32_t i = idx + 1; i < n; i++)
        set_child(tree, buf, i, get_child(tree, buf, i + 1));
    hdr_of(buf)->num_keys = bfs_be32(n - 1);
}

static uint32_t leaf_min(const bfs_btree_t *tree)
{
    return leaf_max_keys(tree) / 2;
}

static uint32_t internal_min(const bfs_btree_t *tree)
{
    return internal_max_keys(tree) / 2;
}

bfs_err_t bfs_btree_delete(bfs_btree_t *tree, const void *key)
{
    if (tree->root == BFS_BLK_NULL)
        return BFS_ERR_NOTFOUND;

    const uint32_t bs = tree->bio->block_size;
    bfs_err_t rc = BFS_OK;

    /* Descend to leaf, recording path */
    path_entry_t path[MAX_TREE_DEPTH];
    uint32_t del_alloc_depth = (tree->height > 0 ? tree->height : 2) + 1;
    uint8_t *node_bufs = malloc((size_t)del_alloc_depth * bs);
    if (!node_bufs) return BFS_ERR_NOMEM;
    uint8_t *sib_buf = alloc_buf(tree);
    if (!sib_buf) { free(node_bufs); return BFS_ERR_NOMEM; }
    #define DNBUF(d) (node_bufs + (d) * bs)
    int depth = 0;

    bfs_blk_t blk = tree->root;
    while (1) {
        if (depth >= MAX_TREE_DEPTH) { rc = BFS_ERR_CORRUPT; goto delete_cleanup; }
        bfs_err_t err = node_read(tree, blk, DNBUF(depth));
        if (err != BFS_OK) { rc = err; goto delete_cleanup; }
        path[depth].blk = blk;

        if (is_leaf(DNBUF(depth)))
            break;

        bool found;
        uint32_t idx = node_search(tree, DNBUF(depth), key, &found);
        path[depth].child_idx = found ? idx + 1 : idx;
        blk = get_child(tree, DNBUF(depth), path[depth].child_idx);
        depth++;
    }

    /* Remove key from leaf */
    uint8_t *leaf = DNBUF(depth);
    bool found;
    uint32_t idx = node_search(tree, leaf, key, &found);
    if (!found) { rc = BFS_ERR_NOTFOUND; goto delete_cleanup; }

    leaf_remove_at(tree, leaf, idx);

    /* If root is a leaf, just COW and done (no minimum fill requirement for root) */
    if (depth == 0) {
        if (num_keys(leaf) == 0) {
            btree_free_node(tree, path[0].blk, leaf);
            tree->root = BFS_BLK_NULL;
            tree->height = 0;
            goto delete_cleanup;
        }
        bfs_blk_t new_blk;
        rc = cow_node(tree, path[0].blk, leaf, &new_blk);
        if (rc != BFS_OK) goto delete_cleanup;
        tree->root = new_blk;
        goto delete_cleanup;
    }

    /* Check if leaf is underfull and needs rebalancing */
    bool merged = false;

    if (num_keys(leaf) < leaf_min(tree)) {
        uint8_t *parent = DNBUF(depth - 1);
        uint32_t ci = path[depth - 1].child_idx;
        uint32_t parent_nkeys = num_keys(parent);
        uint32_t ks = tree->ops->key_size;
        uint32_t vs = tree->ops->val_size;

        /* Try to borrow from right sibling */
        if (ci < parent_nkeys) {
            bfs_blk_t sib_blk = get_child(tree, parent, ci + 1);
            if (node_read(tree, sib_blk, sib_buf) == BFS_OK && num_keys(sib_buf) > leaf_min(tree)) {
                /* Borrow first key/val from right sibling */
                uint32_t ln = num_keys(leaf);
                memcpy(node_key(tree, leaf, ln), node_key(tree, sib_buf, 0), ks);
                memcpy(leaf_val(tree, leaf, ln), leaf_val(tree, sib_buf, 0), vs);
                hdr_of(leaf)->num_keys = bfs_be32(ln + 1);
                leaf_remove_at(tree, sib_buf, 0);

                /* Update parent separator to new first key of right sibling */
                memcpy(node_key(tree, parent, ci), node_key(tree, sib_buf, 0), ks);

                /* COW the sibling */
                bfs_blk_t new_sib;
                rc = cow_node(tree, sib_blk, sib_buf, &new_sib);
                if (rc != BFS_OK) goto delete_cleanup;
                set_child(tree, parent, ci + 1, new_sib);
                goto cow_upward;
            }
        }

        /* Try to borrow from left sibling */
        if (ci > 0) {
            bfs_blk_t sib_blk = get_child(tree, parent, ci - 1);
            if (node_read(tree, sib_blk, sib_buf) == BFS_OK && num_keys(sib_buf) > leaf_min(tree)) {
                /* Borrow last key/val from left sibling */
                uint32_t sn = num_keys(sib_buf);
                uint32_t ln = num_keys(leaf);
                /* Shift leaf entries right */
                for (uint32_t i = ln; i > 0; i--) {
                    memcpy(node_key(tree, leaf, i), node_key(tree, leaf, i - 1), ks);
                    memcpy(leaf_val(tree, leaf, i), leaf_val(tree, leaf, i - 1), vs);
                }
                memcpy(node_key(tree, leaf, 0), node_key(tree, sib_buf, sn - 1), ks);
                memcpy(leaf_val(tree, leaf, 0), leaf_val(tree, sib_buf, sn - 1), vs);
                hdr_of(leaf)->num_keys = bfs_be32(ln + 1);
                hdr_of(sib_buf)->num_keys = bfs_be32(sn - 1);

                /* Update parent separator to new first key of current leaf */
                memcpy(node_key(tree, parent, ci - 1), node_key(tree, leaf, 0), ks);

                bfs_blk_t new_sib;
                rc = cow_node(tree, sib_blk, sib_buf, &new_sib);
                if (rc != BFS_OK) goto delete_cleanup;
                set_child(tree, parent, ci - 1, new_sib);
                goto cow_upward;
            }
        }

        /* Merge with a sibling */
        if (ci < parent_nkeys) {
            /* Merge with right sibling into current leaf */
            bfs_blk_t sib_blk = get_child(tree, parent, ci + 1);
            if (node_read(tree, sib_blk, sib_buf) == BFS_OK) {
                uint32_t ln = num_keys(leaf);
                uint32_t sn = num_keys(sib_buf);
                for (uint32_t i = 0; i < sn; i++) {
                    memcpy(node_key(tree, leaf, ln + i), node_key(tree, sib_buf, i), ks);
                    memcpy(leaf_val(tree, leaf, ln + i), leaf_val(tree, sib_buf, i), vs);
                }
                hdr_of(leaf)->num_keys = bfs_be32(ln + sn);
                hdr_of(leaf)->right_sibling = hdr_of(sib_buf)->right_sibling;
                btree_free_node(tree, sib_blk, sib_buf);
                internal_remove_at(tree, parent, ci);
                merged = true;
            }
        } else if (ci > 0) {
            /* Merge current leaf into left sibling */
            bfs_blk_t sib_blk = get_child(tree, parent, ci - 1);
            if (node_read(tree, sib_blk, sib_buf) == BFS_OK) {
                uint32_t sn = num_keys(sib_buf);
                uint32_t ln = num_keys(leaf);
                for (uint32_t i = 0; i < ln; i++) {
                    memcpy(node_key(tree, sib_buf, sn + i), node_key(tree, leaf, i), ks);
                    memcpy(leaf_val(tree, sib_buf, sn + i), leaf_val(tree, leaf, i), vs);
                }
                hdr_of(sib_buf)->num_keys = bfs_be32(sn + ln);
                hdr_of(sib_buf)->right_sibling = hdr_of(leaf)->right_sibling;
                btree_free_node(tree, path[depth].blk, leaf);

                /* Replace leaf with the merged sibling for COW upward */
                memcpy(leaf, sib_buf, tree->bio->block_size);
                path[depth].blk = sib_blk;
                internal_remove_at(tree, parent, ci - 1);
                path[depth - 1].child_idx = ci - 1;
                merged = true;
            }
        }
    }

cow_upward:
    /* COW the leaf */
    {
        bfs_blk_t new_blk;
        rc = cow_node(tree, path[depth].blk, leaf, &new_blk);
        if (rc != BFS_OK) goto delete_cleanup;

        /* Walk back up, propagating merges */
        for (int d = depth - 1; d >= 0; d--) {
            uint8_t *node = DNBUF(d);
            uint32_t ci = path[d].child_idx;
            set_child(tree, node, ci, new_blk);

            /* Check if internal node needs rebalancing after merge */
            if (merged && d > 0 && num_keys(node) < internal_min(tree)) {
                uint8_t *pp = DNBUF(d - 1);
                uint32_t pci = path[d - 1].child_idx;
                uint32_t pp_nkeys = num_keys(pp);
                uint32_t ks = tree->ops->key_size;

                /* Try borrow from right sibling */
                if (pci < pp_nkeys) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci + 1);
                    if (node_read(tree, sib_blk, sib_buf) == BFS_OK && num_keys(sib_buf) > internal_min(tree)) {
                        uint32_t nn = num_keys(node);
                        /* Bring parent separator down */
                        memcpy(node_key(tree, node, nn), node_key(tree, pp, pci), ks);
                        set_child(tree, node, nn + 1, get_child(tree, sib_buf, 0));
                        hdr_of(node)->num_keys = bfs_be32(nn + 1);
                        /* Move sibling's first key up to parent */
                        memcpy(node_key(tree, pp, pci), node_key(tree, sib_buf, 0), ks);
                        /* Remove first key+child from sibling */
                        uint32_t sn = num_keys(sib_buf);
                        for (uint32_t i = 0; i < sn - 1; i++)
                            memcpy(node_key(tree, sib_buf, i), node_key(tree, sib_buf, i + 1), ks);
                        for (uint32_t i = 0; i < sn; i++)
                            set_child(tree, sib_buf, i, get_child(tree, sib_buf, i + 1));
                        hdr_of(sib_buf)->num_keys = bfs_be32(sn - 1);

                        bfs_blk_t new_sib;
                        rc = cow_node(tree, sib_blk, sib_buf, &new_sib);
                        if (rc != BFS_OK) goto delete_cleanup;
                        set_child(tree, pp, pci + 1, new_sib);
                        merged = false;
                        goto cow_this;
                    }
                }

                /* Try borrow from left sibling */
                if (pci > 0) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci - 1);
                    if (node_read(tree, sib_blk, sib_buf) == BFS_OK && num_keys(sib_buf) > internal_min(tree)) {
                        uint32_t nn = num_keys(node);
                        uint32_t sn = num_keys(sib_buf);
                        /* Shift node entries right */
                        for (uint32_t i = nn; i > 0; i--)
                            memcpy(node_key(tree, node, i), node_key(tree, node, i - 1), ks);
                        for (uint32_t i = nn + 1; i > 0; i--)
                            set_child(tree, node, i, get_child(tree, node, i - 1));
                        /* Bring parent separator down */
                        memcpy(node_key(tree, node, 0), node_key(tree, pp, pci - 1), ks);
                        set_child(tree, node, 0, get_child(tree, sib_buf, sn));
                        hdr_of(node)->num_keys = bfs_be32(nn + 1);
                        /* Move sibling's last key up to parent */
                        memcpy(node_key(tree, pp, pci - 1), node_key(tree, sib_buf, sn - 1), ks);
                        hdr_of(sib_buf)->num_keys = bfs_be32(sn - 1);

                        bfs_blk_t new_sib;
                        rc = cow_node(tree, sib_blk, sib_buf, &new_sib);
                        if (rc != BFS_OK) goto delete_cleanup;
                        set_child(tree, pp, pci - 1, new_sib);
                        merged = false;
                        goto cow_this;
                    }
                }

                /* Merge internal nodes */
                if (pci < pp_nkeys) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci + 1);
                    if (node_read(tree, sib_blk, sib_buf) == BFS_OK) {
                        uint32_t nn = num_keys(node);
                        uint32_t sn = num_keys(sib_buf);
                        memcpy(node_key(tree, node, nn), node_key(tree, pp, pci), ks);
                        for (uint32_t i = 0; i < sn; i++)
                            memcpy(node_key(tree, node, nn + 1 + i), node_key(tree, sib_buf, i), ks);
                        for (uint32_t i = 0; i <= sn; i++)
                            set_child(tree, node, nn + 1 + i, get_child(tree, sib_buf, i));
                        hdr_of(node)->num_keys = bfs_be32(nn + 1 + sn);
                        btree_free_node(tree, sib_blk, sib_buf);
                        internal_remove_at(tree, pp, pci);
                        /* merged stays true, will propagate up */
                    }
                } else if (pci > 0) {
                    bfs_blk_t sib_blk = get_child(tree, pp, pci - 1);
                    if (node_read(tree, sib_blk, sib_buf) == BFS_OK) {
                        uint32_t sn = num_keys(sib_buf);
                        uint32_t nn = num_keys(node);
                        memcpy(node_key(tree, sib_buf, sn), node_key(tree, pp, pci - 1), ks);
                        for (uint32_t i = 0; i < nn; i++)
                            memcpy(node_key(tree, sib_buf, sn + 1 + i), node_key(tree, node, i), ks);
                        for (uint32_t i = 0; i <= nn; i++)
                            set_child(tree, sib_buf, sn + 1 + i, get_child(tree, node, i));
                        hdr_of(sib_buf)->num_keys = bfs_be32(sn + 1 + nn);
                        btree_free_node(tree, path[d].blk, node);
                        memcpy(node, sib_buf, tree->bio->block_size);
                        path[d].blk = sib_blk;
                        internal_remove_at(tree, pp, pci - 1);
                        path[d - 1].child_idx = pci - 1;
                    }
                }
            } else {
                merged = false;
            }

cow_this:
            rc = cow_node(tree, path[d].blk, node, &new_blk);
            if (rc != BFS_OK) goto delete_cleanup;
        }

        tree->root = new_blk;

        /* If root has only one child after merge, collapse it */
        if (tree->height > 1) {
            uint8_t *root_buf = alloc_buf(tree);
            if (root_buf) {
                if (node_read(tree, tree->root, root_buf) == BFS_OK &&
                    !is_leaf(root_buf) && num_keys(root_buf) == 0) {
                    bfs_blk_t child = get_child(tree, root_buf, 0);
                    btree_free_node(tree, tree->root, root_buf);
                    tree->root = child;
                    tree->height--;
                }
                free(root_buf);
            }
        }
    }

delete_cleanup:
    free(sib_buf);
    free(node_bufs);
    #undef DNBUF
    return rc;
}

/* ── Walk all node blocks (for fsck) ───────────────────────── */

static bfs_err_t walk_nodes_recursive(bfs_btree_t *tree, bfs_blk_t blk,
                                      bfs_node_walk_cb cb, void *ctx, int depth)
{
    if (blk == BFS_BLK_NULL) return BFS_OK;
    if (depth > MAX_TREE_DEPTH) return BFS_ERR_CORRUPT;
    uint8_t *buf = alloc_buf(tree);
    if (!buf) return BFS_ERR_NOMEM;
    /* node_read (not raw bfs_bio_read) so num_keys/level are validated before
     * the child-pointer loop below trusts num_keys. */
    bfs_err_t err = node_read(tree, blk, buf);
    if (err != BFS_OK) { free(buf); return err; }

    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    if (bfs_be16(hdr->level) > 0) {
        /* Internal node — recurse into children */
        uint32_t n = bfs_be32(hdr->num_keys);
        uint32_t data_sz = tree->bio->block_size - sizeof(bfs_btnode_hdr_t);
        uint32_t max_keys = (data_sz - 4) / (tree->ops->key_size + 4);
        uint32_t keys_end = sizeof(bfs_btnode_hdr_t) + max_keys * tree->ops->key_size;
        for (uint32_t i = 0; i <= n; i++) {
            err = walk_nodes_recursive(tree, bfs_load_be32(buf + keys_end + i * sizeof(uint32_t)),
                                       cb, ctx, depth + 1);
            if (err != BFS_OK) { free(buf); return err; }
        }
    }
    cb(blk, ctx);
    free(buf);
    return BFS_OK;
}

/* Returns BFS_OK, or the first node-read/structural error encountered. Callers
 * that reference-count via the callback MUST check this — a swallowed read
 * failure silently skips a subtree and corrupts the counts. */
bfs_err_t bfs_btree_walk_nodes(bfs_btree_t *tree, bfs_node_walk_cb cb, void *ctx)
{
    return walk_nodes_recursive(tree, tree->root, cb, ctx, 0);
}

/* ── Compaction ────────────────────────────────────────────── */

/* Threshold for compaction: fill factor < 90% */
#define BFS_COMPACT_THRESHOLD_NUM 9
#define BFS_COMPACT_THRESHOLD_DEN 10

typedef struct {
    bfs_btree_t *new_tree;
    bfs_err_t rc;
} compact_ctx_t;

static bool compact_cb(const void *key, const void *val, void *ctx)
{
    compact_ctx_t *cc = (compact_ctx_t *)ctx;
    cc->rc = bfs_btree_insert(cc->new_tree, key, val);
    return cc->rc == BFS_OK;
}

static void compact_free_old_cb(bfs_blk_t blk, void *ctx)
{
    bfs_btree_t *tree = (bfs_btree_t *)ctx;
    uint8_t *buf = malloc(tree->bio->block_size);
    if (!buf) return;
    if (bfs_bio_read(tree->bio, blk, buf) == BFS_OK) {
        btree_free_node(tree, blk, buf);
    }
    free(buf);
}

static void utilization_walk_recursive(const bfs_btree_t *tree, bfs_blk_t blk,
                                       uint32_t *total_keys, uint32_t *total_capacity, int depth)
{
    if (blk == BFS_BLK_NULL || depth > MAX_TREE_DEPTH) return;
    uint8_t *buf = malloc(tree->bio->block_size);
    if (!buf) return;
    if (node_read(tree, blk, buf) != BFS_OK) {
        free(buf);
        return;
    }

    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    uint32_t n = bfs_be32(hdr->num_keys);
    *total_keys += n;

    if (bfs_be16(hdr->level) > 0) {
        /* Internal node */
        *total_capacity += internal_max_keys(tree);
        /* Recurse into children */
        uint32_t data_sz = tree->bio->block_size - sizeof(bfs_btnode_hdr_t);
        uint32_t max_keys = (data_sz - 4) / (tree->ops->key_size + 4);
        uint32_t keys_end = sizeof(bfs_btnode_hdr_t) + max_keys * tree->ops->key_size;
        for (uint32_t i = 0; i <= n; i++) {
            utilization_walk_recursive(tree, bfs_load_be32(buf + keys_end + i * sizeof(uint32_t)),
                                       total_keys, total_capacity, depth + 1);
        }
    } else {
        /* Leaf node */
        *total_capacity += leaf_max_keys(tree);
    }

    free(buf);
}

static bool bfs_btree_needs_compaction(const bfs_btree_t *tree)
{
    if (tree->root == BFS_BLK_NULL) return false;
    uint32_t total_keys = 0;
    uint32_t total_capacity = 0;
    utilization_walk_recursive(tree, tree->root, &total_keys, &total_capacity, 0);
    if (total_capacity == 0) return false;

    /* Returns true if utilization is < 90% */
    return (uint64_t)total_keys * BFS_COMPACT_THRESHOLD_DEN < (uint64_t)total_capacity * BFS_COMPACT_THRESHOLD_NUM;
}

bfs_err_t bfs_btree_compact(bfs_btree_t *tree)
{
    if (tree->root == BFS_BLK_NULL) return BFS_OK;

    /* Skip compaction if tree is already well-packed (utilization >= 90%) to avoid write amplification */
    if (!bfs_btree_needs_compaction(tree)) {
        return BFS_OK;
    }

    bfs_btree_t new_tree;
    bfs_btree_init(&new_tree, tree->bio, tree->alloc, tree->ops,
                   BFS_BLK_NULL, bfs_btree_txn_id(tree));
    new_tree.fs_ctx = tree->fs_ctx;
    new_tree.txn_id_ptr = tree->txn_id_ptr;

    compact_ctx_t ctx = { .new_tree = &new_tree, .rc = BFS_OK };
    bfs_btree_scan(tree, NULL, compact_cb, &ctx);

    if (ctx.rc == BFS_OK) {
        /* Queue all old blocks for reclamation */
        bfs_btree_walk_nodes(tree, compact_free_old_cb, tree);
        /* Atomically swap root */
        tree->root = new_tree.root;
        tree->height = new_tree.height;
    }
    return ctx.rc;
}
