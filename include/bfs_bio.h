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

/* Context for a fully validated resident B-tree node. Comparator semantics
 * must be stable for the lifetime of this cached result; callers opt in. */
typedef struct bfs_node_validation {
    int (*key_compare)(const void *a, const void *b);
    uint32_t key_size;
    uint32_t val_size;
    uint32_t block_size;
    bfs_blk_t block_count;
} bfs_node_validation_t;

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

    /* Optional: write a B-tree node block whose full-block CRC was just
     * computed and installed by the B-tree engine. A caching BIO may retain
     * the successfully written bytes with their CRC known valid. */
    bfs_err_t (*write_node_block)(bfs_bio_t *bio, bfs_blk_t blk,
                                  const void *buf);

    /* Optional: report whether the resident block's B-tree node CRC is
     * already known valid. This does not imply structural validation.
     * Ordinary or failed writes, eviction and invalidation clear the state;
     * a successful trusted node write may set it. */
    bool (*node_crc_valid)(bfs_bio_t *bio, bfs_blk_t blk);
    void (*mark_node_crc_valid)(bfs_bio_t *bio, bfs_blk_t blk);

    /* Optional: memoize successful CRC and node-local structural validation
     * for the exact resident bytes and context. This excludes parent bounds
     * and the traversal's expected level, which must be checked every time.
     * Every write (including trusted node writes), failed write, eviction and
     * invalidation clears this result. A node write alone cannot set it.
     * The backend must keep resident bytes unchanged throughout the caller's
     * read/validation/mark sequence (e.g. whole-operation serialization). */
    bool (*node_structure_valid)(bfs_bio_t *bio, bfs_blk_t blk,
                                  const bfs_node_validation_t *context);
    void (*mark_node_structure_valid)(bfs_bio_t *bio, bfs_blk_t blk,
                                       const bfs_node_validation_t *context);
} bfs_bio_ops_t;

/* Base block device — all implementations embed this as first member */
struct bfs_bio {
    const bfs_bio_ops_t *ops;
    uint32_t block_size;    /* bytes per block */
    bfs_blk_t block_count; /* total blocks on device */
};

/* Select filesystem geometry without truncating the block address range.
 * A trailing partial block is unused, as in existing v2 volumes.
 * Failure leaves the previous geometry unchanged. */
static inline bfs_err_t bfs_bio_set_geometry(bfs_bio_t *bio, uint64_t bytes,
                                             uint32_t block_size)
{
    if (!bio || !bfs_block_size_valid(block_size) || bytes < block_size)
        return BFS_ERR_INVAL;
    uint64_t blocks = bytes / block_size;
    if (blocks > UINT32_MAX) return BFS_ERR_OVERFLOW;
    bio->block_size = block_size;
    bio->block_count = (bfs_blk_t)blocks;
    return BFS_OK;
}

/* Convenience wrappers */
static inline bfs_err_t bfs_bio_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf) {
    if (!bio || !bio->ops || !bio->ops->read_block || !buf ||
        blk >= bio->block_count)
        return BFS_ERR_INVAL;
    return bio->ops->read_block(bio, blk, buf);
}

static inline bfs_err_t bfs_bio_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf) {
    if (!bio || !bio->ops || !bio->ops->write_block || !buf ||
        blk >= bio->block_count)
        return BFS_ERR_INVAL;
    return bio->ops->write_block(bio, blk, buf);
}

/* Write a B-tree node block. Backends without this optional operation retain
 * the ordinary BIO write behavior. */
static inline bfs_err_t bfs_bio_write_node(bfs_bio_t *bio, bfs_blk_t blk,
                                           const void *buf) {
    if (!bio || !bio->ops || !buf || blk >= bio->block_count)
        return BFS_ERR_INVAL;
    if (bio->ops->write_node_block)
        return bio->ops->write_node_block(bio, blk, buf);
    return bfs_bio_write(bio, blk, buf);
}

static inline bfs_err_t bfs_bio_sync(bfs_bio_t *bio) {
    if (!bio || !bio->ops || !bio->ops->sync)
        return BFS_ERR_INVAL;
    return bio->ops->sync(bio);
}

static inline void bfs_bio_close(bfs_bio_t *bio) {
    if (bio && bio->ops && bio->ops->close) bio->ops->close(bio);
}

static inline bool bfs_bio_node_crc_valid(bfs_bio_t *bio, bfs_blk_t blk) {
    return bio && bio->ops && bio->ops->node_crc_valid &&
           bio->ops->node_crc_valid(bio, blk);
}

static inline void bfs_bio_mark_node_crc_valid(bfs_bio_t *bio, bfs_blk_t blk) {
    if (bio && bio->ops && bio->ops->mark_node_crc_valid)
        bio->ops->mark_node_crc_valid(bio, blk);
}

static inline bool bfs_bio_node_structure_valid(
    bfs_bio_t *bio, bfs_blk_t blk, const bfs_node_validation_t *context) {
    return bio && bio->ops && context && bio->ops->node_structure_valid &&
           bio->ops->node_structure_valid(bio, blk, context);
}

static inline void bfs_bio_mark_node_structure_valid(
    bfs_bio_t *bio, bfs_blk_t blk, const bfs_node_validation_t *context) {
    if (bio && bio->ops && context && bio->ops->mark_node_structure_valid)
        bio->ops->mark_node_structure_valid(bio, blk, context);
}

#endif /* BFS_BIO_H */
