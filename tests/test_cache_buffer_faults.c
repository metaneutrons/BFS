/* SPDX-License-Identifier: MPL-2.0 */
/* Compile the actual cache implementation with isolated allocation faults.
 * Public names are local to this test; the normal core remains unmodified. */
#include "test_harness.h"
#include "bfs_cache.h"
#include "bfs_btree.h"

static unsigned allocation_calls, fail_call, live_allocations;
static void *tracked[32];
static bool tracking_error;

static void *fault_malloc(size_t size)
{
    if (++allocation_calls == fail_call) return NULL;
    void *pointer = malloc(size);
    if (!pointer) return NULL;
    for (unsigned i = 0; i < 32; i++) {
        if (!tracked[i]) {
            tracked[i] = pointer;
            live_allocations++;
            return pointer;
        }
    }
    tracking_error = true;
    free(pointer);
    return NULL;
}

static void fault_free(void *pointer)
{
    if (!pointer) return;
    for (unsigned i = 0; i < 32; i++) {
        if (tracked[i] == pointer) {
            tracked[i] = NULL;
            live_allocations--;
            free(pointer);
            return;
        }
    }
    tracking_error = true;
}

#define malloc fault_malloc
#define free fault_free
#define bfs_bio_alloc_buffer isolated_bio_alloc_buffer
#define bfs_bio_free_buffer isolated_bio_free_buffer
#define bfs_cache_init isolated_cache_init
#define bfs_cache_destroy isolated_cache_destroy
#define bfs_cache_invalidate isolated_cache_invalidate
#define bfs_cache_set_node_write_retention isolated_cache_set_node_write_retention
#include "../src/core/cache.c"
#undef bfs_cache_set_node_write_retention
#undef bfs_cache_invalidate
#undef bfs_cache_destroy
#undef bfs_cache_init
#undef bfs_bio_free_buffer
#undef bfs_bio_alloc_buffer
#undef free
#undef malloc

#define BS BFS_MIN_BLOCK_SIZE
#define BLOCKS 16
typedef struct {
    bfs_bio_t bio;
    uint8_t bytes[BLOCKS][BS];
    bfs_blk_t next;
} memory_device_t;

static bfs_err_t read_block(bfs_bio_t *bio, bfs_blk_t blk, void *buffer)
{
    memory_device_t *memory = (memory_device_t *)bio;
    if (blk >= BLOCKS) return BFS_ERR_INVAL;
    memcpy(buffer, memory->bytes[blk], BS);
    return BFS_OK;
}

static bfs_err_t write_block(bfs_bio_t *bio, bfs_blk_t blk, const void *buffer)
{
    memory_device_t *memory = (memory_device_t *)bio;
    if (blk >= BLOCKS) return BFS_ERR_INVAL;
    memcpy(memory->bytes[blk], buffer, BS);
    return BFS_OK;
}

static bfs_err_t sync_device(bfs_bio_t *bio) { (void)bio; return BFS_OK; }
static const bfs_bio_ops_t device_ops = {
    .read_block = read_block, .write_block = write_block, .sync = sync_device,
};

static void reset(memory_device_t *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->bio = (bfs_bio_t){.ops = &device_ops, .block_size = BS,
                              .block_count = BLOCKS};
    memory->next = 4;
    allocation_calls = fail_call = 0;
    tracking_error = false;
}

static void test_each_lazy_slot_failure(void)
{
    for (unsigned position = 0; position < BFS_CACHE_SCRATCH_SLOTS; position++) {
        memory_device_t memory;
        bfs_cache_t cache;
        void *held[BFS_CACHE_SCRATCH_SLOTS] = {0};
        TEST_ASSERT_EQ(live_allocations, 0);
        reset(&memory);
        TEST_ASSERT_EQ(isolated_cache_init(&cache, &memory.bio, 2), BFS_OK);
        for (unsigned i = 0; i < position; i++) {
            held[i] = isolated_bio_alloc_buffer(&cache.bio, BS);
            TEST_ASSERT(held[i]);
            memset(held[i], (int)(i + 1), BS);
        }
        unsigned before = live_allocations;
        allocation_calls = 0;
        fail_call = 1;
        TEST_ASSERT(isolated_bio_alloc_buffer(&cache.bio, BS) == NULL);
        TEST_ASSERT_EQ(allocation_calls, 1);
        TEST_ASSERT_EQ(live_allocations, before);
        TEST_ASSERT(cache.scratch[position].data == NULL);
        TEST_ASSERT(!cache.scratch[position].busy);
        for (unsigned i = 0; i < position; i++) {
            TEST_ASSERT(cache.scratch[i].busy);
            TEST_ASSERT_EQ(((uint8_t *)held[i])[BS - 1], i + 1);
        }
        fail_call = 0;
        held[position] = isolated_bio_alloc_buffer(&cache.bio, BS);
        TEST_ASSERT(held[position]);
        for (unsigned i = 0; i <= position; i++)
            isolated_bio_free_buffer(&cache.bio, held[i]);
        isolated_cache_destroy(&cache);
        TEST_ASSERT_EQ(live_allocations, 0);
        TEST_ASSERT(!tracking_error);
    }
}

static void test_overflow_and_multiblock_failures(void)
{
    memory_device_t memory;
    bfs_cache_t cache;
    void *held[BFS_CACHE_SCRATCH_SLOTS];
    reset(&memory);
    TEST_ASSERT_EQ(isolated_cache_init(&cache, &memory.bio, 2), BFS_OK);
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        held[i] = isolated_bio_alloc_buffer(&cache.bio, BS);
        TEST_ASSERT(held[i]);
    }
    const size_t sizes[] = {BS, 2u * BS};
    for (unsigned size = 0; size < 2; size++) {
        allocation_calls = 0;
        fail_call = 1;
        TEST_ASSERT(isolated_bio_alloc_buffer(&cache.bio, sizes[size]) == NULL);
        TEST_ASSERT_EQ(allocation_calls, 1);
        for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
            TEST_ASSERT(cache.scratch[i].busy);
    }
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        isolated_bio_free_buffer(&cache.bio, held[i]);
    isolated_cache_destroy(&cache);
    TEST_ASSERT_EQ(live_allocations, 0);
    TEST_ASSERT(!tracking_error);
}

static void test_cache_initialization_failures(void)
{
    for (unsigned failure = 1; failure <= 3; failure++) {
        memory_device_t memory;
        bfs_cache_t cache;
        reset(&memory);
        fail_call = failure;
        TEST_ASSERT_EQ(isolated_cache_init(&cache, &memory.bio, 2), BFS_ERR_NOMEM);
        isolated_cache_destroy(&cache);
        isolated_cache_destroy(&cache);
        TEST_ASSERT_EQ(live_allocations, 0);
        TEST_ASSERT(!tracking_error);
    }
}

static bfs_blk_t alloc_node(bfs_allocator_t *allocator)
{
    memory_device_t *memory = allocator->ctx;
    return memory->next < BLOCKS ? memory->next++ : BFS_BLK_NULL;
}
static bfs_err_t free_node(bfs_allocator_t *allocator, bfs_blk_t blk)
{
    (void)allocator; (void)blk;
    return BFS_OK;
}

static void test_replacement_second_buffer_failure_releases_first(void)
{
    memory_device_t memory;
    bfs_cache_t cache;
    bfs_btree_t tree;
    reset(&memory);
    TEST_ASSERT_EQ(isolated_cache_init(&cache, &memory.bio, 2), BFS_OK);
    bfs_allocator_t allocator = {.alloc = alloc_node, .dealloc = free_node,
                                  .ctx = &memory};
    const bfs_btree_ops_t ops = {.key_compare = bfs_cmp_be32,
                                 .key_size = 4, .val_size = 4};
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &cache.bio, &allocator, &ops,
                                 BFS_BLK_NULL, 1), BFS_OK);
    uint32_t key = bfs_be32(10), value = bfs_be32(20), result;
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &value), BFS_OK);
    bfs_blk_t root = tree.root;
    uint8_t before_bytes[sizeof(memory.bytes)];
    memcpy(before_bytes, memory.bytes, sizeof(before_bytes));
    TEST_ASSERT(cache.scratch[0].data && !cache.scratch[0].busy);
    TEST_ASSERT(cache.scratch[1].data == NULL);
    allocation_calls = 0;
    fail_call = 1;
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, &key, &value, 1), BFS_ERR_NOMEM);
    TEST_ASSERT_EQ(allocation_calls, 1);
    TEST_ASSERT_EQ(tree.root, root);
    TEST_ASSERT_MEM_EQ(memory.bytes, before_bytes, sizeof(before_bytes));
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        TEST_ASSERT(!cache.scratch[i].busy);
    fail_call = 0;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_OK);
    TEST_ASSERT_EQ(result, value);
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, &key, &value, 1), BFS_OK);
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        TEST_ASSERT(!cache.scratch[i].busy);
    isolated_cache_destroy(&cache);
    TEST_ASSERT_EQ(live_allocations, 0);
    TEST_ASSERT(!tracking_error);
}

static bool scratch_idle(const bfs_cache_t *cache)
{
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        if (cache->scratch[i].busy) return false;
    return true;
}

static void test_height_one_mutations_reuse_warm_scratch(void)
{
    memory_device_t memory;
    bfs_cache_t cache;
    bfs_btree_t tree;
    TEST_ASSERT_EQ(live_allocations, 0);
    reset(&memory);
    TEST_ASSERT_EQ(isolated_cache_init(&cache, &memory.bio, 2), BFS_OK);
    bfs_allocator_t allocator = {.alloc = alloc_node, .dealloc = free_node,
                                  .ctx = &memory};
    const bfs_btree_ops_t ops = {.key_compare = bfs_cmp_be32,
                                 .key_size = 4, .val_size = 4};
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &cache.bio, &allocator, &ops,
                                 BFS_BLK_NULL, 1), BFS_OK);

    uint32_t key10 = bfs_be32(10), key20 = bfs_be32(20), key30 = bfs_be32(30);
    uint32_t value10 = bfs_be32(100), value20 = bfs_be32(200);
    uint32_t value30 = bfs_be32(300), replacement20 = bfs_be32(220), result;
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key10, &value10), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key20, &value20), BFS_OK);
    TEST_ASSERT_EQ(tree.height, 1);
    TEST_ASSERT(tree.root != BFS_BLK_NULL);

    void *warm[BFS_CACHE_SCRATCH_SLOTS];
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        warm[i] = isolated_bio_alloc_buffer(&cache.bio, BS);
        TEST_ASSERT(warm[i]);
    }
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        isolated_bio_free_buffer(&cache.bio, warm[i]);
    for (unsigned i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        TEST_ASSERT(cache.scratch[i].data);
        TEST_ASSERT(!cache.scratch[i].busy);
    }

    /* The warmed slots cover block-sized requests; fail any ordinary malloc. */
    allocation_calls = 0;
    fail_call = 1;
    bfs_blk_t old_root = tree.root;
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key30, &value30), BFS_OK);
    TEST_ASSERT_EQ(allocation_calls, 0);
    TEST_ASSERT(scratch_idle(&cache));
    TEST_ASSERT_EQ(tree.height, 1);
    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key30, &result), BFS_OK);
    TEST_ASSERT_EQ(result, value30);

    allocation_calls = 0;
    old_root = tree.root;
    TEST_ASSERT_EQ(bfs_btree_update(&tree, &key20, &replacement20), BFS_OK);
    TEST_ASSERT_EQ(allocation_calls, 0);
    TEST_ASSERT(scratch_idle(&cache));
    TEST_ASSERT_EQ(tree.height, 1);
    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key20, &result), BFS_OK);
    TEST_ASSERT_EQ(result, replacement20);

    allocation_calls = 0;
    old_root = tree.root;
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key30), BFS_OK);
    TEST_ASSERT_EQ(allocation_calls, 0);
    TEST_ASSERT(scratch_idle(&cache));
    TEST_ASSERT_EQ(tree.height, 1);
    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key30, &result), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key10, &result), BFS_OK);
    TEST_ASSERT_EQ(result, value10);

    allocation_calls = 0;
    old_root = tree.root;
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key20), BFS_OK);
    TEST_ASSERT_EQ(allocation_calls, 0);
    TEST_ASSERT(scratch_idle(&cache));
    TEST_ASSERT_EQ(tree.height, 1);
    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key20, &result), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key10, &result), BFS_OK);
    TEST_ASSERT_EQ(result, value10);

    allocation_calls = 0;
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key10), BFS_OK);
    TEST_ASSERT_EQ(allocation_calls, 0);
    TEST_ASSERT(scratch_idle(&cache));
    TEST_ASSERT_EQ(tree.root, BFS_BLK_NULL);
    TEST_ASSERT_EQ(tree.height, 0);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key10, &result), BFS_ERR_NOTFOUND);
    TEST_ASSERT(scratch_idle(&cache));

    fail_call = 0;
    isolated_cache_destroy(&cache);
    TEST_ASSERT_EQ(live_allocations, 0);
    TEST_ASSERT(!tracking_error);
}

TEST_SUITE_BEGIN("Temporary buffer allocation faults")
    TEST_RUN(test_each_lazy_slot_failure);
    TEST_RUN(test_overflow_and_multiblock_failures);
    TEST_RUN(test_cache_initialization_failures);
    TEST_RUN(test_replacement_second_buffer_failure_releases_first);
    TEST_RUN(test_height_one_mutations_reuse_warm_scratch);
TEST_SUITE_END()
