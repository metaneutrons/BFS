/* SPDX-License-Identifier: MPL-2.0 */
/* Exclusive resume coverage for directory enumeration cursors. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"
#include "bfs_dir.h"
#include "block_device_emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CURSOR_IMAGE "test_dir_exclusive_cursor.img"
#define CURSOR_BLOCK_SIZE 4096u
#define CURSOR_BLOCK_COUNT 8192u
#define CURSOR_CACHE_SLOTS 64u
#define CURSOR_PARENT 10u
#define CURSOR_OTHER_PARENT 11u
#define CURSOR_ENTRY_COUNT 420u
#define CURSOR_OTHER_COUNT 3u
#define CURSOR_NAME_CAPACITY 24u

typedef struct {
    bfs_bio_t *device;
    bfs_cache_t cache;
    bfs_freespace_t space;
    bfs_dir_tree_t dirs;
    bool cache_ready;
    bool space_ready;
    bool dirs_ready;
} cursor_fixture_t;

typedef struct {
    char name[CURSOR_NAME_CAPACITY];
    uint8_t name_len;
    uint32_t inode_nr;
    uint32_t entry_type;
} scan_entry_t;

typedef struct {
    scan_entry_t *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t limit;
} scan_log_t;

typedef struct {
    uint32_t calls;
    uint32_t inode_nr;
} raw_value_log_t;

typedef struct {
    cursor_fixture_t *fixture;
    scan_log_t log;
    bool changed;
    bfs_err_t mutation_error;
} mutate_callback_ctx_t;

typedef struct {
    cursor_fixture_t *fixture;
    bfs_blk_t root;
    uint8_t *root_image;
    uint32_t calls;
    bfs_err_t write_error;
} invalidate_callback_ctx_t;

static scan_entry_t expected_entries[CURSOR_ENTRY_COUNT];
static scan_entry_t resumed_entries[CURSOR_ENTRY_COUNT];
static uint32_t compare_calls;
static const bfs_btree_ops_t *compare_base_ops;

static int counted_dir_compare(const void *left, const void *right)
{
    compare_calls++;
    return compare_base_ops->key_compare(left, right);
}

static uint64_t saturated_epoch(bfs_bio_t *bio)
{
    (void)bio;
    return UINT64_MAX;
}

static bool fixture_open(cursor_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    (void)unlink(CURSOR_IMAGE);
    fixture->device = bio_emu_create(CURSOR_IMAGE, CURSOR_BLOCK_SIZE,
                                     CURSOR_BLOCK_COUNT);
    if (!fixture->device) return false;
    if (bfs_cache_init(&fixture->cache, fixture->device,
                       CURSOR_CACHE_SLOTS) != BFS_OK)
        goto fail;
    fixture->cache_ready = true;
    if (bfs_freespace_init(&fixture->space, &fixture->cache.bio,
                           BFS_BLK_NULL, 1) != BFS_OK)
        goto fail;
    fixture->space_ready = true;
    if (bfs_freespace_add(&fixture->space, 2,
                          CURSOR_BLOCK_COUNT - 2) != BFS_OK)
        goto fail;
    if (bfs_freespace_refill_reserve(&fixture->space) != BFS_OK)
        goto fail;
    if (bfs_dir_init(&fixture->dirs, &fixture->cache.bio,
                     bfs_freespace_allocator(&fixture->space),
                     BFS_BLK_NULL, 1) != BFS_OK)
        goto fail;
    fixture->dirs_ready = true;
    return true;

fail:
    if (fixture->cache_ready) bfs_cache_destroy(&fixture->cache);
    if (fixture->device) bfs_bio_close(fixture->device);
    fixture->device = NULL;
    (void)unlink(CURSOR_IMAGE);
    return false;
}

static void fixture_close(cursor_fixture_t *fixture)
{
    (void)fixture->space_ready;
    (void)fixture->dirs_ready;
    if (fixture->cache_ready) {
        bfs_cache_destroy(&fixture->cache);
        fixture->cache_ready = false;
    }
    if (fixture->device) {
        bfs_bio_close(fixture->device);
        fixture->device = NULL;
    }
    (void)unlink(CURSOR_IMAGE);
}

static bool insert_entries(cursor_fixture_t *fixture, uint32_t parent,
                           uint32_t count, uint32_t inode_base)
{
    char name[CURSOR_NAME_CAPACITY];
    for (uint32_t i = 0; i < count; i++) {
        int length = snprintf(name, sizeof(name), "entry%04u", i);
        if (length <= 0 || (size_t)length >= sizeof(name)) return false;
        if (bfs_dir_insert(&fixture->dirs, parent, name, (uint8_t)length,
                           inode_base + i, BFS_INODE_FILE) != BFS_OK)
            return false;
    }
    return true;
}

static bool insert_other_parent(cursor_fixture_t *fixture)
{
    for (uint32_t i = 0; i < CURSOR_OTHER_COUNT; i++) {
        char name[CURSOR_NAME_CAPACITY];
        int length = snprintf(name, sizeof(name), "peer%u", i);
        if (length <= 0 || (size_t)length >= sizeof(name)) return false;
        if (bfs_dir_insert(&fixture->dirs, CURSOR_OTHER_PARENT, name,
                           (uint8_t)length, 9000u + i,
                           BFS_INODE_FILE) != BFS_OK)
            return false;
    }
    return true;
}

static bool append_entry(scan_log_t *log, const char *name, uint8_t name_len,
                         uint32_t inode_nr, uint32_t entry_type)
{
    if (log->count >= log->capacity || name_len >= CURSOR_NAME_CAPACITY)
        return false;
    scan_entry_t *entry = &log->entries[log->count++];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, name_len);
    entry->name_len = name_len;
    entry->inode_nr = inode_nr;
    entry->entry_type = entry_type;
    return true;
}

static bool collect_all(const char *name, uint8_t name_len, uint32_t inode_nr,
                        uint32_t entry_type, void *ctx)
{
    return append_entry((scan_log_t *)ctx, name, name_len, inode_nr,
                        entry_type);
}

static bool collect_one(const char *name, uint8_t name_len, uint32_t inode_nr,
                        uint32_t entry_type, void *ctx)
{
    scan_log_t *log = (scan_log_t *)ctx;
    (void)append_entry(log, name, name_len, inode_nr, entry_type);
    return false;
}

static bool collect_raw_value(const void *key, const void *value, void *ctx)
{
    (void)key;
    raw_value_log_t *log = (raw_value_log_t *)ctx;
    log->calls++;
    log->inode_nr = bfs_load_be32((const uint8_t *)value + BFS_DIR_HEAD_INODE);
    return false;
}

static bool count_entry_callback(const char *name, uint8_t name_len,
                                uint32_t inode_nr, uint32_t entry_type,
                                void *ctx)
{
    (void)name;
    (void)name_len;
    (void)inode_nr;
    (void)entry_type;
    (*(uint32_t *)ctx)++;
    return true;
}

static bool invalidate_cache_in_callback(const char *name, uint8_t name_len,
                                         uint32_t inode_nr,
                                         uint32_t entry_type, void *ctx)
{
    (void)name;
    (void)name_len;
    (void)inode_nr;
    (void)entry_type;
    invalidate_callback_ctx_t *mutation =
        (invalidate_callback_ctx_t *)ctx;
    mutation->calls++;
    hdr_of(mutation->root_image)->crc32 ^= bfs_be32(1);
    mutation->write_error = bfs_bio_write(mutation->fixture->device,
                                          mutation->root,
                                          mutation->root_image);
    if (mutation->write_error == BFS_OK)
        bfs_cache_invalidate(&mutation->fixture->cache);
    return false;
}

static bool scan_entries_equal(const scan_entry_t *left,
                               const scan_entry_t *right)
{
    return left->name_len == right->name_len &&
           left->inode_nr == right->inode_nr &&
           left->entry_type == right->entry_type &&
           memcmp(left->name, right->name, left->name_len) == 0;
}

static void test_multileaf_resume_is_exclusive_and_visits_each_entry_once(void)
{
    cursor_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    TEST_ASSERT(insert_entries(&fixture, CURSOR_PARENT, CURSOR_ENTRY_COUNT,
                               1000));
    TEST_ASSERT(insert_other_parent(&fixture));
    TEST_ASSERT(fixture.dirs.tree.height > 1);

    scan_log_t expected = {
        .entries = expected_entries,
        .capacity = CURSOR_ENTRY_COUNT,
    };
    TEST_ASSERT_EQ(bfs_dir_scan(&fixture.dirs, CURSOR_PARENT, collect_all,
                                &expected), BFS_OK);
    TEST_ASSERT_EQ(expected.count, CURSOR_ENTRY_COUNT);

    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    scan_log_t resumed = {
        .entries = resumed_entries,
        .capacity = CURSOR_ENTRY_COUNT,
    };
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor,
                                      CURSOR_PARENT, NULL, 0, collect_one,
                                      &resumed), BFS_OK);
    TEST_ASSERT_EQ(resumed.count, 1);
    TEST_ASSERT(cursor.stopped);
    TEST_ASSERT(num_keys(cursor.leaf) > cursor.stop_index + 1u);

    uint8_t first_key[BFS_MAX_KEY_SIZE];
    memset(first_key, 0, sizeof(first_key));
    memcpy(first_key, node_key(&fixture.dirs.tree, cursor.leaf,
                               cursor.stop_index),
           fixture.dirs.tree.ops->key_size);

    /* The consumed-key API should continue from the copied stop index. A
     * keyed scan remains inclusive for callers retrying an unconsumed entry. */
    compare_base_ops = fixture.dirs.tree.ops;
    bfs_btree_ops_t counted_ops = *compare_base_ops;
    counted_ops.key_compare = counted_dir_compare;
    fixture.dirs.tree.ops = &counted_ops;
    compare_calls = 0;
    raw_value_log_t retried = {0};
    TEST_ASSERT_EQ(bfs_btree_scan_cursor(&fixture.dirs.tree, &cursor,
                                        first_key, collect_raw_value,
                                        &retried), BFS_OK);
    uint32_t inclusive_compare_calls = compare_calls;
    TEST_ASSERT_EQ(retried.calls, 1);
    TEST_ASSERT_EQ(retried.inode_nr, expected_entries[0].inode_nr);

    compare_calls = 0;
    uint32_t before_resume = resumed.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &resumed), BFS_OK);
    TEST_ASSERT_EQ(resumed.count, before_resume + 1u);
    uint32_t resume_compare_calls = compare_calls;

    bfs_btree_cursor_t keyed_cursor;
    bfs_btree_cursor_init(&keyed_cursor);
    compare_calls = 0;
    memset(&retried, 0, sizeof(retried));
    TEST_ASSERT_EQ(bfs_btree_scan_cursor(&fixture.dirs.tree, &keyed_cursor,
                                        first_key, collect_raw_value,
                                        &retried), BFS_OK);
    uint32_t keyed_compare_calls = compare_calls;
    TEST_ASSERT_EQ(retried.calls, 1);
    TEST_ASSERT_EQ(retried.inode_nr, expected_entries[0].inode_nr);
    TEST_ASSERT(inclusive_compare_calls > 0);
    TEST_ASSERT(keyed_compare_calls > resume_compare_calls);
    bfs_btree_cursor_release(&keyed_cursor);
    fixture.dirs.tree.ops = compare_base_ops;
    compare_base_ops = NULL;

    while (resumed.count < CURSOR_ENTRY_COUNT) {
        uint32_t before = resumed.count;
        TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor,
                                           CURSOR_PARENT, collect_one,
                                           &resumed), BFS_OK);
        if (resumed.count == before) break;
    }
    TEST_ASSERT_EQ(resumed.count, CURSOR_ENTRY_COUNT);
    for (uint32_t i = 0; i < expected.count; i++)
        TEST_ASSERT(scan_entries_equal(&expected.entries[i],
                                       &resumed.entries[i]));
    bfs_btree_cursor_release(&cursor);
    fixture_close(&fixture);
}

static void test_missing_invalid_and_wrong_parent_stops_decline_without_callback(void)
{
    cursor_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    TEST_ASSERT(insert_entries(&fixture, CURSOR_PARENT, 24, 1000));
    TEST_ASSERT(insert_other_parent(&fixture));

    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    scan_log_t log = {.entries = resumed_entries,
                      .capacity = CURSOR_ENTRY_COUNT};
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_all, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, 0);

    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 1);
    TEST_ASSERT(cursor.stopped);
    cursor.stop_index = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_all, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, 1);
    bfs_btree_cursor_release(&cursor);

    bfs_btree_cursor_init(&cursor);
    memset(&log, 0, sizeof(log));
    log.entries = resumed_entries;
    log.capacity = CURSOR_ENTRY_COUNT;
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 1);
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor,
                                       CURSOR_OTHER_PARENT, collect_all,
                                       &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, 1);
    bfs_btree_cursor_release(&cursor);

    /* A resume that reaches the neighboring parent is an ordinary filtered
     * stop: it returns success and does not report that parent's key. */
    bfs_btree_cursor_init(&cursor);
    memset(&log, 0, sizeof(log));
    log.entries = resumed_entries;
    log.capacity = CURSOR_ENTRY_COUNT;
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 1);
    uint32_t before = log.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_all, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 24);
    before = log.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_all, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor,
                                       CURSOR_OTHER_PARENT, collect_all,
                                       &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);

    /* The internal parent-boundary stop is not consumed for Parent B. After
     * Resume declines, the ordinary keyed path must still return B's first
     * entry rather than silently stepping over it. */
    scan_entry_t peer_expected[CURSOR_OTHER_COUNT];
    scan_entry_t peer_first[CURSOR_OTHER_COUNT];
    scan_log_t peer_all = {.entries = peer_expected,
                           .capacity = CURSOR_OTHER_COUNT};
    scan_log_t peer_one = {.entries = peer_first,
                           .capacity = CURSOR_OTHER_COUNT};
    TEST_ASSERT_EQ(bfs_dir_scan(&fixture.dirs, CURSOR_OTHER_PARENT,
                                collect_all, &peer_all), BFS_OK);
    TEST_ASSERT_EQ(peer_all.count, CURSOR_OTHER_COUNT);
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor,
                                      CURSOR_OTHER_PARENT, NULL, 0,
                                      collect_one, &peer_one), BFS_OK);
    TEST_ASSERT_EQ(peer_one.count, 1);
    TEST_ASSERT(scan_entries_equal(&peer_first[0], &peer_expected[0]));
    bfs_btree_cursor_release(&cursor);
    fixture_close(&fixture);
}

static void test_insert_and_delete_between_calls_require_keyed_fallback(void)
{
    cursor_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    TEST_ASSERT(insert_entries(&fixture, CURSOR_PARENT, 32, 1000));

    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    scan_log_t log = {.entries = resumed_entries,
                      .capacity = CURSOR_ENTRY_COUNT};
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 1);
    char consumed[CURSOR_NAME_CAPACITY];
    memcpy(consumed, log.entries[0].name, log.entries[0].name_len + 1u);
    uint8_t consumed_len = log.entries[0].name_len;

    TEST_ASSERT_EQ(bfs_dir_insert(&fixture.dirs, CURSOR_PARENT, "after-mutation",
                                  14, 7000, BFS_INODE_FILE), BFS_OK);
    uint32_t before = log.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);

    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      consumed, consumed_len, collect_one,
                                      &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, before + 1u);
    TEST_ASSERT(memcmp(log.entries[before].name, consumed, consumed_len) != 0);
    bfs_btree_cursor_release(&cursor);

    bfs_btree_cursor_init(&cursor);
    memset(&log, 0, sizeof(log));
    log.entries = resumed_entries;
    log.capacity = CURSOR_ENTRY_COUNT;
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 1);
    TEST_ASSERT_EQ(bfs_dir_remove(&fixture.dirs, CURSOR_PARENT, "entry0010",
                                  9), BFS_OK);
    before = log.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);
    bfs_btree_cursor_release(&cursor);
    fixture_close(&fixture);
}

static bool mutate_tree_while_resuming(const char *name, uint8_t name_len,
                                       uint32_t inode_nr,
                                       uint32_t entry_type, void *ctx)
{
    mutate_callback_ctx_t *mutation = (mutate_callback_ctx_t *)ctx;
    if (!append_entry(&mutation->log, name, name_len, inode_nr, entry_type))
        return false;
    if (!mutation->changed) {
        mutation->changed = true;
        mutation->mutation_error = bfs_dir_insert(
            &mutation->fixture->dirs, CURSOR_PARENT, "callback-add", 12,
            8000, BFS_INODE_FILE);
        return mutation->mutation_error == BFS_OK;
    }
    return false;
}

static void test_callback_mutation_resumes_after_last_reported_entry(void)
{
    cursor_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    TEST_ASSERT(insert_entries(&fixture, CURSOR_PARENT, 40, 1000));
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    scan_log_t initial = {.entries = resumed_entries,
                          .capacity = CURSOR_ENTRY_COUNT};
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &initial), BFS_OK);
    TEST_ASSERT_EQ(initial.count, 1);

    static scan_entry_t callback_entries[4];
    mutate_callback_ctx_t mutation = {
        .fixture = &fixture,
        .log = {.entries = callback_entries,
                .capacity = sizeof(callback_entries) / sizeof(callback_entries[0])},
        .mutation_error = BFS_ERR_INVAL,
    };
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       mutate_tree_while_resuming,
                                       &mutation), BFS_OK);
    TEST_ASSERT(mutation.changed);
    TEST_ASSERT_EQ(mutation.mutation_error, BFS_OK);
    TEST_ASSERT_EQ(mutation.log.count, 2);
    TEST_ASSERT(!scan_entries_equal(&initial.entries[0],
                                    &mutation.log.entries[0]));
    TEST_ASSERT(!scan_entries_equal(&mutation.log.entries[0],
                                    &mutation.log.entries[1]));
    bfs_btree_cursor_release(&cursor);
    fixture_close(&fixture);
}

static void test_epoch_absence_saturation_and_raw_invalidation_decline_cursor(void)
{
    cursor_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    TEST_ASSERT(insert_entries(&fixture, CURSOR_PARENT, 40, 1000));
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    scan_log_t log = {.entries = resumed_entries,
                      .capacity = CURSOR_ENTRY_COUNT};
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    TEST_ASSERT_EQ(log.count, 1);

    const bfs_bio_ops_t *saved_ops = fixture.cache.bio.ops;
    bfs_bio_ops_t test_ops = *saved_ops;
    test_ops.mutation_epoch = NULL;
    fixture.cache.bio.ops = &test_ops;
    uint32_t before = log.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);

    test_ops.mutation_epoch = saturated_epoch;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);
    fixture.cache.bio.ops = saved_ops;

    uint8_t *root_image = (uint8_t *)malloc(CURSOR_BLOCK_SIZE);
    TEST_ASSERT(root_image != NULL);
    bfs_blk_t root = fixture.dirs.tree.root;
    TEST_ASSERT_EQ(bfs_bio_read(fixture.device, root, root_image), BFS_OK);
    hdr_of(root_image)->crc32 ^= bfs_be32(1);
    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, root, root_image), BFS_OK);
    uint64_t epoch_before = fixture.cache.mutation_epoch;
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT(fixture.cache.mutation_epoch > epoch_before);
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);

    uint32_t callbacks = 0;
    TEST_ASSERT_EQ(bfs_dir_scan(&fixture.dirs, CURSOR_PARENT,
                                count_entry_callback, &callbacks),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(callbacks, 0);

    /* Restore the raw block so teardown and any subsequent fixture reuse see
     * a valid image. */
    hdr_of(root_image)->crc32 ^= bfs_be32(1);
    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, root, root_image), BFS_OK);
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT_EQ(bfs_dir_scan(&fixture.dirs, CURSOR_PARENT,
                                count_entry_callback, &callbacks), BFS_OK);
    TEST_ASSERT(callbacks > 0);

    /* Invalidate from inside the stop callback. The cursor epoch must be the
     * pre-callback token, so a later keyless resume cannot trust this copy even
     * though its in-memory tree root and generation did not change. */
    bfs_btree_cursor_release(&cursor);
    bfs_btree_cursor_init(&cursor);
    memset(&log, 0, sizeof(log));
    log.entries = resumed_entries;
    log.capacity = CURSOR_ENTRY_COUNT;
    TEST_ASSERT_EQ(bfs_dir_scan_cursor(&fixture.dirs, &cursor, CURSOR_PARENT,
                                      NULL, 0, collect_one, &log), BFS_OK);
    uint8_t *saved_root = (uint8_t *)malloc(CURSOR_BLOCK_SIZE);
    TEST_ASSERT(saved_root != NULL);
    TEST_ASSERT_EQ(bfs_bio_read(fixture.device, root, saved_root), BFS_OK);
    invalidate_callback_ctx_t invalidate = {
        .fixture = &fixture,
        .root = root,
        .root_image = saved_root,
        .write_error = BFS_ERR_INVAL,
    };
    before = log.count;
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       invalidate_cache_in_callback,
                                       &invalidate), BFS_OK);
    TEST_ASSERT_EQ(invalidate.calls, 1);
    TEST_ASSERT_EQ(invalidate.write_error, BFS_OK);
    TEST_ASSERT_EQ(log.count, before);
    TEST_ASSERT_EQ(bfs_dir_scan_resume(&fixture.dirs, &cursor, CURSOR_PARENT,
                                       collect_one, &log), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(log.count, before);

    hdr_of(saved_root)->crc32 ^= bfs_be32(1);
    TEST_ASSERT_EQ(bfs_bio_write(fixture.device, root, saved_root), BFS_OK);
    bfs_cache_invalidate(&fixture.cache);
    callbacks = 0;
    TEST_ASSERT_EQ(bfs_dir_scan(&fixture.dirs, CURSOR_PARENT,
                                count_entry_callback, &callbacks), BFS_OK);
    TEST_ASSERT(callbacks > 0);
    free(saved_root);
    free(root_image);
    bfs_btree_cursor_release(&cursor);
    fixture_close(&fixture);
}

TEST_SUITE_BEGIN("Directory exclusive scan cursors")
    TEST_RUN(test_multileaf_resume_is_exclusive_and_visits_each_entry_once);
    TEST_RUN(test_missing_invalid_and_wrong_parent_stops_decline_without_callback);
    TEST_RUN(test_insert_and_delete_between_calls_require_keyed_fallback);
    TEST_RUN(test_callback_mutation_resumes_after_last_reported_entry);
    TEST_RUN(test_epoch_absence_saturation_and_raw_invalidation_decline_cursor);
TEST_SUITE_END()
