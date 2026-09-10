/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Filesystem format, mount, sync, unmount
 */

#include "bfs_fs.h"
#include "bfs_internal.h"
#include "bfs_inode.h"
#include "bfs_extent.h"
#include "bfs_snapshot.h"
#include <string.h>
#include <stdlib.h>

/* Global metadata-reserve sizing (blocks held back so delete/rename/COW never
 * hit ENOSPC mid-transaction): target ~1/20 of the volume, but at least
 * BFS_GRESERVE_TARGET on normal volumes, clamped to [FLOOR, CAP], with small-
 * and tiny-volume fallbacks. */
#define BFS_GRESERVE_FRACTION     20    /* target = block_count / 20  (~5%) */
#define BFS_GRESERVE_TARGET       64    /* minimum target on a normal volume */
#define BFS_GRESERVE_SMALL_BLOCKS 1024  /* "small volume" threshold (data blocks) */
#define BFS_GRESERVE_SMALL_DIV    8     /* small volume: data_blocks / 8 */
#define BFS_GRESERVE_FLOOR        8     /* absolute minimum */
#define BFS_GRESERVE_CAP          512   /* absolute maximum */
#define BFS_GRESERVE_TINY_DIV     4     /* fallback if reserve would exceed the volume */

static bool fs_bio_valid(const bfs_bio_t *bio, bool read_only)
{
    return bio && bio->ops && bio->ops->read_block &&
           (read_only || (bio->ops->write_block && bio->ops->sync)) &&
           bfs_block_size_valid(bio->block_size) &&
           bio->block_count >= BFS_MIN_VOLUME_BLOCKS;
}

static bool fs_string_contains(const char *text, char needle)
{
    while (*text) {
        if (*text++ == needle) return true;
    }
    return false;
}

/* ── Format ────────────────────────────────────────────────── */

bfs_err_t bfs_fs_format(bfs_bio_t *bio, const char *volname, uint32_t options)
{
    if (!fs_bio_valid(bio, false) || !volname) return BFS_ERR_INVAL;
    size_t nlen = strlen(volname);
    if (nlen == 0 || nlen >= BFS_VOLNAME_MAX ||
        fs_string_contains(volname, ':') || fs_string_contains(volname, '/') ||
        (options & ~(BFS_OPT_DATA_CHECKSUMS | BFS_OPT_SNAPSHOTS |
                     BFS_OPT_DATA_ORDERED)) != 0)
        return BFS_ERR_INVAL;
    uint32_t bs = bio->block_size;
    bfs_blk_t bc = bio->block_count;

    if (!bfs_block_size_valid(bs) || bc < BFS_MIN_VOLUME_BLOCKS)
        return BFS_ERR_INVAL;

    bfs_blk_t data_start = bfs_data_start_block(bs);
    if (data_start == BFS_BLK_NULL || bc <= data_start)
        return BFS_ERR_INVAL;
    uint32_t data_blocks = bc - data_start;
    uint64_t backup_off = bfs_default_backup_offset(bc, bs);

    bfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic       = bfs_be32(BFS_SB_MAGIC);
    sb.version     = bfs_be32(BFS_SB_VERSION);
    sb.block_size  = bfs_be32(bs);
    sb.block_count = bfs_be32(bc);
    sb.txn_id      = bfs_be64(1);
    sb.free_blocks = bfs_be32(data_blocks);
    sb.options     = bfs_be32(options);
    sb.next_ino    = bfs_be32(BFS_ROOT_INO + 1);
    sb.sb_backup_offset_lo = bfs_be32((uint32_t)backup_off);
    sb.sb_backup_offset_hi = bfs_be32((uint32_t)(backup_off >> 32));

    memcpy(sb.volname, volname, nlen);
    sb.crc32 = bfs_be32(bfs_sb_compute_crc(&sb));

    bfs_err_t err = bfs_sb_write_raw(bio, BFS_SB_OFFSET_A, &sb);
    if (err != BFS_OK) return err;
    err = bfs_sb_write_raw(bio, backup_off, &sb);
    if (err != BFS_OK) return err;

    bfs_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.pending_frees_cap = BFS_PENDING_FREES_MAX;
    bfs_lock_init(&fs.lock);
    fs.bio = bio;
    fs.txn.bio = bio;
    fs.txn.sb = sb;
    fs.txn.sb_new = sb;
    fs.txn.sb_new.txn_id = bfs_be64(2);
    fs.txn.active = true;
    fs.live_txn_id = 2;
    fs.next_ino = BFS_ROOT_INO + 1;
    fs.options = options;

    err = bfs_freespace_init(&fs.freespace, bio, BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) goto out;
    fs.freespace.tree.txn_id_ptr = &fs.live_txn_id;
    fs.freespace.tree.free_sink = bfs_fs_free_sink(&fs);
    fs.freespace.sb = &fs.txn.sb_new;

    uint32_t epool_count = BFS_EMERGENCY_POOL_SIZE;
    if (epool_count > data_blocks / 4) epool_count = data_blocks / 4;
    for (uint32_t i = 0; i < epool_count; i++)
        sb.emergency_pool[i] = bfs_be32(data_start + i);
    sb.emergency_count = bfs_be32(epool_count);
    fs.txn.sb_new.emergency_count = sb.emergency_count;
    memcpy(fs.txn.sb_new.emergency_pool, sb.emergency_pool, sizeof(sb.emergency_pool));

    uint32_t greserve = bc / BFS_GRESERVE_FRACTION;
    if (greserve < BFS_GRESERVE_TARGET)
        greserve = (data_blocks < BFS_GRESERVE_SMALL_BLOCKS)
                   ? (data_blocks / BFS_GRESERVE_SMALL_DIV) : BFS_GRESERVE_TARGET;
    if (greserve < BFS_GRESERVE_FLOOR) greserve = BFS_GRESERVE_FLOOR;
    if (greserve > BFS_GRESERVE_CAP) greserve = BFS_GRESERVE_CAP;
    if (greserve >= data_blocks) greserve = data_blocks / BFS_GRESERVE_TINY_DIV;
    sb.global_reserve = bfs_be32(greserve);
    fs.txn.sb_new.global_reserve = sb.global_reserve;
    fs.freespace.global_reserve = greserve;

    bfs_blk_t backup_blk = (bfs_blk_t)(backup_off / bs);
    if (backup_blk >= data_start + epool_count && backup_blk < bc) {
        err = bfs_freespace_add(&fs.freespace, data_start + epool_count, backup_blk - (data_start + epool_count));
        if (err != BFS_OK) goto out;
        err = bfs_freespace_add(&fs.freespace, backup_blk + 1, bc - (backup_blk + 1));
        if (err != BFS_OK) goto out;
    } else {
        err = bfs_freespace_add(&fs.freespace, data_start + epool_count, data_blocks - epool_count);
        if (err != BFS_OK) goto out;
    }
    err = bfs_freespace_refill_reserve(&fs.freespace);
    if (err != BFS_OK) goto out;

    err = bfs_dir_init(&fs.dir_tree, bio, bfs_freespace_allocator(&fs.freespace),
                  BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) goto out;
    fs.dir_tree.tree.txn_id_ptr = &fs.live_txn_id;
    fs.dir_tree.tree.free_sink = bfs_fs_free_sink(&fs);
    err = bfs_dir_insert(&fs.dir_tree, 0, "/", 1, BFS_ROOT_INO, BFS_INODE_DIR);
    if (err != BFS_OK) goto out;

    err = bfs_inode_init(&fs.inode_tree, bio, bfs_freespace_allocator(&fs.freespace),
                    BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) goto out;
    fs.inode_tree.txn_id_ptr = &fs.live_txn_id;
    fs.inode_tree.free_sink = bfs_fs_free_sink(&fs);
    bfs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.inode_nr = bfs_be32(BFS_ROOT_INO);
    root_inode.type = bfs_be32(BFS_INODE_DIR);
    root_inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs.inode_tree, BFS_ROOT_INO, &root_inode);
    if (err != BFS_OK) goto out;

    err = bfs_freespace_return_reserve(&fs.freespace);
    if (err != BFS_OK) goto out;

    /* Use the normal full commit boundary so COW blocks retired while building
     * the initial trees are reclaimed instead of leaked on a fresh volume. */
    fs.mounted = true;
    err = bfs_txn_commit(&fs);
    fs.mounted = false;
    /* Replace the remaining bootstrap-only copy before reporting success.
     * Either superblock must mount the completed filesystem on a fresh disk. */
    if (err == BFS_OK) err = bfs_sb_write(bio, &fs.txn.sb);
out:
    free(fs.pending_frees_dynamic);
    bfs_lock_destroy(&fs.lock);
    if (err != BFS_OK) return err;
    return bfs_bio_sync(bio);
}

/* ── Mount ─────────────────────────────────────────────────── */

static bfs_err_t fs_open_namespace_trees(bfs_fs_t *fs)
{
    bfs_superblock_t *sb = &fs->txn.sb;
    bfs_allocator_t *alloc = bfs_freespace_allocator(&fs->freespace);
    bfs_err_t err = bfs_dir_init(&fs->dir_tree, fs->bio, alloc,
                                 bfs_be32(sb->dir_tree_root), fs->live_txn_id);
    if (err != BFS_OK) return err;
    fs->dir_tree.tree.txn_id_ptr = &fs->live_txn_id;
    fs->dir_tree.tree.free_sink = bfs_fs_free_sink(fs);
    err = bfs_inode_init(&fs->inode_tree, fs->bio, alloc,
                         bfs_be32(sb->inode_tree_root), fs->live_txn_id);
    if (err != BFS_OK) return err;
    fs->inode_tree.txn_id_ptr = &fs->live_txn_id;
    fs->inode_tree.free_sink = bfs_fs_free_sink(fs);
    return BFS_OK;
}

static bfs_err_t fs_open_refcount_tree(bfs_fs_t *fs)
{
    bfs_superblock_t *sb = &fs->txn.sb;
    bfs_blk_t root = bfs_be32(sb->refcount_tree_root);
    fs->has_snapshots = bfs_be32(sb->snapshot_tree_root) != BFS_BLK_NULL;
    if (!fs->has_snapshots) {
        memset(&fs->refcount, 0, sizeof(fs->refcount));
        return root == BFS_BLK_NULL ? BFS_OK : BFS_ERR_CORRUPT;
    }
    bfs_err_t err = bfs_refcount_init(&fs->refcount, fs->bio,
        bfs_freespace_allocator(&fs->freespace), root, fs->live_txn_id);
    if (err != BFS_OK) return err;
    fs->refcount.tree.txn_id_ptr = &fs->live_txn_id;
    fs->refcount.tree.free_sink = bfs_fs_free_sink(fs);
    return BFS_OK;
}

static bfs_err_t fs_validate_root(bfs_fs_t *fs)
{
    if (fs->next_ino <= BFS_ROOT_INO || fs->next_ino > 0x80000000u)
        return BFS_ERR_CORRUPT;
    uint32_t ino = 0, type = 0;
    bfs_inode_t inode;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, 0, "/", 1, &ino, &type);
    if (err != BFS_OK) return err;
    err = bfs_inode_read(&fs->inode_tree, BFS_ROOT_INO, &inode);
    if (err != BFS_OK) return err;
    return ino == BFS_ROOT_INO && type == BFS_INODE_DIR &&
           bfs_be32(inode.type) == BFS_INODE_DIR ? BFS_OK : BFS_ERR_CORRUPT;
}

static bfs_err_t fs_load_working_state(bfs_fs_t *fs)
{
    bfs_superblock_t *sb = &fs->txn.sb;
    fs->live_txn_id = bfs_txn_id(&fs->txn);
    bfs_err_t err = bfs_freespace_init(&fs->freespace, fs->bio,
        bfs_be32(sb->free_tree_root), fs->live_txn_id);
    if (err != BFS_OK) return err;
    fs->freespace.tree.txn_id_ptr = &fs->live_txn_id;
    fs->freespace.tree.free_sink = bfs_fs_free_sink(fs);
    fs->freespace.total_free = bfs_be32(sb->free_blocks);
    fs->freespace.global_reserve = bfs_be32(sb->global_reserve);
    fs->freespace.sb = &fs->txn.sb_new;
    err = fs_open_namespace_trees(fs);
    if (err != BFS_OK) return err;
    err = fs_open_refcount_tree(fs);
    if (err != BFS_OK) return err;
    fs->next_ino = bfs_be32(sb->next_ino);
    fs->options = bfs_be32(sb->options);
    fs->data_checksums = (fs->options & BFS_OPT_DATA_CHECKSUMS) != 0;
    return fs_validate_root(fs);
}

static bfs_err_t fs_mount(bfs_fs_t *fs, bfs_bio_t *bio, bool read_only)
{
    if (!fs || !fs_bio_valid(bio, read_only)) return BFS_ERR_INVAL;
    memset(fs, 0, sizeof(*fs));
    fs->pending_frees_cap = BFS_PENDING_FREES_MAX;
    bfs_lock_init(&fs->lock);
    fs->bio = bio;
    bfs_err_t err = read_only ? bfs_txn_begin_readonly(&fs->txn, bio)
                              : bfs_txn_begin(&fs->txn, bio);
    if (err != BFS_OK) goto fail;

    err = fs_load_working_state(fs);
    if (err != BFS_OK) goto fail;
    fs->mounted = true;
    fs->read_only = read_only;

    fs->scratch = malloc(bio->block_size);
    if (!fs->scratch) { err = BFS_ERR_NOMEM; goto fail; }

    if (!read_only) {
        /* Open handles cannot survive a process crash. Reclaim their
         * zero-link inodes before exposing the writable namespace. */
        err = bfs_fs_reap_unlinked_on_mount_unlocked(fs);
        if (err != BFS_OK) {
            free(fs->scratch);
            fs->scratch = NULL;
            goto fail;
        }
        /* Resume any interrupted snapshot deletions. */
        err = bfs_snapshot_resume_deletions(fs);
        if (err != BFS_OK) {
            free(fs->scratch);
            fs->scratch = NULL;
            goto fail;
        }
    }

    return BFS_OK;

fail:
    free(fs->scratch);
    fs->scratch = NULL;
    free(fs->pending_frees_dynamic);
    fs->pending_frees_dynamic = NULL;
    fs->mounted = false;
    bfs_lock_destroy(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_mount(bfs_fs_t *fs, bfs_bio_t *bio)
{
    return fs_mount(fs, bio, false);
}

bfs_err_t bfs_fs_mount_readonly(bfs_fs_t *fs, bfs_bio_t *bio)
{
    return fs_mount(fs, bio, true);
}

/* ── Inode allocation ──────────────────────────────────────── */

uint32_t bfs_fs_alloc_ino(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted || fs->recovery_error != BFS_OK ||
        fs->read_only ||
        fs->next_ino <= BFS_ROOT_INO ||
        fs->next_ino >= 0x80000000u)
        return 0;
    return fs->next_ino++;
}

/* ── Directory operations ──────────────────────────────────── */


bfs_err_t bfs_fs_queue_pending_free(bfs_fs_t *fs, bfs_blk_t blk)
{
    if (!fs || !fs->mounted || !fs->bio) return BFS_ERR_INVAL;
    if (fs->read_only) return BFS_ERR_UNSUPPORTED;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    if (blk == BFS_BLK_NULL) return BFS_OK;
    if (blk < bfs_data_start_block(fs->bio->block_size) ||
        blk >= fs->bio->block_count)
        return BFS_ERR_CORRUPT;
    if (fs->pending_count >= bfs_fs_pending_cap(fs))
        return BFS_ERR_AGAIN;
    bfs_fs_pending_items(fs)[fs->pending_count++] = blk;
    return BFS_OK;
}

/* ── Deferred-free sink (attached to B+trees for COW reclamation) ── */

static bfs_err_t fs_defer_free(void *ctx, bfs_blk_t blk)
{
    bfs_fs_t *fs = (bfs_fs_t *)ctx;
    if (!fs || !fs->bio || blk == BFS_BLK_NULL ||
        blk < bfs_data_start_block(fs->bio->block_size) ||
        blk >= fs->bio->block_count)
        return BFS_ERR_CORRUPT;
    if (fs->read_only) return BFS_ERR_UNSUPPORTED;
    if (fs->pending_count >= bfs_fs_pending_cap(fs)) return BFS_ERR_AGAIN;
    bfs_fs_pending_items(fs)[fs->pending_count++] = blk;
    return BFS_OK;
}

static uint32_t fs_free_headroom(void *ctx)
{
    bfs_fs_t *fs = (bfs_fs_t *)ctx;
    if (!fs) return 0;
    uint32_t cap = bfs_fs_pending_cap(fs);
    return fs->pending_count < cap ? cap - fs->pending_count : 0;
}

static bfs_err_t fs_reserve_pending(void *ctx, uint32_t slots)
{
    return bfs_fs_reserve_pending((bfs_fs_t *)ctx, slots);
}

bfs_free_sink_t bfs_fs_free_sink(bfs_fs_t *fs)
{
    /* capacity stays the physical array max — the truncate corruption check uses
     * it as "no real run can exceed this". The runtime fill limit is enforced
     * separately via headroom()/defer() against fs_pending_cap. */
    bfs_free_sink_t sink = {0};
    if (!fs) return sink;
    sink.ctx = fs;
    sink.defer = fs_defer_free;
    sink.headroom = fs_free_headroom;
    sink.reserve = fs_reserve_pending;
    sink.capacity = BFS_PENDING_FREES_MAX;
    return sink;
}

bfs_err_t bfs_fs_reserve_pending(bfs_fs_t *fs, uint32_t slots)
{
    if (!fs || !fs->mounted) return BFS_ERR_INVAL;
    if (fs->read_only) return BFS_ERR_UNSUPPORTED;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    uint32_t cap = bfs_fs_pending_cap(fs);
    if (fs->pending_count > cap) return BFS_ERR_CORRUPT;
    if (slots <= cap) return BFS_OK;
    /* Explicitly reduced caps are fault-injection limits, not growable buffers. */
    if (cap < BFS_PENDING_FREES_MAX) return BFS_ERR_NOSPC;
    if ((uint64_t)slots * sizeof(bfs_blk_t) > SIZE_MAX) return BFS_ERR_NOMEM;
    bfs_blk_t *items = malloc((size_t)slots * sizeof(*items));
    if (!items) return BFS_ERR_NOMEM;
    /* pending_count <= old capacity < slots; allocation arithmetic is checked. */
    memcpy(items, bfs_fs_pending_items(fs), fs->pending_count * sizeof(*items)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    free(fs->pending_frees_dynamic);
    fs->pending_frees_dynamic = items;
    fs->pending_frees_cap = slots;
    return BFS_OK;
}

bfs_err_t bfs_fs_ensure_free_headroom(bfs_fs_t *fs, uint32_t slots)
{
    if (!fs || !fs->mounted) return BFS_ERR_INVAL;
    if (fs->read_only) return BFS_ERR_UNSUPPORTED;
    bfs_err_t reserve_err = bfs_fs_reserve_pending(fs, slots);
    if (reserve_err != BFS_OK) return reserve_err;
    uint32_t cap = bfs_fs_pending_cap(fs);
    if (fs->pending_count <= cap - slots) return BFS_OK;
    bfs_err_t err = bfs_txn_commit(fs);               /* drain at this safe point */
    if (err != BFS_OK) return err;
    if (fs->pending_count > cap - slots) return BFS_ERR_NOSPC;  /* still short */
    return BFS_OK;
}

/* ── Tree compaction (build-swap-commit-free) ──────────────────── */

typedef struct {
    bfs_fs_t    *fs;
    bfs_btree_t *old_tree;   /* handle over the pre-swap root (for bio + sink) */
    bfs_err_t    rc;
} compact_free_ctx_t;

static void compact_free_node_cb(bfs_blk_t blk, void *ctx)
{
    compact_free_ctx_t *c = (compact_free_ctx_t *)ctx;
    if (c->rc != BFS_OK) return;
    /* The old tree may have more nodes than the queue holds. Drain it as we go;
     * this is safe — the root swap is already durable, these nodes are
     * unreferenced garbage, and we are not mid-tree-mutation. */
    c->rc = bfs_fs_ensure_free_headroom(c->fs, 1);
    if (c->rc != BFS_OK) return;
    c->rc = bfs_btree_free_block(c->old_tree, blk);
    if (c->rc == BFS_OK && c->old_tree->free_sink_err != BFS_OK) {
        c->rc = c->old_tree->free_sink_err;
        c->old_tree->free_sink_err = BFS_OK;
    }
}

bfs_err_t bfs_fs_compact_tree(bfs_fs_t *fs, bfs_btree_t *tree)
{
    if (!fs || !tree || !fs->mounted || tree->bio != fs->bio)
        return BFS_ERR_INVAL;
    if (fs->read_only) return BFS_ERR_UNSUPPORTED;
    bfs_lock_write(&fs->lock);
    if (fs->recovery_error != BFS_OK) {
        bfs_err_t err = fs->recovery_error;
        bfs_lock_unlock(&fs->lock);
        return err;
    }

    bfs_blk_t old_root;
    uint32_t old_height = tree->height;
    bfs_err_t err = bfs_btree_compact_build_swap(tree, &old_root);
    if (err != BFS_OK || old_root == tree->root) {
        /* Build failed (tree untouched) or nothing to compact. */
        bfs_lock_unlock(&fs->lock);
        return err;
    }

    /* Make the root swap durable before freeing any old node: afterwards the old
     * tree is unreferenced and its blocks are safe to reclaim. */
    err = bfs_txn_commit(fs);
    if (err != BFS_OK) {
        /* Publication is uncertain; recover the durable roots before more use. */
        bfs_err_t reload_err = bfs_fs_reload_committed_unlocked(fs);
        bfs_lock_unlock(&fs->lock);
        return reload_err == BFS_OK ? err : reload_err;
    }

    /* Post-commit: free the old tree's nodes, draining the queue as needed. */
    bfs_btree_t old_tree = *tree;
    old_tree.root = old_root;
    old_tree.height = old_height;
    old_tree.free_sink_err = BFS_OK;
    compact_free_ctx_t cfx = { fs, &old_tree, BFS_OK };
    bfs_err_t werr = bfs_btree_walk_nodes(&old_tree, compact_free_node_cb, &cfx);

    bfs_lock_unlock(&fs->lock);
    /* A read error while freeing old nodes is a post-commit space leak (fsck
     * reclaims it), not corruption — the new tree is already durable. */
    if (werr != BFS_OK) return werr;
    return cfx.rc;
}

/* ── Sync ──────────────────────────────────────────────────── */

bfs_err_t bfs_fs_sync(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted) return BFS_ERR_INVAL;
    if (fs->read_only) return BFS_ERR_UNSUPPORTED;
    bfs_lock_write(&fs->lock);
    bfs_err_t err = fs->recovery_error != BFS_OK
                        ? fs->recovery_error : bfs_txn_commit(fs);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_unmount(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted) return BFS_ERR_INVAL;
    bfs_lock_write(&fs->lock);
    if (fs->read_only) {
        free(fs->scratch);
        fs->scratch = NULL;
        free(fs->pending_frees_dynamic);
        fs->pending_frees_dynamic = NULL;
        fs->mounted = false;
        bfs_lock_unlock(&fs->lock);
        bfs_lock_destroy(&fs->lock);
        return BFS_OK;
    }
    if (fs->recovery_error != BFS_OK) {
        bfs_err_t recovery_error = fs->recovery_error;
        free(fs->scratch);
        fs->scratch = NULL;
        free(fs->pending_frees_dynamic);
        fs->pending_frees_dynamic = NULL;
        fs->mounted = false;
        bfs_lock_unlock(&fs->lock);
        bfs_lock_destroy(&fs->lock);
        return recovery_error;
    }
    bfs_err_t err = bfs_txn_commit(fs);
    if (err != BFS_OK) {
        bfs_lock_unlock(&fs->lock);
        return err;
    }
    free(fs->scratch);
    fs->scratch = NULL;
    free(fs->pending_frees_dynamic);
    fs->pending_frees_dynamic = NULL;
    fs->mounted = false;
    bfs_lock_unlock(&fs->lock);
    bfs_lock_destroy(&fs->lock);
    return err;
}

void bfs_fs_abandon(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted) return;
    free(fs->scratch);
    fs->scratch = NULL;
    free(fs->pending_frees_dynamic);
    fs->pending_frees_dynamic = NULL;
    fs->mounted = false;
    bfs_lock_destroy(&fs->lock);
}

bfs_err_t bfs_fs_reload_committed_unlocked(bfs_fs_t *fs)
{
    if (!fs || !fs->mounted || !fs->bio) return BFS_ERR_INVAL;

    uint32_t pending_cap = fs->pending_frees_cap;
    bfs_txn_t txn;
    bfs_err_t err = bfs_bio_sync(fs->bio);
    if (err != BFS_OK) goto fail;
    if (fs->recovery_generation == UINT64_MAX) {
        err = BFS_ERR_NOSPC;
        goto fail;
    }
    fs->recovery_generation++;
    err = bfs_txn_begin(&txn, fs->bio);
    if (err != BFS_OK) goto fail;

    fs->txn = txn;
    fs->pending_count = 0;
    fs->pending_frees_cap = pending_cap;
    err = fs_load_working_state(fs);
    if (err != BFS_OK) goto fail;

    fs->recovery_error = BFS_OK;
    return BFS_OK;

fail:
    fs->recovery_error = err;
    return err;
}

/* Advisory ENOSPC pre-check. Each item needs at most ~12 blocks for COW +
 * potential splits across the trees.
 *
 * Locking model: this reads shared free-space accounting, so it takes the read
 * lock to get a torn-free snapshot on the multi-threaded host build. Note the
 * reserve->operation sequence is NOT atomic for concurrent callers — space can
 * be consumed between this check and the (separately locked) mutation. Only the
 * AmigaOS handler, which processes packets sequentially, gets an end-to-end
 * guarantee; multi-threaded callers must treat the result as a hint and still
 * handle BFS_ERR_NOSPC returned by the operation itself. */
bfs_err_t bfs_fs_reserve(bfs_fs_t *fs, uint32_t items)
{
    if (!fs || !fs->mounted) return BFS_ERR_INVAL;
    /* Overflow guard: items * 12 must not wrap, or a huge request would appear
     * to "fit" on a full volume and defeat the check entirely. */
    if (items > UINT32_MAX / 12)
        return BFS_ERR_NOSPC;
    uint32_t needed = items * 12;

    bfs_lock_read(&fs->lock);
    if (fs->recovery_error != BFS_OK) {
        bfs_err_t err = fs->recovery_error;
        bfs_lock_unlock(&fs->lock);
        return err;
    }
    uint64_t available = (uint64_t)fs->freespace.total_free +
                         fs->pending_count + fs->freespace.reserve_count;
    if (fs->freespace.sb) available += bfs_be32(fs->freespace.sb->emergency_count);
    bfs_lock_unlock(&fs->lock);

    return (available < needed) ? BFS_ERR_NOSPC : BFS_OK;
}

void bfs_fs_unreserve(bfs_fs_t *fs, uint32_t items) { (void)fs; (void)items; }
