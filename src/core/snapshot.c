/* SPDX-License-Identifier: MPL-2.0 */
#include "bfs_snapshot.h"
#include "bfs_refcount.h"
#include "bfs_btree.h"
#include "bfs_inode.h"
#include "bfs_extent.h"
#include <string.h>
#include <stdlib.h>

/* Snapshot tree: key=uint32_t snapshot_id, val=bfs_snapshot_record_t */

static const bfs_btree_ops_t snap_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_snapshot_record_t),
};

/* ── Helpers ───────────────────────────────────────────────── */

static void format_deleting_name(char *buf, uint32_t id)
{
    const char *prefix = ".deleting_";
    int i = 0;
    while (prefix[i] != '\0') {
        buf[i] = prefix[i];
        i++;
    }
    char num_buf[16];
    int idx = 0;
    if (id == 0) {
        num_buf[idx++] = '0';
    } else {
        while (id > 0) {
            num_buf[idx++] = '0' + (id % 10);
            id /= 10;
        }
    }
    for (int j = idx - 1; j >= 0; j--) {
        buf[i++] = num_buf[j];
    }
    buf[i] = '\0';
}

static bool is_deleting_name(const char *name)
{
    const char *prefix = ".deleting_";
    for (int i = 0; i < 10; i++) {
        if (name[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

static bfs_err_t ensure_snapshot_trees(bfs_fs_t *fs)
{
    /* Initialize snapshot tree if not yet done */
    if (!fs->has_snapshots) {
        bfs_err_t err = bfs_refcount_init(&fs->refcount, fs->bio,
                         bfs_freespace_allocator(&fs->freespace),
                         BFS_BLK_NULL, bfs_txn_id(&fs->txn));
        if (err != BFS_OK) return err;
        fs->refcount.tree.txn_id_ptr = &fs->live_txn_id;
        fs->refcount.tree.fs_ctx = fs;
        fs->has_snapshots = true;
    }
    return BFS_OK;
}

typedef struct {
    bfs_blk_t *items;
    size_t count;
    size_t cap;
} block_vec_t;

static void block_vec_free(block_vec_t *v)
{
    free(v->items);
    v->items = NULL;
    v->count = v->cap = 0;
}

static bfs_err_t block_vec_push(block_vec_t *v, bfs_blk_t blk)
{
    if (v->count == v->cap) {
        size_t new_cap = v->cap ? v->cap * 2 : 256;
        bfs_blk_t *new_items = malloc(new_cap * sizeof(*new_items));
        if (!new_items) return BFS_ERR_NOMEM;
        if (v->items) {
            memcpy(new_items, v->items, v->count * sizeof(*new_items));
            free(v->items);
        }
        v->items = new_items;
        v->cap = new_cap;
    }
    v->items[v->count++] = blk;
    return BFS_OK;
}

typedef enum {
    SNAP_REF_INC,
    SNAP_REF_DEC,
} snap_ref_mode_t;

typedef struct {
    bfs_fs_t *fs;
    snap_ref_mode_t mode;
    block_vec_t *rollback;
    bfs_err_t err;
} snap_ref_ctx_t;

static bfs_err_t snapshot_ref_block(snap_ref_ctx_t *ctx, bfs_blk_t blk)
{
    if (blk == BFS_BLK_NULL)
        return BFS_OK;
    if (blk >= ctx->fs->bio->block_count)
        return BFS_ERR_CORRUPT;

    if (ctx->mode == SNAP_REF_INC) {
        bfs_err_t err = bfs_refcount_inc(&ctx->fs->refcount, blk);
        if (err != BFS_OK) return err;
        if (ctx->rollback) {
            err = block_vec_push(ctx->rollback, blk);
            if (err != BFS_OK) {
                bool freed = false;
                bfs_refcount_dec(&ctx->fs->refcount, blk, &freed);
                return err;
            }
        }
        return BFS_OK;
    }

    bool freed = false;
    bfs_err_t err = bfs_refcount_dec(&ctx->fs->refcount, blk, &freed);
    if (err != BFS_OK) return err;
    return freed ? bfs_fs_queue_pending_free(ctx->fs, blk) : BFS_OK;
}

static void snapshot_ref_node_cb(bfs_blk_t blk, void *ctx)
{
    snap_ref_ctx_t *rc = (snap_ref_ctx_t *)ctx;
    if (rc->err == BFS_OK)
        rc->err = snapshot_ref_block(rc, blk);
}

/* Walk a tree's index nodes for refcounting, folding the walk's own read
 * failure into rc->err without clobbering an error the callback already set.
 * A swallowed read failure here would silently mis-count shared blocks. */
static void snapshot_ref_walk(bfs_btree_t *tree, snap_ref_ctx_t *rc)
{
    bfs_err_t werr = bfs_btree_walk_nodes(tree, snapshot_ref_node_cb, rc);
    if (rc->err == BFS_OK)
        rc->err = werr;
}

static bool snapshot_ref_extent_cb(const void *key, const void *val, void *ctx)
{
    (void)key;
    snap_ref_ctx_t *rc = (snap_ref_ctx_t *)ctx;
    const bfs_extent_val_t *ev = (const bfs_extent_val_t *)val;
    bfs_blk_t disk = bfs_be32(ev->disk_block);
    uint32_t len = bfs_be32(ev->length);

    for (uint32_t i = 0; i < len; i++) {
        rc->err = snapshot_ref_block(rc, disk + i);
        if (rc->err != BFS_OK) return false;
    }
    return true;
}

static bool snapshot_ref_inode_cb(const void *key, const void *val, void *ctx)
{
    (void)key;
    snap_ref_ctx_t *rc = (snap_ref_ctx_t *)ctx;
    const bfs_inode_t *inode = (const bfs_inode_t *)val;
    bfs_blk_t root = bfs_be32(inode->extent_root);
    if (root == BFS_BLK_NULL)
        return true;

    bfs_extent_tree_t et;
    rc->err = bfs_extent_init(&et, rc->fs->bio, &rc->fs->freespace,
                              root, bfs_txn_id(&rc->fs->txn));
    if (rc->err != BFS_OK) return false;

    snapshot_ref_walk(&et.tree, rc);
    if (rc->err != BFS_OK) return false;

    rc->err = bfs_btree_scan(&et.tree, NULL, snapshot_ref_extent_cb, rc);
    return rc->err == BFS_OK;
}

static bfs_err_t snapshot_ref_graph(bfs_fs_t *fs, bfs_btree_t *dir_tree,
                                    bfs_btree_t *inode_tree,
                                    snap_ref_mode_t mode,
                                    block_vec_t *rollback)
{
    snap_ref_ctx_t rc = {
        .fs = fs,
        .mode = mode,
        .rollback = rollback,
        .err = BFS_OK,
    };

    snapshot_ref_walk(dir_tree, &rc);
    if (rc.err != BFS_OK) return rc.err;

    snapshot_ref_walk(inode_tree, &rc);
    if (rc.err != BFS_OK) return rc.err;

    rc.err = bfs_btree_scan(inode_tree, NULL, snapshot_ref_inode_cb, &rc);
    return rc.err;
}

static void snapshot_rollback_refs(bfs_fs_t *fs, block_vec_t *refs)
{
    while (refs->count > 0) {
        bool freed = false;
        bfs_refcount_dec(&fs->refcount, refs->items[--refs->count], &freed);
    }
}

/* ── Create ────────────────────────────────────────────────── */

bfs_err_t bfs_snapshot_create_unlocked(bfs_fs_t *fs, const char *name)
{
    if (!fs->mounted) return BFS_ERR_INVAL;

    /* Sync first to get a consistent state */
    bfs_err_t err = bfs_fs_sync_unlocked(fs);
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

    block_vec_t rollback = {0};
    err = snapshot_ref_graph(fs, &fs->dir_tree.tree, &fs->inode_tree,
                             SNAP_REF_INC, &rollback);
    if (err != BFS_OK) {
        snapshot_rollback_refs(fs, &rollback);
        block_vec_free(&rollback);
        return err;
    }

    /* Insert into snapshot tree (stored in superblock) */
    uint32_t id = bfs_be32(bfs_snapshot_next_id_unlocked(fs));
    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    err = bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                         &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) return err;
    snap_tree.fs_ctx = fs;

    err = bfs_btree_insert(&snap_tree, &id, &rec);
    if (err != BFS_OK) {
        snapshot_rollback_refs(fs, &rollback);
        block_vec_free(&rollback);
        return err;
    }

    /* Update superblock */
    fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
    fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

    err = bfs_fs_sync_unlocked(fs);
    if (err != BFS_OK) {
        /* Commit failed — reverse the refcount increments, matching the two
         * error paths above, so we don't leave inflated in-memory counts.
         * (A full transactional abort of the half-written roots is a larger
         * change; this at least restores refcount symmetry.) */
        snapshot_rollback_refs(fs, &rollback);
    }
    block_vec_free(&rollback);
    return err;
}

/* ── Delete ────────────────────────────────────────────────── */

typedef struct {
    bfs_fs_t *fs;
    uint32_t count;
    uint32_t last_ino;
    bfs_err_t err;
} reclaim_scan_ctx_t;

static bool reclaim_inode_cb(const void *key, const void *val, void *ctx)
{
    reclaim_scan_ctx_t *c = (reclaim_scan_ctx_t *)ctx;
    uint32_t ino = bfs_load_be32(key);
    const bfs_inode_t *inode = (const bfs_inode_t *)val;

    snap_ref_ctx_t rc = {
        .fs = c->fs,
        .mode = SNAP_REF_DEC,
        .rollback = NULL,
        .err = BFS_OK,
    };

    /* Process data blocks of this inode */
    bfs_blk_t root = bfs_be32(inode->extent_root);
    if (root != BFS_BLK_NULL) {
        bfs_extent_tree_t et;
        rc.err = bfs_extent_init(&et, rc.fs->bio, &rc.fs->freespace,
                                 root, bfs_txn_id(&rc.fs->txn));
        if (rc.err == BFS_OK) {
            snapshot_ref_walk(&et.tree, &rc);
            if (rc.err == BFS_OK) {
                bfs_btree_scan(&et.tree, NULL, snapshot_ref_extent_cb, &rc);
            }
        }
    }

    if (rc.err != BFS_OK) {
        c->err = rc.err;
        return false; /* Stop scan */
    }

    c->last_ino = ino;
    c->count++;
    if (c->count >= 50) {
        return false; /* Stop scan for this batch */
    }
    return true;
}

bfs_err_t bfs_snapshot_delete_unlocked(bfs_fs_t *fs, uint32_t snapshot_id)
{
    if (!fs->has_snapshots) return BFS_ERR_NOTFOUND;

    /* Find the snapshot */
    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    bfs_err_t err = bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) return err;
    snap_tree.fs_ctx = fs;

    uint32_t key = bfs_be32(snapshot_id);
    bfs_snapshot_record_t rec;
    if (bfs_btree_search(&snap_tree, &key, &rec) != BFS_OK)
        return BFS_ERR_NOTFOUND;

    /* Rename to .deleting_<id> first if not already renamed */
    if (!is_deleting_name((const char *)rec.name)) {
        char new_name[BFS_SNAPSHOT_NAME_MAX];
        format_deleting_name(new_name, snapshot_id);
        memset(rec.name, 0, sizeof(rec.name));
        memcpy(rec.name, new_name, strlen(new_name));
        rec.timestamp = bfs_be32(0); /* Init last_reclaimed_ino to 0 */

        err = bfs_btree_update(&snap_tree, &key, &rec);
        if (err != BFS_OK) return err;

        fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
        err = bfs_fs_sync_unlocked(fs);
        if (err != BFS_OK) return err;
    }

    /* Decrement refcounts for all nodes and file data in the snapshot graph progressively. */
    bool done = false;
    while (!done) {
        /* Re-init old_dir and old_inode tree helpers using the latest roots in 'rec' */
        bfs_dir_tree_t old_dir;
        bfs_btree_t old_inode;
        uint64_t snap_txn = (uint64_t)bfs_be32(rec.txn_id_hi) << 32 | bfs_be32(rec.txn_id_lo);

        err = bfs_dir_init(&old_dir, fs->bio, bfs_freespace_allocator(&fs->freespace),
                           bfs_be32(rec.dir_tree_root), snap_txn);
        if (err != BFS_OK) return err;
        err = bfs_inode_init(&old_inode, fs->bio, bfs_freespace_allocator(&fs->freespace),
                             bfs_be32(rec.inode_tree_root), snap_txn);
        if (err != BFS_OK) return err;

        uint32_t last_reclaimed = bfs_be32(rec.timestamp);
        reclaim_scan_ctx_t c = {
            .fs = fs,
            .count = 0,
            .last_ino = last_reclaimed,
            .err = BFS_OK,
        };

        /* Since start_key in bfs_btree_scan returns >= start_key, we search for last_reclaimed + 1 */
        uint32_t next_ino = last_reclaimed + 1;
        uint32_t start_key = bfs_be32(next_ino);

        err = bfs_btree_scan(&old_inode, &start_key, reclaim_inode_cb, &c);
        if (err != BFS_OK) return err;
        if (c.err != BFS_OK) return c.err;

        if (c.count == 0) {
            /* No more inodes to reclaim! We are done with the inode data blocks */
            done = true;

            /* Decrement refcounts of the tree nodes themselves */
            snap_ref_ctx_t rc = {
                .fs = fs,
                .mode = SNAP_REF_DEC,
                .rollback = NULL,
                .err = BFS_OK,
            };
            snapshot_ref_walk(&old_dir.tree, &rc);
            if (rc.err != BFS_OK) return rc.err;

            snapshot_ref_walk(&old_inode, &rc);
            if (rc.err != BFS_OK) return rc.err;

            /* Finally, remove the snapshot record from the snapshot tree completely */
            err = bfs_btree_delete(&snap_tree, &key);
            if (err != BFS_OK) return err;
            fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

            err = bfs_fs_sync_unlocked(fs);
            if (err != BFS_OK) return err;
        } else {
            /* We reclaimed c.count inodes in this batch (up to 50) */
            /* Update the last reclaimed ino in the snapshot record */
            rec.timestamp = bfs_be32(c.last_ino);

            err = bfs_btree_update(&snap_tree, &key, &rec);
            if (err != BFS_OK) return err;

            fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

            err = bfs_fs_sync_unlocked(fs);
            if (err != BFS_OK) return err;
        }
    }
    return BFS_OK;
}

/* ── List ──────────────────────────────────────────────────── */

typedef struct { bfs_snapshot_list_cb cb; void *ctx; } list_ctx_t;

static bool list_scan_cb(const void *key, const void *val, void *ctx)
{
    list_ctx_t *lc = (list_ctx_t *)ctx;
    const bfs_snapshot_record_t *r = (const bfs_snapshot_record_t *)val;
    if (is_deleting_name((const char *)r->name)) {
        return true; /* Skip and continue */
    }
    uint32_t id = bfs_load_be32(key);
    return lc->cb(id, r, lc->ctx);
}

bfs_err_t bfs_snapshot_list_unlocked(bfs_fs_t *fs, bfs_snapshot_list_cb cb, void *ctx)
{
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snap_root == 0 || snap_root == BFS_BLK_NULL) return BFS_OK;

    bfs_btree_t snap_tree;
    bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));

    list_ctx_t lc = { cb, ctx };
    return bfs_btree_scan(&snap_tree, NULL, list_scan_cb, &lc);
}

typedef struct {
    const char *name;
    uint32_t *id_out;
    bfs_snapshot_record_t *rec_out;
    bool found;
} find_name_ctx_t;

static bool find_name_cb(uint32_t id, const bfs_snapshot_record_t *rec, void *ctx)
{
    find_name_ctx_t *fc = (find_name_ctx_t *)ctx;
    size_t i = 0;
    while (i < BFS_SNAPSHOT_NAME_MAX && rec->name[i] && fc->name[i] &&
           rec->name[i] == (uint8_t)fc->name[i])
        i++;
    if ((i == BFS_SNAPSHOT_NAME_MAX || rec->name[i] == 0) && fc->name[i] == 0) {
        if (fc->id_out) *fc->id_out = id;
        if (fc->rec_out) *fc->rec_out = *rec;
        fc->found = true;
        return false;
    }
    return true;
}

bfs_err_t bfs_snapshot_find_by_name_unlocked(bfs_fs_t *fs, const char *name,
                                      uint32_t *id_out,
                                      bfs_snapshot_record_t *rec_out)
{
    find_name_ctx_t fc = {
        .name = name,
        .id_out = id_out,
        .rec_out = rec_out,
        .found = false,
    };
    bfs_err_t err = bfs_snapshot_list_unlocked(fs, find_name_cb, &fc);
    if (err != BFS_OK) return err;
    return fc.found ? BFS_OK : BFS_ERR_NOTFOUND;
}

/* ── Next ID ───────────────────────────────────────────────── */

typedef struct { uint32_t max_id; } max_ctx_t;

static bool max_scan_cb(const void *key, const void *val, void *ctx)
{
    (void)val;
    max_ctx_t *mc = (max_ctx_t *)ctx;
    uint32_t id = bfs_load_be32(key);
    if (id > mc->max_id) mc->max_id = id;
    return true;
}

uint32_t bfs_snapshot_next_id_unlocked(bfs_fs_t *fs)
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

bfs_err_t bfs_snapshot_create(bfs_fs_t *fs, const char *name)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_snapshot_create_unlocked(fs, name);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_snapshot_delete(bfs_fs_t *fs, uint32_t snapshot_id)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_snapshot_delete_unlocked(fs, snapshot_id);
    bfs_lock_unlock(&fs->lock);
    return err;
}

/* Collector for bfs_snapshot_list: snapshots every (id, record) pair into a
 * heap buffer while the fs lock is held, so the user callback can be invoked
 * AFTER the lock is released. Holding the read lock across an arbitrary user
 * callback risks self-deadlock against the non-recursive rwlock if the callback
 * re-enters any bfs_* API that takes a lock. Grows with malloc+memcpy+free (no
 * realloc — the AmigaOS stdlib shim does not provide it). */
typedef struct {
    uint32_t *ids;
    bfs_snapshot_record_t *recs;
    size_t count;
    size_t cap;
    bool oom;
} snap_collect_t;

static bool snap_collect_cb(uint32_t id, const bfs_snapshot_record_t *rec, void *ctx)
{
    snap_collect_t *c = (snap_collect_t *)ctx;
    if (c->count == c->cap) {
        size_t new_cap = c->cap ? c->cap * 2 : 32;
        uint32_t *ni = malloc(new_cap * sizeof(*ni));
        bfs_snapshot_record_t *nr = malloc(new_cap * sizeof(*nr));
        if (!ni || !nr) { free(ni); free(nr); c->oom = true; return false; }
        if (c->ids) { memcpy(ni, c->ids, c->count * sizeof(*ni)); free(c->ids); }
        if (c->recs) { memcpy(nr, c->recs, c->count * sizeof(*nr)); free(c->recs); }
        c->ids = ni;
        c->recs = nr;
        c->cap = new_cap;
    }
    c->ids[c->count] = id;
    c->recs[c->count] = *rec;
    c->count++;
    return true;
}

bfs_err_t bfs_snapshot_list(bfs_fs_t *fs, bfs_snapshot_list_cb cb, void *ctx)
{
    snap_collect_t c = { NULL, NULL, 0, 0, false };

    bfs_lock_read(&fs->lock);
    bfs_err_t err = bfs_snapshot_list_unlocked(fs, snap_collect_cb, &c);
    bfs_lock_unlock(&fs->lock);

    if (err == BFS_OK && c.oom)
        err = BFS_ERR_NOMEM;

    /* Invoke the user callback outside the lock; honour its stop signal. */
    if (err == BFS_OK) {
        for (size_t i = 0; i < c.count; i++) {
            if (!cb(c.ids[i], &c.recs[i], ctx))
                break;
        }
    }

    free(c.ids);
    free(c.recs);
    return err;
}

bfs_err_t bfs_snapshot_find_by_name(bfs_fs_t *fs, const char *name,
                                      uint32_t *id_out,
                                      bfs_snapshot_record_t *rec_out)
{
    bfs_lock_read(&fs->lock);
    bfs_err_t err = bfs_snapshot_find_by_name_unlocked(fs, name, id_out, rec_out);
    bfs_lock_unlock(&fs->lock);
    return err;
}

uint32_t bfs_snapshot_next_id(bfs_fs_t *fs)
{
    bfs_lock_read(&fs->lock);
    uint32_t id = bfs_snapshot_next_id_unlocked(fs);
    bfs_lock_unlock(&fs->lock);
    return id;
}

typedef struct {
    bfs_fs_t *fs;
    uint32_t delete_ids[32];
    uint32_t count;
} resume_ctx_t;

static bool resume_scan_cb(const void *key, const void *val, void *ctx)
{
    resume_ctx_t *c = (resume_ctx_t *)ctx;
    const bfs_snapshot_record_t *rec = (const bfs_snapshot_record_t *)val;
    uint32_t id = bfs_load_be32(key);
    if (is_deleting_name((const char *)rec->name)) {
        if (c->count < 32) {
            c->delete_ids[c->count++] = id;
        }
    }
    return true;
}

bfs_err_t bfs_snapshot_resume_deletions(bfs_fs_t *fs)
{
    bfs_lock_write(&fs->lock);
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snap_root == 0 || snap_root == BFS_BLK_NULL) {
        bfs_lock_unlock(&fs->lock);
        return BFS_OK;
    }

    bfs_btree_t snap_tree;
    bfs_err_t err = bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) {
        bfs_lock_unlock(&fs->lock);
        return err;
    }
    snap_tree.fs_ctx = fs;

    /* To avoid modifying the tree while scanning, we collect all IDs of deleting snapshots first */
    resume_ctx_t c = { .fs = fs, .count = 0 };
    err = bfs_btree_scan(&snap_tree, NULL, resume_scan_cb, &c);
    if (err != BFS_OK) {
        bfs_lock_unlock(&fs->lock);
        return err;
    }

    for (uint32_t i = 0; i < c.count; i++) {
        bfs_snapshot_delete_unlocked(fs, c.delete_ids[i]);
    }
    bfs_lock_unlock(&fs->lock);
    return BFS_OK;
}
