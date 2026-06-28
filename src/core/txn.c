/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Transaction manager
 */

#include "bfs_txn.h"
#include <string.h>

bfs_err_t bfs_txn_begin(bfs_txn_t *txn, bfs_bio_t *bio)
{
    memset(txn, 0, sizeof(*txn));
    txn->bio = bio;
    bfs_err_t err = bfs_sb_read(bio, &txn->sb);
    if (err != BFS_OK) return err;
    txn->sb_new = txn->sb;
    txn->sb_new.txn_id = bfs_be64(bfs_be64(txn->sb.txn_id) + 1);
    txn->active = true;
    return BFS_OK;
}

void bfs_txn_set_dir_root(bfs_txn_t *txn, bfs_blk_t root)
{
    txn->sb_new.dir_tree_root = bfs_be32(root);
}

void bfs_txn_set_free_root(bfs_txn_t *txn, bfs_blk_t root)
{
    txn->sb_new.free_tree_root = bfs_be32(root);
}

void bfs_txn_set_free_blocks(bfs_txn_t *txn, uint32_t count)
{
    txn->sb_new.free_blocks = bfs_be32(count);
}

void bfs_txn_set_inode_root(bfs_txn_t *txn, bfs_blk_t root)
{
    txn->sb_new.inode_tree_root = bfs_be32(root);
}

bfs_err_t bfs_txn_commit(bfs_txn_t *txn)
{
    if (!txn->active) return BFS_ERR_INVAL;
    bfs_err_t err = bfs_sb_write(txn->bio, &txn->sb_new);
    if (err != BFS_OK) return err;
    txn->sb = txn->sb_new;
    txn->sb_new.txn_id = bfs_be64(bfs_be64(txn->sb.txn_id) + 1);
    /* active remains true: the transaction stays open for the next commit cycle.
     * Call txn_abort() to explicitly end the transaction without committing. */
    return BFS_OK;
}

void bfs_txn_abort(bfs_txn_t *txn)
{
    if (!txn->active) return;
    txn->sb_new = txn->sb;
    txn->sb_new.txn_id = bfs_be64(bfs_be64(txn->sb.txn_id) + 1);
    txn->active = false;
}

uint64_t bfs_txn_id(const bfs_txn_t *txn)
{
    return bfs_be64(txn->sb_new.txn_id);
}
