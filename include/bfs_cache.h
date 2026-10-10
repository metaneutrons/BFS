/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block read cache (LRU, configurable slots)
 *
 * Caches recently read blocks to avoid redundant disk I/O during
 * B+tree traversal. Internal nodes are read repeatedly during
 * search/insert/delete — caching them eliminates most disk reads.
 *
 * Write-through: writes update the cache entry if present but always
 * go to disk. The only exception are deferred B-tree nodes (opt-in, see
 * bfs_cache_set_deferred_node_limit): images of nodes that no committed
 * superblock or snapshot references stay dirty until the transaction commit
 * flushes them, so a crash can only lose state that was never published.
 *
 * Slot count follows the AmigaOS "Buffers" mount option (de_NumBuffers in
 * DosEnvec), raised to bfs_cache_mount_slots' floor so that a directory of
 * about 1,000 entries and its inodes stay resident.
 *
 * Not thread-safe: even reads mutate slots and LRU state. Serialize complete
 * B-tree operations (read, validate and mark), not just individual BIO calls.
 * Do not reenter/mutate this cache from a comparator or another BIO callback.
 * Changing device/media or block geometry requires invalidation or reinit;
 * resizing block buffers requires destroy/reinit. A mounted filesystem or
 * live tree must not survive cache destroy/reinit: finish filesystem unmount
 * or abandon, release standalone tree/cursor users, then reinitialize and
 * mount/init fresh handles. This also prevents mutation-epoch reuse (ABA).
 * The Amiga handler processes
 * packets in one task; host users must provide exclusive external locking.
 */

#ifndef BFS_CACHE_H
#define BFS_CACHE_H

#include "bfs_bio.h"

#define BFS_CACHE_SLOTS_DEFAULT 8
#define BFS_CACHE_SLOTS_MAX     128
/* Floor for a mount's slots: 64 nodes, but no more than 256 KiB of them. */
#define BFS_CACHE_MOUNT_SLOTS   64
#define BFS_CACHE_MOUNT_BYTES   (256u * 1024u)
#define BFS_CACHE_SCRATCH_SLOTS 4

typedef struct bfs_cache_slot {
    bfs_blk_t blk;         /* cached block number (UINT32_MAX = empty) */
    // cppcheck-suppress unusedStructMember
    uint16_t  next;        /* next slot in the same hash chain */
    uint32_t  age;          /* LRU counter (higher = more recent) */
    uint8_t  *data;         /* block data */
    bool      node_crc_valid; /* cached node bytes have a valid CRC */
    bool      node_structure_valid;
    // cppcheck-suppress unusedStructMember
    bool      dirty;          /* deferred node image not yet written */
    bfs_node_validation_t node_validation;
    // cppcheck-suppress unusedStructMember
    bfs_node_finalize_fn finalize; /* completes a dirty image before writing */
    // cppcheck-suppress unusedStructMember
    const void *layout;
} bfs_cache_slot_t;

typedef struct bfs_cache_scratch_slot {
    uint8_t *data;
    bool busy;
} bfs_cache_scratch_slot_t;

typedef struct bfs_cache {
    bfs_bio_t          bio;     /* must be first — inherits bfs_bio_t interface */
    bfs_bio_t         *dev;     /* underlying device */
    bfs_cache_slot_t  *slots;   /* dynamically allocated slot array */
    uint32_t           num_slots;
    // cppcheck-suppress unusedStructMember
    uint16_t          *buckets; /* hash index: first slot of each chain */
    // cppcheck-suppress unusedStructMember
    uint32_t           bucket_shift; /* 32 minus log2 of the bucket count */
    uint32_t           clock;   /* LRU clock */
    uint64_t           mutation_epoch; /* changes before potentially visible writes */
    bool               retain_written_nodes;
    // cppcheck-suppress unusedStructMember
    uint32_t           dirty_count;  /* slots holding deferred node images */
    // cppcheck-suppress unusedStructMember
    uint32_t           dirty_limit;  /* 0: no deferred node writes */
    bfs_cache_scratch_slot_t scratch[BFS_CACHE_SCRATCH_SLOTS];
} bfs_cache_t;

/* Initialize cache with num_slots buffers. Use 0 for default (8). */
bfs_err_t bfs_cache_init(bfs_cache_t *cache, bfs_bio_t *dev, uint32_t num_slots);

/* Slots for a mount that asks for buffers slots: at least
 * BFS_CACHE_MOUNT_SLOTS, but the floor never exceeds BFS_CACHE_MOUNT_BYTES of
 * block_size nodes; at most BFS_CACHE_SLOTS_MAX. */
uint32_t bfs_cache_mount_slots(uint32_t buffers, uint32_t block_size);

/* Optionally retain B-tree nodes after successful node writes. Disabled by
 * default so ordinary write-through cache behavior remains unchanged. */
void bfs_cache_set_node_write_retention(bfs_cache_t *cache, bool enabled);

/* Hold up to limit deferred B-tree node images (at most half of the slots);
 * 0 disables deferral, which is the default. The oldest image is written
 * early when the limit is reached. */
void bfs_cache_set_deferred_node_limit(bfs_cache_t *cache, uint32_t limit);

/* Destroy cache (free resident and retained scratch buffers). All filesystem,
 * tree/cursor users and temporary buffer leases must already be released;
 * a live cache must not be copied. */
void bfs_cache_destroy(bfs_cache_t *cache);

/* Invalidate resident entries (call after format, fsck, or any out-of-band
 * device/media mutation). Deferred node images are discarded, not written.
 * Temporary leases and their bytes remain independent of resident cache
 * invalidation. */
void bfs_cache_invalidate(bfs_cache_t *cache);

#endif /* BFS_CACHE_H */
