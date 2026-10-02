/* SPDX-License-Identifier: MPL-2.0 */

#include "test_harness.h"
#include "bfs_cache.h"

#define BLOCK_SIZE BFS_MIN_BLOCK_SIZE
#define BLOCK_COUNT 16

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[BLOCK_COUNT][BLOCK_SIZE];
    unsigned reads;
    unsigned writes;
    unsigned syncs;
    unsigned buffer_allocs;
    unsigned buffer_frees;
    int fail_read;
    int fail_write;
    int fail_sync;
    int fail_buffer_alloc;
} memory_bio_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->reads++;
    if (memory->fail_read) return BFS_ERR_IO;
    if (block >= BLOCK_COUNT) return BFS_ERR_INVAL;
    /* BIO callers supply a full BLOCK_SIZE buffer; the row index is checked. */
    memcpy(buffer, memory->blocks[block], BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block, const void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->writes++;
    if (memory->fail_write) return BFS_ERR_IO;
    if (block >= BLOCK_COUNT) return BFS_ERR_INVAL;
    /* BIO callers supply a full BLOCK_SIZE buffer; the row index is checked. */
    memcpy(memory->blocks[block], buffer, BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_sync(bfs_bio_t *bio)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->syncs++;
    return memory->fail_sync ? BFS_ERR_IO : BFS_OK;
}

static void memory_close(bfs_bio_t *bio)
{
    (void)bio;
}

static void *memory_alloc_buffer(bfs_bio_t *bio, size_t size)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->buffer_allocs++;
    if (memory->fail_buffer_alloc) return NULL;
    return malloc(size);
}

static void memory_free_buffer(bfs_bio_t *bio, void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->buffer_frees++;
    free(buffer);
}

static void *memory_reject_buffer_alloc(bfs_bio_t *bio, size_t size)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    (void)size;
    memory->buffer_allocs++;
    return NULL;
}

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
};

static const bfs_bio_ops_t memory_buffer_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
    .alloc_buffer = memory_alloc_buffer,
    .free_buffer = memory_free_buffer,
};

static const bfs_bio_ops_t memory_alloc_only_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
    .alloc_buffer = memory_reject_buffer_alloc,
};

static const bfs_bio_ops_t memory_free_only_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
    .free_buffer = memory_free_buffer,
};

static void memory_init(memory_bio_t *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->bio.ops = &memory_ops;
    memory->bio.block_size = BLOCK_SIZE;
    memory->bio.block_count = BLOCK_COUNT;
    for (unsigned block = 0; block < BLOCK_COUNT; block++) {
        memset(memory->blocks[block], (int)block, BLOCK_SIZE);
    }
}

static void memory_init_with_buffer_hooks(memory_bio_t *memory)
{
    memory_init(memory);
    memory->bio.ops = &memory_buffer_ops;
}

static int memory_key_compare(const void *left, const void *right)
{
    (void)left;
    (void)right;
    return 0;
}

static bool is_scratch_buffer(const bfs_cache_t *cache, const void *buffer)
{
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        if (cache->scratch[i].data == buffer) return true;
    }
    return false;
}

static void test_raw_bio_buffer_fallback(void)
{
    memory_bio_t memory;
    memory_init(&memory);

    void *buffer = bfs_bio_alloc_buffer(&memory.bio, BLOCK_SIZE);
    TEST_ASSERT(buffer != NULL);
    memset(buffer, 0xA7, BLOCK_SIZE);
    bfs_bio_free_buffer(&memory.bio, buffer);
    bfs_bio_free_buffer(&memory.bio, NULL);
    TEST_ASSERT_EQ(memory.buffer_allocs, 0);
    TEST_ASSERT_EQ(memory.buffer_frees, 0);
}

static void test_partial_buffer_hooks_are_ignored(void)
{
    memory_bio_t memory;
    memory_init(&memory);
    memory.bio.ops = &memory_alloc_only_ops;

    void *buffer = bfs_bio_alloc_buffer(&memory.bio, BLOCK_SIZE);
    TEST_ASSERT(buffer != NULL);
    TEST_ASSERT_EQ(memory.buffer_allocs, 0);
    bfs_bio_free_buffer(&memory.bio, buffer);
    TEST_ASSERT_EQ(memory.buffer_frees, 0);

    memory.bio.ops = &memory_free_only_ops;
    buffer = bfs_bio_alloc_buffer(&memory.bio, BLOCK_SIZE);
    TEST_ASSERT(buffer != NULL);
    TEST_ASSERT_EQ(memory.buffer_allocs, 0);
    bfs_bio_free_buffer(&memory.bio, buffer);
    TEST_ASSERT_EQ(memory.buffer_frees, 0);
}

static void test_complete_buffer_hooks_and_failed_allocation(void)
{
    memory_bio_t memory;
    memory_init_with_buffer_hooks(&memory);

    void *buffer = bfs_bio_alloc_buffer(&memory.bio, 24);
    TEST_ASSERT(buffer != NULL);
    TEST_ASSERT_EQ(memory.buffer_allocs, 1);
    bfs_bio_free_buffer(&memory.bio, buffer);
    TEST_ASSERT_EQ(memory.buffer_frees, 1);

    memory.fail_buffer_alloc = 1;
    buffer = bfs_bio_alloc_buffer(&memory.bio, 24);
    TEST_ASSERT(buffer == NULL);
    TEST_ASSERT_EQ(memory.buffer_allocs, 2);
    TEST_ASSERT_EQ(memory.buffer_frees, 1);
}

static void test_non_block_sized_buffer_requests(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    static const size_t request_sizes[] = {
        2u * BLOCK_SIZE,
        16u, /* A small B-tree key buffer. */
    };
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_OK);

    for (size_t request = 0;
         request < sizeof(request_sizes) / sizeof(request_sizes[0]);
         request++) {
        void *buffer = bfs_bio_alloc_buffer(&cache.bio, request_sizes[request]);
        TEST_ASSERT(buffer != NULL);
        TEST_ASSERT(!is_scratch_buffer(&cache, buffer));
        for (uint32_t slot = 0; slot < BFS_CACHE_SCRATCH_SLOTS; slot++) {
            TEST_ASSERT(!cache.scratch[slot].busy);
        }
        memset(buffer, 0x5C, request_sizes[request]);
        bfs_bio_free_buffer(&cache.bio, buffer);
    }

    bfs_cache_destroy(&cache);
}

static void test_cache_scratch_leases(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t verify[BLOCK_SIZE];
    void *leases[BFS_CACHE_SCRATCH_SLOTS] = {0};
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, verify), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, verify), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 2);

    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        leases[i] = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
        TEST_ASSERT(leases[i] != NULL);
        TEST_ASSERT(is_scratch_buffer(&cache, leases[i]));
        for (uint32_t j = 0; j < i; j++) TEST_ASSERT(leases[i] != leases[j]);
        for (uint32_t j = 0; j < cache.num_slots; j++) {
            TEST_ASSERT(leases[i] != cache.slots[j].data);
        }
        memset(leases[i], (int)(0xA0 + i), BLOCK_SIZE);
    }

    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        TEST_ASSERT(cache.scratch[i].data != NULL);
        TEST_ASSERT(cache.scratch[i].busy);
    }

    void *overflow = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
    TEST_ASSERT(overflow != NULL);
    TEST_ASSERT(!is_scratch_buffer(&cache, overflow));
    memset(overflow, 0xE5, BLOCK_SIZE);
    bfs_bio_free_buffer(&cache.bio, NULL);
    bfs_bio_free_buffer(&cache.bio, overflow);

    unsigned reads_before_hit = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, verify), BFS_OK);
    TEST_ASSERT_MEM_EQ(verify, memory.blocks[1], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);

    bfs_cache_invalidate(&cache);
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        TEST_ASSERT(cache.scratch[i].busy);
        TEST_ASSERT(cache.scratch[i].data == leases[i]);
        for (uint32_t j = 0; j < BLOCK_SIZE; j++) {
            TEST_ASSERT_EQ(((uint8_t *)leases[i])[j], (uint8_t)(0xA0 + i));
        }
    }

    overflow = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
    TEST_ASSERT(overflow != NULL);
    TEST_ASSERT(!is_scratch_buffer(&cache, overflow));
    bfs_bio_free_buffer(&cache.bio, overflow);

    reads_before_hit = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, verify), BFS_OK);
    TEST_ASSERT_MEM_EQ(verify, memory.blocks[1], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit + 1);
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        for (uint32_t j = 0; j < BLOCK_SIZE; j++) {
            TEST_ASSERT_EQ(((uint8_t *)leases[i])[j], (uint8_t)(0xA0 + i));
        }
    }

    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        void *released = leases[i];
        bfs_bio_free_buffer(&cache.bio, released);
        leases[i] = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
        TEST_ASSERT(leases[i] == released);
        TEST_ASSERT(is_scratch_buffer(&cache, leases[i]));
    }
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        bfs_bio_free_buffer(&cache.bio, leases[i]);
        TEST_ASSERT(!cache.scratch[i].busy);
    }

    bfs_cache_destroy(&cache);
}

static void test_cache_scratch_destroy_and_reinit(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    void *leases[BFS_CACHE_SCRATCH_SLOTS] = {0};
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_OK);

    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        leases[i] = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
        TEST_ASSERT(leases[i] != NULL);
        TEST_ASSERT(is_scratch_buffer(&cache, leases[i]));
    }
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        bfs_bio_free_buffer(&cache.bio, leases[i]);
    }
    bfs_cache_destroy(&cache);
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        TEST_ASSERT(cache.scratch[i].data == NULL);
        TEST_ASSERT(!cache.scratch[i].busy);
    }
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_OK);
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        leases[i] = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
        TEST_ASSERT(leases[i] != NULL);
        TEST_ASSERT(is_scratch_buffer(&cache, leases[i]));
    }
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        bfs_bio_free_buffer(&cache.bio, leases[i]);
    }
    bfs_cache_destroy(&cache);
}

static void test_cache_io_and_crc_with_leased_buffer(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    bfs_node_validation_t validation = {
        .key_compare = memory_key_compare,
        .key_size = 4,
        .val_size = 4,
        .block_size = BLOCK_SIZE,
        .block_count = BLOCK_COUNT,
    };
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_OK);

    uint8_t *buffer = bfs_bio_alloc_buffer(&cache.bio, BLOCK_SIZE);
    TEST_ASSERT(buffer != NULL);
    TEST_ASSERT(is_scratch_buffer(&cache, buffer));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 1);
    TEST_ASSERT(buffer != cache.slots[0].data);
    TEST_ASSERT_MEM_EQ(buffer, memory.blocks[3], BLOCK_SIZE);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));

    bfs_bio_mark_node_crc_valid(&cache.bio, 3);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 3));
    bfs_bio_mark_node_structure_valid(&cache.bio, 3, &validation);
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 3, &validation));
    memset(buffer, 0x6D, BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 1);
    TEST_ASSERT_MEM_EQ(memory.blocks[3], buffer, BLOCK_SIZE);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 3, &validation));

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 1);
    TEST_ASSERT_MEM_EQ(buffer, memory.blocks[3], BLOCK_SIZE);
    bfs_bio_mark_node_crc_valid(&cache.bio, 3);
    bfs_bio_mark_node_structure_valid(&cache.bio, 3, &validation);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 3));
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 3, &validation));

    bfs_cache_invalidate(&cache);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 3, &validation));
    TEST_ASSERT(cache.scratch[0].busy);
    TEST_ASSERT(cache.scratch[0].data == buffer);
    bfs_bio_free_buffer(&cache.bio, buffer);
    bfs_cache_destroy(&cache);
}

static void test_defaults_and_limits(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    memory_init(&memory);

    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 0), BFS_OK);
    TEST_ASSERT_EQ(cache.num_slots, BFS_CACHE_SLOTS_DEFAULT);
    TEST_ASSERT_EQ(cache.bio.block_size, BLOCK_SIZE);
    TEST_ASSERT_EQ(cache.bio.block_count, BLOCK_COUNT);
    bfs_cache_destroy(&cache);

    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, BFS_CACHE_SLOTS_MAX + 1), BFS_OK);
    TEST_ASSERT_EQ(cache.num_slots, BFS_CACHE_SLOTS_MAX);
    bfs_cache_destroy(&cache);
    bfs_cache_destroy(&cache);
}

static void test_invalid_initialization(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    memory_init(&memory);

    TEST_ASSERT_EQ(bfs_cache_init(NULL, &memory.bio, 1), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, NULL, 1), BFS_ERR_INVAL);

    memory.bio.ops = NULL;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_ERR_INVAL);
    memory_init(&memory);
    memory.bio.block_size = 0;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_ERR_INVAL);
}

static void test_hit_and_invalidate(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t buffer[BLOCK_SIZE];
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT_EQ(buffer[0], 3);
    TEST_ASSERT_EQ(memory.reads, 1);
    memset(buffer, 0, sizeof(buffer));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT_EQ(buffer[0], 3);
    TEST_ASSERT_EQ(memory.reads, 1);

    bfs_cache_invalidate(&cache);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 2);
    bfs_cache_destroy(&cache);
}

static void test_lru_eviction(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t buffer[BLOCK_SIZE];
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 0, buffer), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, buffer), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 0, buffer), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 3);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 4);
    bfs_cache_destroy(&cache);
}

static void test_write_through(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t buffer[BLOCK_SIZE];
    uint8_t replacement[BLOCK_SIZE];
    memory_init(&memory);
    memset(replacement, 0xA5, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 4, buffer), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 4, replacement), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 1);
    TEST_ASSERT_MEM_EQ(memory.blocks[4], replacement, BLOCK_SIZE);

    memset(buffer, 0, sizeof(buffer));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 4, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 1);
    TEST_ASSERT_MEM_EQ(buffer, replacement, BLOCK_SIZE);

    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 5, replacement), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 2);
    TEST_ASSERT_EQ(memory.reads, 2);
    bfs_cache_destroy(&cache);
}

static void test_io_errors(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t buffer[BLOCK_SIZE] = {0};
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    memory.fail_read = 1;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 0, buffer), BFS_ERR_IO);
    memory.fail_write = 1;
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 0, buffer), BFS_ERR_IO);
    memory.fail_sync = 1;
    TEST_ASSERT_EQ(bfs_bio_sync(&cache.bio), BFS_ERR_IO);
    TEST_ASSERT_EQ(memory.syncs, 1);

    bfs_bio_close(&cache.bio);
    bfs_cache_destroy(&cache);
}

static void test_verified_node_lifecycle(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t buffer[BLOCK_SIZE];
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&memory.bio, 3));
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    bfs_bio_mark_node_crc_valid(&cache.bio, 3);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 3));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 3));

    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    bfs_bio_mark_node_crc_valid(&cache.bio, 3);
    memory.fail_write = 1;
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 3, buffer), BFS_ERR_IO);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    memory.fail_write = 0;

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, buffer), BFS_OK);
    bfs_bio_mark_node_crc_valid(&cache.bio, 3);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 4, buffer), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    bfs_bio_mark_node_crc_valid(&cache.bio, 4);
    bfs_cache_invalidate(&cache);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 4));
    bfs_cache_destroy(&cache);
}

static void test_node_write_retention(void)
{
    memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t buffer[BLOCK_SIZE];
    uint8_t result[BLOCK_SIZE];
    memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 1), BFS_OK);

    memset(buffer, 0x31, sizeof(buffer));
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 3, buffer), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 3));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 3, result), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 1);

    bfs_cache_set_node_write_retention(&cache, true);
    memset(buffer, 0x42, sizeof(buffer));
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 4, buffer), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 4, result), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 2);

    /* This mock tests cache transitions; the B-tree test supplies a real
     * checksummed node to the trusted node-write operation. */
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 5, buffer), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 5));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, result), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 2);
    TEST_ASSERT_MEM_EQ(result, buffer, BLOCK_SIZE);

    memory.fail_write = 1;
    memset(buffer, 0x53, sizeof(buffer));
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 5, buffer), BFS_ERR_IO);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 5));
    memory.fail_write = 0;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, result), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 3);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[5], BLOCK_SIZE);

    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 6, buffer), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 6));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 7, result), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 6));

    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 8, buffer), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 8));
    bfs_cache_invalidate(&cache);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 8));
    bfs_cache_destroy(&cache);
}

static void test_node_write_raw_bio_fallback(void)
{
    memory_bio_t memory;
    uint8_t buffer[BLOCK_SIZE];
    memory_init(&memory);
    memset(buffer, 0xA9, sizeof(buffer));

    TEST_ASSERT_EQ(bfs_bio_write_node(&memory.bio, 2, buffer), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 1);
    TEST_ASSERT_MEM_EQ(memory.blocks[2], buffer, BLOCK_SIZE);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&memory.bio, 2));
}

TEST_SUITE_BEGIN("Block Cache")
    TEST_RUN(test_raw_bio_buffer_fallback);
    TEST_RUN(test_partial_buffer_hooks_are_ignored);
    TEST_RUN(test_complete_buffer_hooks_and_failed_allocation);
    TEST_RUN(test_non_block_sized_buffer_requests);
    TEST_RUN(test_cache_scratch_leases);
    TEST_RUN(test_cache_scratch_destroy_and_reinit);
    TEST_RUN(test_cache_io_and_crc_with_leased_buffer);
    TEST_RUN(test_defaults_and_limits);
    TEST_RUN(test_invalid_initialization);
    TEST_RUN(test_hit_and_invalidate);
    TEST_RUN(test_lru_eviction);
    TEST_RUN(test_write_through);
    TEST_RUN(test_io_errors);
    TEST_RUN(test_verified_node_lifecycle);
    TEST_RUN(test_node_write_retention);
    TEST_RUN(test_node_write_raw_bio_fallback);
TEST_SUITE_END()
