/* SPDX-License-Identifier: MPL-2.0 */
#include "bfs_snapshot.h"
#include "bfs_internal.h"
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

uint64_t bfs_snapshot_record_txn_id(const bfs_snapshot_record_t *rec)
{
    if (!rec) return 0;
    return ((uint64_t)bfs_be32(rec->txn_id_hi) << 32) | bfs_be32(rec->txn_id_lo);
}

bfs_err_t bfs_snapshot_open(const bfs_snapshot_record_t *rec, bfs_bio_t *bio,
                            bfs_allocator_t *alloc,
                            bfs_dir_tree_t *dir_out, bfs_btree_t *inode_out)
{
    if (!rec || !bio || (!dir_out && !inode_out)) return BFS_ERR_INVAL;
    uint64_t txn = bfs_snapshot_record_txn_id(rec);
    if (dir_out) {
        bfs_err_t err = bfs_dir_init(dir_out, bio, alloc, bfs_be32(rec->dir_tree_root), txn);
        if (err != BFS_OK) return err;
    }
    if (inode_out) {
        bfs_err_t err = bfs_inode_init(inode_out, bio, alloc, bfs_be32(rec->inode_tree_root), txn);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

/* ── Helpers ───────────────────────────────────────────────── */

static bfs_err_t snapshot_next_id_checked(bfs_fs_t *fs, uint32_t *id_out);

static bfs_err_t snapshot_recover_after_error(bfs_fs_t *fs, bfs_err_t error)
{
    bfs_err_t reload_error = bfs_fs_reload_committed_unlocked(fs);
    return reload_error == BFS_OK ? error : reload_error;
}

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

static bool snapshot_name_contains(const char *name, char needle)
{
    while (*name) {
        if (*name++ == needle) return true;
    }
    return false;
}

static size_t snapshot_name_length(const char *name)
{
    size_t length = 0;
    while (length < BFS_SNAPSHOT_NAME_MAX && name[length]) length++;
    return length;
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
        fs->refcount.tree.free_sink = bfs_fs_free_sink(fs);
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
        if (v->cap > SIZE_MAX / 2 ||
            (v->cap ? v->cap * 2 : 256) > SIZE_MAX / sizeof(*v->items))
            return BFS_ERR_NOMEM;
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
                bfs_err_t rollback_err = bfs_refcount_dec(&ctx->fs->refcount,
                                                          blk, &freed);
                return rollback_err == BFS_OK ? err : rollback_err;
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

static bool snapshot_ref_inode_cb(const void *key, const void *val, void *ctx)
{
    (void)key;
    snap_ref_ctx_t *rc = (snap_ref_ctx_t *)ctx;
    const bfs_inode_t *inode = (const bfs_inode_t *)val;
    /* Refcount this inode's extent-tree node blocks AND its data blocks. */
    bfs_err_t werr = bfs_extent_walk(rc->fs->bio, &rc->fs->freespace, rc->fs->live_txn_id,
                                     bfs_be32(inode->extent_root),
                                     snapshot_ref_node_cb, snapshot_ref_node_cb, rc);
    if (rc->err == BFS_OK) rc->err = werr;
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

    bfs_err_t scan_err = bfs_btree_scan(inode_tree, NULL,
                                        snapshot_ref_inode_cb, &rc);
    return rc.err != BFS_OK ? rc.err : scan_err;
}

static bfs_err_t snapshot_rollback_refs(bfs_fs_t *fs, block_vec_t *refs)
{
    bfs_err_t result = BFS_OK;
    while (refs->count > 0) {
        bool freed = false;
        bfs_err_t err = bfs_refcount_dec(&fs->refcount,
                                         refs->items[--refs->count], &freed);
        if (err != BFS_OK && result == BFS_OK)
            result = err;
    }
    return result;
}

/* ── Create ────────────────────────────────────────────────── */

bfs_err_t bfs_snapshot_create_unlocked(bfs_fs_t *fs, const char *name)
{
    if (!fs || !fs->mounted || !name) return BFS_ERR_INVAL;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    size_t nlen = snapshot_name_length(name);
    if (nlen == 0 || nlen >= BFS_SNAPSHOT_NAME_MAX ||
        snapshot_name_contains(name, '/') || snapshot_name_contains(name, ':') ||
        is_deleting_name(name))
        return BFS_ERR_INVAL;

    bfs_err_t err = bfs_snapshot_find_by_name_unlocked(fs, name, NULL, NULL);
    if (err == BFS_OK) return BFS_ERR_EXISTS;
    if (err != BFS_ERR_NOTFOUND) return err;

    /* Sync first to get a consistent state */
    err = bfs_txn_commit(fs);
    if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

    /* Ensure refcount + snapshot trees exist */
    bool had_snapshots = fs->has_snapshots;
    err = ensure_snapshot_trees(fs);
    if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

    /* Build snapshot record */
    bfs_snapshot_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.dir_tree_root = bfs_be32(fs->dir_tree.tree.root);
    rec.inode_tree_root = bfs_be32(fs->inode_tree.root);
    { uint64_t t = bfs_txn_id(&fs->txn); rec.txn_id_hi = bfs_be32((uint32_t)(t >> 32)); rec.txn_id_lo = bfs_be32((uint32_t)t); }
    memcpy(rec.name, name, nlen);

    /* Validate the snapshot tree and reserve an ID before incrementing any
     * refcounts, so a corrupt tree cannot leave partial refcount mutations. */
    uint32_t next_id;
    err = snapshot_next_id_checked(fs, &next_id);
    if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    err = bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                         &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) return snapshot_recover_after_error(fs, err);
    snap_tree.free_sink = bfs_fs_free_sink(fs);

    block_vec_t rollback = {0};
    err = snapshot_ref_graph(fs, &fs->dir_tree.tree, &fs->inode_tree,
                             SNAP_REF_INC, &rollback);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = snapshot_rollback_refs(fs, &rollback);
        block_vec_free(&rollback);
        if (!had_snapshots && rollback_err == BFS_OK &&
            fs->refcount.tree.root == BFS_BLK_NULL)
            fs->has_snapshots = false;
        return snapshot_recover_after_error(
            fs, rollback_err == BFS_OK ? err : rollback_err);
    }

    /* Insert into snapshot tree (stored in superblock) */
    uint32_t id = bfs_be32(next_id);
    err = bfs_btree_insert(&snap_tree, &id, &rec);
    if (err != BFS_OK) {
        bfs_err_t rollback_err = snapshot_rollback_refs(fs, &rollback);
        block_vec_free(&rollback);
        if (!had_snapshots && rollback_err == BFS_OK &&
            fs->refcount.tree.root == BFS_BLK_NULL)
            fs->has_snapshots = false;
        return snapshot_recover_after_error(
            fs, rollback_err == BFS_OK ? err : rollback_err);
    }

    /* Update superblock */
    fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
    fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

    err = bfs_txn_commit(fs);
    block_vec_free(&rollback);
    return err == BFS_OK ? BFS_OK : snapshot_recover_after_error(fs, err);
}

/* ── Delete ────────────────────────────────────────────────── */

typedef struct {
    bfs_fs_t *fs;
    uint32_t count;
    uint32_t last_ino;
    bfs_err_t err;
    bool preflight;
} reclaim_scan_ctx_t;

typedef struct {
    block_vec_t blocks;
    size_t limit;
    bfs_err_t err;
} reclaim_blocks_t;

static void reclaim_collect_block(bfs_blk_t blk, void *ctx)
{
    reclaim_blocks_t *c = (reclaim_blocks_t *)ctx;
    if (c->err != BFS_OK) return;
    if (blk == BFS_BLK_NULL || c->blocks.count >= c->limit) {
        c->err = blk == BFS_BLK_NULL ? BFS_ERR_CORRUPT : BFS_ERR_NOSPC;
        return;
    }
    c->err = block_vec_push(&c->blocks, blk);
}

static void block_vec_sort(block_vec_t *blocks)
{
    for (size_t gap = blocks->count / 2u; gap > 0; gap /= 2u) {
        for (size_t i = gap; i < blocks->count; i++) {
            bfs_blk_t value = blocks->items[i];
            size_t j = i;
            while (j >= gap && blocks->items[j - gap] > value) {
                blocks->items[j] = blocks->items[j - gap];
                j -= gap;
            }
            blocks->items[j] = value;
        }
    }
}

static bfs_err_t snapshot_queue_reclaimed(bfs_fs_t *fs, const block_vec_t *blocks,
                                          const uint32_t *counts)
{
    uint32_t pending_before = fs->pending_count;
    for (size_t i = 0; i < blocks->count; i++) {
        if (counts[i] != 1u) continue;
        bfs_err_t err = bfs_fs_queue_pending_free(fs, blocks->items[i]);
        if (err != BFS_OK) {
            fs->pending_count = pending_before;
            return err;
        }
    }
    return BFS_OK;
}

static bfs_err_t snapshot_restore_counts(bfs_fs_t *fs, const block_vec_t *blocks,
                                         const uint32_t *counts, size_t processed)
{
    bfs_err_t result = BFS_OK;
    while (processed > 0) {
        processed--;
        if (counts[processed] <= 1u) continue;
        bfs_err_t err = bfs_refcount_inc(&fs->refcount, blocks->items[processed]);
        if (err != BFS_OK && result == BFS_OK) result = err;
    }
    return result;
}

static bfs_err_t snapshot_ref_dec_blocks_atomic(bfs_fs_t *fs,
                                                 block_vec_t *blocks)
{
    if (!fs || !blocks) return BFS_ERR_INVAL;
    if (blocks->count == 0) return BFS_OK;
    if (blocks->count > UINT32_MAX - BFS_BTREE_MAX_OP_FREES)
        return BFS_ERR_NOSPC;

    block_vec_sort(blocks);
    for (size_t i = 1; i < blocks->count; i++) {
        if (blocks->items[i] == blocks->items[i - 1])
            return BFS_ERR_CORRUPT;
    }

    bfs_err_t err = bfs_fs_ensure_free_headroom(
        fs, (uint32_t)blocks->count + BFS_BTREE_MAX_OP_FREES);
    if (err != BFS_OK) return err;

    uint32_t *counts = malloc(blocks->count * sizeof(*counts));
    if (!counts) return BFS_ERR_NOMEM;
    for (size_t i = 0; i < blocks->count; i++) {
        err = bfs_refcount_get_checked(&fs->refcount, blocks->items[i],
                                       &counts[i]);
        if (err != BFS_OK) {
            free(counts);
            return err;
        }
    }

    size_t processed = 0;
    while (processed < blocks->count) {
        bool freed = false;
        err = bfs_refcount_dec(&fs->refcount, blocks->items[processed], &freed);
        if (err != BFS_OK) break;
        processed++;
        if (freed != (counts[processed - 1u] == 1u)) {
            err = BFS_ERR_CORRUPT;
            break;
        }
    }

    if (err == BFS_OK)
        err = snapshot_queue_reclaimed(fs, blocks, counts);

    if (err != BFS_OK) {
        bfs_err_t rollback_err = snapshot_restore_counts(fs, blocks, counts, processed);
        if (rollback_err != BFS_OK)
            err = rollback_err;
    }

    free(counts);
    return err;
}

static bool reclaim_inode_cb(const void *key, const void *val, void *ctx)
{
    reclaim_scan_ctx_t *c = (reclaim_scan_ctx_t *)ctx;
    uint32_t ino = bfs_load_be32(key);
    const bfs_inode_t *inode = (const bfs_inode_t *)val;

    uint32_t cap = bfs_fs_pending_cap(c->fs);
    reclaim_blocks_t collect = {
        .blocks = {0},
        .limit = cap >= BFS_PENDING_FREES_MAX ? c->fs->bio->block_count :
                 cap > BFS_BTREE_MAX_OP_FREES
                     ? cap - BFS_BTREE_MAX_OP_FREES : 0,
        .err = BFS_OK,
    };
    bfs_err_t walk_err = bfs_extent_walk(
        c->fs->bio, &c->fs->freespace, c->fs->live_txn_id,
        bfs_be32(inode->extent_root), reclaim_collect_block,
        reclaim_collect_block, &collect);
    if (walk_err != BFS_OK)
        collect.err = walk_err;
    if (collect.err == BFS_OK && c->preflight) {
        block_vec_sort(&collect.blocks);
        for (size_t i = 1; i < collect.blocks.count; i++) {
            if (collect.blocks.items[i] == collect.blocks.items[i - 1]) {
                collect.err = BFS_ERR_CORRUPT;
                break;
            }
        }
        if (collect.err == BFS_OK) {
            collect.err = collect.blocks.count > UINT32_MAX - BFS_BTREE_MAX_OP_FREES
                ? BFS_ERR_NOSPC : bfs_fs_reserve_pending(
                    c->fs, (uint32_t)collect.blocks.count + BFS_BTREE_MAX_OP_FREES);
        }
    }
    if (collect.err == BFS_OK && !c->preflight)
        collect.err = snapshot_ref_dec_blocks_atomic(c->fs, &collect.blocks);
    block_vec_free(&collect.blocks);

    if (collect.err != BFS_OK) {
        c->err = collect.err;
        return false; /* Stop scan */
    }

    c->last_ino = ino;
    c->count++;
    /* Persist progress after every inode. This makes the record cursor and its
     * refcount changes one COW transaction, so an interrupted deletion never
     * replays a partially reclaimed inode. */
    return c->preflight;
}

static bfs_err_t snapshot_preflight_delete(bfs_fs_t *fs,
                                            const bfs_snapshot_record_t *rec)
{
    bfs_dir_tree_t dir;
    bfs_btree_t inodes;
    bfs_err_t err = bfs_snapshot_open(rec, fs->bio,
                                     bfs_freespace_allocator(&fs->freespace),
                                     &dir, &inodes);
    if (err != BFS_OK) return err;
    reclaim_scan_ctx_t scan = {.fs = fs, .preflight = true};
    err = bfs_btree_scan(&inodes, NULL, reclaim_inode_cb, &scan);
    if (err != BFS_OK) return err;
    if (scan.err != BFS_OK) return scan.err;

    uint32_t cap = bfs_fs_pending_cap(fs);
    reclaim_blocks_t collect = {
        .limit = cap >= BFS_PENDING_FREES_MAX ? fs->bio->block_count :
                 cap > BFS_BTREE_MAX_OP_FREES
                     ? cap - BFS_BTREE_MAX_OP_FREES : 0,
    };
    err = bfs_btree_walk_nodes(&dir.tree, reclaim_collect_block, &collect);
    if (err == BFS_OK && collect.err == BFS_OK)
        err = bfs_btree_walk_nodes(&inodes, reclaim_collect_block, &collect);
    if (err == BFS_OK) err = collect.err;
    if (err == BFS_OK) {
        err = collect.blocks.count > UINT32_MAX - BFS_BTREE_MAX_OP_FREES
            ? BFS_ERR_NOSPC : bfs_fs_reserve_pending(
                fs, (uint32_t)collect.blocks.count + BFS_BTREE_MAX_OP_FREES);
    }
    block_vec_free(&collect.blocks);
    return err;
}

bfs_err_t bfs_snapshot_delete_unlocked(bfs_fs_t *fs, uint32_t snapshot_id)
{
    if (!fs || !fs->mounted || snapshot_id == 0) return BFS_ERR_INVAL;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    if (!fs->has_snapshots) return BFS_ERR_NOTFOUND;

    /* Establish a clean durable baseline. Every later reclaim unit either
     * commits its cursor with its refcount changes or reloads from disk. */
    bfs_err_t err = bfs_txn_commit(fs);
    if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

    /* Find the snapshot */
    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    err = bfs_btree_init(&snap_tree, fs->bio,
                         bfs_freespace_allocator(&fs->freespace),
                         &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) return err;
    snap_tree.free_sink = bfs_fs_free_sink(fs);

    uint32_t key = bfs_be32(snapshot_id);
    bfs_snapshot_record_t rec;
    err = bfs_btree_search(&snap_tree, &key, &rec);
    if (err != BFS_OK)
        return err;

    /* Rename to .deleting_<id> first if not already renamed */
    if (!is_deleting_name((const char *)rec.name)) {
        /* Reject an oversized or unreadable graph before making deletion
         * resumable; otherwise every later mount would retry the same failure. */
        err = snapshot_preflight_delete(fs, &rec);
        if (err != BFS_OK) return err;
        char new_name[BFS_SNAPSHOT_NAME_MAX];
        format_deleting_name(new_name, snapshot_id);
        memset(rec.name, 0, sizeof(rec.name));
        memcpy(rec.name, new_name, strlen(new_name));
        rec.timestamp = bfs_be32(0); /* Init last_reclaimed_ino to 0 */

        err = bfs_btree_update(&snap_tree, &key, &rec);
        if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

        fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
        err = bfs_txn_commit(fs);
        if (err != BFS_OK) return snapshot_recover_after_error(fs, err);
    }

    /* Decrement refcounts for all nodes and file data in the snapshot graph progressively. */
    bool done = false;
    while (!done) {
        /* Re-init old_dir and old_inode tree helpers using the latest roots in 'rec' */
        bfs_dir_tree_t old_dir;
        bfs_btree_t old_inode;
        err = bfs_snapshot_open(&rec, fs->bio, bfs_freespace_allocator(&fs->freespace),
                                &old_dir, &old_inode);
        if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

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
        if (err != BFS_OK) return snapshot_recover_after_error(fs, err);
        if (c.err != BFS_OK) return snapshot_recover_after_error(fs, c.err);

        if (c.count == 0) {
            /* No more inodes to reclaim! We are done with the inode data blocks */
            done = true;

            /* Validate and collect both metadata trees before changing a
             * refcount, reserving the complete atomic reclaim unit in memory. */
            uint32_t cap = bfs_fs_pending_cap(fs);
            reclaim_blocks_t collect = {
                .blocks = {0},
                .limit = cap >= BFS_PENDING_FREES_MAX ? fs->bio->block_count :
                         cap > BFS_BTREE_MAX_OP_FREES
                             ? cap - BFS_BTREE_MAX_OP_FREES : 0,
                .err = BFS_OK,
            };
            err = bfs_btree_walk_nodes(&old_dir.tree, reclaim_collect_block,
                                       &collect);
            if (err == BFS_OK && collect.err == BFS_OK)
                err = bfs_btree_walk_nodes(&old_inode, reclaim_collect_block,
                                           &collect);
            if (err == BFS_OK) err = collect.err;
            if (err == BFS_OK)
                err = snapshot_ref_dec_blocks_atomic(fs, &collect.blocks);
            block_vec_free(&collect.blocks);
            if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

            /* Finally, remove the snapshot record from the snapshot tree completely */
            err = bfs_btree_delete(&snap_tree, &key);
            if (err != BFS_OK) return snapshot_recover_after_error(fs, err);
            if (snap_tree.root == BFS_BLK_NULL &&
                fs->refcount.tree.root != BFS_BLK_NULL)
                return snapshot_recover_after_error(fs, BFS_ERR_CORRUPT);
            fs->has_snapshots = snap_tree.root != BFS_BLK_NULL;
            fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

            err = bfs_txn_commit(fs);
            if (err != BFS_OK) return snapshot_recover_after_error(fs, err);
        } else {
            /* Persist the single fully reclaimed inode. */
            /* Update the last reclaimed ino in the snapshot record */
            rec.timestamp = bfs_be32(c.last_ino);

            err = bfs_btree_update(&snap_tree, &key, &rec);
            if (err != BFS_OK) return snapshot_recover_after_error(fs, err);

            fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
            fs->txn.sb_new.refcount_tree_root = bfs_be32(fs->refcount.tree.root);

            err = bfs_txn_commit(fs);
            if (err != BFS_OK) return snapshot_recover_after_error(fs, err);
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
    if (!fs || !fs->mounted || !cb) return BFS_ERR_INVAL;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snap_root == 0 || snap_root == BFS_BLK_NULL) return BFS_OK;

    bfs_btree_t snap_tree;
    bfs_err_t err = bfs_btree_init(&snap_tree, fs->bio,
                                   bfs_freespace_allocator(&fs->freespace),
                                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) return err;

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
    if (!fs || !fs->mounted || !name || !name[0] ||
        snapshot_name_length(name) >= BFS_SNAPSHOT_NAME_MAX)
        return BFS_ERR_INVAL;
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

static bfs_err_t snapshot_next_id_checked(bfs_fs_t *fs, uint32_t *id_out)
{
    if (!fs || !fs->mounted || !id_out) return BFS_ERR_INVAL;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snap_root == 0 || snap_root == BFS_BLK_NULL) {
        *id_out = 1;
        return BFS_OK;
    }

    bfs_btree_t snap_tree;
    bfs_err_t err = bfs_btree_init(&snap_tree, fs->bio,
                                   bfs_freespace_allocator(&fs->freespace),
                                   &snap_ops, snap_root, bfs_txn_id(&fs->txn));
    if (err != BFS_OK) return err;

    max_ctx_t mc = { 0 };
    err = bfs_btree_scan(&snap_tree, NULL, max_scan_cb, &mc);
    if (err != BFS_OK) return err;
    if (mc.max_id == UINT32_MAX) return BFS_ERR_NOSPC;
    *id_out = mc.max_id + 1;
    return BFS_OK;
}

uint32_t bfs_snapshot_next_id_unlocked(bfs_fs_t *fs)
{
    uint32_t id = 0;
    if (snapshot_next_id_checked(fs, &id) != BFS_OK) return 0;
    return id;
}

bfs_err_t bfs_snapshot_create(bfs_fs_t *fs, const char *name)
{
    if (!fs || !fs->mounted) return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_snapshot_create_unlocked(fs, name);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_snapshot_delete(bfs_fs_t *fs, uint32_t snapshot_id)
{
    if (!fs || !fs->mounted || snapshot_id == 0) return BFS_ERR_INVAL;
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
        if (c->cap > SIZE_MAX / 2 ||
            (c->cap ? c->cap * 2 : 32) > SIZE_MAX / sizeof(*c->ids) ||
            (c->cap ? c->cap * 2 : 32) > SIZE_MAX / sizeof(*c->recs)) {
            c->oom = true;
            return false;
        }
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
    if (!fs || !fs->mounted || !cb) return BFS_ERR_INVAL;
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
    if (!fs || !fs->mounted || !name || !name[0] ||
        strlen(name) >= BFS_SNAPSHOT_NAME_MAX)
        return BFS_ERR_INVAL;
    bfs_lock_read(&fs->lock);
    bfs_err_t err = bfs_snapshot_find_by_name_unlocked(fs, name, id_out, rec_out);
    bfs_lock_unlock(&fs->lock);
    return err;
}

uint32_t bfs_snapshot_next_id(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted) return 0;
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
    if (!fs || !fs->mounted)
        return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs->recovery_error;
    while (err == BFS_OK) {
        bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
        if (snap_root == BFS_BLK_NULL)
            break;

        bfs_btree_t snap_tree;
        err = bfs_btree_init(&snap_tree, fs->bio,
                             bfs_freespace_allocator(&fs->freespace),
                             &snap_ops, snap_root, bfs_txn_id(&fs->txn));
        if (err != BFS_OK)
            break;

        /* Collect before mutating; repeat so more than 32 interrupted deletions
         * are handled without retaining an unbounded in-memory ID list. */
        resume_ctx_t c = { .fs = fs, .count = 0 };
        err = bfs_btree_scan(&snap_tree, NULL, resume_scan_cb, &c);
        if (err != BFS_OK || c.count == 0)
            break;

        for (uint32_t i = 0; i < c.count; i++) {
            err = bfs_snapshot_delete_unlocked(fs, c.delete_ids[i]);
            if (err != BFS_OK)
                break;
        }
        if (err != BFS_OK)
            break;
    }
    bfs_lock_unlock(&fs->lock);
    return err;
}
