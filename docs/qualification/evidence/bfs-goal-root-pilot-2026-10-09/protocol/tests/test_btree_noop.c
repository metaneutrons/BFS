/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Focused tests for B+tree update no-op handling.
 */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdlib.h>

typedef struct {
    bfs_allocator_t base;
    bfs_blk_t next_block;
    bfs_blk_t max_block;
    bfs_blk_t freed[4096];
    uint32_t freed_count;
} bootstrap_alloc_t;
extern bootstrap_alloc_t *bootstrap_create(bfs_blk_t start, bfs_blk_t max);

#define TEST_IMG "test_btree_noop.img"
#define BLOCK_SIZE 1024
#define BLOCK_COUNT 512

static int u32_compare(const void *a, const void *b)
{
    uint32_t va = bfs_load_be32(a);
    uint32_t vb = bfs_load_be32(b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t u32_ops = {
    .key_compare = u32_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
};

static void make_key(uint32_t *key, uint32_t value) { *key = bfs_be32(value); }
static void make_val(uint32_t *val, uint32_t value) { *val = bfs_be32(value); }
static uint32_t read_val(const void *val) { return bfs_load_be32(val); }

typedef struct {
    bfs_allocator_t iface;
    bfs_allocator_t *wrapped;
    uint32_t alloc_calls;
    uint32_t dealloc_calls;
    bfs_blk_t allocated[16];
    bfs_blk_t deallocated[16];
} counted_allocator_t;

static bfs_blk_t counted_alloc(bfs_allocator_t *allocator)
{
    counted_allocator_t *counted = allocator->ctx;
    bfs_blk_t blk = counted->wrapped->alloc(counted->wrapped);
    if (counted->alloc_calls <
        sizeof(counted->allocated) / sizeof(counted->allocated[0]))
        counted->allocated[counted->alloc_calls] = blk;
    counted->alloc_calls++;
    return blk;
}

static bfs_err_t counted_dealloc(bfs_allocator_t *allocator, bfs_blk_t blk)
{
    counted_allocator_t *counted = allocator->ctx;
    if (counted->dealloc_calls <
        sizeof(counted->deallocated) / sizeof(counted->deallocated[0]))
        counted->deallocated[counted->dealloc_calls] = blk;
    counted->dealloc_calls++;
    return counted->wrapped->dealloc(counted->wrapped, blk);
}

static bfs_err_t counted_alloc_error(bfs_allocator_t *allocator)
{
    counted_allocator_t *counted = allocator->ctx;
    return counted->wrapped->error
        ? counted->wrapped->error(counted->wrapped) : BFS_ERR_NOSPC;
}

static void counted_allocator_init(counted_allocator_t *counted,
                                   bfs_allocator_t *wrapped)
{
    memset(counted, 0, sizeof(*counted));
    counted->wrapped = wrapped;
    counted->iface.alloc = counted_alloc;
    counted->iface.dealloc = counted_dealloc;
    counted->iface.error = counted_alloc_error;
    counted->iface.ctx = counted;
}

typedef struct {
    bfs_blk_t deferred[16];
    uint32_t defer_calls;
    uint32_t headroom_calls;
    uint32_t available;
} counted_sink_t;

static bfs_err_t counted_defer(void *ctx, bfs_blk_t blk)
{
    counted_sink_t *sink = ctx;
    if (sink->defer_calls < sizeof(sink->deferred) / sizeof(sink->deferred[0]))
        sink->deferred[sink->defer_calls] = blk;
    sink->defer_calls++;
    return BFS_OK;
}

static uint32_t counted_headroom(void *ctx)
{
    counted_sink_t *sink = ctx;
    sink->headroom_calls++;
    return sink->available;
}

static bfs_bio_t *counted_bio_target;
static const bfs_bio_ops_t *counted_bio_original_ops;
static uint32_t bio_read_calls;
static uint32_t bio_write_calls;
static bool fail_next_read;
static bool fail_next_write;

static bfs_err_t counted_read_block(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    (void)bio;
    bio_read_calls++;
    if (fail_next_read) {
        fail_next_read = false;
        return BFS_ERR_IO;
    }
    return counted_bio_original_ops->read_block(counted_bio_target, blk, buf);
}

static bfs_err_t counted_write_block(bfs_bio_t *bio, bfs_blk_t blk,
                                     const void *buf)
{
    (void)bio;
    bio_write_calls++;
    if (fail_next_write) {
        fail_next_write = false;
        return BFS_ERR_IO;
    }
    return counted_bio_original_ops->write_block(counted_bio_target, blk, buf);
}

static const bfs_bio_ops_t counted_bio_ops = {
    .read_block = counted_read_block,
    .write_block = counted_write_block,
    .sync = NULL,
    .close = NULL,
};

static void install_bio_counters(bfs_bio_t *bio)
{
    counted_bio_target = bio;
    counted_bio_original_ops = bio->ops;
    bio_read_calls = 0;
    bio_write_calls = 0;
    fail_next_read = false;
    fail_next_write = false;
    bio->ops = &counted_bio_ops;
}

static void remove_bio_counters(bfs_bio_t *bio)
{
    bio->ops = counted_bio_original_ops;
}

typedef struct {
    bfs_bio_t *bio;
    bootstrap_alloc_t *bootstrap;
    counted_allocator_t allocator;
    bfs_btree_t tree;
    uint64_t live_txn_id;
} fixture_t;

static bool open_fixture(fixture_t *fixture, uint32_t item_count)
{
    memset(fixture, 0, sizeof(*fixture));
    unlink(TEST_IMG);
    fixture->bio = bio_emu_create(TEST_IMG, BLOCK_SIZE, BLOCK_COUNT);
    if (!fixture->bio) return false;
    fixture->bootstrap = bootstrap_create(2, BLOCK_COUNT);
    if (!fixture->bootstrap) {
        bfs_bio_close(fixture->bio);
        unlink(TEST_IMG);
        return false;
    }
    counted_allocator_init(&fixture->allocator, &fixture->bootstrap->base);
    if (bfs_btree_init(&fixture->tree, fixture->bio,
                       &fixture->allocator.iface, &u32_ops,
                       BFS_BLK_NULL, 10) != BFS_OK) {
        free(fixture->bootstrap);
        bfs_bio_close(fixture->bio);
        unlink(TEST_IMG);
        return false;
    }
    fixture->live_txn_id = 10;
    fixture->tree.txn_id_ptr = &fixture->live_txn_id;
    for (uint32_t i = 0; i < item_count; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_val(&val, i + 100);
        if (bfs_btree_insert(&fixture->tree, &key, &val) != BFS_OK) {
            free(fixture->bootstrap);
            bfs_bio_close(fixture->bio);
            unlink(TEST_IMG);
            return false;
        }
    }
    fixture->allocator.alloc_calls = 0;
    fixture->allocator.dealloc_calls = 0;
    return true;
}

static void close_fixture(fixture_t *fixture)
{
    free(fixture->bootstrap);
    bfs_bio_close(fixture->bio);
    unlink(TEST_IMG);
}

static void add_counted_sink(fixture_t *fixture, counted_sink_t *sink,
                             uint32_t capacity, uint32_t available)
{
    memset(sink, 0, sizeof(*sink));
    sink->available = available;
    fixture->tree.free_sink = (bfs_free_sink_t){
        .ctx = sink,
        .defer = counted_defer,
        .headroom = counted_headroom,
        .capacity = capacity,
    };
}

static bool read_root_block(fixture_t *fixture, bfs_blk_t root, uint8_t *buf)
{
    return bfs_bio_read(fixture->bio, root, buf) == BFS_OK;
}

static void test_update_same_value_leaf_noop(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    uint8_t old_bytes[BLOCK_SIZE], after_bytes[BLOCK_SIZE];
    TEST_ASSERT(read_root_block(&fixture, old_root, old_bytes));
    const uint64_t old_txn = bfs_be64(((bfs_btnode_hdr_t *)old_bytes)->txn_id);
    TEST_ASSERT_EQ(old_txn, 10);

    fixture.live_txn_id = 11;
    counted_sink_t sink;
    add_counted_sink(&fixture, &sink, BFS_BTREE_MAX_OP_FREES, 4);
    install_bio_counters(fixture.bio);
    uint32_t key, same_value;
    make_key(&key, 1);
    make_val(&same_value, 101);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &same_value);
    uint32_t reads = bio_read_calls;
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_OK);
    TEST_ASSERT_EQ(reads, 1);
    TEST_ASSERT_EQ(writes, 0);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 0);
    TEST_ASSERT_EQ(sink.defer_calls, 0);
    TEST_ASSERT_EQ(bfs_bio_read(fixture.bio, old_root, after_bytes), BFS_OK);
    TEST_ASSERT_MEM_EQ(old_bytes, after_bytes, BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_be64(((bfs_btnode_hdr_t *)after_bytes)->txn_id), old_txn);
    close_fixture(&fixture);
}

static void test_update_same_value_internal_noop(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 130));
    TEST_ASSERT(fixture.tree.height > 1);
    const bfs_blk_t old_root = fixture.tree.root;
    uint8_t old_root_bytes[BLOCK_SIZE], after_root_bytes[BLOCK_SIZE];
    TEST_ASSERT(read_root_block(&fixture, old_root, old_root_bytes));
    const uint64_t old_txn = bfs_be64(((bfs_btnode_hdr_t *)old_root_bytes)->txn_id);

    fixture.live_txn_id = old_txn + 1;
    counted_sink_t sink;
    add_counted_sink(&fixture, &sink, BFS_BTREE_MAX_OP_FREES, 4);
    install_bio_counters(fixture.bio);
    uint32_t key, same_value;
    make_key(&key, 1);
    make_val(&same_value, 101);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &same_value);
    uint32_t reads = bio_read_calls;
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_OK);
    TEST_ASSERT(reads >= fixture.tree.height);
    TEST_ASSERT_EQ(writes, 0);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 0);
    TEST_ASSERT_EQ(sink.defer_calls, 0);
    TEST_ASSERT_EQ(bfs_bio_read(fixture.bio, old_root, after_root_bytes), BFS_OK);
    TEST_ASSERT_MEM_EQ(old_root_bytes, after_root_bytes, BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_be64(((bfs_btnode_hdr_t *)after_root_bytes)->txn_id), old_txn);
    close_fixture(&fixture);
}

static void test_update_changed_value_still_cows(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    uint8_t old_bytes[BLOCK_SIZE], after_old_bytes[BLOCK_SIZE];
    TEST_ASSERT(read_root_block(&fixture, old_root, old_bytes));
    install_bio_counters(fixture.bio);
    uint32_t key, new_value;
    make_key(&key, 1);
    make_val(&new_value, 777);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &new_value);
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_OK);
    TEST_ASSERT(fixture.tree.root != old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 1);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 1);
    TEST_ASSERT_EQ(writes, 1);
    TEST_ASSERT_EQ(fixture.allocator.deallocated[0], old_root);
    TEST_ASSERT_EQ(bfs_bio_read(fixture.bio, old_root, after_old_bytes), BFS_OK);
    TEST_ASSERT_MEM_EQ(old_bytes, after_old_bytes, BLOCK_SIZE);
    uint32_t got;
    TEST_ASSERT_EQ(bfs_btree_search(&fixture.tree, &key, &got), BFS_OK);
    TEST_ASSERT_EQ(read_val(&got), 777);
    close_fixture(&fixture);
}

static void test_update_same_value_does_not_consume_write_fault(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    uint32_t key, same_value, changed_value;
    make_key(&key, 1);
    make_val(&same_value, 101);
    make_val(&changed_value, 909);
    install_bio_counters(fixture.bio);
    fail_next_write = true;
    bfs_err_t no_op_err = bfs_btree_update(&fixture.tree, &key, &same_value);
    uint32_t writes_after_noop = bio_write_calls;
    bool fault_remained = fail_next_write;
    bfs_err_t changed_err = bfs_btree_update(&fixture.tree, &key, &changed_value);
    uint32_t writes_total = bio_write_calls;
    bool fault_consumed = !fail_next_write;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(no_op_err, BFS_OK);
    TEST_ASSERT_EQ(writes_after_noop, 0);
    TEST_ASSERT(fault_remained);
    TEST_ASSERT_EQ(changed_err, BFS_ERR_IO);
    TEST_ASSERT_EQ(writes_total, 1);
    TEST_ASSERT(fault_consumed);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 1);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 1);
    TEST_ASSERT_EQ(fixture.allocator.deallocated[0],
                   fixture.allocator.allocated[0]);
    TEST_ASSERT(fixture.allocator.allocated[0] != old_root);
    close_fixture(&fixture);
}

static void test_update_read_failure_precedes_equality(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    install_bio_counters(fixture.bio);
    fail_next_read = true;
    uint32_t key, same_value;
    make_key(&key, 1);
    make_val(&same_value, 101);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &same_value);
    uint32_t reads = bio_read_calls;
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_ERR_IO);
    TEST_ASSERT_EQ(reads, 1);
    TEST_ASSERT_EQ(writes, 0);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 0);
    close_fixture(&fixture);
}

static void test_update_crc_valid_bad_order_precedes_equality(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    uint8_t bad[BLOCK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(fixture.bio, old_root, bad), BFS_OK);
    memcpy(node_key(&fixture.tree, bad, 1),
           node_key(&fixture.tree, bad, 0), u32_ops.key_size);
    ((bfs_btnode_hdr_t *)bad)->crc32 = 0;
    ((bfs_btnode_hdr_t *)bad)->crc32 =
        bfs_be32(node_compute_crc(&fixture.tree, bad));
    TEST_ASSERT_EQ(bfs_bio_write(fixture.bio, old_root, bad), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(((bfs_btnode_hdr_t *)bad)->crc32),
                   node_compute_crc(&fixture.tree, bad));

    install_bio_counters(fixture.bio);
    uint32_t key, same_value;
    make_key(&key, 0);
    make_val(&same_value, 100);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &same_value);
    uint32_t reads = bio_read_calls;
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(reads, 1);
    TEST_ASSERT_EQ(writes, 0);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 0);
    close_fixture(&fixture);
}

static void test_update_missing_key_returns_notfound(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    install_bio_counters(fixture.bio);
    uint32_t key, value;
    make_key(&key, 1000);
    make_val(&value, 99);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &value);
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 0);
    TEST_ASSERT_EQ(writes, 0);
    close_fixture(&fixture);
}

static void test_update_equal_value_still_requires_pending_headroom(void)
{
    fixture_t fixture;
    TEST_ASSERT(open_fixture(&fixture, 3));
    const bfs_blk_t old_root = fixture.tree.root;
    counted_sink_t sink;
    add_counted_sink(&fixture, &sink, 1, 0);
    install_bio_counters(fixture.bio);
    uint32_t key, same_value;
    make_key(&key, 1);
    make_val(&same_value, 101);
    bfs_err_t err = bfs_btree_update(&fixture.tree, &key, &same_value);
    uint32_t reads = bio_read_calls;
    uint32_t writes = bio_write_calls;
    remove_bio_counters(fixture.bio);

    TEST_ASSERT_EQ(err, BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(sink.headroom_calls, 1);
    TEST_ASSERT_EQ(sink.defer_calls, 0);
    TEST_ASSERT_EQ(reads, 0);
    TEST_ASSERT_EQ(writes, 0);
    TEST_ASSERT_EQ(fixture.tree.root, old_root);
    TEST_ASSERT_EQ(fixture.allocator.alloc_calls, 0);
    TEST_ASSERT_EQ(fixture.allocator.dealloc_calls, 0);
    close_fixture(&fixture);
}

TEST_SUITE_BEGIN("B+tree update no-op")
    TEST_RUN(test_update_same_value_leaf_noop);
    TEST_RUN(test_update_same_value_internal_noop);
    TEST_RUN(test_update_changed_value_still_cows);
    TEST_RUN(test_update_same_value_does_not_consume_write_fault);
    TEST_RUN(test_update_read_failure_precedes_equality);
    TEST_RUN(test_update_crc_valid_bad_order_precedes_equality);
    TEST_RUN(test_update_missing_key_returns_notfound);
    TEST_RUN(test_update_equal_value_still_requires_pending_headroom);
TEST_SUITE_END()
