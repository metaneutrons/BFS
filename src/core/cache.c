/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Block read cache (LRU, write-through except deferred B-tree nodes)
 */

#include "bfs_cache.h"
#include <string.h>
#include <stdlib.h>

/* ── Cache bio ops ─────────────────────────────────────────── */

/* The least recently used clean slot, or UINT32_MAX if every slot is dirty.
 * Dirty slots are never reused without being written. */
static uint32_t cache_victim(const bfs_cache_t *cache)
{
    uint32_t victim = UINT32_MAX;
    uint32_t min_age = UINT32_MAX;
    for (uint32_t i = 0; i < cache->num_slots; i++) {
        if (cache->slots[i].blk == UINT32_MAX) return i;
        if (!cache->slots[i].dirty && cache->slots[i].age <= min_age) {
            min_age = cache->slots[i].age;
            victim = i;
        }
    }
    return victim;
}

static void cache_drop_slot(bfs_cache_t *c, bfs_cache_slot_t *slot)
{
    if (slot->dirty) c->dirty_count--;
    slot->blk = UINT32_MAX;
    slot->dirty = false;
    slot->node_crc_valid = false;
    slot->node_structure_valid = false;
}

/* Complete and write a dirty node image. On failure the image stays dirty:
 * it is the only copy of the transaction's node. */
static bfs_err_t cache_write_back(bfs_cache_t *c, bfs_cache_slot_t *slot)
{
    bfs_err_t err = slot->finalize(slot->layout, c->bio.block_size, slot->data);
    if (err == BFS_OK) err = bfs_bio_write(c->dev, slot->blk, slot->data);
    if (err != BFS_OK) return err;
    slot->dirty = false;
    c->dirty_count--;
    slot->node_crc_valid = true;
    slot->node_structure_valid = false;
    return BFS_OK;
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
    if (victim == UINT32_MAX) return BFS_OK;
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

    /* Write-through: always write to device. A written block supersedes any
     * deferred image of it. */
    bfs_err_t err = bfs_bio_write(c->dev, blk, buf);
    if (err != BFS_OK) {
        /* A failed write may have reached media partially. Drop any cached
         * copy rather than retaining a validated view of uncertain contents. */
        for (uint32_t i = 0; i < c->num_slots; i++)
            if (c->slots[i].blk == blk) cache_drop_slot(c, &c->slots[i]);
        return err;
    }

    /* Ordinary writes update existing slots but never populate a new one.
     * Node writes may retain the bytes and the CRC computed by the B-tree. */
    for (uint32_t i = 0; i < c->num_slots; i++) {
        if (c->slots[i].blk == blk) {
            memcpy(c->slots[i].data, buf, bio->block_size);
            c->slots[i].age = ++c->clock;
            if (c->slots[i].dirty) {
                c->slots[i].dirty = false;
                c->dirty_count--;
            }
            c->slots[i].node_crc_valid = retain_node;
            c->slots[i].node_structure_valid = false;
            return BFS_OK;
        }
    }

    if (retain_node) {
        uint32_t victim = cache_victim(c);
        if (victim == UINT32_MAX) return BFS_OK;
        /* Every slot buffer was allocated with this cache's block_size. */
        memcpy(c->slots[victim].data, buf, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
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

static bool cache_range_resident(const bfs_cache_t *c, bfs_blk_t blk, uint32_t count)
{
    for (uint32_t i = 0; i < c->num_slots; i++) {
        bfs_blk_t resident = c->slots[i].blk;
        if (resident != UINT32_MAX && resident >= blk && resident - blk < count)
            return true;
    }
    return false;
}

/* Bulk reads (file data) bypass the slots so they do not evict metadata.
 * A range that overlaps a resident block is read slot by slot instead, so a
 * deferred node is never bypassed. */
static bfs_err_t cache_read_blocks(bfs_bio_t *bio, bfs_blk_t blk, uint32_t count,
                                   void *buf)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    if (!cache_range_resident(c, blk, count))
        return bfs_bio_read_blocks(c->dev, blk, count, buf);
    for (uint32_t i = 0; i < count; i++) {
        bfs_err_t err = cache_read(bio, blk + i, (uint8_t *)buf + (size_t)i * bio->block_size);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

/* Bulk writes go to the device in one transfer; resident copies of written
 * blocks are refreshed, or dropped if the write failed. */
static bfs_err_t cache_write_blocks(bfs_bio_t *bio, bfs_blk_t blk, uint32_t count,
                                    const void *buf, uint32_t *written)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    bfs_err_t err = bfs_bio_write_blocks(c->dev, blk, count, buf, written);
    for (uint32_t i = 0; i < c->num_slots; i++) {
        bfs_cache_slot_t *slot = &c->slots[i];
        if (slot->blk == UINT32_MAX || slot->blk < blk || slot->blk - blk >= count)
            continue;
        if (err != BFS_OK && slot->blk - blk >= *written) {
            cache_drop_slot(c, slot);
            continue;
        }
        /* Every slot buffer was allocated with this cache's block_size. */
        memcpy(slot->data, (const uint8_t *)buf + (size_t)(slot->blk - blk) * bio->block_size, /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
               bio->block_size);
        slot->age = ++c->clock;
        if (slot->dirty) {
            slot->dirty = false;
            c->dirty_count--;
        }
        slot->node_crc_valid = false;
        slot->node_structure_valid = false;
    }
    return err;
}

/* Keep a node image dirty. Its CRC is not computed yet, so the slot is marked
 * as a trusted node: readers skip the CRC check but still validate the
 * structure. */
static bfs_err_t cache_defer_node(bfs_bio_t *bio, bfs_blk_t blk, const void *buf,
                                  bfs_node_finalize_fn finalize, const void *layout)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    if (c->dirty_limit == 0) return BFS_ERR_UNSUPPORTED;
    bfs_cache_slot_t *slot = NULL;
    for (uint32_t i = 0; i < c->num_slots; i++)
        if (c->slots[i].blk == blk) {
            slot = &c->slots[i];
            break;
        }
    if ((!slot || !slot->dirty) && c->dirty_count >= c->dirty_limit) {
        bfs_cache_slot_t *oldest = NULL;
        for (uint32_t i = 0; i < c->num_slots; i++)
            if (c->slots[i].dirty && (!oldest || c->slots[i].age < oldest->age))
                oldest = &c->slots[i];
        if (oldest) {
            bfs_err_t err = cache_write_back(c, oldest);
            if (err != BFS_OK) return err;
        }
    }
    if (!slot) {
        uint32_t victim = cache_victim(c);
        if (victim == UINT32_MAX) return BFS_ERR_UNSUPPORTED;
        slot = &c->slots[victim];
        slot->blk = blk;
    }
    /* Every slot buffer was allocated with this cache's block_size. */
    memcpy(slot->data, buf, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    slot->age = ++c->clock;
    if (!slot->dirty) {
        slot->dirty = true;
        c->dirty_count++;
    }
    slot->finalize = finalize;
    slot->layout = layout;
    slot->node_crc_valid = true;
    slot->node_structure_valid = false;
    return BFS_OK;
}

/* Write every dirty image in ascending block order. */
static bfs_err_t cache_flush_deferred(bfs_bio_t *bio)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    while (c->dirty_count > 0) {
        bfs_cache_slot_t *next = NULL;
        for (uint32_t i = 0; i < c->num_slots; i++)
            if (c->slots[i].dirty && (!next || c->slots[i].blk < next->blk))
                next = &c->slots[i];
        if (!next) return BFS_ERR_CORRUPT;
        bfs_err_t err = cache_write_back(c, next);
        if (err != BFS_OK) return err;
    }
    return BFS_OK;
}

static void cache_discard_deferred(bfs_bio_t *bio, bfs_blk_t blk)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots && c->dirty_count > 0; i++) {
        bfs_cache_slot_t *slot = &c->slots[i];
        if (slot->dirty && (blk == BFS_BLK_NULL || slot->blk == blk))
            cache_drop_slot(c, slot);
    }
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

static bool slot_structure_valid(const bfs_cache_slot_t *slot,
                                 const bfs_node_validation_t *context)
{
    const bfs_node_validation_t *known = &slot->node_validation;
    return slot->node_structure_valid && slot->node_crc_valid &&
           known->key_compare == context->key_compare &&
           known->key_size == context->key_size &&
           known->val_size == context->val_size &&
           known->block_size == context->block_size &&
           known->block_count == context->block_count;
}

static bool cache_node_structure_valid(bfs_bio_t *bio, bfs_blk_t blk,
                                        const bfs_node_validation_t *context)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots; i++) {
        const bfs_cache_slot_t *slot = &c->slots[i];
        if (slot->blk != blk) continue;
        return slot_structure_valid(slot, context);
    }
    return false;
}

static const void *cache_peek_valid_node(bfs_bio_t *bio, bfs_blk_t blk,
                                         const bfs_node_validation_t *context)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;
    for (uint32_t i = 0; i < c->num_slots; i++) {
        bfs_cache_slot_t *slot = &c->slots[i];
        if (slot->blk != blk) continue;
        if (!slot_structure_valid(slot, context)) return NULL;
        slot->age = ++c->clock;
        return slot->data;
    }
    return NULL;
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

static void *cache_alloc_buffer(bfs_bio_t *bio, size_t size)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;

    if (size == bio->block_size) {
        for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
            bfs_cache_scratch_slot_t *slot = &c->scratch[i];
            if (slot->busy) continue;

            if (!slot->data) {
                slot->data = malloc(size);
                if (!slot->data) return NULL;
            }
            slot->busy = true;
            return slot->data;
        }
    }

    return malloc(size);
}

static void cache_free_buffer(bfs_bio_t *bio, void *buffer)
{
    bfs_cache_t *c = (bfs_cache_t *)bio;

    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        bfs_cache_scratch_slot_t *slot = &c->scratch[i];
        if (slot->data == buffer) {
            slot->busy = false;
            return;
        }
    }

    free(buffer);
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
    .alloc_buffer = cache_alloc_buffer,
    .free_buffer = cache_free_buffer,
    .defer_node_block = cache_defer_node,
    .flush_deferred = cache_flush_deferred,
    .discard_deferred = cache_discard_deferred,
    .read_blocks = cache_read_blocks,
    .write_blocks = cache_write_blocks,
    .peek_valid_node = cache_peek_valid_node,
};

void *bfs_bio_alloc_buffer(bfs_bio_t *bio, size_t size)
{
    if (bio && bio->ops && bio->ops->alloc_buffer && bio->ops->free_buffer)
        return bio->ops->alloc_buffer(bio, size);
    return malloc(size);
}

void bfs_bio_free_buffer(bfs_bio_t *bio, void *buffer)
{
    if (!buffer) return;
    if (bio && bio->ops && bio->ops->alloc_buffer && bio->ops->free_buffer) {
        bio->ops->free_buffer(bio, buffer);
        return;
    }
    free(buffer);
}

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
        cache->slots[i].dirty = false;
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

void bfs_cache_set_deferred_node_limit(bfs_cache_t *cache, uint32_t limit)
{
    if (!cache) return;
    if (limit > cache->num_slots / 2u) limit = cache->num_slots / 2u;
    cache->dirty_limit = limit;
}

void bfs_cache_destroy(bfs_cache_t *cache)
{
    if (!cache) return;
    if (cache->slots) {
        for (uint32_t i = 0; i < cache->num_slots; i++) {
            free(cache->slots[i].data);
        }
        free(cache->slots);
    }
    cache->slots = NULL;
    cache->num_slots = 0;
    cache->clock = 0;
    cache->dirty_count = 0;

    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        free(cache->scratch[i].data);
        cache->scratch[i].data = NULL;
        cache->scratch[i].busy = false;
    }
}

void bfs_cache_invalidate(bfs_cache_t *cache)
{
    if (!cache || !cache->slots) return;
    for (uint32_t i = 0; i < cache->num_slots; i++) {
        cache->slots[i].blk = UINT32_MAX;
        cache->slots[i].node_crc_valid = false;
        cache->slots[i].node_structure_valid = false;
        cache->slots[i].dirty = false;
    }
    cache->dirty_count = 0;
    cache->clock = 0;
}
