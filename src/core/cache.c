/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block read cache (LRU, 8 slots, write-through)
 */

#include "bfs_cache.h"
#include <string.h>
#include <stdlib.h>

/* ── Cache bio ops ─────────────────────────────────────────── */

static bfs_err_t cache_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;

    /* Search cache */
    for (uint32_t i = 0; i < c->num_slots; i++) {
        if (c->slots[i].blk == blk) {
            memcpy(buf, c->slots[i].data, bio->block_size);
            c->slots[i].age = ++c->clock;
            return BFS_OK;
        }
    }

    /* Cache miss — read from device */
    bfs_err_t err = bfs_bio_read(c->dev, blk, buf);
    if (err != BFS_OK) return err;

    /* Insert into LRU slot */
    uint32_t min_age = UINT32_MAX;
    int victim = 0;
    for (uint32_t i = 0; i < c->num_slots; i++) {
        if (c->slots[i].blk == UINT32_MAX) { victim = i; break; }
        if (c->slots[i].age < min_age) { min_age = c->slots[i].age; victim = i; }
    }
    memcpy(c->slots[victim].data, buf, bio->block_size);
    c->slots[victim].blk = blk;
    c->slots[victim].age = ++c->clock;

    return BFS_OK;
}

static bfs_err_t cache_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;

    /* Write-through: always write to device */
    bfs_err_t err = bfs_bio_write(c->dev, blk, buf);
    if (err != BFS_OK) return err;

    /* Update cache if block is cached (keeps cache coherent) */
    for (uint32_t i = 0; i < c->num_slots; i++) {
        if (c->slots[i].blk == blk) {
            memcpy(c->slots[i].data, buf, bio->block_size);
            c->slots[i].age = ++c->clock;
            return BFS_OK;
        }
    }

    return BFS_OK;
}

static bfs_err_t cache_sync(bfs_bio_t *bio)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    return bfs_bio_sync(c->dev);
}

static void cache_close(bfs_bio_t *bio)
{
    (void)bio; /* cache doesn't own the device */
}

static const bfs_bio_ops_t cache_ops = {
    .read_block  = cache_read,
    .write_block = cache_write,
    .sync        = cache_sync,
    .close       = cache_close,
};

/* ── Public API ────────────────────────────────────────────── */

bfs_err_t bfs_cache_init(bfs_cache_t *cache, bfs_bio_t *dev, uint32_t num_slots)
{
    cache->bio.ops = &cache_ops;
    cache->bio.block_size = dev->block_size;
    cache->bio.block_count = dev->block_count;
    cache->dev = dev;
    cache->clock = 0;

    if (num_slots == 0) num_slots = BFS_CACHE_SLOTS_DEFAULT;
    if (num_slots > BFS_CACHE_SLOTS_MAX) num_slots = BFS_CACHE_SLOTS_MAX;
    cache->num_slots = num_slots;

    cache->slots = malloc(num_slots * sizeof(bfs_cache_slot_t));
    if (!cache->slots) return BFS_ERR_NOMEM;

    for (uint32_t i = 0; i < num_slots; i++) {
        cache->slots[i].blk = UINT32_MAX;
        cache->slots[i].age = 0;
        cache->slots[i].data = malloc(dev->block_size);
        if (!cache->slots[i].data) {
            for (uint32_t j = 0; j < i; j++) free(cache->slots[j].data);
            free(cache->slots);
            return BFS_ERR_NOMEM;
        }
    }
    return BFS_OK;
}

void bfs_cache_destroy(bfs_cache_t *cache)
{
    if (!cache->slots) return;
    for (uint32_t i = 0; i < cache->num_slots; i++) {
        free(cache->slots[i].data);
    }
    free(cache->slots);
    cache->slots = NULL;
}

void bfs_cache_invalidate(bfs_cache_t *cache)
{
    for (uint32_t i = 0; i < cache->num_slots; i++)
        cache->slots[i].blk = UINT32_MAX;
    cache->clock = 0;
}
