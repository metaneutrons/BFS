/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — File I/O operations
 */

#include "bfs_file.h"
#include "bfs_internal.h"
#include "bfs_inode.h"
#include "bfs_crc32.h"
#include "bfs_refcount.h"
#include <string.h>
#include <stdlib.h>

static bfs_err_t file_handle_error(const bfs_file_t *f)
{
    if (!f || !f->fs || !f->fs->mounted) return BFS_ERR_INVAL;
    if (f->fs->recovery_error != BFS_OK) return f->fs->recovery_error;
    return f->recovery_generation == f->fs->recovery_generation
               ? BFS_OK : BFS_ERR_IO;
}

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

bfs_err_t bfs_file_open_unlocked(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr)
{
    if (!f || !fs || !fs->mounted || inode_nr == 0)
        return BFS_ERR_INVAL;
    if (fs->recovery_error != BFS_OK) return fs->recovery_error;
    memset(f, 0, sizeof(*f));
    f->fs = fs;
    f->inode_nr = inode_nr;
    f->offset = 0;
    f->recovery_generation = fs->recovery_generation;

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
    f->extents.tree.free_sink = bfs_fs_free_sink(fs);
    return BFS_OK;
}

static bfs_err_t file_update_inode(bfs_file_t *f)
{
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&f->fs->inode_tree, f->inode_nr, &inode);
    if (err != BFS_OK) return err;
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
    return bfs_txn_commit(f->fs);
}

static bfs_err_t file_publish_or_recover(bfs_file_t *f, bool update_inode)
{
    bfs_fs_t *fs = f->fs;
    /* A reclamation failure can follow a successful tree-root change. Do not
     * publish a partial result from a transaction whose ownership is uncertain. */
    bfs_err_t state_error = fs->recovery_error;
    if (state_error == BFS_OK) state_error = f->extents.tree.free_sink_err;
    if (state_error == BFS_OK) state_error = fs->inode_tree.free_sink_err;
    if (state_error == BFS_OK) state_error = fs->freespace.tree.free_sink_err;
    if (state_error == BFS_OK) state_error = fs->refcount.tree.free_sink_err;
    if (state_error == BFS_OK && update_inode)
        state_error = file_update_inode(f);
    if (state_error != BFS_OK) {
        bfs_err_t reload_error = bfs_fs_reload_committed_unlocked(fs);
        return reload_error == BFS_OK ? state_error : reload_error;
    }
    return BFS_OK;
}

static int32_t file_finish_write(bfs_file_t *f, uint32_t total, bfs_err_t error)
{
    bfs_err_t state_error = file_publish_or_recover(f, total > 0);
    if (state_error != BFS_OK) return state_error;
    return total > 0 ? (int32_t)total : (int32_t)error;
}

int32_t bfs_file_read_unlocked(bfs_file_t *f, void *buf, uint32_t len)
{
    bfs_err_t handle_err = file_handle_error(f);
    if (handle_err != BFS_OK) return handle_err;
    if ((len != 0 && !buf) || len > INT32_MAX) return BFS_ERR_INVAL;
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
        if (err == BFS_ERR_NOTFOUND) {
            /* Sparse region — return zeros */
            memset(out, 0, chunk);
        } else if (err != BFS_OK) {
            return (total > 0) ? (int32_t)total : (int32_t)err;
        } else {
            err = bfs_bio_read(f->fs->bio, disk_blk, blk_buf);
            if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }

            /* Verify data CRC32 if checksums enabled */
            if (f->extents.data_checksums) {
                bfs_extent_val_t ev;
                err = bfs_extent_lookup_val(&f->extents, file_blk, &ev);
                if (err != BFS_OK)
                    return (total > 0) ? (int32_t)total : (int32_t)err;
                uint32_t stored = bfs_be32(ev.data_crc32);
                if (stored != 0) {
                    uint32_t computed = bfs_crc32(0, blk_buf, bs);
                    if (computed != stored) {
                        return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_CORRUPT;
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

static uint64_t data_alloc_available(const bfs_fs_t *fs)
{
    return (uint64_t)fs->freespace.total_free + fs->freespace.reserve_count;
}

static bfs_err_t file_alloc_error(const bfs_freespace_t *fs)
{
    return fs->last_error == BFS_OK ? BFS_ERR_NOSPC : fs->last_error;
}

int32_t bfs_file_write_unlocked(bfs_file_t *f, const void *buf, uint32_t len)
{
    bfs_err_t handle_err = file_handle_error(f);
    if (handle_err != BFS_OK) return handle_err;
    if ((len != 0 && !buf) || len > INT32_MAX) return BFS_ERR_INVAL;
    const uint32_t bs = f->fs->bio->block_size;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t total = 0;
    uint8_t *blk_buf = f->fs->scratch;
    if (!blk_buf) return BFS_ERR_NOMEM;
    if (f->offset >= max_file_size_for_block_size(bs))
        return BFS_ERR_INVAL;

    while (len > 0) {
        /* Reserve deferred-free headroom for this block's worst-case COW node
         * frees (extent tree + freespace tree + refcount) before mutating. Drain
         * via the inode-flushing sync if short — a raw commit here would persist a
         * stale inode (see file_flush_and_sync / C1). */
        if (f->fs->pending_count + 3u * BFS_BTREE_MAX_OP_FREES > bfs_fs_pending_cap(f->fs)) {
            bfs_err_t ferr = file_flush_and_sync(f);
            if (ferr != BFS_OK)
                return file_finish_write(f, total, ferr);
        }
        uint32_t file_blk;
        bfs_err_t err = file_block_for_offset(f->fs, f->offset, &file_blk);
        if (err != BFS_OK) return file_finish_write(f, total, err);

        uint32_t blk_off = (uint32_t)(f->offset % bs);
        uint32_t chunk = bs - blk_off;
        if (chunk > len) chunk = len;
        if ((uint64_t)chunk > max_file_size_for_block_size(bs) - f->offset)
            chunk = (uint32_t)(max_file_size_for_block_size(bs) - f->offset);
        if (chunk == 0)
            return file_finish_write(f, total, BFS_ERR_INVAL);

        bfs_blk_t disk_blk;
        err = bfs_extent_lookup(&f->extents, file_blk, &disk_blk);
        bool shared_block = false;
        bool new_mapping = false;
        bool cow_block = false;

        if (err == BFS_ERR_NOTFOUND) {
            /* Try to reclaim pending frees if needed and worthwhile */
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                if (f->fs->pending_count > BFS_SYNC_THRESHOLD) {
                    bfs_err_t serr = file_flush_and_sync(f);
                    if (serr != BFS_OK)
                        return file_finish_write(f, total, serr);
                }
            }
            /* Stop data writes when only global reserve remains */
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                return file_finish_write(f, total, BFS_ERR_NOSPC);
            }
            /* Allocate and initialize data before publishing its extent mapping. */
            disk_blk = bfs_freespace_alloc(&f->fs->freespace, 1);
            if (disk_blk == BFS_BLK_NULL)
                return file_finish_write(f, total,
                                         file_alloc_error(&f->fs->freespace));
            new_mapping = true;
            memset(blk_buf, 0, bs);
        } else if (err != BFS_OK) {
            return file_finish_write(f, total, err);
        } else {
            if (f->fs->has_snapshots) {
                uint32_t refcount;
                err = bfs_refcount_get_checked(&f->fs->refcount, disk_blk, &refcount);
                if (err != BFS_OK)
                    return file_finish_write(f, total, err);
                shared_block = refcount > 1;
            }
            cow_block = shared_block || f->extents.data_checksums;
            if (blk_off != 0 || chunk < bs) {
                /* Partial writes need the old block contents. */
                err = bfs_bio_read(f->fs->bio, disk_blk, blk_buf);
                if (err != BFS_OK) return file_finish_write(f, total, err);
            }
        }

        memcpy(blk_buf + blk_off, in, chunk);

        uint32_t crc = f->extents.data_checksums ? bfs_crc32(0, blk_buf, bs) : 0;

        if (new_mapping) {
            err = bfs_bio_write(f->fs->bio, disk_blk, blk_buf);
            if (err == BFS_OK)
                err = bfs_extent_map_block(&f->extents, file_blk, disk_blk, crc);
            if (err != BFS_OK) {
                if (f->extents.tree.free_sink_err != BFS_OK)
                    return file_finish_write(f, total, err);
                bfs_err_t cleanup_err = bfs_freespace_free(&f->fs->freespace, disk_blk, 1);
                if (cleanup_err != BFS_OK) f->fs->recovery_error = cleanup_err;
                return file_finish_write(f, total, err);
            }
        } else if (cow_block) {
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                bfs_err_t serr = file_flush_and_sync(f);
                if (serr != BFS_OK)
                    return file_finish_write(f, total, serr);
            }
            if (data_alloc_available(f->fs) <= f->fs->freespace.global_reserve) {
                return file_finish_write(f, total, BFS_ERR_NOSPC);
            }

            bfs_blk_t new_blk = bfs_freespace_alloc(&f->fs->freespace, 1);
            if (new_blk == BFS_BLK_NULL)
                return file_finish_write(f, total,
                                         file_alloc_error(&f->fs->freespace));

            err = bfs_bio_write(f->fs->bio, new_blk, blk_buf);
            if (err != BFS_OK) {
                bfs_err_t cleanup_err = bfs_freespace_free(&f->fs->freespace, new_blk, 1);
                if (cleanup_err != BFS_OK) f->fs->recovery_error = cleanup_err;
                return file_finish_write(f, total, err);
            }

            bfs_blk_t old_blk = BFS_BLK_NULL;
            err = bfs_extent_remap_block_crc(&f->extents, file_blk, new_blk, crc,
                                             &old_blk);
            if (err != BFS_OK) {
                if (f->extents.tree.free_sink_err != BFS_OK)
                    return file_finish_write(f, total, err);
                bfs_err_t cleanup_err = bfs_freespace_free(&f->fs->freespace, new_blk, 1);
                if (cleanup_err != BFS_OK) f->fs->recovery_error = cleanup_err;
                return file_finish_write(f, total, err);
            }

            err = bfs_fs_queue_pending_free(f->fs, old_blk);
            if (err != BFS_OK) {
                f->fs->recovery_error = err;
                return file_finish_write(f, total, err);
            }
            disk_blk = new_blk;
        } else {
            err = bfs_bio_write(f->fs->bio, disk_blk, blk_buf);
            if (err != BFS_OK)
                return file_finish_write(f, total, err);
        }

        in += chunk;
        f->offset += chunk;
        total += chunk;
        len -= chunk;

        if (f->offset > f->size) f->size = f->offset;
    }

    return file_finish_write(f, total, BFS_OK);
}

static int64_t file_seek_unlocked(bfs_file_t *f, int64_t offset, int mode)
{
    bfs_err_t handle_err = file_handle_error(f);
    if (handle_err != BFS_OK) return handle_err;
    uint64_t base;
    switch (mode) {
        case BFS_SEEK_SET: base = 0; break;
        case BFS_SEEK_CUR: base = f->offset; break;
        case BFS_SEEK_END: base = f->size; break;
        default: return BFS_ERR_INVAL;
    }
    uint64_t new_off;
    if (offset >= 0) {
        uint64_t add = (uint64_t)offset;
        if (base > UINT64_MAX - add) return BFS_ERR_INVAL;
        new_off = base + add;
    } else {
        uint64_t subtract = (uint64_t)(-(offset + 1)) + 1;
        if (subtract > base) return BFS_ERR_INVAL;
        new_off = base - subtract;
    }
    if (new_off > max_file_size_for_block_size(f->fs->bio->block_size) ||
        new_off > INT64_MAX)
        return BFS_ERR_INVAL;
    f->offset = new_off;
    return (int64_t)f->offset;
}

static bfs_err_t file_zero_truncated_tail(bfs_file_t *f, uint64_t new_size)
{
    uint32_t bs = f->fs->bio->block_size;
    uint32_t offset = (uint32_t)(new_size % bs);
    if (offset == 0) return BFS_OK;
    bfs_blk_t disk;
    bfs_err_t err = bfs_extent_lookup(&f->extents, (uint32_t)(new_size / bs), &disk);
    if (err == BFS_ERR_NOTFOUND) return BFS_OK;
    if (err != BFS_OK) return err;

    uint32_t length = bs - offset;
    uint8_t *zeroes = malloc(length);
    if (!zeroes) return BFS_ERR_NOMEM;
    memset(zeroes, 0, length);
    uint64_t old_size = f->size, old_offset = f->offset;
    f->offset = new_size;
    /* Use the normal writer so snapshot sharing and checksums remain valid. */
    int32_t written = bfs_file_write_unlocked(f, zeroes, length);
    free(zeroes);
    err = file_handle_error(f);
    if (err != BFS_OK) return err;
    f->size = old_size;
    f->offset = old_offset;
    err = file_publish_or_recover(f, true);
    if (err != BFS_OK) return err;
    return written == (int32_t)length ? BFS_OK :
           written < 0 ? (bfs_err_t)written : BFS_ERR_IO;
}

static bfs_err_t file_finish_truncate(bfs_file_t *f, bfs_err_t error)
{
    bfs_err_t state_error = file_publish_or_recover(f, true);
    return state_error == BFS_OK ? error : state_error;
}

bfs_err_t bfs_file_truncate_unlocked(bfs_file_t *f, uint64_t new_size)
{
    bfs_err_t handle_err = file_handle_error(f);
    if (handle_err != BFS_OK) return handle_err;
    uint32_t bs = f->fs->bio->block_size;
    if (new_size > max_file_size_for_block_size(bs))
        return BFS_ERR_INVAL;

    if (new_size < f->size) {
        bfs_err_t tail_error = file_zero_truncated_tail(f, new_size);
        if (tail_error != BFS_OK) return tail_error;
        /* Free blocks beyond new_size in batches to avoid pending_frees overflow */
        uint64_t first_free = (new_size + bs - 1) / bs;
        if (first_free > UINT32_MAX)
            first_free = UINT32_MAX;
        uint32_t first_free_blk = (uint32_t)first_free;
        bfs_err_t err;
        bfs_blk_t prev_root = BFS_BLK_NULL;
        bool have_prev = false;
        while ((err = bfs_extent_truncate_batch(&f->extents, first_free_blk, 128)) == BFS_ERR_AGAIN) {
            /* AGAIN means the next extent's frees won't fit the deferred-free queue
             * right now; the flush+sync below drains it and the retry proceeds. But
             * if the batch removed NO extent since the last drain (the extent-tree
             * root is unchanged), a single extent is larger than the whole queue and
             * draining can never make it fit — fail loudly instead of spinning. */
            if (have_prev && f->extents.tree.root == prev_root)
                return file_finish_truncate(f, BFS_ERR_NOSPC);
            prev_root = f->extents.tree.root;
            have_prev = true;
            /* Flush the inode so it reflects the partially-truncated extent tree
             * before the commit, then sync to reclaim pending_frees — a crash
             * mid-truncate must not leave the inode pointing at freed blocks. */
            bfs_err_t sync_err = file_flush_and_sync(f);
            if (sync_err != BFS_OK) return file_finish_truncate(f, sync_err);
        }
        if (err != BFS_OK) return file_finish_truncate(f, err);
    }

    f->size = new_size;
    if (f->offset > f->size) f->offset = f->size;
    return file_finish_truncate(f, BFS_OK);
}

bfs_err_t bfs_file_open(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr)
{
    if (!f || !fs || !fs->mounted || inode_nr == 0) return BFS_ERR_INVAL;
    bfs_lock_read(&fs->lock);
    bfs_err_t err = bfs_file_open_unlocked(f, fs, inode_nr);
    bfs_lock_unlock(&fs->lock);
    return err;
}

int64_t bfs_file_seek(bfs_file_t *f, int64_t offset, int mode)
{
    if (!f || !f->fs || !f->fs->mounted) return BFS_ERR_INVAL;
    bfs_lock_write(&f->fs->lock);
    int64_t result = file_seek_unlocked(f, offset, mode);
    bfs_lock_unlock(&f->fs->lock);
    return result;
}

int32_t bfs_file_read(bfs_file_t *f, void *buf, uint32_t len)
{
    if (!f || !f->fs || !f->fs->mounted || (len != 0 && !buf))
        return BFS_ERR_INVAL;
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
    if (!f || !f->fs || !f->fs->mounted || (len != 0 && !buf))
        return BFS_ERR_INVAL;
    bfs_lock_write(&f->fs->lock);
    int32_t err = bfs_file_write_unlocked(f, buf, len);
    bfs_lock_unlock(&f->fs->lock);
    return err;
}

bfs_err_t bfs_file_truncate(bfs_file_t *f, uint64_t new_size)
{
    if (!f || !f->fs || !f->fs->mounted) return BFS_ERR_INVAL;
    bfs_lock_write(&f->fs->lock);
    bfs_err_t err = bfs_file_truncate_unlocked(f, new_size);
    bfs_lock_unlock(&f->fs->lock);
    return err;
}
