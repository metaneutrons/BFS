/* SPDX-License-Identifier: MPL-2.0 */
#include "bfs_refcount.h"
#include <string.h>

static int blk_compare(const void *a, const void *b)
{
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t refcount_ops = {
    .key_compare = blk_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
};

bfs_err_t bfs_refcount_init(bfs_refcount_t *rc, bfs_bio_t *bio,
                             bfs_allocator_t *alloc, bfs_blk_t root,
                             uint64_t txn_id)
{
    return bfs_btree_init(&rc->tree, bio, alloc, &refcount_ops, root, txn_id);
}

bfs_err_t bfs_refcount_inc(bfs_refcount_t *rc, bfs_blk_t blk)
{
    uint32_t key = bfs_be32(blk);
    uint32_t val;

    if (bfs_btree_search(&rc->tree, &key, &val) == BFS_OK) {
        uint32_t count = bfs_be32(val) + 1;
        val = bfs_be32(count);
        return bfs_btree_update(&rc->tree, &key, &val);
    }

    /* Not in tree (implicit refcount=1) — insert with refcount=2 */
    val = bfs_be32(2);
    return bfs_btree_insert(&rc->tree, &key, &val);
}

bfs_err_t bfs_refcount_dec(bfs_refcount_t *rc, bfs_blk_t blk, bool *freed)
{
    uint32_t key = bfs_be32(blk);
    uint32_t val;
    *freed = false;

    if (bfs_btree_search(&rc->tree, &key, &val) != BFS_OK) {
        /* Not in tree = refcount 1 → decrement to 0 = free */
        *freed = true;
        return BFS_OK;
    }

    uint32_t count = bfs_be32(val);
    if (count <= 1) {
        bfs_btree_delete(&rc->tree, &key);
        *freed = true;
        return BFS_OK;
    }

    count--;
    if (count == 1) {
        /* Back to implicit refcount=1 — remove from tree */
        return bfs_btree_delete(&rc->tree, &key);
    }

    /* Update in-place */
    val = bfs_be32(count);
    return bfs_btree_update(&rc->tree, &key, &val);
}

uint32_t bfs_refcount_get(bfs_refcount_t *rc, bfs_blk_t blk)
{
    uint32_t key = bfs_be32(blk);
    uint32_t val;
    if (bfs_btree_search(&rc->tree, &key, &val) == BFS_OK)
        return bfs_be32(val);
    return 1; /* implicit: not shared */
}
