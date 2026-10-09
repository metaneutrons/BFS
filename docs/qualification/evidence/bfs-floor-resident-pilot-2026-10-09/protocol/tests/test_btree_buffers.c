/* SPDX-License-Identifier: MPL-2.0 */
/* Cached B-tree operations must return every temporary buffer lease. */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "block_device_emu.h"

#include <stdlib.h>
#include <unistd.h>

#define TEST_IMG "test_btree_buffers.img"
#define TEST_BLOCK_COUNT 4096u
#define MAX_TEST_KEYS 600u
#define MAX_MOUNTED_TEST_DATA (2u * 4096u + 19u)

typedef struct {
    bfs_bio_t bio;
    bfs_bio_t *inner;
    bfs_cache_t *cache;
    bfs_fs_t *mounted_fs;
    bool fail_next_read;
    uint32_t fail_read_countdown;
    bool fail_next_write;
    uint32_t read_calls;
    uint32_t failed_reads;
    uint32_t failed_writes;
    uint8_t max_busy_on_write[TEST_BLOCK_COUNT];
    uint32_t max_busy_during_io;
    uint32_t multiple_busy_io_calls;
    uint32_t nested_allocator_io_calls;
    bool lease_alias_seen;
} observing_bio_t;

typedef struct {
    bfs_allocator_t iface;
    bfs_blk_t next_block;
    bfs_blk_t block_limit;
    bfs_blk_t returned[TEST_BLOCK_COUNT];
    uint32_t returned_count;
} reusable_allocator_t;

typedef struct {
    bfs_bio_t *device;
    observing_bio_t observed;
    bfs_cache_t cache;
    reusable_allocator_t allocator;
    bfs_btree_t tree;
} fixture_t;

typedef struct {
    bfs_bio_t *device;
    observing_bio_t observed;
    bfs_cache_t cache;
    bfs_fs_t fs;
} mounted_fixture_t;

static int compare_u32(const void *left, const void *right)
{
    uint32_t a = bfs_load_be32(left);
    uint32_t b = bfs_load_be32(right);
    return a < b ? -1 : a > b ? 1 : 0;
}

static const bfs_btree_ops_t u32_ops = {
    .key_compare = compare_u32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = true,
};

static void make_u32(uint32_t *out, uint32_t value)
{
    *out = bfs_be32(value);
}

static uint32_t get_u32(const void *value)
{
    return bfs_load_be32(value);
}

static uint32_t observe_busy_leases(observing_bio_t *observed)
{
    bfs_cache_t *cache = observed->cache;
    uint32_t busy = 0;
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++) {
        const bfs_cache_scratch_slot_t *slot = &cache->scratch[i];
        if (!slot->busy) continue;
        busy++;
        if (!slot->data) observed->lease_alias_seen = true;
        for (uint32_t j = 0; j < i; j++) {
            if (cache->scratch[j].busy &&
                cache->scratch[j].data == slot->data)
                observed->lease_alias_seen = true;
        }
        for (uint32_t j = 0; j < cache->num_slots; j++) {
            if (cache->slots[j].data == slot->data)
                observed->lease_alias_seen = true;
        }
    }
    if (busy > observed->max_busy_during_io)
        observed->max_busy_during_io = busy;
    if (busy >= 2) observed->multiple_busy_io_calls++;
    if (busy >= 2 && observed->mounted_fs &&
        observed->mounted_fs->freespace.in_alloc)
        observed->nested_allocator_io_calls++;
    return busy;
}

static bfs_err_t observed_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    observing_bio_t *observed = (observing_bio_t *)bio;
    observed->read_calls++;
    (void)observe_busy_leases(observed);
    if (observed->fail_next_read) {
        observed->fail_next_read = false;
        observed->failed_reads++;
        return BFS_ERR_IO;
    }
    if (observed->fail_read_countdown > 0 &&
        --observed->fail_read_countdown == 0) {
        observed->failed_reads++;
        return BFS_ERR_IO;
    }
    return bfs_bio_read(observed->inner, block, buffer);
}

static bfs_err_t observed_write(bfs_bio_t *bio, bfs_blk_t block,
                                const void *buffer)
{
    observing_bio_t *observed = (observing_bio_t *)bio;
    uint32_t busy = observe_busy_leases(observed);
    if (block < TEST_BLOCK_COUNT && busy > observed->max_busy_on_write[block])
        observed->max_busy_on_write[block] = (uint8_t)busy;
    if (observed->fail_next_write) {
        observed->fail_next_write = false;
        observed->failed_writes++;
        return BFS_ERR_IO;
    }
    return bfs_bio_write(observed->inner, block, buffer);
}

static bfs_err_t observed_sync(bfs_bio_t *bio)
{
    observing_bio_t *observed = (observing_bio_t *)bio;
    return bfs_bio_sync(observed->inner);
}

static void observed_close(bfs_bio_t *bio)
{
    observing_bio_t *observed = (observing_bio_t *)bio;
    bfs_bio_close(observed->inner);
}

static const bfs_bio_ops_t observing_ops = {
    .read_block = observed_read,
    .write_block = observed_write,
    .sync = observed_sync,
    .close = observed_close,
};

static bfs_blk_t allocator_alloc(bfs_allocator_t *iface)
{
    reusable_allocator_t *allocator = iface->ctx;
    if (allocator->returned_count > 0)
        return allocator->returned[--allocator->returned_count];
    if (allocator->next_block >= allocator->block_limit)
        return BFS_BLK_NULL;
    return allocator->next_block++;
}

static bfs_err_t allocator_dealloc(bfs_allocator_t *iface, bfs_blk_t block)
{
    reusable_allocator_t *allocator = iface->ctx;
    if (allocator->returned_count >= TEST_BLOCK_COUNT) return BFS_ERR_NOSPC;
    allocator->returned[allocator->returned_count++] = block;
    return BFS_OK;
}

static bfs_err_t allocator_error(bfs_allocator_t *iface)
{
    (void)iface;
    return BFS_ERR_NOSPC;
}

static bool leases_are_clear(const bfs_cache_t *cache)
{
    for (uint32_t i = 0; i < BFS_CACHE_SCRATCH_SLOTS; i++)
        if (cache->scratch[i].busy) return false;
    return true;
}

static bool fixture_open(fixture_t *fixture, uint32_t block_size)
{
    memset(fixture, 0, sizeof(*fixture));
    unlink(TEST_IMG);
    fixture->device = bio_emu_create(TEST_IMG, block_size, TEST_BLOCK_COUNT);
    if (!fixture->device) return false;

    fixture->observed.bio.ops = &observing_ops;
    fixture->observed.bio.block_size = block_size;
    fixture->observed.bio.block_count = TEST_BLOCK_COUNT;
    fixture->observed.inner = fixture->device;
    fixture->observed.cache = &fixture->cache;

    if (bfs_cache_init(&fixture->cache, &fixture->observed.bio, 2) != BFS_OK)
        goto fail;
    bfs_cache_set_node_write_retention(&fixture->cache, true);

    fixture->allocator.iface.alloc = allocator_alloc;
    fixture->allocator.iface.dealloc = allocator_dealloc;
    fixture->allocator.iface.error = allocator_error;
    fixture->allocator.iface.ctx = &fixture->allocator;
    fixture->allocator.next_block = 2;
    fixture->allocator.block_limit = TEST_BLOCK_COUNT;

    if (bfs_btree_init(&fixture->tree, &fixture->cache.bio,
                       &fixture->allocator.iface, &u32_ops,
                       BFS_BLK_NULL, 1) != BFS_OK)
        goto fail;
    if (!leases_are_clear(&fixture->cache)) goto fail;
    return true;

fail:
    bfs_cache_destroy(&fixture->cache);
    bfs_bio_close(fixture->device);
    unlink(TEST_IMG);
    fixture->device = NULL;
    return false;
}

static void fixture_close(fixture_t *fixture)
{
    bfs_cache_destroy(&fixture->cache);
    bfs_bio_close(fixture->device);
    unlink(TEST_IMG);
}

static bool mounted_fixture_open(mounted_fixture_t *fixture,
                                 uint32_t block_size)
{
    memset(fixture, 0, sizeof(*fixture));
    unlink(TEST_IMG);
    fixture->device = bio_emu_create(TEST_IMG, block_size, TEST_BLOCK_COUNT);
    if (!fixture->device) return false;

    fixture->observed.bio.ops = &observing_ops;
    fixture->observed.bio.block_size = block_size;
    fixture->observed.bio.block_count = TEST_BLOCK_COUNT;
    fixture->observed.inner = fixture->device;
    fixture->observed.cache = &fixture->cache;
    if (bfs_cache_init(&fixture->cache, &fixture->observed.bio, 2) != BFS_OK)
        goto fail;
    bfs_cache_set_node_write_retention(&fixture->cache, true);

    if (bfs_fs_format(&fixture->cache.bio, "BufferLease", 0) != BFS_OK ||
        !leases_are_clear(&fixture->cache))
        goto fail;
    bfs_cache_invalidate(&fixture->cache);
    if (!leases_are_clear(&fixture->cache) ||
        bfs_fs_mount(&fixture->fs, &fixture->cache.bio) != BFS_OK ||
        !leases_are_clear(&fixture->cache))
        goto fail;
    fixture->observed.mounted_fs = &fixture->fs;
    return true;

fail:
    if (fixture->fs.mounted) bfs_fs_abandon(&fixture->fs);
    bfs_cache_destroy(&fixture->cache);
    bfs_bio_close(fixture->device);
    unlink(TEST_IMG);
    fixture->device = NULL;
    return false;
}

static void mounted_fixture_close(mounted_fixture_t *fixture)
{
    bfs_cache_destroy(&fixture->cache);
    bfs_bio_close(fixture->device);
    unlink(TEST_IMG);
}

typedef struct {
    uint32_t expected_count;
    uint32_t seen;
    bool ordered;
    bool values_match;
} scan_check_t;

static bool check_scanned_pair(const void *key, const void *value, void *ctx)
{
    scan_check_t *check = (scan_check_t *)ctx;
    uint32_t expected_key = check->seen;
    uint32_t expected_value = expected_key * 17u + 3u;
    if (check->seen >= check->expected_count || get_u32(key) != expected_key)
        check->ordered = false;
    if (check->seen >= check->expected_count ||
        get_u32(value) != expected_value)
        check->values_match = false;
    check->seen++;
    return true;
}

static void count_node(bfs_blk_t block, void *ctx)
{
    uint32_t *count = ctx;
    (void)block;
    (*count)++;
}

static bool tree_matches_prefix(fixture_t *fixture, uint32_t count)
{
    scan_check_t scan = {
        .expected_count = count,
        .ordered = true,
        .values_match = true,
    };
    bfs_err_t err = bfs_btree_scan(&fixture->tree, NULL,
                                  check_scanned_pair, &scan);
    return err == BFS_OK && scan.seen == count && scan.ordered &&
           scan.values_match && leases_are_clear(&fixture->cache);
}

static void test_cached_tree_lifecycle(uint32_t block_size)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture, block_size));
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    uint32_t capacity = leaf_max_keys(&fixture.tree);
    uint32_t count = capacity + 3u;
    TEST_ASSERT(count <= MAX_TEST_KEYS);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key, value;
        make_u32(&key, i);
        make_u32(&value, i * 17u + 3u);
        if (i == capacity) {
            bfs_blk_t old_root = fixture.tree.root;
            TEST_ASSERT_EQ(fixture.tree.height, 1);
            uint32_t writes_before = fixture.observed.failed_writes;
            fixture.observed.fail_next_write = true;
            TEST_ASSERT_EQ(bfs_btree_insert(&fixture.tree, &key, &value),
                           BFS_ERR_IO);
            TEST_ASSERT_EQ(fixture.observed.failed_writes, writes_before + 1);
            TEST_ASSERT_EQ(fixture.tree.root, old_root);
            TEST_ASSERT_EQ(fixture.tree.height, 1);
            TEST_ASSERT(leases_are_clear(&fixture.cache));
            TEST_ASSERT(tree_matches_prefix(&fixture, capacity));
        }
        TEST_ASSERT_EQ(bfs_btree_insert(&fixture.tree, &key, &value), BFS_OK);
        TEST_ASSERT(leases_are_clear(&fixture.cache));
    }
    TEST_ASSERT_EQ(fixture.tree.height, 2);

    scan_check_t scan = {
        .expected_count = count,
        .ordered = true,
        .values_match = true,
    };
    TEST_ASSERT_EQ(bfs_btree_scan(&fixture.tree, NULL,
                                  check_scanned_pair, &scan), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(scan.seen, count);
    TEST_ASSERT(scan.ordered && scan.values_match);

    uint32_t walked = 0;
    TEST_ASSERT_EQ(bfs_btree_walk_nodes(&fixture.tree, count_node, &walked),
                   BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(walked >= 2);

    /* A recursive walk keeps its parent buffer leased while reading a child.
     * Fail the second cache miss to witness two live leases unwinding. */
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    uint32_t read_calls_before = fixture.observed.read_calls;
    uint32_t failed_reads_before = fixture.observed.failed_reads;
    fixture.observed.max_busy_during_io = 0;
    fixture.observed.multiple_busy_io_calls = 0;
    fixture.observed.fail_read_countdown = 2;
    walked = 0;
    TEST_ASSERT_EQ(bfs_btree_walk_nodes(&fixture.tree, count_node, &walked),
                   BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.observed.read_calls, read_calls_before + 2);
    TEST_ASSERT_EQ(fixture.observed.failed_reads, failed_reads_before + 1);
    TEST_ASSERT(fixture.observed.max_busy_during_io >= 2);
    TEST_ASSERT(fixture.observed.multiple_busy_io_calls > 0);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    walked = 0;
    TEST_ASSERT_EQ(bfs_btree_walk_nodes(&fixture.tree, count_node, &walked),
                   BFS_OK);
    TEST_ASSERT(walked >= 2);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    uint32_t min_leaf = capacity / 2u; /* btree.c: leaf_min = leaf_max / 2 */
    TEST_ASSERT_EQ(capacity % 2u, 0);
    uint32_t right_count_after_split = capacity - capacity / 2u + 3u;
    uint32_t right_deletes_to_min = right_count_after_split - min_leaf;
    for (uint32_t i = 0; i < right_deletes_to_min; i++) {
        uint32_t key;
        make_u32(&key, count - 1u - i);
        TEST_ASSERT_EQ(bfs_btree_delete(&fixture.tree, &key), BFS_OK);
        TEST_ASSERT(leases_are_clear(&fixture.cache));
    }
    uint32_t remaining = count - right_deletes_to_min;
    TEST_ASSERT_EQ(fixture.tree.height, 2);
    TEST_ASSERT(tree_matches_prefix(&fixture, remaining));

    /* Removing the next rightmost key leaves leaf_min - 1 entries. The left
     * sibling is at leaf_min, so this is a merge, not a borrow. */
    uint32_t merge_key;
    make_u32(&merge_key, remaining - 1u);
    bfs_blk_t root_before_merge_error = fixture.tree.root;
    uint32_t writes_before_merge_error = fixture.observed.failed_writes;
    fixture.observed.max_busy_during_io = 0;
    fixture.observed.fail_next_write = true;
    TEST_ASSERT_EQ(bfs_btree_delete(&fixture.tree, &merge_key), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.observed.failed_writes, writes_before_merge_error + 1);
    TEST_ASSERT_EQ(fixture.tree.root, root_before_merge_error);
    TEST_ASSERT_EQ(fixture.tree.height, 2);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(tree_matches_prefix(&fixture, remaining));
    TEST_ASSERT_EQ(fixture.tree.root, root_before_merge_error);
    TEST_ASSERT_EQ(fixture.tree.height, 2);

    TEST_ASSERT_EQ(bfs_btree_delete(&fixture.tree, &merge_key), BFS_OK);
    remaining--;
    TEST_ASSERT_EQ(fixture.tree.height, 1);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(tree_matches_prefix(&fixture, remaining));

    for (uint32_t i = remaining; i > 0; i--) {
        uint32_t key;
        make_u32(&key, i - 1u);
        TEST_ASSERT_EQ(bfs_btree_delete(&fixture.tree, &key), BFS_OK);
        TEST_ASSERT(leases_are_clear(&fixture.cache));
    }
    TEST_ASSERT_EQ(fixture.tree.root, BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.tree.height, 0);
    walked = 0;
    TEST_ASSERT_EQ(bfs_btree_walk_nodes(&fixture.tree, count_node, &walked),
                   BFS_OK);
    TEST_ASSERT_EQ(walked, 0);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    uint32_t key, value, result;
    make_u32(&key, 7);
    make_u32(&value, 10);
    TEST_ASSERT_EQ(bfs_btree_insert(&fixture.tree, &key, &value), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    failed_reads_before = fixture.observed.failed_reads;
    fixture.observed.fail_next_read = true;
    TEST_ASSERT_EQ(bfs_btree_search(&fixture.tree, &key, &result), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.observed.failed_reads, failed_reads_before + 1);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(bfs_btree_search(&fixture.tree, &key, &result), BFS_OK);
    TEST_ASSERT_EQ(get_u32(&result), 10);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    bfs_blk_t root_before_write_error = fixture.tree.root;
    uint32_t replacement_value;
    make_u32(&replacement_value, 11);
    uint32_t failed_writes_before = fixture.observed.failed_writes;
    fixture.observed.fail_next_write = true;
    TEST_ASSERT_EQ(bfs_btree_update(&fixture.tree, &key, &replacement_value),
                   BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.observed.failed_writes, failed_writes_before + 1);
    TEST_ASSERT_EQ(fixture.tree.root, root_before_write_error);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(bfs_btree_search(&fixture.tree, &key, &result), BFS_OK);
    TEST_ASSERT_EQ(get_u32(&result), 10);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    /* The production root-leaf replacement primitive holds old and new node
     * buffers concurrently. First fail its write to prove both leases return
     * on abort, then retry and observe both real leases at the underlying BIO. */
    uint32_t keys[3], values[3];
    for (uint32_t i = 0; i < 3; i++) {
        make_u32(&keys[i], 20u + i);
        make_u32(&values[i], 200u + i);
    }
    bfs_blk_t root_before_replace = fixture.tree.root;
    failed_writes_before = fixture.observed.failed_writes;
    fixture.observed.max_busy_during_io = 0;
    fixture.observed.multiple_busy_io_calls = 0;
    fixture.observed.fail_next_write = true;
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&fixture.tree, keys, values, 3),
                   BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.observed.failed_writes, failed_writes_before + 1);
    TEST_ASSERT_EQ(fixture.tree.root, root_before_replace);
    TEST_ASSERT(fixture.observed.max_busy_during_io >= 2);
    TEST_ASSERT(fixture.observed.multiple_busy_io_calls > 0);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(!fixture.observed.lease_alias_seen);
    TEST_ASSERT_EQ(bfs_btree_search(&fixture.tree, &key, &result), BFS_OK);
    TEST_ASSERT_EQ(get_u32(&result), 10);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    fixture.observed.max_busy_during_io = 0;
    fixture.observed.multiple_busy_io_calls = 0;
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&fixture.tree, keys, values, 3),
                   BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(fixture.observed.max_busy_during_io >= 2);
    TEST_ASSERT(fixture.observed.multiple_busy_io_calls > 0);
    TEST_ASSERT(!fixture.observed.lease_alias_seen);

    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQ(bfs_btree_search(&fixture.tree, &keys[i], &result),
                       BFS_OK);
        TEST_ASSERT_EQ(get_u32(&result), 200u + i);
        TEST_ASSERT(leases_are_clear(&fixture.cache));
    }
    walked = 0;
    TEST_ASSERT_EQ(bfs_btree_walk_nodes(&fixture.tree, count_node, &walked),
                   BFS_OK);
    TEST_ASSERT_EQ(walked, 1);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(!fixture.observed.lease_alias_seen);

    fixture_close(&fixture);
}

static void test_cached_tree_lifecycle_1024(void)
{
    test_cached_tree_lifecycle(1024u);
}

static void test_cached_tree_lifecycle_4096(void)
{
    test_cached_tree_lifecycle(4096u);
}

static bool fsck_is_clean(bfs_fs_t *fs)
{
    bfs_fsck_report_t report = {0};
    return bfs_fs_check(fs, false, &report) == BFS_OK &&
           report.errors == 0 && report.warnings == 0 &&
           report.leaked_blocks == 0;
}

static void test_mounted_cached_free_tree_cycle(uint32_t block_size)
{
    mounted_fixture_t fixture;
    TEST_ASSERT(mounted_fixture_open(&fixture, block_size));
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "lease", 5, &ino), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    uint8_t expected[MAX_MOUNTED_TEST_DATA];
    uint8_t actual[sizeof(expected)];
    uint32_t length = 2u * block_size + 19u;
    TEST_ASSERT(length <= sizeof(expected));
    for (uint32_t i = 0; i < length; i++)
        expected[i] = (uint8_t)(i * 37u + (i >> 7) + 11u);

    TEST_ASSERT_EQ(bfs_file_write(&file, expected, length), (int32_t)length);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(bfs_file_read(&file, actual, length), (int32_t)length);
    TEST_ASSERT_MEM_EQ(actual, expected, length);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(fixture.observed.nested_allocator_io_calls > 0);
    TEST_ASSERT(!fixture.observed.lease_alias_seen);

    TEST_ASSERT_EQ(fixture.fs.freespace.tree.height, 1);
    TEST_ASSERT(fixture.fs.pending_count > 0);
    bfs_blk_t free_root_before_sync = fixture.fs.freespace.tree.root;
    fixture.observed.max_busy_during_io = 0;
    fixture.observed.multiple_busy_io_calls = 0;
    memset(fixture.observed.max_busy_on_write, 0,
           sizeof(fixture.observed.max_busy_on_write));
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(fixture.fs.pending_count, 0);
    TEST_ASSERT(fixture.fs.freespace.tree.root != free_root_before_sync);
    TEST_ASSERT(fixture.fs.freespace.tree.root < TEST_BLOCK_COUNT);
    TEST_ASSERT(fixture.observed.max_busy_on_write[
                    fixture.fs.freespace.tree.root] >= 2);
    TEST_ASSERT(fixture.observed.max_busy_during_io >= 2);
    TEST_ASSERT(fixture.observed.multiple_busy_io_calls > 0);
    TEST_ASSERT(fsck_is_clean(&fixture.fs));
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    memset(actual, 0, sizeof(actual));
    TEST_ASSERT_EQ(bfs_file_read(&file, actual, length), (int32_t)length);
    TEST_ASSERT_MEM_EQ(actual, expected, length);
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    TEST_ASSERT_EQ(bfs_fs_delete_file(&fixture.fs, BFS_ROOT_INO,
                                      "lease", 5), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_OK);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    uint32_t found_ino = 0, found_type = 0;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fixture.fs.dir_tree, BFS_ROOT_INO,
                                  "lease", 5, &found_ino, &found_type),
                   BFS_ERR_NOTFOUND);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(fsck_is_clean(&fixture.fs));
    TEST_ASSERT(leases_are_clear(&fixture.cache));

    TEST_ASSERT_EQ(bfs_fs_unmount(&fixture.fs), BFS_OK);
    TEST_ASSERT(!fixture.fs.mounted);
    TEST_ASSERT(leases_are_clear(&fixture.cache));
    TEST_ASSERT(!fixture.observed.lease_alias_seen);
    mounted_fixture_close(&fixture);
}

static void test_mounted_cached_free_tree_cycle_1024(void)
{
    test_mounted_cached_free_tree_cycle(1024u);
}

static void test_mounted_cached_free_tree_cycle_4096(void)
{
    test_mounted_cached_free_tree_cycle(4096u);
}

TEST_SUITE_BEGIN("Cached B-tree temporary buffers")
    TEST_RUN(test_cached_tree_lifecycle_1024);
    TEST_RUN(test_cached_tree_lifecycle_4096);
    TEST_RUN(test_mounted_cached_free_tree_cycle_1024);
    TEST_RUN(test_mounted_cached_free_tree_cycle_4096);
TEST_SUITE_END()
