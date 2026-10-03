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

/* Completes a deferred B-tree node image in place (canonical padding and CRC)
 * right before it is written. layout is the opaque value passed with it. */
typedef bfs_err_t (*bfs_node_finalize_fn)(const void *layout, uint32_t block_size,
                                          uint8_t *buf);

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

    /* Optional paired hooks for private temporary operation buffers. They are
     * used only when both hooks are present. Each live allocation must be
     * independent of other live buffers and resident node-cache bytes. */
    void *(*alloc_buffer)(bfs_bio_t *bio, size_t size);
    void (*free_buffer)(bfs_bio_t *bio, void *buffer);

    /* Optional write-back of B-tree nodes that no committed superblock or
     * snapshot references. defer_node_block keeps the image resident and
     * dirty; reads of blk return it, and finalize runs before it is written.
     * It returns BFS_ERR_UNSUPPORTED when the image cannot be held, so the
     * caller writes it through. The backend may write a dirty image at any
     * time to make room; a failure to do so does not refuse the new image. flush_deferred writes every dirty image; a failed
     * image stays dirty. discard_deferred drops the image of blk, or of every
     * block for BFS_BLK_NULL, without writing it. All three are present or
     * absent together. */
    bfs_err_t (*defer_node_block)(bfs_bio_t *bio, bfs_blk_t blk, const void *buf,
                                  bfs_node_finalize_fn finalize, const void *layout);
    bfs_err_t (*flush_deferred)(bfs_bio_t *bio);
    void (*discard_deferred)(bfs_bio_t *bio, bfs_blk_t blk);

    /* Optional: transfer count consecutive blocks in as few device requests
     * as the device allows. buf is ordinary caller memory; a backend with
     * DMA restrictions bounces it. On a write error *written is the number of
     * leading blocks known to be written; later blocks of the range may hold
     * old, new or partial contents. Without these hooks the wrappers below
     * loop over single blocks. */
    bfs_err_t (*read_blocks)(bfs_bio_t *bio, bfs_blk_t blk, uint32_t count, void *buf);
    bfs_err_t (*write_blocks)(bfs_bio_t *bio, bfs_blk_t blk, uint32_t count,
                              const void *buf, uint32_t *written);

    /* Optional: the resident bytes of blk if they hold a node whose CRC and
     * node-local structure are already validated for context (see
     * node_structure_valid), else NULL. The pointer is read-only and valid
     * only until the next call into this BIO. */
    const void *(*peek_valid_node)(bfs_bio_t *bio, bfs_blk_t blk,
                                   const bfs_node_validation_t *context);
} bfs_bio_ops_t;

/* Base block device — all implementations embed this as first member */
struct bfs_bio {
    const bfs_bio_ops_t *ops;
    uint32_t block_size;    /* bytes per block */
    bfs_blk_t block_count; /* total blocks on device */
};

/* Allocate and release temporary buffers associated with a BIO. Backends
 * without a complete hook pair use the ordinary heap. Allocation failure is
 * returned unchanged; NULL release is a no-op. Release through the same BIO
 * and unchanged hook pair before destroying/reinitializing that backend.
 * Callers initialize bytes exactly as for malloc; no content is promised. */
void *bfs_bio_alloc_buffer(bfs_bio_t *bio, size_t size);
void bfs_bio_free_buffer(bfs_bio_t *bio, void *buffer);

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

static inline bool bfs_bio_range_valid(const bfs_bio_t *bio, bfs_blk_t blk,
                                       uint32_t count) {
    return count > 0 && blk < bio->block_count && count <= bio->block_count - blk;
}

static inline bfs_err_t bfs_bio_read_blocks(bfs_bio_t *bio, bfs_blk_t blk,
                                            uint32_t count, void *buf) {
    if (!bio || !bio->ops || !bio->ops->read_block || !buf ||
        !bfs_bio_range_valid(bio, blk, count))
        return BFS_ERR_INVAL;
    if (bio->ops->read_blocks) return bio->ops->read_blocks(bio, blk, count, buf);
    for (uint32_t i = 0; i < count; i++) {
        bfs_err_t err = bio->ops->read_block(bio, blk + i,
                                             (uint8_t *)buf + (size_t)i * bio->block_size);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

static inline bfs_err_t bfs_bio_write_blocks(bfs_bio_t *bio, bfs_blk_t blk,
                                             uint32_t count, const void *buf,
                                             uint32_t *written) {
    uint32_t done = 0;
    if (!written) written = &done;
    *written = 0;
    if (!bio || !bio->ops || !bio->ops->write_block || !buf ||
        !bfs_bio_range_valid(bio, blk, count))
        return BFS_ERR_INVAL;
    if (bio->ops->write_blocks)
        return bio->ops->write_blocks(bio, blk, count, buf, written);
    for (uint32_t i = 0; i < count; i++) {
        bfs_err_t err = bio->ops->write_block(
            bio, blk + i, (const uint8_t *)buf + (size_t)i * bio->block_size);
        if (err != BFS_OK) return err;
        *written = i + 1;
    }
    return BFS_OK;
}

static inline bool bfs_bio_can_defer_nodes(const bfs_bio_t *bio) {
    return bio && bio->ops && bio->ops->defer_node_block &&
           bio->ops->flush_deferred && bio->ops->discard_deferred;
}

static inline bfs_err_t bfs_bio_defer_node(bfs_bio_t *bio, bfs_blk_t blk,
                                           const void *buf,
                                           bfs_node_finalize_fn finalize,
                                           const void *layout) {
    if (!bfs_bio_can_defer_nodes(bio)) return BFS_ERR_UNSUPPORTED;
    if (!buf || !finalize || blk >= bio->block_count) return BFS_ERR_INVAL;
    return bio->ops->defer_node_block(bio, blk, buf, finalize, layout);
}

static inline bfs_err_t bfs_bio_flush_deferred(bfs_bio_t *bio) {
    return bfs_bio_can_defer_nodes(bio) ? bio->ops->flush_deferred(bio) : BFS_OK;
}

static inline void bfs_bio_discard_deferred(bfs_bio_t *bio, bfs_blk_t blk) {
    if (bfs_bio_can_defer_nodes(bio)) bio->ops->discard_deferred(bio, blk);
}

static inline const void *bfs_bio_peek_valid_node(
    bfs_bio_t *bio, bfs_blk_t blk, const bfs_node_validation_t *context) {
    return bio && bio->ops && context && bio->ops->peek_valid_node
               ? bio->ops->peek_valid_node(bio, blk, context) : NULL;
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
