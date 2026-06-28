/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Transaction manager
 *
 * COW transaction model:
 *   txn_begin   → snapshot superblock, start tracking changes
 *   (mutations  → all COW'd, tree roots updated in the working copy)
 *   txn_commit  → the single filesystem commit boundary: return reserve, gather
 *                 tree roots, write the superblock, drain the COW pending-frees
 *   txn_abort   → discard working copy, revert to snapshot
 *
 * bfs_txn_write_sb is the low-level "make the working superblock durable"
 * primitive; bfs_txn_commit(fs) orchestrates the full filesystem commit on top
 * of it. The transaction manager is fs-aware on purpose — a BFS transaction IS
 * the filesystem's atomic state transition.
 *
 * Crash recovery: on mount, read both superblocks, pick highest valid txn_id.
 * No log replay needed — the COW model keeps the committed state consistent.
 */

#include "bfs_txn.h"
#include "bfs_fs.h"
#include <string.h>
#include <stdlib.h>

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

/* Low-level commit primitive: write the working superblock and roll to the next
 * txn_id. The full filesystem commit boundary is bfs_txn_commit(fs), below. */
bfs_err_t bfs_txn_write_sb(bfs_txn_t *txn)
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

/* ── Full filesystem commit boundary ───────────────────────── */

static void update_tree_txns(bfs_fs_t *fs)
{
    fs->live_txn_id = bfs_txn_id(&fs->txn);
}

static void shellsort_blocks(bfs_blk_t *arr, uint32_t count)
{
    static const uint32_t gaps[] = {1750, 701, 301, 132, 57, 23, 10, 4, 1, 0};
    for (const uint32_t *g = gaps; *g; g++) {
        uint32_t gap = *g;
        for (uint32_t i = gap; i < count; i++) {
            bfs_blk_t tmp = arr[i];
            uint32_t j = i;
            while (j >= gap && arr[j - gap] > tmp) {
                arr[j] = arr[j - gap];
                j -= gap;
            }
            arr[j] = tmp;
        }
    }
}

/* The single transaction-commit boundary for a mounted filesystem: flush data
 * (data=ordered), return the reserve pool, gather the current tree roots into
 * the working superblock and write it, then drain the COW pending-free queue
 * (refcount-aware) and re-commit the free tree. Every caller that needs to make
 * filesystem state durable — file/snapshot/namespace mid-op, sync, unmount —
 * goes through here. */
bfs_err_t bfs_txn_commit(bfs_fs_t *fs)
{
    if (!fs->mounted) return BFS_ERR_INVAL;

    /* Phase 0: If data=ordered is enabled, flush all data writes to physical media
     * before we commit the metadata that points to them. This ensures that a
     * crash never leaves an inode pointing to uninitialized data blocks. */
    if (fs->options & BFS_OPT_DATA_ORDERED) {
        bfs_bio_sync(fs->bio);
    }

    bfs_err_t err = bfs_freespace_return_reserve(&fs->freespace);
    if (err != BFS_OK) return err;

    /* Update superblock with current tree roots */
    bfs_txn_set_dir_root(&fs->txn, fs->dir_tree.tree.root);
    bfs_txn_set_free_root(&fs->txn, fs->freespace.tree.root);
    bfs_txn_set_free_blocks(&fs->txn, fs->freespace.total_free);
    bfs_txn_set_inode_root(&fs->txn, fs->inode_tree.root);
    if (fs->has_snapshots)
        fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);
    fs->txn.sb_new.next_ino = bfs_be32(fs->next_ino);

    err = bfs_txn_write_sb(&fs->txn);
    if (err != BFS_OK) return err;
    update_tree_txns(fs);

    /* Process pending frees: Use a local buffer to avoid overwriting while processing */
    int sync_iterations = 0;
    while (fs->pending_count > 0 && sync_iterations < 256) {
        sync_iterations++;
        uint32_t count = fs->pending_count;
        bfs_blk_t *process_buf = malloc(count * sizeof(bfs_blk_t));
        if (!process_buf) return BFS_ERR_NOMEM;

        memcpy(process_buf, fs->pending_frees, count * sizeof(bfs_blk_t));
        fs->pending_count = 0;

        shellsort_blocks(process_buf, count);

        uint32_t i = 0;
        while (i < count) {
            bfs_blk_t start = process_buf[i];
            uint32_t len = 1;
            while (i + len < count && process_buf[i + len] == start + len) len++;

            if (fs->has_snapshots && fs->refcount.tree.root != BFS_BLK_NULL) {
                for (uint32_t b = 0; b < len; b++) {
                    uint32_t rc = bfs_refcount_get(&fs->refcount, start + b);
                    if (rc > 0) {
                        bool freed;
                        bfs_refcount_dec(&fs->refcount, start + b, &freed);
                        if (freed) bfs_freespace_free(&fs->freespace, start + b, 1);
                    } else {
                        bfs_freespace_free(&fs->freespace, start + b, 1);
                    }
                }
            } else {
                bfs_freespace_free(&fs->freespace, start, len);
            }
            i += len;
        }
        free(process_buf);

        err = bfs_freespace_return_reserve(&fs->freespace);
        if (err != BFS_OK) return err;

        /* Final commit of free tree changes */
        bfs_txn_set_free_root(&fs->txn, fs->freespace.tree.root);
        bfs_txn_set_free_blocks(&fs->txn, fs->freespace.total_free);
        if (fs->has_snapshots)
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);
        err = bfs_txn_write_sb(&fs->txn);
        if (err != BFS_OK) return err;
    }

    update_tree_txns(fs);
    return bfs_bio_sync(fs->bio);
}
