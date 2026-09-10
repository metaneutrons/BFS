/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Inode B+tree operations
 */

#include "bfs_inode.h"
#include <string.h>

static const bfs_btree_ops_t bfs_inode_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_inode_t),
};

static bfs_err_t validate_inode(const bfs_btree_t *tree, uint32_t ino,
                                const bfs_inode_t *inode, bool unlinked)
{
    if (!tree || !inode || ino == 0 || ino >= 0x80000000u ||
        bfs_be32(inode->inode_nr) != ino ||
        bfs_be32(inode->type) > BFS_INODE_HARDLINK ||
        (bfs_be32(inode->link_count) == 0) != unlinked)
        return BFS_ERR_INVAL;
    bfs_blk_t extent_root = bfs_be32(inode->extent_root);
    if (extent_root != BFS_BLK_NULL && (!tree->bio || extent_root >= tree->bio->block_count))
        return BFS_ERR_INVAL;
    return BFS_OK;
}

bfs_err_t bfs_inode_init(bfs_btree_t *tree, bfs_bio_t *bio,
                     bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id)
{
    return bfs_btree_init(tree, bio, alloc, &bfs_inode_ops, root, txn_id);
}

bfs_err_t bfs_inode_read(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
{
    if (!tree || !out || ino == 0 || ino >= 0x80000000u)
        return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);
    bfs_err_t err = bfs_btree_search(tree, &key, out);
    if (err != BFS_OK) return err;
    if (validate_inode(tree, ino, out, false) != BFS_OK)
        return BFS_ERR_CORRUPT;
    return BFS_OK;
}

bfs_err_t bfs_inode_read_unlinked(bfs_btree_t *tree, uint32_t ino, bfs_inode_t *out)
{
    if (!tree || !out || ino == 0 || ino >= 0x80000000u)
        return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);
    bfs_err_t err = bfs_btree_search(tree, &key, out);
    if (err != BFS_OK) return err;
    return validate_inode(tree, ino, out, true) == BFS_OK ? BFS_OK : BFS_ERR_CORRUPT;
}

bfs_err_t bfs_inode_write(bfs_btree_t *tree, uint32_t ino, const bfs_inode_t *inode)
{
    if (validate_inode(tree, ino, inode, false) != BFS_OK) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);

    /* Try update (single traversal for existing inodes) */
    bfs_err_t err = bfs_btree_update(tree, &key, inode);
    if (err == BFS_ERR_NOTFOUND)
        return bfs_btree_insert(tree, &key, inode);
    return err;
}

bfs_err_t bfs_inode_write_unlinked(bfs_btree_t *tree, uint32_t ino,
                                   const bfs_inode_t *inode)
{
    if (validate_inode(tree, ino, inode, true) != BFS_OK) return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);
    bfs_err_t err = bfs_btree_update(tree, &key, inode);
    if (err == BFS_ERR_NOTFOUND) return BFS_ERR_NOTFOUND;
    return err;
}

bfs_err_t bfs_inode_delete(bfs_btree_t *tree, uint32_t ino)
{
    if (!tree || ino == 0 || ino >= 0x80000000u)
        return BFS_ERR_INVAL;
    uint32_t key = bfs_be32(ino);
    return bfs_btree_delete(tree, &key);
}
