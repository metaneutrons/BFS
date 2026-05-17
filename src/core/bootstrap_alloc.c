/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Bootstrap bump allocator
 *
 * Simple allocator that hands out blocks sequentially starting from
 * a given block number. Used for early B+tree testing before the
 * free-space tree exists. Free is a no-op (blocks are tracked in a
 * freed list for accounting but not reused).
 */

#include "bfs_btree.h"
#include <stdlib.h>

#define BOOTSTRAP_MAX_FREED 4096

typedef struct {
    bfs_allocator_t base;
    bfs_blk_t next_block;
    bfs_blk_t max_block;
    bfs_blk_t freed[BOOTSTRAP_MAX_FREED];
    uint32_t freed_count;
} bootstrap_alloc_t;

static bfs_blk_t bootstrap_alloc_fn(bfs_allocator_t *a)
{
    bootstrap_alloc_t *ba = (bootstrap_alloc_t *)a->ctx;
    if (ba->next_block >= ba->max_block) return BFS_BLK_NULL;
    return ba->next_block++;
}

static void bootstrap_free_fn(bfs_allocator_t *a, bfs_blk_t blk)
{
    bootstrap_alloc_t *ba = (bootstrap_alloc_t *)a->ctx;
    if (blk && ba->freed_count < BOOTSTRAP_MAX_FREED)
        ba->freed[ba->freed_count++] = blk;
}

bootstrap_alloc_t *bootstrap_create(bfs_blk_t start, bfs_blk_t max)
{
    bootstrap_alloc_t *instance = malloc(sizeof(bootstrap_alloc_t));
    if (!instance) return NULL;
    instance->base.alloc = bootstrap_alloc_fn;
    instance->base.dealloc = bootstrap_free_fn;
    instance->base.ctx = instance;
    instance->next_block = start;
    instance->max_block = max;
    instance->freed_count = 0;
    return instance;
}
