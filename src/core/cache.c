/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block read cache (LRU, 8 slots, write-through)
 */

#include "bfs_cache.h"
#include <string.h>
#include <stdlib.h>

/* ── Cache bio ops ─────────────────────────────────────────── */

static uint32_t cache_victim(const bfs_cache_t *cache)
{
    uint32_t victim = 0;
    uint32_t min_age = UINT32_MAX;
    for (uint32_t i = 0; i < cache->num_slots; i++) {
        if (cache->slots[i].blk == UINT32_MAX) return i;
        if (cache->slots[i].age < min_age) {
            min_age = cache->slots[i].age;
            victim = i;
        }
    }
    return victim;
}

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
    uint32_t victim = cache_victim(c);
    memcpy(c->slots[victim].data, buf, bio->block_size);
    c->slots[victim].blk = blk;
    c->slots[victim].age = ++c->clock;
    c->slots[victim].node_crc_valid = false;
    c->slots[victim].node_structure_valid = false;

    return BFS_OK;
}

static bfs_err_t cache_write_common(bfs_bio_t *bio, bfs_blk_t blk,
                                     const void *buf, bool retain_node)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;

    /* Write-through: always write to device */
    bfs_err_t err = bfs_bio_write(c->dev, blk, buf);
    if (err != BFS_OK) {
        /* A failed write may have reached media partially. Drop any cached
         * copy rather than retaining a validated view of uncertain contents. */
        for (uint32_t i = 0; i < c->num_slots; i++)
            if (c->slots[i].blk == blk) {
                c->slots[i].blk = UINT32_MAX;
                c->slots[i].node_crc_valid = false;
                c->slots[i].node_structure_valid = false;
            }
        return err;
    }

    /* Ordinary writes update existing slots but never populate a new one.
     * Node writes may retain the bytes and the CRC computed by the B-tree. */
    for (uint32_t i = 0; i < c->num_slots; i++) {
        if (c->slots[i].blk == blk) {
            memcpy(c->slots[i].data, buf, bio->block_size);
            c->slots[i].age = ++c->clock;
            c->slots[i].node_crc_valid = retain_node;
            c->slots[i].node_structure_valid = false;
            return BFS_OK;
        }
    }

    if (retain_node) {
        uint32_t victim = cache_victim(c);
        memcpy(c->slots[victim].data, buf, bio->block_size);
        c->slots[victim].blk = blk;
        c->slots[victim].age = ++c->clock;
        c->slots[victim].node_crc_valid = true;
        c->slots[victim].node_structure_valid = false;
    }

    return BFS_OK;
}

static bfs_err_t cache_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    return cache_write_common(bio, blk, buf, false);
}

static bfs_err_t cache_write_node(bfs_bio_t *bio, bfs_blk_t blk,
                                  const void *buf)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    return cache_write_common(bio, blk, buf, c->retain_written_nodes);
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

static bool cache_node_crc_valid(bfs_bio_t *bio, bfs_blk_t blk)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots; i++)
        if (c->slots[i].blk == blk) return c->slots[i].node_crc_valid;
    return false;
}

static void cache_mark_node_crc_valid(bfs_bio_t *bio, bfs_blk_t blk)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots; i++)
        if (c->slots[i].blk == blk) {
            c->slots[i].node_crc_valid = true;
            return;
        }
}

static bool cache_node_structure_valid(bfs_bio_t *bio, bfs_blk_t blk,
                                        const bfs_node_validation_t *context)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots; i++) {
        const bfs_cache_slot_t *slot = &c->slots[i];
        if (slot->blk != blk) continue;
        const bfs_node_validation_t *known = &slot->node_validation;
        return slot->node_structure_valid && slot->node_crc_valid &&
               known->key_compare == context->key_compare &&
               known->key_size == context->key_size &&
               known->val_size == context->val_size &&
               known->block_size == context->block_size &&
               known->block_count == context->block_count;
    }
    return false;
}

static void cache_mark_node_structure_valid(bfs_bio_t *bio, bfs_blk_t blk,
                                             const bfs_node_validation_t *context)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots; i++) {
        bfs_cache_slot_t *slot = &c->slots[i];
        if (slot->blk == blk && slot->node_crc_valid) {
            slot->node_validation = *context;
            slot->node_structure_valid = true;
            return;
        }
    }
}

static const bfs_bio_ops_t cache_ops = {
    .read_block  = cache_read,
    .write_block = cache_write,
    .sync        = cache_sync,
    .close       = cache_close,
    .write_node_block = cache_write_node,
    .node_crc_valid = cache_node_crc_valid,
    .mark_node_crc_valid = cache_mark_node_crc_valid,
    .node_structure_valid = cache_node_structure_valid,
    .mark_node_structure_valid = cache_mark_node_structure_valid,
};

/* ── Public API ────────────────────────────────────────────── */

bfs_err_t bfs_cache_init(bfs_cache_t *cache, bfs_bio_t *dev, uint32_t num_slots)
{
    if (!cache || !dev || !dev->ops || !dev->ops->read_block ||
        !dev->ops->write_block || !dev->ops->sync ||
        !bfs_block_size_valid(dev->block_size) || dev->block_count == 0)
        return BFS_ERR_INVAL;

    memset(cache, 0, sizeof(*cache));
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
        cache->slots[i].node_crc_valid = false;
        cache->slots[i].node_structure_valid = false;
        cache->slots[i].data = malloc(dev->block_size);
        if (!cache->slots[i].data) {
            for (uint32_t j = 0; j < i; j++) free(cache->slots[j].data);
            free(cache->slots);
            cache->slots = NULL;
            cache->num_slots = 0;
            return BFS_ERR_NOMEM;
        }
    }
    return BFS_OK;
}

void bfs_cache_set_node_write_retention(bfs_cache_t *cache, bool enabled)
{
    if (cache) cache->retain_written_nodes = enabled;
}

void bfs_cache_destroy(bfs_cache_t *cache)
{
    if (!cache) return;
    if (!cache->slots) return;
    for (uint32_t i = 0; i < cache->num_slots; i++) {
        free(cache->slots[i].data);
    }
    free(cache->slots);
    cache->slots = NULL;
    cache->num_slots = 0;
    cache->clock = 0;
}

void bfs_cache_invalidate(bfs_cache_t *cache)
{
    if (!cache || !cache->slots) return;
    for (uint32_t i = 0; i < cache->num_slots; i++) {
        cache->slots[i].blk = UINT32_MAX;
        cache->slots[i].node_crc_valid = false;
        cache->slots[i].node_structure_valid = false;
    }
    cache->clock = 0;
}
