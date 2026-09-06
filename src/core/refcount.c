/* SPDX-License-Identifier: MPL-2.0 */
#include "bfs_refcount.h"
#include <string.h>

static const bfs_btree_ops_t refcount_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
};

bfs_err_t bfs_refcount_init(bfs_refcount_t *rc, bfs_bio_t *bio,
                             bfs_allocator_t *alloc, bfs_blk_t root,
                             uint64_t txn_id)
{
    if (!rc || !bio) return BFS_ERR_INVAL;
    return bfs_btree_init(&rc->tree, bio, alloc, &refcount_ops, root, txn_id);
}

bfs_err_t bfs_refcount_inc(bfs_refcount_t *rc, bfs_blk_t blk)
{
    if (!rc || blk == BFS_BLK_NULL) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(blk);
    uint32_t val;

    bfs_err_t err = bfs_btree_search(&rc->tree, &key, &val);
    if (err == BFS_OK) {
        uint32_t count = bfs_be32(val);
        if (count == 0) return BFS_ERR_CORRUPT;
        if (count == UINT32_MAX) return BFS_ERR_NOSPC;
        count++;
        val = bfs_be32(count);
        return bfs_btree_update(&rc->tree, &key, &val);
    }
    if (err != BFS_ERR_NOTFOUND) return err;

    /* Not in tree (implicit refcount=1) — insert with refcount=2 */
    val = bfs_be32(2);
    return bfs_btree_insert(&rc->tree, &key, &val);
}

bfs_err_t bfs_refcount_dec(bfs_refcount_t *rc, bfs_blk_t blk, bool *freed)
{
    if (!rc || !freed || blk == BFS_BLK_NULL) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(blk);
    uint32_t val;
    *freed = false;

    bfs_err_t err = bfs_btree_search(&rc->tree, &key, &val);
    if (err == BFS_ERR_NOTFOUND) {
        /* Not in tree = refcount 1 → decrement to 0 = free */
        *freed = true;
        return BFS_OK;
    }
    if (err != BFS_OK) return err;

    uint32_t count = bfs_be32(val);
    if (count == 0) return BFS_ERR_CORRUPT;
    if (count <= 1) {
        err = bfs_btree_delete(&rc->tree, &key);
        if (err != BFS_OK) return err;
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

bfs_err_t bfs_refcount_get_checked(bfs_refcount_t *rc, bfs_blk_t blk,
                                   uint32_t *count_out)
{
    if (!rc || !count_out || blk == BFS_BLK_NULL) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(blk);
    uint32_t val;
    bfs_err_t err = bfs_btree_search(&rc->tree, &key, &val);
    if (err == BFS_ERR_NOTFOUND) {
        *count_out = 1;
        return BFS_OK;
    }
    if (err != BFS_OK) return err;
    *count_out = bfs_be32(val);
    return *count_out == 0 ? BFS_ERR_CORRUPT : BFS_OK;
}

uint32_t bfs_refcount_get(bfs_refcount_t *rc, bfs_blk_t blk)
{
    uint32_t count = 0;
    if (bfs_refcount_get_checked(rc, blk, &count) != BFS_OK) return 0;
    return count;
}
