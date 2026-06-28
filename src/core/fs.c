/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Filesystem format, mount, sync, unmount
 */

#include "bfs_fs.h"
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

/* ── Format ────────────────────────────────────────────────── */

bfs_err_t bfs_fs_format(bfs_bio_t *bio, const char *volname, uint32_t options)
{
    if (!bio) return BFS_ERR_INVAL;
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

    size_t nlen = strlen(volname);
    if (nlen > BFS_VOLNAME_MAX - 1) nlen = BFS_VOLNAME_MAX - 1;
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

    bfs_freespace_init(&fs.freespace, bio, BFS_BLK_NULL, fs.live_txn_id);
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
        if (err != BFS_OK) return err;
        err = bfs_freespace_add(&fs.freespace, backup_blk + 1, bc - (backup_blk + 1));
        if (err != BFS_OK) return err;
        fs.freespace.total_free = data_blocks - epool_count - 1;
    } else {
        err = bfs_freespace_add(&fs.freespace, data_start + epool_count, data_blocks - epool_count);
        if (err != BFS_OK) return err;
        fs.freespace.total_free = data_blocks - epool_count;
    }
    bfs_freespace_refill_reserve(&fs.freespace);

    err = bfs_dir_init(&fs.dir_tree, bio, bfs_freespace_allocator(&fs.freespace),
                  BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) return err;
    fs.dir_tree.tree.txn_id_ptr = &fs.live_txn_id;
    fs.dir_tree.tree.free_sink = bfs_fs_free_sink(&fs);
    err = bfs_dir_insert(&fs.dir_tree, 0, "/", 1, BFS_ROOT_INO, BFS_INODE_DIR);
    if (err != BFS_OK) return err;

    err = bfs_inode_init(&fs.inode_tree, bio, bfs_freespace_allocator(&fs.freespace),
                    BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) return err;
    fs.inode_tree.txn_id_ptr = &fs.live_txn_id;
    fs.inode_tree.free_sink = bfs_fs_free_sink(&fs);
    bfs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.inode_nr = bfs_be32(BFS_ROOT_INO);
    root_inode.type = bfs_be32(BFS_INODE_DIR);
    root_inode.link_count = bfs_be32(1);
    err = bfs_inode_write(&fs.inode_tree, BFS_ROOT_INO, &root_inode);
    if (err != BFS_OK) return err;

    err = bfs_freespace_return_reserve(&fs.freespace);
    if (err != BFS_OK) return err;

    bfs_txn_set_dir_root(&fs.txn, fs.dir_tree.tree.root);
    bfs_txn_set_free_root(&fs.txn, fs.freespace.tree.root);
    bfs_txn_set_free_blocks(&fs.txn, fs.freespace.total_free);
    bfs_txn_set_inode_root(&fs.txn, fs.inode_tree.root);

    err = bfs_txn_write_sb(&fs.txn);
    bfs_lock_destroy(&fs.lock);
    if (err != BFS_OK) return err;
    return bfs_bio_sync(bio);
}

/* ── Mount ─────────────────────────────────────────────────── */

bfs_err_t bfs_fs_mount(bfs_fs_t *fs, bfs_bio_t *bio)
{
    memset(fs, 0, sizeof(*fs));
    fs->pending_frees_cap = BFS_PENDING_FREES_MAX;
    bfs_lock_init(&fs->lock);
    fs->bio = bio;
    bfs_err_t err = bfs_txn_begin(&fs->txn, bio);
    if (err != BFS_OK) goto fail;

    bfs_superblock_t *sb = &fs->txn.sb;
    fs->live_txn_id = bfs_txn_id(&fs->txn);

    bfs_blk_t free_root = bfs_be32(sb->free_tree_root);
    err = bfs_freespace_init(&fs->freespace, bio, free_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->freespace.tree.txn_id_ptr = &fs->live_txn_id;
    fs->freespace.tree.free_sink = bfs_fs_free_sink(fs);
    fs->freespace.total_free = bfs_be32(sb->free_blocks);
    fs->freespace.global_reserve = bfs_be32(sb->global_reserve);
    fs->freespace.sb = &fs->txn.sb_new;

    bfs_blk_t dir_root = bfs_be32(sb->dir_tree_root);
    err = bfs_dir_init(&fs->dir_tree, bio, bfs_freespace_allocator(&fs->freespace),
                  dir_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->dir_tree.tree.txn_id_ptr = &fs->live_txn_id;
    fs->dir_tree.tree.free_sink = bfs_fs_free_sink(fs);

    bfs_blk_t inode_root = bfs_be32(sb->inode_tree_root);
    err = bfs_inode_init(&fs->inode_tree, bio, bfs_freespace_allocator(&fs->freespace),
                    inode_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->inode_tree.txn_id_ptr = &fs->live_txn_id;
    fs->inode_tree.free_sink = bfs_fs_free_sink(fs);

    bfs_blk_t rc_root = bfs_be32(sb->refcount_tree_root);
    fs->has_snapshots = (rc_root != BFS_BLK_NULL && rc_root != 0);
    if (fs->has_snapshots) {
        err = bfs_refcount_init(&fs->refcount, bio, bfs_freespace_allocator(&fs->freespace),
                                 rc_root, fs->live_txn_id);
        if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
        fs->refcount.tree.txn_id_ptr = &fs->live_txn_id;
        fs->refcount.tree.free_sink = bfs_fs_free_sink(fs);
    }

    fs->mounted = true;
    fs->next_ino = bfs_be32(sb->next_ino);
    fs->options = bfs_be32(sb->options);
    fs->data_checksums = (fs->options & BFS_OPT_DATA_CHECKSUMS) != 0;
    fs->scratch = malloc(bio->block_size);
    if (!fs->scratch) { err = BFS_ERR_NOMEM; goto fail; }

    /* Resume any interrupted snapshot deletions */
    err = bfs_snapshot_resume_deletions(fs);
    if (err != BFS_OK) {
        free(fs->scratch);
        fs->scratch = NULL;
        goto fail;
    }

    return BFS_OK;

fail:
    bfs_lock_destroy(&fs->lock);
    return err;
}

/* ── Inode allocation ──────────────────────────────────────── */

uint32_t bfs_fs_alloc_ino(bfs_fs_t *fs)
{
    return fs->next_ino++;
}

/* ── Directory operations ──────────────────────────────────── */


bfs_err_t bfs_fs_queue_pending_free(bfs_fs_t *fs, bfs_blk_t blk)
{
    if (blk == BFS_BLK_NULL) return BFS_OK;
    if (!fs->has_snapshots)
        return bfs_freespace_free(&fs->freespace, blk, 1);
    if (fs->pending_count >= bfs_fs_pending_cap(fs)) {
        bfs_err_t err = bfs_txn_commit(fs);
        if (err != BFS_OK) return err;
    }
    if (fs->pending_count >= bfs_fs_pending_cap(fs))
        return BFS_ERR_NOSPC;
    fs->pending_frees[fs->pending_count++] = blk;
    return BFS_OK;
}

/* ── Deferred-free sink (attached to B+trees for COW reclamation) ── */

static bfs_err_t fs_defer_free(void *ctx, bfs_blk_t blk)
{
    bfs_fs_t *fs = (bfs_fs_t *)ctx;
    if (fs->pending_count >= bfs_fs_pending_cap(fs)) return BFS_ERR_AGAIN;
    fs->pending_frees[fs->pending_count++] = blk;
    return BFS_OK;
}

static uint32_t fs_free_headroom(void *ctx)
{
    bfs_fs_t *fs = (bfs_fs_t *)ctx;
    return bfs_fs_pending_cap(fs) - fs->pending_count;
}

bfs_free_sink_t bfs_fs_free_sink(bfs_fs_t *fs)
{
    /* capacity stays the physical array max — the truncate corruption check uses
     * it as "no real run can exceed this". The runtime fill limit is enforced
     * separately via headroom()/defer() against fs_pending_cap. */
    bfs_free_sink_t sink = { fs, fs_defer_free, fs_free_headroom, BFS_PENDING_FREES_MAX };
    return sink;
}

bfs_err_t bfs_fs_ensure_free_headroom(bfs_fs_t *fs, uint32_t slots)
{
    uint32_t cap = bfs_fs_pending_cap(fs);
    if (slots > cap) return BFS_ERR_NOSPC;            /* never fits, even empty */
    if (fs->pending_count + slots <= cap) return BFS_OK;
    bfs_err_t err = bfs_txn_commit(fs);               /* drain at this safe point */
    if (err != BFS_OK) return err;
    if (fs->pending_count + slots > cap) return BFS_ERR_NOSPC;  /* still short */
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
    bfs_btree_free_block(c->old_tree, blk);
    if (c->old_tree->free_sink_err != BFS_OK) {
        c->rc = BFS_ERR_NOSPC;
        c->old_tree->free_sink_err = BFS_OK;
    }
}

bfs_err_t bfs_fs_compact_tree(bfs_fs_t *fs, bfs_btree_t *tree)
{
    bfs_lock_write(&fs->lock);
    /* Enforced so the commit below has no reachable pre-SB-write failure: the only
     * one bfs_txn_commit has is !mounted (txn.c), which this rules out. Every other
     * commit failure is post-SB-write (the new root already durable), making the
     * "keep the new root on failure" handling below unconditionally correct. */
    if (!fs->mounted) { bfs_lock_unlock(&fs->lock); return BFS_ERR_INVAL; }

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
        /* bfs_txn_commit writes the NEW root into the superblock and syncs it to
         * stable storage BEFORE any reachable error return (e.g. a later in-commit
         * bio-sync failure), so on failure the new compacted root is already
         * durable and is what a crash-mount recovers. Do NOT roll tree->root back
         * to old_root — that would diverge from the committed superblock, and a
         * later sync would write the stale old root, reverting the durable
         * compaction and leaking the whole new tree. Keep the new root; the OLD
         * nodes are simply left unfreed here (a bounded leak that fsck reclaims). */
        bfs_lock_unlock(&fs->lock);
        return err;
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
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_txn_commit(fs);
    bfs_lock_unlock(&fs->lock);
    return err;
}

bfs_err_t bfs_fs_unmount(bfs_fs_t *fs)
{
    bfs_lock_write(&fs->lock);
    if (!fs->mounted) {
        bfs_lock_unlock(&fs->lock);
        return BFS_ERR_INVAL;
    }
    bfs_err_t err = bfs_txn_commit(fs);
    free(fs->scratch);
    fs->mounted = false;
    bfs_lock_unlock(&fs->lock);
    bfs_lock_destroy(&fs->lock);
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
    /* Overflow guard: items * 12 must not wrap, or a huge request would appear
     * to "fit" on a full volume and defeat the check entirely. */
    if (items > UINT32_MAX / 12)
        return BFS_ERR_NOSPC;
    uint32_t needed = items * 12;

    bfs_lock_read(&fs->lock);
    uint64_t available = (uint64_t)fs->freespace.total_free +
                         fs->pending_count + fs->freespace.reserve_count;
    if (fs->freespace.sb) available += bfs_be32(fs->freespace.sb->emergency_count);
    bfs_lock_unlock(&fs->lock);

    return (available < needed) ? BFS_ERR_NOSPC : BFS_OK;
}

void bfs_fs_unreserve(bfs_fs_t *fs, uint32_t items) { (void)fs; (void)items; }

