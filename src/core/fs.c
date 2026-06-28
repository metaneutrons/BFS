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

static bool valid_block_size(uint32_t bs)
{
    return bs >= BFS_MIN_BLOCK_SIZE && bs <= BFS_MAX_BLOCK_SIZE && (bs & (bs - 1)) == 0;
}

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

    if (!valid_block_size(bs) || bc < BFS_MIN_VOLUME_BLOCKS)
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
    fs.freespace.tree.fs_ctx = &fs;
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
    fs.dir_tree.tree.fs_ctx = &fs;
    err = bfs_dir_insert(&fs.dir_tree, 0, "/", 1, BFS_ROOT_INO, BFS_INODE_DIR);
    if (err != BFS_OK) return err;

    err = bfs_inode_init(&fs.inode_tree, bio, bfs_freespace_allocator(&fs.freespace),
                    BFS_BLK_NULL, fs.live_txn_id);
    if (err != BFS_OK) return err;
    fs.inode_tree.txn_id_ptr = &fs.live_txn_id;
    fs.inode_tree.fs_ctx = &fs;
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

    err = bfs_txn_commit(&fs.txn);
    bfs_lock_destroy(&fs.lock);
    if (err != BFS_OK) return err;
    return bfs_bio_sync(bio);
}

/* ── Mount ─────────────────────────────────────────────────── */

bfs_err_t bfs_fs_mount(bfs_fs_t *fs, bfs_bio_t *bio)
{
    memset(fs, 0, sizeof(*fs));
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
    fs->freespace.tree.fs_ctx = fs;
    fs->freespace.total_free = bfs_be32(sb->free_blocks);
    fs->freespace.global_reserve = bfs_be32(sb->global_reserve);
    fs->freespace.sb = &fs->txn.sb_new;

    bfs_blk_t dir_root = bfs_be32(sb->dir_tree_root);
    err = bfs_dir_init(&fs->dir_tree, bio, bfs_freespace_allocator(&fs->freespace),
                  dir_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->dir_tree.tree.txn_id_ptr = &fs->live_txn_id;
    fs->dir_tree.tree.fs_ctx = fs;

    bfs_blk_t inode_root = bfs_be32(sb->inode_tree_root);
    err = bfs_inode_init(&fs->inode_tree, bio, bfs_freespace_allocator(&fs->freespace),
                    inode_root, fs->live_txn_id);
    if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
    fs->inode_tree.txn_id_ptr = &fs->live_txn_id;
    fs->inode_tree.fs_ctx = fs;

    bfs_blk_t rc_root = bfs_be32(sb->refcount_tree_root);
    fs->has_snapshots = (rc_root != BFS_BLK_NULL && rc_root != 0);
    if (fs->has_snapshots) {
        err = bfs_refcount_init(&fs->refcount, bio, bfs_freespace_allocator(&fs->freespace),
                                 rc_root, fs->live_txn_id);
        if (err != BFS_OK) { err = BFS_ERR_CORRUPT; goto fail; }
        fs->refcount.tree.txn_id_ptr = &fs->live_txn_id;
        fs->refcount.tree.fs_ctx = fs;
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
    if (fs->pending_count >= BFS_PENDING_FREES_MAX) {
        bfs_err_t err = bfs_fs_sync_unlocked(fs);
        if (err != BFS_OK) return err;
    }
    if (fs->pending_count >= BFS_PENDING_FREES_MAX)
        return BFS_ERR_NOSPC;
    fs->pending_frees[fs->pending_count++] = blk;
    return BFS_OK;
}

/* ── Sync ──────────────────────────────────────────────────── */

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

bfs_err_t bfs_fs_sync_unlocked(bfs_fs_t *fs)
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

    err = bfs_txn_commit(&fs->txn);
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
        err = bfs_txn_commit(&fs->txn);
        if (err != BFS_OK) return err;
    }

    update_tree_txns(fs);
    return bfs_bio_sync(fs->bio);
}

bfs_err_t bfs_fs_sync(bfs_fs_t *fs)
{
    bfs_lock_write(&fs->lock);
    bfs_err_t err = bfs_fs_sync_unlocked(fs);
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
    bfs_err_t err = bfs_fs_sync_unlocked(fs);
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

