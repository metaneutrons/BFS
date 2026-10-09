/* SPDX-License-Identifier: MPL-2.0 */
/* Adversarial tests for allocation-goal lookups through a resident root. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"

#define TEST_BLOCK_SIZE 1024u
#define TEST_BLOCK_COUNT 4096u
#define FIRST_EXTENT_START 64u
#define FIRST_EXTENT_LENGTH 320u
#define SECOND_EXTENT_START 500u
#define SECOND_EXTENT_LENGTH 100u
#define MAX_TEST_EXTENTS 256u

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    uint32_t reads;
    uint32_t writes;
} memory_device_t;

typedef struct {
    void *(*base_alloc)(bfs_bio_t *bio, size_t size);
    void (*base_free)(bfs_bio_t *bio, void *buffer);
    const void *(*base_peek)(bfs_bio_t *bio, bfs_blk_t block,
                             const bfs_node_validation_t *context);
    uint32_t alloc_calls;
    uint32_t free_calls;
    uint32_t peek_calls;
    bool fail_alloc_before_peek;
} cache_probe_t;

typedef struct {
    memory_device_t device;
    bfs_cache_t cache;
    const bfs_bio_ops_t *cache_ops;
    bfs_bio_ops_t probed_ops;
    cache_probe_t probe;
    bfs_freespace_t space;
} fixture_t;

typedef struct {
    bfs_allocator_t iface;
    bfs_blk_t next;
    bfs_blk_t limit;
} bump_allocator_t;

typedef struct {
    bfs_blk_t start;
    uint32_t length;
} free_extent_t;

typedef struct {
    free_extent_t items[MAX_TEST_EXTENTS];
    uint32_t count;
    bool overflow;
} extent_list_t;

static cache_probe_t *active_probe;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    device->reads++;
    memcpy(buffer, device->bytes + (size_t)block * bio->block_size,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    device->writes++;
    memcpy(device->bytes + (size_t)block * bio->block_size, buffer,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_sync(bfs_bio_t *bio)
{
    (void)bio;
    return BFS_OK;
}

static void memory_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
};

static void *probed_alloc(bfs_bio_t *bio, size_t size)
{
    cache_probe_t *probe = active_probe;
    if (!probe) return NULL;
    probe->alloc_calls++;
    if (probe->fail_alloc_before_peek && probe->peek_calls == 0) return NULL;
    return probe->base_alloc(bio, size);
}

static void probed_free(bfs_bio_t *bio, void *buffer)
{
    cache_probe_t *probe = active_probe;
    if (!probe) return;
    probe->free_calls++;
    probe->base_free(bio, buffer);
}

static const void *probed_peek_valid_node(
    bfs_bio_t *bio, bfs_blk_t block,
    const bfs_node_validation_t *context)
{
    cache_probe_t *probe = active_probe;
    if (!probe) return NULL;
    probe->peek_calls++;
    return probe->base_peek(bio, block, context);
}

static bfs_blk_t bump_alloc(bfs_allocator_t *iface)
{
    bump_allocator_t *allocator = iface->ctx;
    if (allocator->next >= allocator->limit) return BFS_BLK_NULL;
    return allocator->next++;
}

static bfs_err_t bump_dealloc(bfs_allocator_t *iface, bfs_blk_t block)
{
    (void)iface;
    (void)block;
    return BFS_OK;
}

static bfs_err_t bump_error(bfs_allocator_t *iface)
{
    (void)iface;
    return BFS_ERR_NOSPC;
}

static bool fixture_open(fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->device.bytes = calloc(TEST_BLOCK_COUNT, TEST_BLOCK_SIZE);
    if (!fixture->device.bytes) return false;
    fixture->device.bio.ops = &memory_ops;
    fixture->device.bio.block_size = TEST_BLOCK_SIZE;
    fixture->device.bio.block_count = TEST_BLOCK_COUNT;
    if (bfs_cache_init(&fixture->cache, &fixture->device.bio, 8) != BFS_OK)
        goto fail;

    fixture->cache_ops = fixture->cache.bio.ops;
    fixture->probe.base_alloc = fixture->cache_ops->alloc_buffer;
    fixture->probe.base_free = fixture->cache_ops->free_buffer;
    fixture->probe.base_peek = fixture->cache_ops->peek_valid_node;
    fixture->probed_ops = *fixture->cache_ops;
    fixture->probed_ops.alloc_buffer = probed_alloc;
    fixture->probed_ops.free_buffer = probed_free;
    fixture->probed_ops.peek_valid_node = probed_peek_valid_node;
    fixture->cache.bio.ops = &fixture->probed_ops;
    active_probe = &fixture->probe;

    if (bfs_freespace_init(&fixture->space, &fixture->cache.bio,
                           BFS_BLK_NULL, 1) != BFS_OK ||
        bfs_freespace_add(&fixture->space, FIRST_EXTENT_START,
                          FIRST_EXTENT_LENGTH) != BFS_OK ||
        bfs_freespace_add(&fixture->space, SECOND_EXTENT_START,
                          SECOND_EXTENT_LENGTH) != BFS_OK)
        goto fail;
    return true;

fail:
    active_probe = NULL;
    fixture->cache.bio.ops = fixture->cache_ops;
    bfs_cache_destroy(&fixture->cache);
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
    return false;
}

static void fixture_close(fixture_t *fixture)
{
    active_probe = NULL;
    fixture->cache.bio.ops = fixture->cache_ops;
    bfs_cache_destroy(&fixture->cache);
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
}

static void reset_probe(cache_probe_t *probe)
{
    probe->alloc_calls = 0;
    probe->free_calls = 0;
    probe->peek_calls = 0;
}

static bool scratch_is_clear(const bfs_cache_t *cache)
{
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        if (cache->scratch[i].busy) return false;
    return true;
}

static bool warm_floor(fixture_t *fixture, bfs_blk_t search,
                       bfs_blk_t *found_start, uint32_t *found_length)
{
    uint32_t query = bfs_be32(search);
    uint32_t key = 0, length = 0;
    bfs_err_t err = bfs_btree_search_floor(&fixture->space.tree, &query,
                                           &key, &length);
    if (err != BFS_OK) return false;
    *found_start = bfs_load_be32(&key);
    *found_length = bfs_load_be32(&length);
    return true;
}

static bool collect_extent(const void *key, const void *value, void *ctx)
{
    extent_list_t *list = ctx;
    if (list->count == MAX_TEST_EXTENTS) {
        list->overflow = true;
        return false;
    }
    list->items[list->count].start = bfs_load_be32(key);
    list->items[list->count].length = bfs_load_be32(value);
    list->count++;
    return true;
}

static bool read_extents(fixture_t *fixture, extent_list_t *list)
{
    memset(list, 0, sizeof(*list));
    return bfs_btree_scan(&fixture->space.tree, NULL, collect_extent, list) ==
               BFS_OK &&
           !list->overflow;
}

static uint32_t extent_sum(const extent_list_t *list)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < list->count; i++)
        total += list->items[i].length;
    return total;
}

static void install_root_crc(fixture_t *fixture)
{
    bfs_blk_t root = fixture->space.tree.root;
    uint8_t *node = fixture->device.bytes +
                    (size_t)root * TEST_BLOCK_SIZE;
    bfs_btnode_hdr_t *header = hdr_of(node);
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&fixture->space.tree, node));
}

static bool build_tall_tree(fixture_t *fixture, bfs_btree_t *tree,
                            bump_allocator_t *allocator)
{
    memset(allocator, 0, sizeof(*allocator));
    allocator->iface.alloc = bump_alloc;
    allocator->iface.dealloc = bump_dealloc;
    allocator->iface.error = bump_error;
    allocator->iface.ctx = allocator;
    allocator->next = 1000;
    allocator->limit = TEST_BLOCK_COUNT;
    if (bfs_btree_init(tree, &fixture->cache.bio, &allocator->iface,
                       fixture->space.tree.ops, BFS_BLK_NULL, 1) != BFS_OK)
        return false;
    uint32_t entries = bfs_btree_leaf_capacity(tree) + 5u;
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t key = bfs_be32(100u + i * 2u);
        uint32_t value = bfs_be32(1000u + i);
        if (bfs_btree_insert(tree, &key, &value) != BFS_OK) return false;
    }
    return tree->height > 1;
}

static void test_empty_and_resident_hit_match_generic_without_leases(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    bump_allocator_t allocator = {0};
    allocator.iface.alloc = bump_alloc;
    allocator.iface.dealloc = bump_dealloc;
    allocator.iface.error = bump_error;
    allocator.iface.ctx = &allocator;
    allocator.next = 1000;
    allocator.limit = TEST_BLOCK_COUNT;
    bfs_btree_t empty;
    TEST_ASSERT_EQ(bfs_btree_init(&empty, &fixture.cache.bio,
                                  &allocator.iface, fixture.space.tree.ops,
                                  BFS_BLK_NULL, 1), BFS_OK);
    uint32_t query = bfs_be32(100);
    uint32_t found_key = 0xdeadbeefu, found_value = 0x10203040u;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &empty, &query, &found_key, &found_value),
                   BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &empty, NULL, &found_key, &found_value), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &empty, &query, NULL, &found_value), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &empty, &query, &found_key, NULL), BFS_ERR_INVAL);

    bfs_blk_t generic_key = 0;
    uint32_t generic_value = 0;
    TEST_ASSERT(warm_floor(&fixture, 100, &generic_key, &generic_value));
    TEST_ASSERT_EQ(generic_key, FIRST_EXTENT_START + 16u);
    TEST_ASSERT_EQ(generic_value, FIRST_EXTENT_LENGTH - 16u);

    reset_probe(&fixture.probe);
    fixture.probe.fail_alloc_before_peek = true;
    uint32_t reads = fixture.device.reads;
    uint32_t writes = fixture.device.writes;
    found_key = 0xdeadbeefu;
    found_value = 0x10203040u;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_OK);
    TEST_ASSERT_EQ(bfs_load_be32(&found_key), generic_key);
    TEST_ASSERT_EQ(bfs_load_be32(&found_value), generic_value);
    TEST_ASSERT_EQ(fixture.probe.peek_calls, 1);
    TEST_ASSERT_EQ(fixture.probe.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.probe.free_calls, 0);
    TEST_ASSERT_EQ(fixture.device.reads, reads);
    TEST_ASSERT_EQ(fixture.device.writes, writes);
    TEST_ASSERT(scratch_is_clear(&fixture.cache));

    uint32_t below = bfs_be32(FIRST_EXTENT_START);
    found_key = 0xdeadbeefu;
    found_value = 0x10203040u;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &below, &found_key, &found_value),
                   BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);

    fixture.probe.fail_alloc_before_peek = false;
    uint32_t generic_query = bfs_be32(510);
    uint32_t generic_out_key = 0, generic_out_value = 0;
    uint32_t resident_out_key = 0, resident_out_value = 0;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.space.tree, &generic_query,
                                          &generic_out_key, &generic_out_value),
                   BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &generic_query, &resident_out_key,
                       &resident_out_value), BFS_OK);
    TEST_ASSERT_EQ(resident_out_key, generic_out_key);
    TEST_ASSERT_EQ(resident_out_value, generic_out_value);
    fixture_close(&fixture);
}

static void test_unsupported_modes_invalid_shapes_and_wrong_root_level(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    bump_allocator_t allocator;
    bfs_btree_t tall;
    TEST_ASSERT(build_tall_tree(&fixture, &tall, &allocator));
    uint32_t query = bfs_be32(200);
    uint32_t found_key = 0xdeadbeefu, found_value = 0x10203040u;

    /* A taller tree falls back without touching caller outputs. */
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &tall, &query, &found_key, &found_value),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);
    uint32_t generic_key = 0, generic_value = 0;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&tall, &query, &generic_key,
                                          &generic_value), BFS_OK);
    TEST_ASSERT_EQ(generic_key, bfs_be32(200));
    TEST_ASSERT_EQ(generic_value, bfs_be32(1050));

    bfs_btree_ops_t noncache_ops = *tall.ops;
    noncache_ops.cache_key_order = false;
    bfs_btree_t noncache = tall;
    noncache.ops = &noncache_ops;
    found_key = 0xdeadbeefu;
    found_value = 0x10203040u;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &noncache, &query, &found_key, &found_value),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);

    /* The validated root is internal, so treating this same block as a
     * height-one tree is corrupt rather than a cache miss. */
    bfs_btree_t wrong_level = tall;
    wrong_level.height = 1;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &wrong_level, &query, &found_key, &found_value),
                   BFS_ERR_CORRUPT);

    bfs_btree_t invalid_root = tall;
    invalid_root.root = TEST_BLOCK_COUNT;
    invalid_root.height = 1;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &invalid_root, &query, &found_key, &found_value),
                   BFS_ERR_CORRUPT);

    bfs_btree_t invalid_empty = tall;
    invalid_empty.root = BFS_BLK_NULL;
    invalid_empty.height = 1;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &invalid_empty, &query, &found_key, &found_value),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       NULL, &query, &found_key, &found_value), BFS_ERR_INVAL);
    fixture_close(&fixture);
}

static void test_mutation_rebinding_and_invalidation_read_current_root(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    bfs_blk_t expected_key = 0;
    uint32_t expected_value = 0;
    TEST_ASSERT(warm_floor(&fixture, 100, &expected_key, &expected_value));

    TEST_ASSERT_EQ(bfs_freespace_add(&fixture.space, 800, 30), BFS_OK);
    uint32_t query = bfs_be32(800);
    uint32_t found_key = 0xdeadbeefu, found_value = 0x10203040u;
    reset_probe(&fixture.probe);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);

    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.space.tree, &query,
                                          &found_key, &found_value), BFS_OK);
    TEST_ASSERT_EQ(found_key, bfs_be32(800));
    TEST_ASSERT_EQ(found_value, bfs_be32(30));
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_OK);
    TEST_ASSERT_EQ(found_key, bfs_be32(800));
    TEST_ASSERT_EQ(found_value, bfs_be32(30));

    /* Rebind to another independently written and validated root. */
    bump_allocator_t fixed = {0};
    fixed.iface.alloc = bump_alloc;
    fixed.iface.dealloc = bump_dealloc;
    fixed.iface.error = bump_error;
    fixed.iface.ctx = &fixed;
    fixed.next = 3000;
    fixed.limit = TEST_BLOCK_COUNT;
    bfs_btree_t other;
    TEST_ASSERT_EQ(bfs_btree_init(&other, &fixture.cache.bio, &fixed.iface,
                                  fixture.space.tree.ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    uint32_t other_keys[2] = {bfs_be32(110), bfs_be32(550)};
    uint32_t other_values[2] = {bfs_be32(77), bfs_be32(88)};
    TEST_ASSERT_EQ(bfs_btree_create_root_leaf(&other, other_keys, other_values,
                                               2), BFS_OK);
    query = bfs_be32(120);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&other, &query, &found_key,
                                          &found_value), BFS_OK);
    fixture.space.tree.root = other.root;
    fixture.space.tree.height = other.height;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_OK);
    TEST_ASSERT_EQ(found_key, bfs_be32(110));
    TEST_ASSERT_EQ(found_value, bfs_be32(77));

    bfs_cache_invalidate(&fixture.cache);
    found_key = 0xdeadbeefu;
    found_value = 0x10203040u;
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.space.tree, &query,
                                          &found_key, &found_value), BFS_OK);
    TEST_ASSERT_EQ(found_key, bfs_be32(110));
    TEST_ASSERT_EQ(found_value, bfs_be32(77));
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_OK);
    TEST_ASSERT_EQ(found_value, bfs_be32(77));
    fixture_close(&fixture);
}

static void test_cold_crc_consistent_bad_order_falls_back_as_corrupt(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    uint8_t *node = fixture.device.bytes +
                    (size_t)fixture.space.tree.root * TEST_BLOCK_SIZE;
    uint32_t first, second, saved;
    memcpy(&first, node_key(&fixture.space.tree, node, 0), sizeof(first));
    memcpy(&second, node_key(&fixture.space.tree, node, 1), sizeof(second));
    saved = first;
    memcpy(node_key(&fixture.space.tree, node, 0), &second, sizeof(second));
    memcpy(node_key(&fixture.space.tree, node, 1), &saved, sizeof(saved));
    install_root_crc(&fixture);
    bfs_cache_invalidate(&fixture.cache);

    uint32_t query = bfs_be32(100);
    uint32_t found_key = 0xdeadbeefu, found_value = 0x10203040u;
    reset_probe(&fixture.probe);
    TEST_ASSERT_EQ(bfs_btree_search_floor_resident_root(
                       &fixture.space.tree, &query, &found_key, &found_value),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(found_key, 0xdeadbeefu);
    TEST_ASSERT_EQ(found_value, 0x10203040u);
    TEST_ASSERT_EQ(fixture.probe.alloc_calls, 0);
    TEST_ASSERT(scratch_is_clear(&fixture.cache));

    fixture.space.global_reserve = UINT32_MAX;
    uint32_t total_before = fixture.space.total_free;
    uint32_t writes_before = fixture.device.writes;
    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 1, 100),
                   BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before);
    TEST_ASSERT_EQ(fixture.device.writes, writes_before);

    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.space.tree, &query,
                                          &found_key, &found_value),
                   BFS_ERR_CORRUPT);
    fixture_close(&fixture);
}

static void test_allocator_goal_success_uses_resident_lookup_and_accounts(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    bfs_blk_t first_start = 0;
    uint32_t first_length = 0;
    TEST_ASSERT(warm_floor(&fixture, 100, &first_start, &first_length));
    TEST_ASSERT_EQ(first_start, FIRST_EXTENT_START + 16u);
    TEST_ASSERT_EQ(first_length, FIRST_EXTENT_LENGTH - 16u);
    fixture.space.global_reserve = UINT32_MAX;
    bfs_blk_t goal = first_start + 10u;
    uint32_t total_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    uint32_t reads_before = fixture.device.reads;
    fixture.probe.fail_alloc_before_peek = true;
    reset_probe(&fixture.probe);

    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 3, goal), goal);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_OK);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before - 3u);
    TEST_ASSERT_EQ(fixture.space.data_roving, goal + 3u);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    TEST_ASSERT(fixture.probe.peek_calls > 0);
    TEST_ASSERT(fixture.probe.alloc_calls > 0);
    TEST_ASSERT_EQ(fixture.device.reads, reads_before);
    TEST_ASSERT(scratch_is_clear(&fixture.cache));

    extent_list_t after;
    TEST_ASSERT(read_extents(&fixture, &after));
    TEST_ASSERT_EQ(after.count, 3);
    TEST_ASSERT_EQ(after.items[0].start, first_start);
    TEST_ASSERT_EQ(after.items[0].length, 10);
    TEST_ASSERT_EQ(after.items[1].start, goal + 3u);
    TEST_ASSERT_EQ(after.items[1].length, first_length - 13u);
    TEST_ASSERT_EQ(extent_sum(&after), fixture.space.total_free);
    fixture.probe.fail_alloc_before_peek = false;
    fixture_close(&fixture);
}

static void test_allocator_goal_miss_falls_back_to_first_fit(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    bfs_blk_t first_start = 0;
    uint32_t first_length = 0;
    TEST_ASSERT(warm_floor(&fixture, 100, &first_start, &first_length));
    fixture.space.global_reserve = UINT32_MAX;
    uint32_t total_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    fixture.probe.fail_alloc_before_peek = true;
    reset_probe(&fixture.probe);

    /* The goal lies in the allocated gap between the two free extents. */
    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 4, 400),
                   first_start);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_OK);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before - 4u);
    TEST_ASSERT_EQ(fixture.space.data_roving, first_start + 4u);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    TEST_ASSERT(fixture.probe.peek_calls > 0);
    TEST_ASSERT(scratch_is_clear(&fixture.cache));

    extent_list_t after;
    TEST_ASSERT(read_extents(&fixture, &after));
    TEST_ASSERT_EQ(after.count, 2);
    TEST_ASSERT_EQ(after.items[0].start, first_start + 4u);
    TEST_ASSERT_EQ(after.items[0].length, first_length - 4u);
    TEST_ASSERT_EQ(after.items[1].start, SECOND_EXTENT_START);
    TEST_ASSERT_EQ(after.items[1].length, SECOND_EXTENT_LENGTH);
    TEST_ASSERT_EQ(extent_sum(&after), fixture.space.total_free);
    fixture.probe.fail_alloc_before_peek = false;
    fixture_close(&fixture);
}

static void test_allocator_rejects_resident_zero_length_extent(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    uint8_t *node = fixture.device.bytes +
                    (size_t)fixture.space.tree.root * TEST_BLOCK_SIZE;
    uint32_t zero = bfs_be32(0);
    memcpy(leaf_val(&fixture.space.tree, node, 0), &zero, sizeof(zero));
    install_root_crc(&fixture);
    bfs_cache_invalidate(&fixture.cache);

    uint32_t query = bfs_be32(FIRST_EXTENT_START + 16u);
    uint32_t found_key = 0, found_value = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.space.tree, &query,
                                          &found_key, &found_value), BFS_OK);
    TEST_ASSERT_EQ(found_value, 0);
    fixture.space.global_reserve = UINT32_MAX;
    uint32_t total_before = fixture.space.total_free;
    uint32_t writes_before = fixture.device.writes;
    bfs_blk_t root_before = fixture.space.tree.root;
    reset_probe(&fixture.probe);

    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 1,
                                            FIRST_EXTENT_START + 17u),
                   BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before);
    TEST_ASSERT_EQ(fixture.device.writes, writes_before);
    TEST_ASSERT(fixture.probe.peek_calls > 0);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before);
    fixture_close(&fixture);
}

static void test_allocator_owner_and_reserve_guards_precede_root_lookup(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    bfs_blk_t expected_key = 0;
    uint32_t expected_length = 0;
    TEST_ASSERT(warm_floor(&fixture, 100, &expected_key, &expected_length));
    fixture.space.global_reserve = UINT32_MAX;
    uint32_t total_before = fixture.space.total_free;
    uint32_t writes_before = fixture.device.writes;
    bfs_blk_t root_before = fixture.space.tree.root;
    bool readonly = true;
    fixture.space.readonly_state = &readonly;
    reset_probe(&fixture.probe);

    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 1, expected_key),
                   BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(fixture.probe.peek_calls, 0);
    TEST_ASSERT_EQ(fixture.device.writes, writes_before);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);

    readonly = false;
    fixture.space.readonly_state = NULL;
    fixture.space.reserve_count = BFS_ALLOC_RESERVE_SIZE + 1u;
    reset_probe(&fixture.probe);
    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 1, expected_key),
                   BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.probe.peek_calls, 0);
    TEST_ASSERT_EQ(fixture.device.writes, writes_before);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);

    fixture.space.reserve_count = 0;
    fixture_close(&fixture);
}

TEST_SUITE_BEGIN("Allocator resident-root goal lookup")
    TEST_RUN(test_empty_and_resident_hit_match_generic_without_leases);
    TEST_RUN(test_unsupported_modes_invalid_shapes_and_wrong_root_level);
    TEST_RUN(test_mutation_rebinding_and_invalidation_read_current_root);
    TEST_RUN(test_cold_crc_consistent_bad_order_falls_back_as_corrupt);
    TEST_RUN(test_allocator_goal_success_uses_resident_lookup_and_accounts);
    TEST_RUN(test_allocator_goal_miss_falls_back_to_first_fit);
    TEST_RUN(test_allocator_rejects_resident_zero_length_extent);
    TEST_RUN(test_allocator_owner_and_reserve_guards_precede_root_lookup);
TEST_SUITE_END()
