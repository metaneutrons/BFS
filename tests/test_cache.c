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
    int fail_read;
    int fail_write;
    int fail_sync;
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

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
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

TEST_SUITE_BEGIN("Block Cache")
    TEST_RUN(test_defaults_and_limits);
    TEST_RUN(test_invalid_initialization);
    TEST_RUN(test_hit_and_invalidate);
    TEST_RUN(test_lru_eviction);
    TEST_RUN(test_write_through);
    TEST_RUN(test_io_errors);
TEST_SUITE_END()
