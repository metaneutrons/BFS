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
    if (!txn || !bio || !bio->ops || !bio->ops->read_block ||
        !bio->ops->write_block || !bio->ops->sync)
        return BFS_ERR_INVAL;
    memset(txn, 0, sizeof(*txn));
    txn->bio = bio;
    bfs_err_t err = bfs_sb_read(bio, &txn->sb);
    if (err != BFS_OK) return err;
    uint64_t committed_id = bfs_be64(txn->sb.txn_id);
    if (committed_id >= UINT64_MAX - 1) return BFS_ERR_NOSPC;
    txn->sb_new = txn->sb;
    txn->sb_new.txn_id = bfs_be64(committed_id + 1);
    txn->active = true;
    return BFS_OK;
}

void bfs_txn_set_dir_root(bfs_txn_t *txn, bfs_blk_t root)
{
    if (!txn) return;
    txn->sb_new.dir_tree_root = bfs_be32(root);
}

void bfs_txn_set_free_root(bfs_txn_t *txn, bfs_blk_t root)
{
    if (!txn) return;
    txn->sb_new.free_tree_root = bfs_be32(root);
}

void bfs_txn_set_free_blocks(bfs_txn_t *txn, uint32_t count)
{
    if (!txn) return;
    txn->sb_new.free_blocks = bfs_be32(count);
}

void bfs_txn_set_inode_root(bfs_txn_t *txn, bfs_blk_t root)
{
    if (!txn) return;
    txn->sb_new.inode_tree_root = bfs_be32(root);
}

/* Low-level commit primitive: write the working superblock and roll to the next
 * txn_id. The full filesystem commit boundary is bfs_txn_commit(fs), below. */
bfs_err_t bfs_txn_write_sb(bfs_txn_t *txn)
{
    if (!txn || !txn->active || !txn->bio) return BFS_ERR_INVAL;
    uint64_t next_id = bfs_be64(txn->sb_new.txn_id);
    if (next_id == UINT64_MAX) return BFS_ERR_NOSPC;
    bfs_err_t err = bfs_sb_write(txn->bio, &txn->sb_new);
    if (err != BFS_OK) return err;
    txn->sb = txn->sb_new;
    txn->sb_new.txn_id = bfs_be64(next_id + 1);
    /* active remains true: the transaction stays open for the next commit cycle.
     * Call txn_abort() to explicitly end the transaction without committing. */
    return BFS_OK;
}

void bfs_txn_abort(bfs_txn_t *txn)
{
    if (!txn || !txn->active) return;
    txn->sb_new = txn->sb;
    txn->sb_new.txn_id = bfs_be64(bfs_be64(txn->sb.txn_id) + 1);
    txn->active = false;
}

uint64_t bfs_txn_id(const bfs_txn_t *txn)
{
    if (!txn) return 0;
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

static bool preserve_pending_tail(bfs_fs_t *fs, const bfs_blk_t *items,
                                  uint32_t first, uint32_t count)
{
    uint32_t cap = bfs_fs_pending_cap(fs);
    while (first < count && fs->pending_count < cap)
        bfs_fs_pending_items(fs)[fs->pending_count++] = items[first++];
    return first == count;
}

/* The single transaction-commit boundary for a mounted filesystem: flush data
 * (data=ordered), return the reserve pool, gather the current tree roots into
 * the working superblock and write it, then drain the COW pending-free queue
 * (refcount-aware) and re-commit the free tree. Every caller that needs to make
 * filesystem state durable — file/snapshot/namespace mid-op, sync, unmount —
 * goes through here. */
static bfs_err_t reclaim_shared_blocks(bfs_fs_t *fs, const bfs_blk_t *blocks,
                                       uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        bool freed = false;
        bfs_err_t err = bfs_refcount_dec(&fs->refcount, blocks[i], &freed);
        if (err == BFS_OK && freed)
            err = bfs_freespace_free(&fs->freespace, blocks[i], 1);
        if (err != BFS_OK)
            return preserve_pending_tail(fs, blocks, i + 1, count)
                       ? err : BFS_ERR_NOSPC;
    }
    return BFS_OK;
}

static bfs_err_t reclaim_block_ranges(bfs_fs_t *fs, const bfs_blk_t *blocks,
                                      uint32_t count)
{
    uint32_t i = 0;
    while (i < count) {
        bfs_blk_t start = blocks[i];
        uint32_t len = 1;
        while (len < count - i && blocks[i + len] == start + len) len++;
        bfs_err_t err = bfs_freespace_free(&fs->freespace, start, len);
        if (err != BFS_OK)
            return preserve_pending_tail(fs, blocks, i + len, count)
                       ? err : BFS_ERR_NOSPC;
        i += len;
    }
    return BFS_OK;
}

static bfs_err_t reclaim_pending_batch(bfs_fs_t *fs)
{
    uint32_t count = fs->pending_count;
    if (count > bfs_fs_pending_cap(fs) ||
        (uint64_t)count * sizeof(bfs_blk_t) > SIZE_MAX)
        return BFS_ERR_CORRUPT;
    bfs_blk_t *blocks = malloc(count * sizeof(*blocks));
    if (!blocks) return BFS_ERR_NOMEM;
    /* The source capacity and exactly matching allocation size were checked above. */
    memcpy(blocks, bfs_fs_pending_items(fs), count * sizeof(*blocks)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    shellsort_blocks(blocks, count);
    for (uint32_t i = 1; i < count; i++) {
        if (blocks[i] == blocks[i - 1]) {
            free(blocks);
            return BFS_ERR_CORRUPT;
        }
    }
    fs->pending_count = 0;
    bfs_err_t err = fs->has_snapshots && fs->refcount.tree.root != BFS_BLK_NULL
        ? reclaim_shared_blocks(fs, blocks, count)
        : reclaim_block_ranges(fs, blocks, count);
    free(blocks);
    return err;
}

static bfs_err_t txn_commit_working(bfs_fs_t *fs)
{
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
        err = reclaim_pending_batch(fs);
        if (err != BFS_OK) return err;

        err = bfs_freespace_return_reserve(&fs->freespace);
        if (err != BFS_OK) return err;

        /* Final commit of free tree changes */
        bfs_txn_set_free_root(&fs->txn, fs->freespace.tree.root);
        bfs_txn_set_free_blocks(&fs->txn, fs->freespace.total_free);
        if (fs->has_snapshots)
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);
        err = bfs_txn_write_sb(&fs->txn);
        if (err != BFS_OK) return err;
        update_tree_txns(fs);
    }

    if (fs->pending_count > 0) return BFS_ERR_AGAIN;

    update_tree_txns(fs);
    return bfs_bio_sync(fs->bio);
}

bfs_err_t bfs_txn_commit(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted || !fs->bio || !fs->txn.active)
        return BFS_ERR_INVAL;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    /* An ordered-data flush has not modified commit state, so it can be retried. */
    if (fs->options & BFS_OPT_DATA_ORDERED) {
        bfs_err_t err = bfs_bio_sync(fs->bio);
        if (err != BFS_OK) return err;
    }
    bfs_err_t err = txn_commit_working(fs);
    /* Later failures can follow a partial superblock write or reclamation.
     * Require recovery instead of letting the next operation commit that state. */
    if (err != BFS_OK) fs->recovery_error = err;
    return err;
}
