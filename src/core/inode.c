/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode B+tree operations
 */

#include "bfs_inode.h"
#include <string.h>

static int inode_key_cmp(const void *a, const void *b)
{
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t bfs_inode_ops = {
    .key_compare = inode_key_cmp,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_inode_t),
};

bfs_err_t bfs_inode_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id)
{
    return bfs_btree_init(tree, bio, alloc, &bfs_inode_ops, root, txn_id);
}

bfs_err_t bfs_inode_read(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
{
    uint32_t key = bfs_be32(ino);
    return bfs_btree_search(tree, &key, out);
}

bfs_err_t bfs_inode_write(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode)
{
    uint32_t key = bfs_be32(ino);

    /* Try update (single traversal for existing inodes) */
    bfs_err_t err = bfs_btree_update(tree, &key, inode);
    if (err == BFS_ERR_NOTFOUND)
        return bfs_btree_insert(tree, &key, inode);
    return err;
}

bfs_err_t bfs_inode_delete(bfs_btree_t *tree, uint32_t ino)
{
    uint32_t key = bfs_be32(ino);
    return bfs_btree_delete(tree, &key);
}
