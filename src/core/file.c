/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — File I/O operations
 */

#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_crc32.h"
#include "bfs_refcount.h"
#include <string.h>
#include <stdlib.h>

static uint64_t max_file_size_for_block_size(uint32_t block_size)
{
    return ((uint64_t)UINT32_MAX + 1ULL) * (uint64_t)block_size;
}

static bfs_err_t file_block_for_offset(bfs_fs_t *fs, uint64_t offset, uint32_t *file_block)
{
    uint64_t blk = offset / fs->bio->block_size;
    if (blk > UINT32_MAX)
        return BFS_ERR_INVAL;
    *file_block = (uint32_t)blk;
    return BFS_OK;
}

static bfs_err_t queue_pending_free(bfs_fs_t *fs, bfs_blk_t blk)
{
    if (fs->pending_count >= BFS_PENDING_FREES_MAX) {
        bfs_err_t err = fs_sync_unlocked(fs);
        if (err != BFS_OK) return err;
    }
    if (fs->pending_count >= BFS_PENDING_FREES_MAX)
        return BFS_ERR_NOSPC;
    fs->pending_frees[fs->pending_count++] = blk;
    return BFS_OK;
}

bfs_err_t bfs_file_open_unlocked(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr)
{
    memset(f, 0, sizeof(*f));
    f->fs = fs;
    f->inode_nr = inode_nr;
    f->offset = 0;

    /* Read inode to get extent_root and size */
    bfs_blk_t extent_root = BFS_BLK_NULL;
    uint64_t size = 0;
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&fs->inode_tree, inode_nr, &inode);
    if (err != BFS_OK) return err;
    uint32_t type = bfs_be32(inode.type);
    if (type != BFS_INODE_FILE && type != BFS_INODE_SOFTLINK && type != BFS_INODE_HARDLINK)
        return BFS_ERR_INVAL;
    extent_root = bfs_be32(inode.extent_root);
    size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
    f->size = size;

    err = bfs_extent_init(&f->extents, fs->bio, &fs->freespace, extent_root,
                     fs->live_txn_id);
    if (err != BFS_OK) return err;
    f->extents.tree.txn_id_ptr = &fs->live_txn_id;
    f->extents.data_checksums = fs->data_checksums;
    f->extents.tree.fs_ctx = fs;
    return BFS_OK;
}

static bfs_err_t file_update_inode(bfs_file_t *f)
{
    bfs_inode_t inode;
    if (bfs_inode_read(&f->fs->inode_tree, f->inode_nr, &inode) != BFS_OK)
        memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(f->inode_nr);
    inode.size_hi = bfs_be32((uint32_t)(f->size >> 32));
    inode.size_lo = bfs_be32((uint32_t)(f->size & 0xFFFFFFFF));
    inode.extent_root = bfs_be32(f->extents.tree.root);
    return bfs_inode_write(&f->fs->inode_tree, f->inode_nr, &inode);
}

/* Reached a transaction commit point in the middle of a write or truncate: the
 * inode must be made current (size + extent_root) BEFORE the commit. Otherwise a
 * crash — or any later error return — leaves the just-allocated extent/data
 * blocks unreferenced by the inode (orphaned and leaked), with the on-disk size
 * inconsistent with the extent map. Always flush the inode, then sync. */
static bfs_err_t file_flush_and_sync(bfs_file_t *f)
{
    bfs_err_t err = file_update_inode(f);
    if (err != BFS_OK) return err;
    return fs_sync_unlocked(f->fs);
}

int32_t bfs_file_read_unlocked(bfs_file_t *f, void *buf, uint32_t len)
{
    uint32_t bs = f->fs->bio->block_size;
    uint8_t *out = (uint8_t *)buf;
    uint32_t total = 0;

    /* Clamp to file size */
    if (f->offset >= f->size) return 0;
    if ((uint64_t)len > f->size - f->offset) len = (uint32_t)(f->size - f->offset);

    uint8_t *blk_buf = f->fs->scratch;
    if (!blk_buf) return BFS_ERR_NOMEM;

    while (len > 0) {
        uint32_t file_blk;
        if (file_block_for_offset(f->fs, f->offset, &file_blk) != BFS_OK)
            return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_INVAL;
        uint32_t blk_off = (uint32_t)(f->offset % bs);
        uint32_t chunk = bs - blk_off;
        if (chunk > len) chunk = len;

        bfs_blk_t disk_blk;
        bfs_err_t err = bfs_extent_lookup(&f->extents, file_blk, &disk_blk);
        if (err != BFS_OK) {
            /* Sparse region — return zeros */
            memset(out, 0, chunk);
        } else {
            err = bfs_bio_read(f->fs->bio, disk_blk, blk_buf);
            if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }

            /* Verify data CRC32 if checksums enabled */
            if (f->extents.data_checksums) {
                bfs_extent_val_t ev;
                if (bfs_extent_lookup_val(&f->extents, file_blk, &ev) == BFS_OK) {
                    uint32_t stored = bfs_be32(ev.data_crc32);
                    if (stored != 0) {
                        uint32_t computed = bfs_crc32(0, blk_buf, bs);
                        if (computed != stored) {
                            return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_CORRUPT;
                        }
                    }
                }
            }

            memcpy(out, blk_buf + blk_off, chunk);
        }

        out += chunk;
        f->offset += chunk;
        total += chunk;
        len -= chunk;
    }

    return (int32_t)total;
}

/* Threshold to trigger transaction commit and block reclamation 
 * when near ENOSPC, balancing overhead vs. allocation success. */
#define BFS_SYNC_THRESHOLD 256

static uint32_t data_alloc_available(const bfs_fs_t *fs)
{
    return fs->freespace.total_free + fs->freespace.reserve_count;
}

int32_t bfs_file_write_unlocked(bfs_file_t *f, const void *buf, uint32_t len)
{
    const uint32_t bs = f->fs->bio->block_size;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t total = 0;
    uint8_t *blk_buf = f->fs->scratch;
    if (!blk_buf) return BFS_ERR_NOMEM;
    if (f->offset >= max_file_size_for_block_size(bs))
        return BFS_ERR_INVAL;

    while (len > 0) {
        uint32_t file_blk;
        bfs_err_t err = file_block_for_offset(f->fs, f->offset, &file_blk);
        if (err != BFS_OK) return (total > 0) ? (int32_t)total : (int32_t)err;

        uint32_t blk_off = (uint32_t)(f->offset % bs);
        uint32_t chunk = bs - blk_off;
        if (chunk > len) chunk = len;
        if ((uint64_t)chunk > max_file_size_for_block_size(bs) - f->offset)
            chunk = (uint32_t)(max_file_size_for_block_size(bs) - f->offset);
        if (chunk == 0)
            return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_INVAL;

        bfs_blk_t disk_blk;
        err = bfs_extent_lookup(&f->extents, file_blk, &disk_blk);
        bool shared_block = false;

        if (err == BFS_ERR_NOTFOUND) {
            /* Try to reclaim pending frees if needed and worthwhile */
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                if (f->fs->pending_count > BFS_SYNC_THRESHOLD) {
                    bfs_err_t serr = file_flush_and_sync(f);
                    if (serr != BFS_OK)
                        return (total > 0) ? (int32_t)total : (int32_t)serr;
                }
            }
            /* Stop data writes when only global reserve remains */
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_NOSPC;
            }
            /* Allocate a new block */
            err = bfs_extent_append(&f->extents, file_blk, 1, &disk_blk);
            if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }
            memset(blk_buf, 0, bs);
        } else if (err != BFS_OK) {
            return (total > 0) ? (int32_t)total : (int32_t)err;
        } else {
            shared_block = f->fs->has_snapshots &&
                           f->fs->refcount.tree.root != BFS_BLK_NULL &&
                           bfs_refcount_get(&f->fs->refcount, disk_blk) > 1;
            if (shared_block || blk_off != 0 || chunk < bs) {
                /* Partial writes and snapshot COW need the old block contents. */
                err = bfs_bio_read(f->fs->bio, disk_blk, blk_buf);
                if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }
            }
        }

        memcpy(blk_buf + blk_off, in, chunk);

        if (shared_block) {
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                bfs_err_t serr = file_flush_and_sync(f);
                if (serr != BFS_OK)
                    return (total > 0) ? (int32_t)total : (int32_t)serr;
            }
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_NOSPC;
            }

            bfs_blk_t new_blk = bfs_freespace_alloc(&f->fs->freespace, 1);
            if (new_blk == BFS_BLK_NULL)
                return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_NOSPC;

            err = bfs_bio_write(f->fs->bio, new_blk, blk_buf);
            if (err != BFS_OK) {
                bfs_freespace_free(&f->fs->freespace, new_blk, 1);
                return (total > 0) ? (int32_t)total : (int32_t)err;
            }

            bfs_blk_t old_blk = BFS_BLK_NULL;
            err = bfs_extent_remap_block(&f->extents, file_blk, new_blk, &old_blk);
            if (err != BFS_OK) {
                bfs_freespace_free(&f->fs->freespace, new_blk, 1);
                return (total > 0) ? (int32_t)total : (int32_t)err;
            }

            err = queue_pending_free(f->fs, old_blk);
            if (err != BFS_OK)
                return (total > 0) ? (int32_t)total : (int32_t)err;
            disk_blk = new_blk;
        }

        err = bfs_bio_write(f->fs->bio, disk_blk, blk_buf);
        if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }

        /* Update data CRC32 if checksums enabled */
        if (f->extents.data_checksums) {
            uint32_t crc = bfs_crc32(0, blk_buf, bs);
            bfs_extent_update_crc(&f->extents, file_blk, crc);
        }

        in += chunk;
        f->offset += chunk;
        total += chunk;
        len -= chunk;

        if (f->offset > f->size) f->size = f->offset;
    }

    if (total > 0) {
        bfs_err_t err = file_update_inode(f);
        if (err != BFS_OK) return (int32_t)err;
    }
    return (int32_t)total;
}

int64_t bfs_file_seek(bfs_file_t *f, int64_t offset, int mode)
{
    int64_t new_off;
    switch (mode) {
        case BFS_SEEK_SET: new_off = offset; break;
        case BFS_SEEK_CUR: new_off = (int64_t)f->offset + offset; break;
        case BFS_SEEK_END: new_off = (int64_t)f->size + offset; break;
        default: return BFS_ERR_INVAL;
    }
    if (new_off < 0) return BFS_ERR_INVAL;
    if ((uint64_t)new_off > max_file_size_for_block_size(f->fs->bio->block_size))
        return BFS_ERR_INVAL;
    f->offset = (uint64_t)new_off;
    return (int64_t)f->offset;
}

bfs_err_t bfs_file_truncate_unlocked(bfs_file_t *f, uint64_t new_size)
{
    uint32_t bs = f->fs->bio->block_size;
    if (new_size > max_file_size_for_block_size(bs))
        return BFS_ERR_INVAL;

    if (new_size < f->size) {
        /* Free blocks beyond new_size in batches to avoid pending_frees overflow */
        uint64_t first_free = (new_size + bs - 1) / bs;
        if (first_free > UINT32_MAX)
            first_free = UINT32_MAX;
        uint32_t first_free_blk = (uint32_t)first_free;
        bfs_err_t err;
        while ((err = bfs_extent_truncate_batch(&f->extents, first_free_blk, 128)) == BFS_ERR_AGAIN) {
            /* Flush the inode so it reflects the partially-truncated extent tree
             * before the commit, then sync to reclaim pending_frees — a crash
             * mid-truncate must not leave the inode pointing at freed blocks. */
            bfs_err_t sync_err = file_flush_and_sync(f);
            if (sync_err != BFS_OK) return sync_err;
        }
        if (err != BFS_OK) return err;
    }

    f->size = new_size;
    if (f->offset > f->size) f->offset = f->size;
    return file_update_inode(f);
}

bfs_err_t bfs_file_open(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr)
{
    bfs_lock_read(&fs->lock);
    bfs_err_t err = bfs_file_open_unlocked(f, fs, inode_nr);
    bfs_lock_unlock(&fs->lock);
    return err;
}

int32_t bfs_file_read(bfs_file_t *f, void *buf, uint32_t len)
{
    /* File data I/O reads disk blocks into the filesystem-wide fs->scratch
     * buffer, which is shared by every open file handle. Two concurrent readers
     * holding a shared read lock would clobber each other's scratch and return
     * corrupted data, so file reads must be exclusive: take the WRITE lock.
     * (Metadata-only readers — bfs_file_open, dir lookup, snapshot list/find —
     * never touch fs->scratch and keep the shared read lock, so they still run
     * concurrently.) */
    bfs_lock_write(&f->fs->lock);
    int32_t err = bfs_file_read_unlocked(f, buf, len);
    bfs_lock_unlock(&f->fs->lock);
    return err;
}

int32_t bfs_file_write(bfs_file_t *f, const void *buf, uint32_t len)
{
    bfs_lock_write(&f->fs->lock);
    int32_t err = bfs_file_write_unlocked(f, buf, len);
    bfs_lock_unlock(&f->fs->lock);
    return err;
}

bfs_err_t bfs_file_truncate(bfs_file_t *f, uint64_t new_size)
{
    bfs_lock_write(&f->fs->lock);
    bfs_err_t err = bfs_file_truncate_unlocked(f, new_size);
    bfs_lock_unlock(&f->fs->lock);
    return err;
}
