/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — File I/O operations
 */

#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_crc32.h"
#include <string.h>
#include <stdlib.h>

bfs_err_t bfs_file_open(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr)
{
    memset(f, 0, sizeof(*f));
    f->fs = fs;
    f->inode_nr = inode_nr;
    f->offset = 0;

    /* Read inode to get extent_root and size */
    bfs_blk_t extent_root = BFS_BLK_NULL;
    uint64_t size = 0;
    bfs_inode_t inode;
    if (bfs_inode_read(&fs->inode_tree, inode_nr, &inode) == BFS_OK) {
        extent_root = bfs_be32(inode.extent_root);
        size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
    }
    f->size = size;

    bfs_err_t err = bfs_extent_init(&f->extents, fs->bio, &fs->freespace, extent_root,
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

int32_t bfs_file_read(bfs_file_t *f, void *buf, uint32_t len)
{
    uint32_t bs = f->fs->bio->block_size;
    uint8_t *out = (uint8_t *)buf;
    uint32_t total = 0;

    /* Clamp to file size */
    if (f->offset >= f->size) return 0;
    if (f->offset + len > f->size) len = (uint32_t)(f->size - f->offset);

    uint8_t *blk_buf = f->fs->scratch;
    if (!blk_buf) return BFS_ERR_NOMEM;

    while (len > 0) {
        uint32_t file_blk = (uint32_t)(f->offset / bs);
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

int32_t bfs_file_write(bfs_file_t *f, const void *buf, uint32_t len)
{
    const uint32_t bs = f->fs->bio->block_size;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t total = 0;
    uint8_t *blk_buf = f->fs->scratch;
    if (!blk_buf) return BFS_ERR_NOMEM;

    while (len > 0) {
        uint32_t file_blk = (uint32_t)(f->offset / bs);
        uint32_t blk_off = (uint32_t)(f->offset % bs);
        uint32_t chunk = bs - blk_off;
        if (chunk > len) chunk = len;

        bfs_blk_t disk_blk;
        bfs_err_t err = bfs_extent_lookup(&f->extents, file_blk, &disk_blk);

        if (err == BFS_ERR_NOTFOUND) {
            /* Try to reclaim pending frees if needed and worthwhile */
            if (f->fs->freespace.total_free <= f->fs->freespace.global_reserve) {
                if (f->fs->pending_count > BFS_SYNC_THRESHOLD) {
                    bfs_fs_sync(f->fs);
                }
            }
            /* Stop data writes when only global reserve remains */
            if (f->fs->freespace.total_free <= f->fs->freespace.global_reserve) {
                return (total > 0) ? (int32_t)total : (int32_t)BFS_ERR_NOSPC;
            }
            /* Allocate a new block */
            err = bfs_extent_append(&f->extents, file_blk, 1, &disk_blk);
            if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }
            memset(blk_buf, 0, bs);
        } else if (err != BFS_OK) {
            return (total > 0) ? (int32_t)total : (int32_t)err;
        } else if (blk_off != 0 || chunk < bs) {
            /* Partial block write — read existing data first */
            err = bfs_bio_read(f->fs->bio, disk_blk, blk_buf);
            if (err != BFS_OK) { return (total > 0) ? (int32_t)total : (int32_t)err; }
        }

        memcpy(blk_buf + blk_off, in, chunk);
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
    f->offset = (uint64_t)new_off;
    return (int64_t)f->offset;
}

bfs_err_t bfs_file_truncate(bfs_file_t *f, uint64_t new_size)
{
    uint32_t bs = f->fs->bio->block_size;

    if (new_size < f->size) {
        /* Free blocks beyond new_size in batches to avoid pending_frees overflow */
        uint32_t first_free_blk = (uint32_t)((new_size + bs - 1) / bs);
        bfs_err_t err;
        while ((err = bfs_extent_truncate_batch(&f->extents, first_free_blk, 128)) == BFS_ERR_AGAIN) {
            /* Sync to reclaim pending_frees before next batch */
            bfs_err_t sync_err = bfs_fs_sync(f->fs);
            if (sync_err != BFS_OK) return sync_err;
        }
        if (err != BFS_OK) return err;
    }

    f->size = new_size;
    if (f->offset > f->size) f->offset = f->size;
    return file_update_inode(f);
}
