/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block I/O abstraction
 *
 * All disk access goes through this interface. The core filesystem
 * code never touches hardware directly.
 *
 * Implementations:
 *   - tests/block_device_emu.c  (file-backed, for host testing)
 *   - src/amiga/amiga_bio.c     (TD64/NSD/SCSI, for Amiga)
 */

#ifndef BFS_BIO_H
#define BFS_BIO_H

#include "bfs_types.h"

/* Opaque block device handle */
typedef struct bfs_bio bfs_bio_t;

/* Block device operations — vtable for backend implementations */
typedef struct bfs_bio_ops {
    /* Read one block. buf must be at least block_size bytes. */
    bfs_err_t (*read_block)(bfs_bio_t *bio, bfs_blk_t blk, void *buf);

    /* Write one block. buf must be at least block_size bytes. */
    bfs_err_t (*write_block)(bfs_bio_t *bio, bfs_blk_t blk, const void *buf);

    /* Flush any cached writes to stable storage. */
    bfs_err_t (*sync)(bfs_bio_t *bio);

    /* Close and free resources. */
    void (*close)(bfs_bio_t *bio);
} bfs_bio_ops_t;

/* Base block device — all implementations embed this as first member */
struct bfs_bio {
    const bfs_bio_ops_t *ops;
    uint32_t block_size;    /* bytes per block */
    bfs_blk_t block_count; /* total blocks on device */
};

/* Convenience wrappers */
static inline bfs_err_t bfs_bio_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf) {
    return bio->ops->read_block(bio, blk, buf);
}

static inline bfs_err_t bfs_bio_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf) {
    return bio->ops->write_block(bio, blk, buf);
}

static inline bfs_err_t bfs_bio_sync(bfs_bio_t *bio) {
    return bio->ops->sync(bio);
}

static inline void bfs_bio_close(bfs_bio_t *bio) {
    if (bio && bio->ops->close) bio->ops->close(bio);
}

#endif /* BFS_BIO_H */
