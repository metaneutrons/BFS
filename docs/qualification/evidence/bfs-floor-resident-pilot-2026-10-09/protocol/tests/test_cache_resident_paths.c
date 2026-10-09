/* SPDX-License-Identifier: MPL-2.0 */
/* Focused tests for resident cache bytes and validation state. */

#include "test_harness.h"
#include "bfs_cache.h"

#define BLOCK_SIZE BFS_MIN_BLOCK_SIZE
#define BLOCK_COUNT 16

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[BLOCK_COUNT][BLOCK_SIZE];
    uint32_t reads;
    uint32_t writes;
    bool fail_write;
    bool partial_fail_write;
} resident_memory_bio_t;

static bfs_err_t resident_memory_read(bfs_bio_t *bio, bfs_blk_t block,
                                  void *buffer)
{
    resident_memory_bio_t *memory = (resident_memory_bio_t *)bio;
    memory->reads++;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, memory->blocks[block], bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t resident_memory_write(bfs_bio_t *bio, bfs_blk_t block,
                                   const void *buffer)
{
    resident_memory_bio_t *memory = (resident_memory_bio_t *)bio;
    memory->writes++;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    if (memory->fail_write) {
        if (memory->partial_fail_write) {
            size_t partial_size = bio->block_size / 2;
            memcpy(memory->blocks[block], buffer, partial_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        }
        return BFS_ERR_IO;
    }
    memcpy(memory->blocks[block], buffer, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t resident_memory_sync(bfs_bio_t *bio)
{
    (void)bio;
    return BFS_OK;
}

static void resident_memory_close(bfs_bio_t *bio)
{
    (void)bio;
}

static const bfs_bio_ops_t resident_memory_ops = {
    .read_block = resident_memory_read,
    .write_block = resident_memory_write,
    .sync = resident_memory_sync,
    .close = resident_memory_close,
};

static void resident_memory_init(resident_memory_bio_t *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->bio.ops = &resident_memory_ops;
    memory->bio.block_size = BLOCK_SIZE;
    memory->bio.block_count = BLOCK_COUNT;
    for (uint32_t block = 0; block < BLOCK_COUNT; block++)
        memset(memory->blocks[block], (int)block, BLOCK_SIZE);
}

static uint32_t resident_find_slot(const bfs_cache_t *cache, bfs_blk_t block)
{
    for (uint32_t slot = 0; slot < cache->num_slots; slot++)
        if (cache->slots[slot].blk == block) return slot;
    return cache->num_slots;
}

static int resident_key_compare(const void *left, const void *right)
{
    (void)left;
    (void)right;
    return 0;
}

static int resident_other_key_compare(const void *left, const void *right)
{
    (void)left;
    (void)right;
    return 0;
}

static bfs_node_validation_t resident_validation(const resident_memory_bio_t *memory)
{
    bfs_node_validation_t validation = {
        .key_compare = resident_key_compare,
        .key_size = 4,
        .val_size = 4,
        .block_size = memory->bio.block_size,
        .block_count = memory->bio.block_count,
    };
    return validation;
}

static void test_resident_read_hit_and_validation(void)
{
    resident_memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t result[BLOCK_SIZE];
    resident_memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, result), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, result), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 2);
    uint32_t block_two_slot = resident_find_slot(&cache, 2);
    uint32_t block_five_slot = resident_find_slot(&cache, 5);
    TEST_ASSERT(block_two_slot < cache.num_slots);
    TEST_ASSERT(block_five_slot < cache.num_slots);
    TEST_ASSERT(block_two_slot != block_five_slot);

    uint32_t clock_before_callbacks = cache.clock;
    uint32_t block_two_age = cache.slots[block_two_slot].age;
    uint32_t block_five_age = cache.slots[block_five_slot].age;
    bfs_node_validation_t validation = resident_validation(&memory);
    bfs_bio_mark_node_crc_valid(&cache.bio, 5);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 5));
    bfs_bio_mark_node_structure_valid(&cache.bio, 5, &validation);
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 5, &validation));

    /* Validation callbacks do not disturb LRU state. */
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 5));
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 5, &validation));

    bfs_node_validation_t changed = validation;
    changed.key_compare = resident_other_key_compare;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 5, &changed));
    changed = validation;
    changed.key_size++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 5, &changed));
    changed = validation;
    changed.val_size++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 5, &changed));
    changed = validation;
    changed.block_size++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 5, &changed));
    changed = validation;
    changed.block_count++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 5, &changed));
    TEST_ASSERT(cache.clock == clock_before_callbacks);
    TEST_ASSERT_EQ(cache.slots[block_two_slot].age, block_two_age);
    TEST_ASSERT_EQ(cache.slots[block_five_slot].age, block_five_age);

    unsigned reads_before_hit = memory.reads;
    memset(result, 0xA4, sizeof(result));
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[5], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 5));
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 5, &validation));

    /* Repeated hits preserve exact bytes without a device read. */
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[5], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);

    bfs_cache_destroy(&cache);
}

static void test_resident_hits_at_supported_slot_counts(void)
{
    static const uint32_t slot_counts[] = {1, 2, 30, 128};
    resident_memory_bio_t memory;
    uint8_t result[BLOCK_SIZE];
    resident_memory_init(&memory);

    for (size_t i = 0; i < sizeof(slot_counts) / sizeof(slot_counts[0]); i++) {
        bfs_cache_t cache;
        uint32_t slots = slot_counts[i];
        TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, slots), BFS_OK);
        TEST_ASSERT_EQ(cache.num_slots, slots);

        unsigned reads_before = memory.reads;
        TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 7, result), BFS_OK);
        TEST_ASSERT_MEM_EQ(result, memory.blocks[7], BLOCK_SIZE);
        TEST_ASSERT_EQ(memory.reads, reads_before + 1);

        /* Repeated reads must remain hits at each selected capacity. */
        if (slots > 1) {
            TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 7, result), BFS_OK);
            TEST_ASSERT_MEM_EQ(result, memory.blocks[7], BLOCK_SIZE);
            TEST_ASSERT_EQ(memory.reads, reads_before + 1);
        }

        TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 7, result), BFS_OK);
        TEST_ASSERT_MEM_EQ(result, memory.blocks[7], BLOCK_SIZE);
        TEST_ASSERT_EQ(memory.reads, reads_before + 1);

        bfs_cache_destroy(&cache);

        TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, slots), BFS_OK);
        bfs_cache_destroy(&cache);
    }
}

static void test_lru_victim_replacement(void)
{
    resident_memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t result[BLOCK_SIZE];
    resident_memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 0, result), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, result), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 0, result), BFS_OK);
    TEST_ASSERT_EQ(memory.reads, 2);
    uint32_t victim = resident_find_slot(&cache, 1);
    TEST_ASSERT(victim < cache.num_slots);

    /* The least-recently-read resident must be replaced on a miss. */
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[2], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, 3);
    TEST_ASSERT_EQ(cache.slots[victim].blk, 2);

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[1], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, 4);
    bfs_cache_destroy(&cache);
}

static void test_successful_write_paths_track_resident_slot(void)
{
    resident_memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t result[BLOCK_SIZE];
    uint8_t replacement[BLOCK_SIZE];
    resident_memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);
    memset(replacement, 0xA5, sizeof(replacement));

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 4, result), BFS_OK);
    uint32_t resident = resident_find_slot(&cache, 4);
    TEST_ASSERT(resident < cache.num_slots);
    bfs_node_validation_t validation = resident_validation(&memory);
    bfs_bio_mark_node_crc_valid(&cache.bio, 4);
    bfs_bio_mark_node_structure_valid(&cache.bio, 4, &validation);
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 4, &validation));

    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 4, replacement), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 1);
    TEST_ASSERT_MEM_EQ(memory.blocks[4], replacement, BLOCK_SIZE);
    TEST_ASSERT_MEM_EQ(cache.slots[resident].data, replacement, BLOCK_SIZE);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 4));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 4, &validation));

    unsigned reads_before_hit = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 4, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, replacement, BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);

    bfs_cache_set_node_write_retention(&cache, true);
    memset(replacement, 0x3C, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 6, replacement), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 2);
    uint32_t node_slot = resident_find_slot(&cache, 6);
    TEST_ASSERT(node_slot < cache.num_slots);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 6));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 6, &validation));

    reads_before_hit = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 6, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, replacement, BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);
    bfs_bio_mark_node_structure_valid(&cache.bio, 6, &validation);
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 6, &validation));

    /* A retained node write also refreshes an already resident slot. */
    memset(replacement, 0xE4, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, 6, replacement), BFS_OK);
    TEST_ASSERT_EQ(memory.writes, 3);
    TEST_ASSERT_MEM_EQ(memory.blocks[6], replacement, BLOCK_SIZE);
    TEST_ASSERT_MEM_EQ(cache.slots[node_slot].data, replacement, BLOCK_SIZE);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, 6));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&cache.bio, 6, &validation));
    reads_before_hit = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 6, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, replacement, BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);

    bfs_cache_destroy(&cache);
}

static void test_failed_and_partial_writes_preserve_other_residents(void)
{
    resident_memory_bio_t memory;
    bfs_cache_t cache;
    uint8_t result[BLOCK_SIZE];
    uint8_t replacement[BLOCK_SIZE];
    uint8_t expected_partial[BLOCK_SIZE];
    resident_memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 2), BFS_OK);
    memset(replacement, 0xD7, sizeof(replacement));

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, result), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, result), BFS_OK);
    bfs_node_validation_t validation = resident_validation(&memory);
    bfs_bio_mark_node_crc_valid(&cache.bio, 1);
    bfs_bio_mark_node_crc_valid(&cache.bio, 2);
    bfs_bio_mark_node_structure_valid(&cache.bio, 1, &validation);
    bfs_bio_mark_node_structure_valid(&cache.bio, 2, &validation);
    uint32_t block_one_slot = resident_find_slot(&cache, 1);
    uint32_t block_two_slot = resident_find_slot(&cache, 2);
    TEST_ASSERT(block_one_slot < cache.num_slots);
    TEST_ASSERT(block_two_slot < cache.num_slots);

    memory.fail_write = true;
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 1, replacement), BFS_ERR_IO);
    TEST_ASSERT_EQ(cache.slots[block_one_slot].blk, UINT32_MAX);
    TEST_ASSERT(!cache.slots[block_one_slot].node_crc_valid);
    TEST_ASSERT(!cache.slots[block_one_slot].node_structure_valid);
    TEST_ASSERT_EQ(cache.slots[block_two_slot].blk, 2);
    TEST_ASSERT(cache.slots[block_two_slot].node_crc_valid);
    TEST_ASSERT(cache.slots[block_two_slot].node_structure_valid);

    memory.fail_write = false;
    unsigned reads_before = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[2], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before);
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 2, &validation));

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[1], BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, result), BFS_OK);
    memcpy(expected_partial, memory.blocks[1], BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    memcpy(expected_partial, replacement, BLOCK_SIZE / 2); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    memory.fail_write = true;
    memory.partial_fail_write = true;
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 1, replacement), BFS_ERR_IO);
    TEST_ASSERT_MEM_EQ(memory.blocks[1], expected_partial, BLOCK_SIZE);
    block_one_slot = resident_find_slot(&cache, 1);
    block_two_slot = resident_find_slot(&cache, 2);
    TEST_ASSERT_EQ(block_one_slot, cache.num_slots);
    TEST_ASSERT(block_two_slot < cache.num_slots);
    TEST_ASSERT(cache.slots[block_two_slot].node_crc_valid);
    TEST_ASSERT(cache.slots[block_two_slot].node_structure_valid);

    memory.fail_write = false;
    memory.partial_fail_write = false;
    reads_before = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 1, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, expected_partial, BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before + 1);
    reads_before = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 2, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[2], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before);
    TEST_ASSERT(bfs_bio_node_structure_valid(&cache.bio, 2, &validation));
    bfs_cache_destroy(&cache);
}

static void test_explicit_invalidation_and_separate_caches(void)
{
    resident_memory_bio_t memory;
    bfs_cache_t first;
    bfs_cache_t second;
    uint8_t result[BLOCK_SIZE];
    resident_memory_init(&memory);
    TEST_ASSERT_EQ(bfs_cache_init(&first, &memory.bio, 2), BFS_OK);
    TEST_ASSERT_EQ(bfs_cache_init(&second, &memory.bio, 2), BFS_OK);

    TEST_ASSERT_EQ(bfs_bio_read(&first.bio, 0, result), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&first.bio, 1, result), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&second.bio, 3, result), BFS_OK);
    TEST_ASSERT(resident_find_slot(&first, 1) < first.num_slots);
    TEST_ASSERT(resident_find_slot(&second, 3) < second.num_slots);

    unsigned reads_before_hit = memory.reads;
    TEST_ASSERT_EQ(bfs_bio_read(&first.bio, 1, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[1], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);

    TEST_ASSERT_EQ(bfs_bio_read(&second.bio, 3, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[3], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit);

    bfs_cache_invalidate(&first);
    for (uint32_t slot = 0; slot < first.num_slots; slot++)
        TEST_ASSERT_EQ(first.slots[slot].blk, UINT32_MAX);

    TEST_ASSERT_EQ(bfs_bio_read(&first.bio, 1, result), BFS_OK);
    TEST_ASSERT_MEM_EQ(result, memory.blocks[1], BLOCK_SIZE);
    TEST_ASSERT_EQ(memory.reads, reads_before_hit + 1);

    bfs_cache_destroy(&first);
    bfs_cache_destroy(&second);
}

TEST_SUITE_BEGIN("Cache Resident Paths")
    TEST_RUN(test_resident_read_hit_and_validation);
    TEST_RUN(test_resident_hits_at_supported_slot_counts);
    TEST_RUN(test_lru_victim_replacement);
    TEST_RUN(test_successful_write_paths_track_resident_slot);
    TEST_RUN(test_failed_and_partial_writes_preserve_other_residents);
    TEST_RUN(test_explicit_invalidation_and_separate_caches);
TEST_SUITE_END()
