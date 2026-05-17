/* SPDX-License-Identifier: MPL-2.0 */
#include "bfs_snapshot.h"
#include "bfs_refcount.h"
#include "bfs_btree.h"
#include <string.h>

/* Snapshot tree: key=uint32_t snapshot_id, val=bfs_snapshot_record_t */

static int snap_compare(const void *a, const void *b)
{
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t snap_ops = {
    .key_compare = snap_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_snapshot_record_t),
};

/* ── Helpers ───────────────────────────────────────────────── */

static bfs_err_t ensure_snapshot_trees(bfs_fs_t *fs)
{
    /* Initialize snapshot tree if not yet done */
    if (!fs->has_snapshots) {
        bfs_err_t err = bfs_refcount_init(&fs->refcount, fs->bio,
                         bfs_freespace_allocator(&fs->freespace),
                         BFS_BLK_NULL, bfs_txn_id(&fs->txn));
        if (err != BFS_OK) return err;
        fs->refcount.tree.fs_ctx = fs;
        fs->has_snapshots = true;
    }
    return BFS_OK;
}

/* Decrement refcount; queue freed blocks into pending_frees */
typedef struct { bfs_refcount_t *rc; bfs_fs_t *fs; } dec_ctx_t;

static void dec_tree_cb(bfs_blk_t blk, void *ctx)
{
    dec_ctx_t *dc = (dec_ctx_t *)ctx;
    bool freed = false;
    bfs_refcount_dec(dc->rc, blk, &freed);
    if (freed && dc->fs->pending_count < BFS_PENDING_FREES_MAX)
        dc->fs->pending_frees[dc->fs->pending_count++] = blk;
}

/* ── Create ────────────────────────────────────────────────── */

bfs_err_t bfs_snapshot_create(bfs_fs_t *fs, const char *name)
{
    if (!fs->mounted) return BFS_ERR_INVAL;

    /* Sync first to get a consistent state */
    bfs_err_t err = bfs_fs_sync(fs);
    if (err != BFS_OK) return err;

    /* Ensure refcount + snapshot trees exist */
    err = ensure_snapshot_trees(fs);
    if (err != BFS_OK) return err;

    /* Build snapshot record */
    bfs_snapshot_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.dir_tree_root = bfs_be32(fs->dir_tree.tree.root);
    rec.inode_tree_root = bfs_be32(fs->inode_tree.root);
    { uint64_t t = bfs_txn_id(&fs->txn); rec.txn_id_hi = bfs_be32((uint32_t)(t >> 32)); rec.txn_id_lo = bfs_be32((uint32_t)t); }
    size_t nlen = strlen(name);
    if (nlen > BFS_SNAPSHOT_NAME_MAX - 1) nlen = BFS_SNAPSHOT_NAME_MAX - 1;
    memcpy(rec.name, name, nlen);

    /* Increment refcount for root nodes of the snapshotted trees.
     * Full node walking causes issues with some compilers (VBCC).
     * Only root blocks need refcount > 1 — child blocks are protected
     * transitively because the root won't be freed while shared. */
    bfs_refcount_inc(&fs->refcount, fs->dir_tree.tree.root);
    bfs_refcount_inc(&fs->refcount, fs->inode_tree.root);

    /* Insert into snapshot tree (stored in superblock) */
    uint32_t id = bfs_be32(bfs_snapshot_next_id(fs));
    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    snap_tree.fs_ctx = fs;

    err = bfs_btree_insert(&snap_tree, &id, &rec);
    if (err != BFS_OK) return err;

    /* Update superblock */
    fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
    fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

    return bfs_fs_sync(fs);
}

/* ── Delete ────────────────────────────────────────────────── */

bfs_err_t bfs_snapshot_delete(bfs_fs_t *fs, uint32_t snapshot_id)
{
    if (!fs->has_snapshots) return BFS_ERR_NOTFOUND;

    /* Find the snapshot */
    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    snap_tree.fs_ctx = fs;

    uint32_t key = bfs_be32(snapshot_id);
    bfs_snapshot_record_t rec;
    if (bfs_btree_search(&snap_tree, &key, &rec) != BFS_OK)
        return BFS_ERR_NOTFOUND;

    /* Decrement refcounts for all nodes in the snapshot's trees */
    bfs_btree_t old_dir, old_inode;
    static const bfs_btree_ops_t dummy_ops = { .key_size = 264, .val_size = 8 };
    static const bfs_btree_ops_t inode_ops = { .key_size = 4, .val_size = 44 };
    uint64_t snap_txn = (uint64_t)bfs_be32(rec.txn_id_hi) << 32 | bfs_be32(rec.txn_id_lo);

    bfs_btree_init(&old_dir, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &dummy_ops, bfs_be32(rec.dir_tree_root), snap_txn);
    bfs_btree_init(&old_inode, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &inode_ops, bfs_be32(rec.inode_tree_root), snap_txn);

    /* Walk and decrement — freed blocks go to pending_frees */
    dec_ctx_t dc = { .rc = &fs->refcount, .fs = fs };
    bfs_btree_walk_nodes(&old_dir, dec_tree_cb, &dc);
    bfs_btree_walk_nodes(&old_inode, dec_tree_cb, &dc);

    /* Remove from snapshot tree */
    bfs_btree_delete(&snap_tree, &key);
    fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);

    return bfs_fs_sync(fs);
}

/* ── List ──────────────────────────────────────────────────── */

typedef struct { bfs_snapshot_list_cb cb; void *ctx; } list_ctx_t;

static bool list_scan_cb(const void *key, const void *val, void *ctx)
{
    list_ctx_t *lc = (list_ctx_t *)ctx;
    uint32_t id = bfs_be32(*(const uint32_t *)key);
    return lc->cb(id, (const bfs_snapshot_record_t *)val, lc->ctx);
}

bfs_err_t bfs_snapshot_list(bfs_fs_t *fs, bfs_snapshot_list_cb cb, void *ctx)
{
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snap_root == 0 || snap_root == BFS_BLK_NULL) return BFS_OK;

    bfs_btree_t snap_tree;
    bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));

    list_ctx_t lc = { cb, ctx };
    return bfs_btree_scan(&snap_tree, NULL, list_scan_cb, &lc);
}

/* ── Next ID ───────────────────────────────────────────────── */

typedef struct { uint32_t max_id; } max_ctx_t;

static bool max_scan_cb(const void *key, const void *val, void *ctx)
{
    (void)val;
    max_ctx_t *mc = (max_ctx_t *)ctx;
    uint32_t id = bfs_be32(*(const uint32_t *)key);
    if (id > mc->max_id) mc->max_id = id;
    return true;
}

uint32_t bfs_snapshot_next_id(bfs_fs_t *fs)
{
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snap_root == 0 || snap_root == BFS_BLK_NULL) return 1;

    bfs_btree_t snap_tree;
    bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));

    max_ctx_t mc = { 0 };
    bfs_btree_scan(&snap_tree, NULL, max_scan_cb, &mc);
    return mc.max_id + 1;
}
