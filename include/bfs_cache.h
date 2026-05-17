/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block read cache (LRU, configurable slots)
 *
 * Caches recently read blocks to avoid redundant disk I/O during
 * B+tree traversal. Internal nodes are read repeatedly during
 * search/insert/delete — caching them eliminates most disk reads.
 *
 * Write-through: writes update the cache entry if present but always
 * go to disk. This ensures crash consistency (no dirty buffers).
 *
 * Slot count is configurable via the AmigaOS "Buffers" mount option
 * (de_NumBuffers in DosEnvec). Default: 8. Recommended: 16-32.
 */

#ifndef BFS_CACHE_H
#define BFS_CACHE_H

#include "bfs_bio.h"

#define BFS_CACHE_SLOTS_DEFAULT 8
#define BFS_CACHE_SLOTS_MAX     128

typedef struct bfs_cache_slot {
    bfs_blk_t blk;         /* cached block number (UINT32_MAX = empty) */
    uint32_t  age;          /* LRU counter (higher = more recent) */
    uint8_t  *data;         /* block data */
} bfs_cache_slot_t;

typedef struct bfs_cache {
    bfs_bio_t          bio;     /* must be first — inherits bfs_bio_t interface */
    bfs_bio_t         *dev;     /* underlying device */
    bfs_cache_slot_t  *slots;   /* dynamically allocated slot array */
    uint32_t           num_slots;
    uint32_t           clock;   /* LRU clock */
} bfs_cache_t;

/* Initialize cache with num_slots buffers. Use 0 for default (8). */
bfs_err_t bfs_cache_init(bfs_cache_t *cache, bfs_bio_t *dev, uint32_t num_slots);

/* Destroy cache (free buffers). */
void bfs_cache_destroy(bfs_cache_t *cache);

/* Invalidate all entries (call after format or fsck). */
void bfs_cache_invalidate(bfs_cache_t *cache);

#endif /* BFS_CACHE_H */
