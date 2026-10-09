/* SPDX-License-Identifier: MPL-2.0 */
/* Focused allocator tests for atomic height-one run carving. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "bfs_file.h"
#include "bfs_fsck.h"
#include "bfs_superblock.h"
#include <stdlib.h>

#define MEM_BLOCK_SIZE 1024u
#define MEM_BLOCK_COUNT 1024u
#define FIRST_EXTENT_START 10u
#define FIRST_EXTENT_LEN 100u
#define SECOND_EXTENT_START 200u
#define SECOND_EXTENT_LEN 100u
#define RUN_COUNT 5u
#define FS_BLOCK_SIZE 4096u
#define FS_BLOCK_COUNT 4096u
#define APPEND_CHUNK_SIZE (64u * 1024u)
#define APPEND_TEST_SIZE (8u * 1024u * 1024u)

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    uint32_t reads;
    uint32_t writes;
    bool fail_read;
    uint32_t fail_read_at;
    uint32_t failed_read_at;
    uint32_t fail_write_at;
    bool partial_write;
    uint32_t failed_write_at;
} memory_device_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    device->reads++;
    if (device->fail_read ||
        (device->fail_read_at != 0 && device->reads == device->fail_read_at)) {
        device->failed_read_at = device->reads;
        return BFS_ERR_IO;
    }
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, device->bytes + (size_t)block * bio->block_size,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    device->writes++;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    if (device->fail_write_at == device->writes) {
        device->failed_write_at = device->writes;
        if (device->partial_write)
            memcpy(device->bytes + (size_t)block * bio->block_size, buffer,
                   bio->block_size / 2); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        return BFS_ERR_IO;
    }
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

typedef struct {
    memory_device_t device;
    bfs_freespace_t space;
} allocator_fixture_t;

static bool memory_init(memory_device_t *device, uint32_t block_size,
                        bfs_blk_t block_count)
{
    memset(device, 0, sizeof(*device));
    if (block_count > SIZE_MAX / block_size) return false;
    device->bytes = calloc((size_t)block_count, block_size);
    if (!device->bytes) return false;
    device->bio.ops = &memory_ops;
    device->bio.block_size = block_size;
    device->bio.block_count = block_count;
    return true;
}

static bool fixture_init_with_first_len(allocator_fixture_t *fixture,
                                        uint32_t first_extent_len);

static bool fixture_init(allocator_fixture_t *fixture)
{
    return fixture_init_with_first_len(fixture, FIRST_EXTENT_LEN);
}

static bool fixture_init_with_first_len(allocator_fixture_t *fixture,
                                        uint32_t first_extent_len)
{
    memset(fixture, 0, sizeof(*fixture));
    if (!memory_init(&fixture->device, MEM_BLOCK_SIZE, MEM_BLOCK_COUNT))
        return false;
    if (bfs_freespace_init(&fixture->space, &fixture->device.bio,
                           BFS_BLK_NULL, 1) != BFS_OK)
        return false;
    if (bfs_freespace_add(&fixture->space, FIRST_EXTENT_START,
                          first_extent_len) != BFS_OK ||
        bfs_freespace_add(&fixture->space, SECOND_EXTENT_START,
                          SECOND_EXTENT_LEN) != BFS_OK ||
        bfs_freespace_refill_reserve(&fixture->space) != BFS_OK)
        return false;
    fixture->space.roving = 0;
    return fixture->space.tree.height == 1 && fixture->space.reserve_count >= 20;
}

static void fixture_destroy(allocator_fixture_t *fixture)
{
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
}

#define MAX_EXTENTS 256u
typedef struct {
    bfs_blk_t start;
    uint32_t length;
} free_extent_t;

typedef struct {
    free_extent_t items[MAX_EXTENTS];
    uint32_t count;
    bool overflow;
} free_extent_list_t;

static bool collect_extent(const void *key, const void *val, void *ctx)
{
    free_extent_list_t *list = ctx;
    if (list->count == MAX_EXTENTS) {
        list->overflow = true;
        return false;
    }
    list->items[list->count].start = bfs_load_be32(key);
    list->items[list->count].length = bfs_load_be32(val);
    list->count++;
    return true;
}

static bool read_free_extents(allocator_fixture_t *fixture,
                              free_extent_list_t *list)
{
    memset(list, 0, sizeof(*list));
    return bfs_btree_scan(&fixture->space.tree, NULL, collect_extent, list) == BFS_OK &&
           !list->overflow;
}

static bool extent_lists_equal(const free_extent_list_t *actual,
                               const free_extent_list_t *expected)
{
    if (actual->count != expected->count || actual->overflow ||
        expected->overflow)
        return false;
    for (uint32_t i = 0; i < actual->count; i++)
        if (actual->items[i].start != expected->items[i].start ||
            actual->items[i].length != expected->items[i].length)
            return false;
    return true;
}

typedef struct {
    uint32_t headroom;
    uint32_t defer_calls;
    bfs_blk_t deferred[8];
    bfs_err_t defer_error;
} sink_fixture_t;

static uint32_t sink_headroom(void *ctx)
{
    return ((sink_fixture_t *)ctx)->headroom;
}

static bfs_err_t sink_defer(void *ctx, bfs_blk_t block)
{
    sink_fixture_t *sink = ctx;
    if (sink->defer_calls < sizeof(sink->deferred) / sizeof(sink->deferred[0]))
        sink->deferred[sink->defer_calls] = block;
    sink->defer_calls++;
    return sink->defer_error;
}

static bfs_free_sink_t make_sink(sink_fixture_t *sink, uint32_t capacity)
{
    bfs_free_sink_t result = {
        .ctx = sink,
        .defer = sink_defer,
        .headroom = sink_headroom,
        .capacity = capacity,
    };
    return result;
}

static void test_first_fit_run_carve_emits_one_root_leaf_write(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    TEST_ASSERT_EQ(fixture.space.tree.height, 1);
    TEST_ASSERT_EQ(fixture.space.roving, 0);
    uint32_t total_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    fixture.device.writes = 0;

    bfs_blk_t start = bfs_freespace_alloc(&fixture.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, FIRST_EXTENT_START + 16u);
    TEST_ASSERT_EQ(fixture.device.writes, 1);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before - RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.roving, start + RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

/* File data starts at the lowest free block, continues behind its goal and
 * never takes the tail of the highest extent, where metadata comes from. */
static void test_data_alloc_ascends_from_goal(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    free_extent_list_t before, after;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    TEST_ASSERT_EQ(before.count, 2);
    bfs_blk_t low = before.items[0].start;
    uint32_t low_len = before.items[0].length;
    uint32_t total = fixture.space.total_free;
    fixture.device.writes = 0;

    bfs_blk_t first = bfs_freespace_alloc_data(&fixture.space, 1, BFS_BLK_NULL);
    TEST_ASSERT_EQ(first, low);
    bfs_blk_t second = bfs_freespace_alloc_data(&fixture.space, 1, first + 1);
    TEST_ASSERT_EQ(second, first + 1);
    bfs_blk_t run = bfs_freespace_alloc_data(&fixture.space, RUN_COUNT, second + 1);
    TEST_ASSERT_EQ(run, second + 1);
    /* Each takes the head of the extent by moving its key: one leaf write. */
    TEST_ASSERT_EQ(fixture.device.writes, 3);
    TEST_ASSERT_EQ(fixture.space.total_free, total - 2 - RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.data_roving, run + RUN_COUNT);
    TEST_ASSERT(read_free_extents(&fixture, &after));
    TEST_ASSERT_EQ(after.count, 2);
    TEST_ASSERT_EQ(after.items[0].start, low + 2 + RUN_COUNT);
    TEST_ASSERT_EQ(after.items[0].length, low_len - 2 - RUN_COUNT);
    TEST_ASSERT_EQ(after.items[1].start, before.items[1].start);
    TEST_ASSERT_EQ(after.items[1].length, before.items[1].length);
    fixture_destroy(&fixture);
}

/* A goal inside an extent splits it and a goal at its end shortens it, each
 * with one leaf write; a goal that is not free falls back to the first fit
 * at the data roving pointer. */
static void test_data_alloc_goal_inside_end_and_taken(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    free_extent_list_t before, after;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    TEST_ASSERT_EQ(before.count, 2);
    bfs_blk_t low = before.items[0].start;
    uint32_t low_len = before.items[0].length;
    bfs_blk_t high = before.items[1].start;
    uint32_t high_len = before.items[1].length;
    TEST_ASSERT(low_len > 20);
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 2, low + 10), low + 10);
    TEST_ASSERT_EQ(fixture.device.writes, 1);
    TEST_ASSERT(read_free_extents(&fixture, &after));
    TEST_ASSERT_EQ(after.count, 3);
    TEST_ASSERT_EQ(after.items[0].start, low);
    TEST_ASSERT_EQ(after.items[0].length, 10);
    TEST_ASSERT_EQ(after.items[1].start, low + 12);
    TEST_ASSERT_EQ(after.items[1].length, low_len - 12);

    bfs_blk_t end = high + high_len - 1;
    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 1, end), end);
    TEST_ASSERT_EQ(fixture.device.writes, 2);
    TEST_ASSERT(read_free_extents(&fixture, &after));
    TEST_ASSERT_EQ(after.items[2].length, high_len - 1);

    /* low + 10 is taken; the roving pointer behind end wraps to low. */
    TEST_ASSERT_EQ(fixture.space.data_roving, end + 1);
    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 1, low + 10), low);
    /* Too few free blocks at the goal fall back as well: the first fit
     * behind low is the rest of the first extent. */
    TEST_ASSERT_EQ(bfs_freespace_alloc_data(&fixture.space, 3, low + 8), low + 1);
    fixture_destroy(&fixture);
}

static bool expected_carve(const free_extent_list_t *before, uint32_t index,
                           uint32_t count, free_extent_list_t *after)
{
    if (index >= before->count || before->items[index].length < count)
        return false;
    *after = *before;
    if (before->items[index].length == count) {
        for (uint32_t i = index + 1; i < after->count; i++)
            after->items[i - 1] = after->items[i];
        after->count--;
    } else {
        after->items[index].start += count;
        after->items[index].length -= count;
    }
    return true;
}

static bool copy_fixture_root(const allocator_fixture_t *fixture, uint8_t *out)
{
    bfs_blk_t root = fixture->space.tree.root;
    if (root == BFS_BLK_NULL || root >= fixture->device.bio.block_count)
        return false;
    memcpy(out, fixture->device.bytes + (size_t)root * MEM_BLOCK_SIZE,
           MEM_BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return true;
}

static bool fixture_root_matches(const allocator_fixture_t *fixture,
                                 bfs_blk_t root, const uint8_t *bytes)
{
    if (root >= fixture->device.bio.block_count) return false;
    return memcmp(fixture->device.bytes + (size_t)root * MEM_BLOCK_SIZE,
                  bytes, MEM_BLOCK_SIZE) == 0;
}

static bool reserve_contains(const bfs_freespace_t *space, bfs_blk_t block)
{
    for (uint32_t i = 0; i < space->reserve_count; i++)
        if (space->reserve[i] == block) return true;
    return false;
}

static void test_roving_wrap_skips_short_first_fit_extent(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init_with_first_len(&fixture, 19u));
    free_extent_list_t before, expected, actual;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    TEST_ASSERT_EQ(before.count, 2);
    TEST_ASSERT_EQ(before.items[0].length, 3);
    TEST_ASSERT(expected_carve(&before, 1, RUN_COUNT, &expected));
    fixture.space.roving = MEM_BLOCK_COUNT - 1;
    uint32_t total_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    fixture.device.writes = 0;

    bfs_blk_t start = bfs_freespace_alloc(&fixture.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, before.items[1].start);
    TEST_ASSERT_EQ(fixture.device.writes, 1);
    TEST_ASSERT_EQ(fixture.space.roving, start + RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before - RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &expected));
    fixture_destroy(&fixture);
}

static void test_exact_fit_and_single_block_keep_legacy_selection(void)
{
    allocator_fixture_t exact;
    TEST_ASSERT(fixture_init_with_first_len(&exact, 21u));
    free_extent_list_t before, expected, actual;
    TEST_ASSERT(read_free_extents(&exact, &before));
    TEST_ASSERT_EQ(before.count, 2);
    TEST_ASSERT_EQ(before.items[0].length, RUN_COUNT);
    TEST_ASSERT(expected_carve(&before, 0, RUN_COUNT, &expected));
    exact.device.writes = 0;
    bfs_blk_t start = bfs_freespace_alloc(&exact.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, before.items[0].start);
    TEST_ASSERT_EQ(exact.device.writes, 1);
    TEST_ASSERT(read_free_extents(&exact, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &expected));
    fixture_destroy(&exact);

    allocator_fixture_t single;
    TEST_ASSERT(fixture_init(&single));
    TEST_ASSERT(read_free_extents(&single, &before));
    TEST_ASSERT_EQ(before.count, 2);
    bfs_blk_t expected_single = before.items[1].start + before.items[1].length - 1;
    single.space.roving = 0;
    single.device.writes = 0;
    start = bfs_freespace_alloc(&single.space, 1);
    TEST_ASSERT_EQ(start, expected_single);
    TEST_ASSERT_EQ(single.device.writes, 1);
    TEST_ASSERT_EQ(single.space.roving, start + 1);
    fixture_destroy(&single);
}

static void test_deeper_free_tree_keeps_legacy_mutation_path(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    uint32_t added = 0;
    while (fixture.space.tree.height == 1 && added < 180) {
        bfs_blk_t start = 300u + 2u * added;
        TEST_ASSERT(start < MEM_BLOCK_COUNT);
        TEST_ASSERT_EQ(bfs_freespace_add(&fixture.space, start, 1), BFS_OK);
        added++;
    }
    TEST_ASSERT(fixture.space.tree.height > 1);
    /* The height-two reserve target is larger. Refill it before measuring the
     * allocation so the allocator's normal preflight cannot change accounting
     * as a side effect of the tree-depth transition. */
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fixture.space), BFS_OK);
    TEST_ASSERT(fixture.space.reserve_count >= 24);
    /* Model a later transaction so the legacy delete/reinsert path must COW
     * the existing internal-tree root rather than rewriting it in place. */
    fixture.space.tree.txn_id_fallback++;
    fixture.space.roving = 0;
    free_extent_list_t before, expected, actual;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    TEST_ASSERT(expected_carve(&before, 0, RUN_COUNT, &expected));
    uint32_t total_before = fixture.space.total_free;
    bfs_blk_t old_root = fixture.space.tree.root;
    fixture.device.writes = 0;

    bfs_blk_t start = bfs_freespace_alloc(&fixture.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, FIRST_EXTENT_START + 16u);
    TEST_ASSERT(fixture.device.writes > 0);
    TEST_ASSERT_EQ(fixture.space.total_free, total_before - RUN_COUNT);
    TEST_ASSERT(fixture.space.tree.root != BFS_BLK_NULL);
    TEST_ASSERT(fixture.space.tree.height >= 1);
    TEST_ASSERT(fixture.space.tree.root != old_root);
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &expected));
    fixture_destroy(&fixture);
}

static void test_read_failure_does_not_mutate_allocator_state(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    free_extent_list_t before, actual;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    bfs_blk_t root = fixture.space.tree.root;
    uint32_t total = fixture.space.total_free;
    bfs_blk_t roving = fixture.space.roving;
    uint32_t reserve_count = fixture.space.reserve_count;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(reserve, fixture.space.reserve, sizeof(reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    fixture.device.writes = 0;
    fixture.device.fail_read = true;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fixture.space, RUN_COUNT), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fixture.space.tree.root, root);
    TEST_ASSERT_EQ(fixture.space.total_free, total);
    TEST_ASSERT_EQ(fixture.space.roving, roving);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_count);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, reserve, sizeof(reserve));
    fixture.device.fail_read = false;
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &before));
    fixture_destroy(&fixture);
}

static void test_read_failure_cuts_cover_every_pre_swap_read(void)
{
    uint32_t injected_failures = 0;
    uint32_t successful_past_end = 0;
    for (uint32_t cut = 1; cut <= 8; cut++) {
        allocator_fixture_t fixture;
        TEST_ASSERT(fixture_init(&fixture));
        bfs_blk_t root = fixture.space.tree.root;
        uint8_t root_bytes[MEM_BLOCK_SIZE];
        TEST_ASSERT(copy_fixture_root(&fixture, root_bytes));
        uint32_t total = fixture.space.total_free;
        bfs_blk_t roving = fixture.space.roving;
        uint32_t reserve_count = fixture.space.reserve_count;
        bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
        memcpy(reserve, fixture.space.reserve, sizeof(reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        fixture.device.reads = 0;
        fixture.device.writes = 0;
        fixture.device.fail_read_at = cut;

        bfs_blk_t start = bfs_freespace_alloc(&fixture.space, RUN_COUNT);
        if (fixture.device.failed_read_at != 0) {
            injected_failures++;
            TEST_ASSERT_EQ(fixture.device.failed_read_at, cut);
            TEST_ASSERT_EQ(start, BFS_BLK_NULL);
            TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_IO);
            TEST_ASSERT_EQ(fixture.device.writes, 0);
            TEST_ASSERT_EQ(fixture.space.tree.root, root);
            TEST_ASSERT(fixture_root_matches(&fixture, root, root_bytes));
            TEST_ASSERT_EQ(fixture.space.total_free, total);
            TEST_ASSERT_EQ(fixture.space.roving, roving);
            TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_count);
            TEST_ASSERT_MEM_EQ(fixture.space.reserve, reserve, sizeof(reserve));
        } else {
            successful_past_end++;
            TEST_ASSERT_EQ(start, FIRST_EXTENT_START + 16u);
            TEST_ASSERT(fixture.device.reads < cut);
            TEST_ASSERT_EQ(fixture.device.writes, 1);
            TEST_ASSERT_EQ(fixture.space.total_free, total - RUN_COUNT);
            TEST_ASSERT_EQ(fixture.space.roving, start + RUN_COUNT);
        }
        fixture_destroy(&fixture);
    }
    TEST_ASSERT(injected_failures >= 3);
    TEST_ASSERT(successful_past_end > 0);
}

static void test_partial_leaf_write_failure_is_atomic_and_retryable(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    free_extent_list_t before, expected, actual;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    TEST_ASSERT(expected_carve(&before, 0, RUN_COUNT, &expected));
    bfs_blk_t old_root = fixture.space.tree.root;
    uint8_t old_root_bytes[MEM_BLOCK_SIZE];
    TEST_ASSERT(copy_fixture_root(&fixture, old_root_bytes));
    uint32_t total = fixture.space.total_free;
    bfs_blk_t roving = fixture.space.roving;
    uint32_t reserve_count = fixture.space.reserve_count;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(reserve, fixture.space.reserve, sizeof(reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    fixture.device.writes = 0;
    fixture.device.fail_write_at = 1;
    fixture.device.partial_write = true;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fixture.space, RUN_COUNT), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.device.failed_write_at, 1);
    TEST_ASSERT_EQ(fixture.device.writes, 1);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.space.tree.root, old_root);
    TEST_ASSERT(fixture_root_matches(&fixture, old_root, old_root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, total);
    TEST_ASSERT_EQ(fixture.space.roving, roving);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_count);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, reserve, sizeof(reserve));
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &before));

    fixture.device.fail_write_at = 0;
    fixture.device.partial_write = false;
    fixture.device.failed_write_at = 0;
    fixture.device.writes = 0;
    bfs_blk_t start = bfs_freespace_alloc(&fixture.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, before.items[0].start);
    TEST_ASSERT_EQ(fixture.device.writes, 1);
    TEST_ASSERT(fixture.space.tree.root != old_root);
    TEST_ASSERT(fixture_root_matches(&fixture, old_root, old_root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, total - RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.roving, start + RUN_COUNT);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_count);
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &expected));
    fixture_destroy(&fixture);
}

static void test_pending_headroom_again_precedes_root_swap(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    sink_fixture_t sink = { .headroom = 0, .defer_error = BFS_OK };
    fixture.space.tree.txn_id_fallback = 2;
    fixture.space.tree.free_sink = make_sink(&sink, 1);
    bfs_blk_t root = fixture.space.tree.root;
    uint32_t total = fixture.space.total_free;
    bfs_blk_t roving = fixture.space.roving;
    uint32_t reserve_count = fixture.space.reserve_count;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(reserve, fixture.space.reserve, sizeof(reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    uint8_t root_bytes[MEM_BLOCK_SIZE];
    TEST_ASSERT(copy_fixture_root(&fixture, root_bytes));
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fixture.space, RUN_COUNT), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(sink.defer_calls, 0);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fixture.space.tree.root, root);
    TEST_ASSERT(fixture_root_matches(&fixture, root, root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, total);
    TEST_ASSERT_EQ(fixture.space.roving, roving);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_count);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, reserve, sizeof(reserve));
    fixture_destroy(&fixture);
}

static void test_current_and_older_root_retirement_ownership(void)
{
    allocator_fixture_t current;
    TEST_ASSERT(fixture_init(&current));
    sink_fixture_t current_sink = { .headroom = 1, .defer_error = BFS_OK };
    current.space.tree.free_sink = make_sink(&current_sink, 1);
    uint64_t root_txn;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&current.space.tree, &root_txn),
                   BFS_OK);
    TEST_ASSERT_EQ(root_txn, 1);
    bfs_blk_t current_old_root = current.space.tree.root;
    uint32_t current_reserve = current.space.reserve_count;
    uint32_t current_total = current.space.total_free;
    current.device.writes = 0;
    bfs_blk_t start = bfs_freespace_alloc(&current.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, FIRST_EXTENT_START + 16u);
    TEST_ASSERT_EQ(current.device.writes, 1);
    TEST_ASSERT_EQ(current_sink.defer_calls, 0);
    TEST_ASSERT_EQ(current.space.reserve_count, current_reserve);
    TEST_ASSERT(reserve_contains(&current.space, current_old_root));
    TEST_ASSERT_EQ(current.space.total_free, current_total - RUN_COUNT);
    fixture_destroy(&current);

    allocator_fixture_t older;
    TEST_ASSERT(fixture_init(&older));
    sink_fixture_t older_sink = { .headroom = 1, .defer_error = BFS_OK };
    older.space.tree.txn_id_fallback = 2;
    older.space.tree.free_sink = make_sink(&older_sink, 1);
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&older.space.tree, &root_txn),
                   BFS_OK);
    TEST_ASSERT_EQ(root_txn, 1);
    bfs_blk_t older_old_root = older.space.tree.root;
    uint32_t older_reserve = older.space.reserve_count;
    uint32_t older_total = older.space.total_free;
    older.device.writes = 0;
    start = bfs_freespace_alloc(&older.space, RUN_COUNT);
    TEST_ASSERT_EQ(start, FIRST_EXTENT_START + 16u);
    TEST_ASSERT_EQ(older.device.writes, 1);
    TEST_ASSERT_EQ(older_sink.defer_calls, 1);
    TEST_ASSERT_EQ(older_sink.deferred[0], older_old_root);
    TEST_ASSERT_EQ(older.space.reserve_count, older_reserve - 1);
    TEST_ASSERT(!reserve_contains(&older.space, older_old_root));
    TEST_ASSERT_EQ(older.space.total_free, older_total - RUN_COUNT);
    fixture_destroy(&older);
}

static void test_allocator_scratch_exhaustion_preserves_root(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    free_extent_list_t before, actual;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    bfs_blk_t root = fixture.space.tree.root;
    uint8_t root_bytes[MEM_BLOCK_SIZE];
    TEST_ASSERT(copy_fixture_root(&fixture, root_bytes));
    uint32_t total = fixture.space.total_free;
    bfs_blk_t roving = fixture.space.roving;
    fixture.space.reserve_count = 0;
    fixture.space.global_reserve = UINT32_MAX; /* prevent refill before COW */
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fixture.space, RUN_COUNT), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_NOSPC);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fixture.space.tree.root, root);
    TEST_ASSERT(fixture_root_matches(&fixture, root, root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, total);
    TEST_ASSERT_EQ(fixture.space.roving, roving);
    TEST_ASSERT_EQ(fixture.space.reserve_count, 0);
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT(extent_lists_equal(&actual, &before));
    fixture_destroy(&fixture);
}

static void test_crc_valid_overlapping_leaf_is_rejected_before_write(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    free_extent_list_t before, actual;
    TEST_ASSERT(read_free_extents(&fixture, &before));
    TEST_ASSERT_EQ(before.count, 2);
    TEST_ASSERT(before.items[0].start + before.items[0].length <=
                before.items[1].start);

    /* Keep sorted keys but make the first interval overlap the second. Use
     * the shared node-layout/CRC oracle so the malformed semantic state still
     * has a valid full-node CRC. */
    uint32_t first_key = bfs_be32(before.items[0].start);
    uint32_t overlapping_length =
        before.items[1].start - before.items[0].start + 1u;
    uint8_t malformed_node[MEM_BLOCK_SIZE];
    TEST_ASSERT(copy_fixture_root(&fixture, malformed_node));
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)malformed_node;
    uint32_t key_count = bfs_be32(header->num_keys);
    bool changed = false;
    for (uint32_t i = 0; i < key_count; i++) {
        if (bfs_load_be32(node_key(&fixture.space.tree, malformed_node, i)) ==
            before.items[0].start) {
            bfs_store_be32(leaf_val(&fixture.space.tree, malformed_node, i),
                           overlapping_length);
            changed = true;
            break;
        }
    }
    TEST_ASSERT(changed);
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&fixture.space.tree,
                                               malformed_node));
    TEST_ASSERT_EQ(memory_write(&fixture.device.bio, fixture.space.tree.root,
                                malformed_node), BFS_OK);
    uint32_t stored_length = 0;
    TEST_ASSERT_EQ(bfs_btree_search(&fixture.space.tree, &first_key,
                                    &stored_length), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(stored_length), overlapping_length);

    bfs_blk_t root = fixture.space.tree.root;
    uint8_t root_bytes[MEM_BLOCK_SIZE];
    TEST_ASSERT(copy_fixture_root(&fixture, root_bytes));
    uint32_t total = fixture.space.total_free;
    bfs_blk_t roving = fixture.space.roving;
    uint32_t reserve_count = fixture.space.reserve_count;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(reserve, fixture.space.reserve, sizeof(reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fixture.space, RUN_COUNT),
                   BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.last_error, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fixture.space.tree.root, root);
    TEST_ASSERT(fixture_root_matches(&fixture, root, root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, total);
    TEST_ASSERT_EQ(fixture.space.roving, roving);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_count);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, reserve, sizeof(reserve));
    TEST_ASSERT(read_free_extents(&fixture, &actual));
    TEST_ASSERT_EQ(actual.count, 2);
    TEST_ASSERT(actual.items[0].start + actual.items[0].length >
                actual.items[1].start);
    fixture_destroy(&fixture);
}

typedef struct {
    uint64_t backup_offset;
    uint8_t primary[BFS_SB_SIZE];
    uint8_t backup[BFS_SB_SIZE];
} superblock_pair_t;

static bool capture_superblock_pair(const memory_device_t *device,
                                    const bfs_superblock_t *sb,
                                    superblock_pair_t *pair)
{
    pair->backup_offset =
        ((uint64_t)bfs_be32(sb->sb_backup_offset_hi) << 32) |
        bfs_be32(sb->sb_backup_offset_lo);
    uint64_t image_size = (uint64_t)device->bio.block_size *
                          device->bio.block_count;
    if (pair->backup_offset == BFS_SB_OFFSET_A ||
        pair->backup_offset > image_size ||
        BFS_SB_SIZE > image_size - pair->backup_offset)
        return false;
    memcpy(pair->primary, device->bytes + BFS_SB_OFFSET_A, BFS_SB_SIZE);
    memcpy(pair->backup, device->bytes + pair->backup_offset, BFS_SB_SIZE);
    return true;
}

static bool superblock_pair_unchanged(const memory_device_t *device,
                                      const superblock_pair_t *pair)
{
    return memcmp(pair->primary, device->bytes + BFS_SB_OFFSET_A,
                  BFS_SB_SIZE) == 0 &&
           memcmp(pair->backup, device->bytes + pair->backup_offset,
                  BFS_SB_SIZE) == 0;
}

static bool mounted_fsck_is_clean(bfs_fs_t *fs)
{
    bfs_fsck_report_t report = {0};
    return bfs_fs_check(fs, false, &report) == BFS_OK && report.errors == 0 &&
           report.warnings == 0 && report.leaked_blocks == 0;
}

static bool format_and_mount(memory_device_t *device, bfs_fs_t *fs)
{
    return bfs_fs_format(&device->bio, "RootShift", 0) == BFS_OK &&
           bfs_fs_mount(fs, &device->bio) == BFS_OK;
}

static bool copy_mounted_root(memory_device_t *device, bfs_blk_t root,
                              uint8_t *bytes)
{
    if (root == BFS_BLK_NULL || root >= device->bio.block_count) return false;
    memcpy(bytes, device->bytes + (size_t)root * device->bio.block_size,
           device->bio.block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return true;
}

static bool mounted_root_unchanged(const memory_device_t *device,
                                   bfs_blk_t root, const uint8_t *bytes)
{
    return root < device->bio.block_count &&
           memcmp(device->bytes + (size_t)root * device->bio.block_size,
                  bytes, device->bio.block_size) == 0;
}

static void test_mounted_retirement_failure_is_sticky_and_unpublished(
    bfs_err_t defer_error)
{
    memory_device_t device;
    bfs_fs_t fs;
    TEST_ASSERT(memory_init(&device, FS_BLOCK_SIZE, FS_BLOCK_COUNT));
    TEST_ASSERT(format_and_mount(&device, &fs));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    superblock_pair_t published_pair;
    TEST_ASSERT(capture_superblock_pair(&device, &fs.txn.sb, &published_pair));
    bfs_superblock_t published_sb = fs.txn.sb;

    /* Fill reserve through the real mounted allocator. This gives the root
     * shift spare a normal allocation source without faulting during refill. */
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fs.freespace), BFS_OK);
    TEST_ASSERT_EQ(fs.freespace.tree.height, 1);
    TEST_ASSERT(fs.freespace.reserve_count >= 20);
    uint64_t root_txn = 0;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&fs.freespace.tree, &root_txn),
                   BFS_OK);
    TEST_ASSERT_EQ(root_txn, fs.live_txn_id);

    /* Advance the uncommitted working transaction generation: the existing
     * root is now an older owned root, while the on-disk commit is untouched. */
    uint64_t live_txn = fs.live_txn_id + 1;
    fs.txn.sb_new.txn_id = bfs_be64(live_txn);
    fs.live_txn_id = live_txn;
    fs.txn.sb_new.free_tree_root = bfs_be32(fs.freespace.tree.root);
    fs.txn.sb_new.free_blocks = bfs_be32(fs.freespace.total_free);
    TEST_ASSERT(root_txn < fs.live_txn_id);

    bfs_blk_t old_root = fs.freespace.tree.root;
    uint8_t old_root_bytes[FS_BLOCK_SIZE];
    TEST_ASSERT(copy_mounted_root(&device, old_root, old_root_bytes));
    uint32_t total_before = fs.freespace.total_free;
    uint32_t reserve_before = fs.freespace.reserve_count;
    sink_fixture_t sink = { .headroom = 1, .defer_error = defer_error };
    fs.freespace.tree.free_sink = make_sink(&sink, 1);
    device.writes = 0;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fs.freespace, RUN_COUNT), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fs.freespace.last_error, defer_error);
    TEST_ASSERT_EQ(sink.defer_calls, 1);
    TEST_ASSERT_EQ(sink.deferred[0], old_root);
    TEST_ASSERT_EQ(device.writes, 1);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT(mounted_root_unchanged(&device, old_root, old_root_bytes));
    TEST_ASSERT_EQ(fs.freespace.total_free, total_before - RUN_COUNT);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, reserve_before - 1);
    TEST_ASSERT_EQ(fs.recovery_error, defer_error);
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&fs.freespace.tree, &root_txn),
                   BFS_OK);
    TEST_ASSERT_EQ(root_txn, live_txn);

    bfs_blk_t failed_root = fs.freespace.tree.root;
    uint8_t failed_root_bytes[FS_BLOCK_SIZE];
    TEST_ASSERT(copy_mounted_root(&device, failed_root, failed_root_bytes));
    uint32_t failed_total = fs.freespace.total_free;
    uint32_t failed_reserve_count = fs.freespace.reserve_count;
    bfs_blk_t failed_reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(failed_reserve, fs.freespace.reserve, sizeof(failed_reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_superblock_t failed_working_sb = fs.txn.sb_new;

    /* Each B-tree operation clears this local diagnostic before its own
     * attempt. The mounted owner's recovery latch must outlive that reset. */
    fs.freespace.tree.free_sink_err = BFS_OK;
    TEST_ASSERT_EQ(fs.recovery_error, defer_error);
    uint32_t writes_after_failure = device.writes;
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), defer_error);
    TEST_ASSERT_EQ(device.writes, writes_after_failure);
    TEST_ASSERT(superblock_pair_unchanged(&device, &published_pair));
    TEST_ASSERT_EQ(fs.txn.sb.txn_id, published_sb.txn_id);

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fs.freespace, RUN_COUNT),
                   BFS_BLK_NULL);
    TEST_ASSERT_EQ(fs.freespace.last_error, defer_error);
    TEST_ASSERT_EQ(device.writes, writes_after_failure);
    TEST_ASSERT_EQ(fs.freespace.tree.root, failed_root);
    TEST_ASSERT(mounted_root_unchanged(&device, failed_root,
                                       failed_root_bytes));
    TEST_ASSERT_EQ(fs.freespace.total_free, failed_total);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, failed_reserve_count);
    TEST_ASSERT_MEM_EQ(fs.freespace.reserve, failed_reserve,
                       sizeof(failed_reserve));

    fs.freespace.in_alloc = true;
    TEST_ASSERT_EQ(fs.freespace.iface.alloc(&fs.freespace.iface),
                   BFS_BLK_NULL);
    fs.freespace.in_alloc = false;
    TEST_ASSERT_EQ(fs.freespace.iface.error(&fs.freespace.iface), defer_error);
    TEST_ASSERT_EQ(device.writes, writes_after_failure);
    TEST_ASSERT_EQ(fs.freespace.tree.root, failed_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, failed_total);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, failed_reserve_count);
    TEST_ASSERT_MEM_EQ(fs.freespace.reserve, failed_reserve,
                       sizeof(failed_reserve));
    TEST_ASSERT_MEM_EQ(&fs.txn.sb_new, &failed_working_sb,
                       sizeof(failed_working_sb));
    TEST_ASSERT(superblock_pair_unchanged(&device, &published_pair));
    bfs_fs_abandon(&fs);

    bfs_fs_t check_fs;
    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&check_fs, &device.bio), BFS_OK);
    TEST_ASSERT_EQ(check_fs.txn.sb.txn_id, published_sb.txn_id);
    TEST_ASSERT(mounted_fsck_is_clean(&check_fs));
    TEST_ASSERT_EQ(bfs_fs_unmount(&check_fs), BFS_OK);
    free(device.bytes);
}

static void test_mounted_retirement_defer_io_failure(void)
{
    test_mounted_retirement_failure_is_sticky_and_unpublished(BFS_ERR_IO);
}

static void test_mounted_retirement_defer_unsupported_failure(void)
{
    test_mounted_retirement_failure_is_sticky_and_unpublished(
        BFS_ERR_UNSUPPORTED);
}

static void test_readonly_mount_rejects_single_and_run_allocations(void)
{
    memory_device_t device;
    bfs_fs_t fs;
    TEST_ASSERT(memory_init(&device, FS_BLOCK_SIZE, FS_BLOCK_COUNT));
    TEST_ASSERT(format_and_mount(&device, &fs));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&fs, &device.bio), BFS_OK);
    TEST_ASSERT(fs.read_only);
    TEST_ASSERT_EQ(fs.freespace.tree.height, 1);
    bfs_blk_t root = fs.freespace.tree.root;
    uint8_t root_bytes[FS_BLOCK_SIZE];
    TEST_ASSERT(copy_mounted_root(&device, root, root_bytes));
    uint32_t total = fs.freespace.total_free;
    uint32_t reserve_count = fs.freespace.reserve_count;
    bfs_blk_t roving = fs.freespace.roving;
    device.writes = 0;

    TEST_ASSERT_EQ(bfs_freespace_alloc(&fs.freespace, 1), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fs.freespace.last_error, BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(bfs_freespace_alloc(&fs.freespace, RUN_COUNT), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fs.freespace.last_error, BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(device.writes, 0);
    TEST_ASSERT_EQ(fs.freespace.tree.root, root);
    TEST_ASSERT(mounted_root_unchanged(&device, root, root_bytes));
    TEST_ASSERT_EQ(fs.freespace.total_free, total);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, reserve_count);
    TEST_ASSERT_EQ(fs.freespace.roving, roving);

    /* Exercise the lower-level recursive-COW allocator guard while its normal
     * in_alloc route would otherwise spend reserve or emergency stock. */
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(reserve, fs.freespace.reserve, sizeof(reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_superblock_t working_sb = fs.txn.sb_new;
    fs.freespace.in_alloc = true;
    TEST_ASSERT_EQ(fs.freespace.iface.alloc(&fs.freespace.iface),
                   BFS_BLK_NULL);
    fs.freespace.in_alloc = false;
    TEST_ASSERT_EQ(fs.freespace.iface.error(&fs.freespace.iface),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(device.writes, 0);
    TEST_ASSERT_EQ(fs.freespace.tree.root, root);
    TEST_ASSERT(mounted_root_unchanged(&device, root, root_bytes));
    TEST_ASSERT_EQ(fs.freespace.total_free, total);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, reserve_count);
    TEST_ASSERT_MEM_EQ(fs.freespace.reserve, reserve, sizeof(reserve));
    TEST_ASSERT_MEM_EQ(&fs.txn.sb_new, &working_sb, sizeof(working_sb));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    free(device.bytes);
}

static void test_large_append_sync_readonly_remount_and_strict_fsck(void)
{
    const uint32_t data_size = APPEND_TEST_SIZE;
    memory_device_t device;
    bfs_fs_t fs;
    TEST_ASSERT(memory_init(&device, FS_BLOCK_SIZE, FS_BLOCK_COUNT));
    TEST_ASSERT(format_and_mount(&device, &fs));
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "large", 5, &ino),
                   BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);

    uint8_t *expected = malloc(data_size);
    uint8_t *actual = malloc(data_size);
    TEST_ASSERT(expected != NULL && actual != NULL);
    for (uint32_t i = 0; i < data_size; i++)
        expected[i] = (uint8_t)((i * 37u + (i >> 16) * 19u) ^ (i >> 7));
    for (uint32_t offset = 0; offset < data_size;
         offset += APPEND_CHUNK_SIZE) {
        TEST_ASSERT_EQ(bfs_file_append(&file, expected + offset,
                                       APPEND_CHUNK_SIZE),
                       (int32_t)APPEND_CHUNK_SIZE);
    }
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(mounted_fsck_is_clean(&fs));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&fs, &device.bio), BFS_OK);
    TEST_ASSERT(mounted_fsck_is_clean(&fs));
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&file, actual, data_size), (int32_t)data_size);
    TEST_ASSERT_MEM_EQ(actual, expected, data_size);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    free(actual);
    free(expected);
    free(device.bytes);
}

TEST_SUITE_BEGIN("Atomic root-leaf allocator run shift")
    TEST_RUN(test_first_fit_run_carve_emits_one_root_leaf_write);
    TEST_RUN(test_data_alloc_ascends_from_goal);
    TEST_RUN(test_data_alloc_goal_inside_end_and_taken);
    TEST_RUN(test_roving_wrap_skips_short_first_fit_extent);
    TEST_RUN(test_exact_fit_and_single_block_keep_legacy_selection);
    TEST_RUN(test_deeper_free_tree_keeps_legacy_mutation_path);
    TEST_RUN(test_read_failure_does_not_mutate_allocator_state);
    TEST_RUN(test_read_failure_cuts_cover_every_pre_swap_read);
    TEST_RUN(test_partial_leaf_write_failure_is_atomic_and_retryable);
    TEST_RUN(test_pending_headroom_again_precedes_root_swap);
    TEST_RUN(test_current_and_older_root_retirement_ownership);
    TEST_RUN(test_allocator_scratch_exhaustion_preserves_root);
    TEST_RUN(test_crc_valid_overlapping_leaf_is_rejected_before_write);
    TEST_RUN(test_mounted_retirement_defer_io_failure);
    TEST_RUN(test_mounted_retirement_defer_unsupported_failure);
    TEST_RUN(test_readonly_mount_rejects_single_and_run_allocations);
    TEST_RUN(test_large_append_sync_readonly_remount_and_strict_fsck);
TEST_SUITE_END()
