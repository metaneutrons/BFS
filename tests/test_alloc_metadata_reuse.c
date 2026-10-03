/* SPDX-License-Identifier: MPL-2.0 */
/* Focused tests for reusing mounted free-space metadata COW blocks. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "bfs_file.h"
#include "bfs_fsck.h"
#include "bfs_snapshot.h"
#include "bfs_superblock.h"

#define TEST_BLOCK_SIZE 1024u
#define TEST_BLOCK_COUNT 512u
#define FS_BLOCK_SIZE 4096u
#define FS_BLOCK_COUNT 2048u
#define TEST_DATA_START 4u
#define HEIGHT_ONE_FLOOR 20u
#define REUSE_WARMUP 8u

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    uint32_t reads;
    uint32_t writes;
    bool fail_read;
    bool fail_write;
    bool partial_write;
    uint32_t fail_write_countdown;
    uint32_t failed_write_at;
} memory_device_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    device->reads++;
    if (device->fail_read) return BFS_ERR_IO;
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
    if (device->fail_write_countdown > 0 &&
        --device->fail_write_countdown == 0) {
        device->failed_write_at = device->writes;
        if (device->partial_write)
            memcpy(device->bytes + (size_t)block * bio->block_size, buffer,
                   bio->block_size / 2); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        return BFS_ERR_IO;
    }
    if (device->fail_write) {
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
    bfs_superblock_t working;
    bfs_superblock_t committed;
    bool mounted;
    bool snapshots;
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

static void memory_destroy(memory_device_t *device)
{
    free(device->bytes);
    device->bytes = NULL;
}

static bool fixture_init(allocator_fixture_t *fixture, bfs_blk_t block_count)
{
    memset(fixture, 0, sizeof(*fixture));
    if (!memory_init(&fixture->device, TEST_BLOCK_SIZE, block_count))
        return false;
    if (bfs_freespace_init(&fixture->space, &fixture->device.bio,
                           BFS_BLK_NULL, 2) != BFS_OK)
        return false;
    uint32_t count = block_count - TEST_DATA_START - 1;
    if (bfs_freespace_add(&fixture->space, TEST_DATA_START, count) != BFS_OK ||
        bfs_freespace_refill_reserve(&fixture->space) != BFS_OK)
        return false;

    fixture->working.txn_id = bfs_be64(2);
    fixture->working.free_tree_root = bfs_be32(fixture->space.tree.root);
    fixture->committed.txn_id = bfs_be64(1);
    /* This sentinel is outside the free extents and represents a known old
     * committed root for ownership-protection checks. */
    fixture->committed.free_tree_root = bfs_be32(block_count - 1);
    fixture->space.sb = &fixture->working;
    fixture->space.committed_sb = &fixture->committed;
    fixture->space.mounted_state = &fixture->mounted;
    fixture->space.snapshot_state = &fixture->snapshots;
    fixture->mounted = true;
    /* Synthetic ownership fixtures start after the policy-only warmup. The
     * warmup/reset tests below exercise its real interface request counting. */
    fixture->space.metadata_reuse_txn = 2;
    fixture->space.metadata_requests = REUSE_WARMUP;
    return fixture->space.tree.height == 1 &&
           fixture->space.reserve_count == HEIGHT_ONE_FLOOR;
}

static void fixture_destroy(allocator_fixture_t *fixture)
{
    memory_destroy(&fixture->device);
}

static bfs_blk_t allocate_ordinary(allocator_fixture_t *fixture)
{
    return bfs_freespace_alloc(&fixture->space, 1);
}

static bool block_is_free(allocator_fixture_t *fixture, bfs_blk_t block)
{
    uint32_t search = bfs_be32(block), key = 0, length = 0;
    if (bfs_btree_search_floor(&fixture->space.tree, &search, &key,
                               &length) != BFS_OK)
        return false;
    uint32_t start = bfs_be32(key), count = bfs_be32(length);
    return block >= start && block - start < count;
}

typedef struct {
    bfs_blk_t first;
    bool found;
} first_extent_t;

static bool capture_first_extent(const void *key, const void *val, void *ctx)
{
    (void)val;
    first_extent_t *first = (first_extent_t *)ctx;
    first->first = bfs_load_be32(key);
    first->found = true;
    return false;
}

static bool first_free_block(allocator_fixture_t *fixture, bfs_blk_t *out)
{
    first_extent_t first = {0};
    if (bfs_btree_scan(&fixture->space.tree, NULL, capture_first_extent,
                       &first) != BFS_OK || !first.found)
        return false;
    *out = first.first;
    return true;
}

/* Build a one-leaf FreeTree with two short extents separated by caller-owned
 * blocks. The returned range was allocated in full before the extents were
 * returned, so the gaps are valid deallocation candidates. */
static bool fixture_make_gap_extents(allocator_fixture_t *fixture,
                                     bfs_blk_t *owned_start)
{
    if (!fixture_init(fixture, TEST_BLOCK_COUNT)) return false;
    uint32_t free_count = fixture->space.total_free;
    bfs_blk_t owned = bfs_freespace_alloc(&fixture->space, free_count);
    if (owned == BFS_BLK_NULL) return false;
    if (bfs_freespace_free(&fixture->space, owned + 1, 2) != BFS_OK ||
        bfs_freespace_free(&fixture->space, owned + 5, 2) != BFS_OK ||
        fixture->space.tree.height != 1 ||
        fixture->space.tree.root == BFS_BLK_NULL)
        return false;
    *owned_start = owned;
    return true;
}

static void fixture_refresh_root_crc(allocator_fixture_t *fixture)
{
    uint8_t *node = fixture->device.bytes +
                    (size_t)fixture->space.tree.root * TEST_BLOCK_SIZE;
    bfs_btnode_hdr_t *hdr = hdr_of(node);
    hdr->crc32 = 0;
    hdr->crc32 = bfs_be32(node_compute_crc(&fixture->space.tree, node));
}

typedef struct {
    bfs_blk_t root;
    uint32_t total_free;
    uint32_t reserve_count;
    uint32_t writes;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    bfs_superblock_t working;
    uint8_t root_bytes[TEST_BLOCK_SIZE];
} allocator_state_snapshot_t;

typedef enum {
    STOCK_DUPLICATE_FIRST_PAIR,
    STOCK_DUPLICATE_MIDDLE_PAIR,
    STOCK_DUPLICATE_NONADJACENT_PAIR,
    STOCK_DUPLICATE_BEFORE_PROTECTED,
    STOCK_PROTECTED_BEFORE_DUPLICATE,
    STOCK_ACTIVE_EMERGENCY_ALIAS,
} stock_fault_kind_t;

static void fixture_capture_allocator_state(
    const allocator_fixture_t *fixture, allocator_state_snapshot_t *snapshot)
{
    snapshot->root = fixture->space.tree.root;
    snapshot->total_free = fixture->space.total_free;
    snapshot->reserve_count = fixture->space.reserve_count;
    snapshot->writes = fixture->device.writes;
    memcpy(snapshot->reserve, fixture->space.reserve,
           sizeof(snapshot->reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    memcpy(&snapshot->working, &fixture->working,
           sizeof(snapshot->working)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    memcpy(snapshot->root_bytes,
           fixture->device.bytes + (size_t)snapshot->root * TEST_BLOCK_SIZE,
           sizeof(snapshot->root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
}

static bool fixture_allocator_state_unchanged(
    const allocator_fixture_t *fixture,
    const allocator_state_snapshot_t *snapshot)
{
    return fixture->space.tree.root == snapshot->root &&
           fixture->space.tree.root < fixture->device.bio.block_count &&
           fixture->space.total_free == snapshot->total_free &&
           fixture->space.reserve_count == snapshot->reserve_count &&
           fixture->device.writes == snapshot->writes &&
           memcmp(fixture->space.reserve, snapshot->reserve,
                  sizeof(snapshot->reserve)) == 0 &&
           memcmp(&fixture->working, &snapshot->working,
                  sizeof(snapshot->working)) == 0 &&
           memcmp(fixture->device.bytes +
                      (size_t)snapshot->root * TEST_BLOCK_SIZE,
                  snapshot->root_bytes, sizeof(snapshot->root_bytes)) == 0;
}

static bool reserve_stock_fault_case(stock_fault_kind_t kind,
                                     bfs_err_t expected)
{
    allocator_fixture_t fixture;
    if (!fixture_init(&fixture, TEST_BLOCK_COUNT)) return false;
    bfs_blk_t candidate = allocate_ordinary(&fixture);
    if (candidate == BFS_BLK_NULL || candidate == 0) {
        fixture_destroy(&fixture);
        return false;
    }

    bfs_blk_t x = fixture.space.reserve[0];
    bfs_blk_t y = fixture.space.reserve[1];
    bfs_blk_t z = fixture.space.reserve[2];
    if (candidate == x || candidate == y || candidate == z) {
        fixture_destroy(&fixture);
        return false;
    }
    bfs_blk_t protected_root = bfs_be32(fixture.committed.free_tree_root);
    switch (kind) {
    case STOCK_DUPLICATE_FIRST_PAIR:
        fixture.space.reserve_count = 3;
        fixture.space.reserve[0] = x;
        fixture.space.reserve[1] = x;
        fixture.space.reserve[2] = z;
        break;
    case STOCK_DUPLICATE_MIDDLE_PAIR:
        fixture.space.reserve_count = 3;
        fixture.space.reserve[0] = z;
        fixture.space.reserve[1] = x;
        fixture.space.reserve[2] = x;
        break;
    case STOCK_DUPLICATE_NONADJACENT_PAIR:
        fixture.space.reserve_count = 3;
        fixture.space.reserve[0] = x;
        fixture.space.reserve[1] = y;
        fixture.space.reserve[2] = x;
        break;
    case STOCK_DUPLICATE_BEFORE_PROTECTED:
        fixture.space.reserve_count = 3;
        fixture.space.reserve[0] = x;
        fixture.space.reserve[1] = protected_root;
        fixture.space.reserve[2] = x;
        break;
    case STOCK_PROTECTED_BEFORE_DUPLICATE:
        fixture.space.reserve_count = 2;
        fixture.space.reserve[0] = protected_root;
        fixture.space.reserve[1] = protected_root;
        break;
    case STOCK_ACTIVE_EMERGENCY_ALIAS:
        fixture.working.emergency_pool[0] = bfs_be32(x);
        fixture.working.emergency_count = bfs_be32(1);
        break;
    default:
        fixture_destroy(&fixture);
        return false;
    }

    allocator_state_snapshot_t before;
    fixture_capture_allocator_state(&fixture, &before);
    bfs_err_t result = fixture.space.iface.dealloc(&fixture.space.iface,
                                                   candidate);
    bool unchanged = fixture_allocator_state_unchanged(&fixture, &before);
    fixture_destroy(&fixture);
    return result == expected && unchanged;
}

static void test_dealloc_stashes_without_free_tree_mutation(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before + 1);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT(!block_is_free(&fixture, block));
    fixture_destroy(&fixture);
}

static void test_alloc_pops_stashed_suffix_above_height_one_floor(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    fixture.device.writes = 0;

    bfs_blk_t reused = fixture.space.iface.alloc(&fixture.space.iface);
    TEST_ASSERT_EQ(reused, block);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, HEIGHT_ONE_FLOOR);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    fixture_destroy(&fixture);
}

static void test_alloc_stashed_stock_uses_one_root_read(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    fixture.device.reads = 0;

    TEST_ASSERT_EQ(fixture.space.iface.alloc(&fixture.space.iface), block);
    TEST_ASSERT_EQ(fixture.device.reads, 1);
    TEST_ASSERT_EQ(fixture.space.reserve_count, HEIGHT_ONE_FLOOR);
    fixture_destroy(&fixture);
}

static void test_alloc_never_spends_the_height_one_floor(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    uint32_t free_before = fixture.space.total_free;
    fixture.device.writes = 0;
    TEST_ASSERT(free_before > 0);

    bfs_blk_t block = fixture.space.iface.alloc(&fixture.space.iface);
    TEST_ASSERT(block != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.reserve_count, HEIGHT_ONE_FLOOR);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before - 1);
    TEST_ASSERT(!block_is_free(&fixture, block));
    fixture_destroy(&fixture);
}

static void test_allocator_skips_inactive_historical_emergency_slots(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t lower = allocate_ordinary(&fixture);
    bfs_blk_t historical = allocate_ordinary(&fixture);
    TEST_ASSERT(lower != BFS_BLK_NULL && historical != BFS_BLK_NULL);
    fixture.space.reserve[fixture.space.reserve_count++] = lower;
    fixture.space.reserve[fixture.space.reserve_count++] = historical;
    fixture.working.emergency_pool[BFS_EMERGENCY_POOL_SIZE - 1] =
        bfs_be32(historical);
    fixture.working.emergency_count = bfs_be32(0); /* inactive history */
    uint32_t prefix[HEIGHT_ONE_FLOOR];
    memcpy(prefix, fixture.space.reserve, sizeof(prefix)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    fixture.device.writes = 0;

    bfs_blk_t reused = fixture.space.iface.alloc(&fixture.space.iface);
    TEST_ASSERT_EQ(reused, lower);
    TEST_ASSERT_EQ(fixture.space.reserve_count, HEIGHT_ONE_FLOOR + 1);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, prefix, sizeof(prefix));
    TEST_ASSERT_EQ(fixture.space.reserve[HEIGHT_ONE_FLOOR], historical);
    TEST_ASSERT_EQ(bfs_be32(fixture.working.emergency_pool[
                       BFS_EMERGENCY_POOL_SIZE - 1]), historical);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    fixture_destroy(&fixture);
}

static void test_duplicate_and_reserve_blocks_are_rejected_unchanged(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t owned = allocate_ordinary(&fixture);
    TEST_ASSERT(owned != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, owned),
                   BFS_OK);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    bfs_blk_t reserve_block = fixture.space.reserve[0];

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, owned),
                   BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                               reserve_block), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_metadata_stock_accepts_healthy_active_prefixes(void)
{
    /* Synthetic active-prefix probes: truncating reserve_count leaves the
     * suffix inactive and is not a publishable/free-space-accounting fixture. */
    for (uint32_t active_count = 0; active_count <= 1; active_count++) {
        allocator_fixture_t fixture;
        TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
        bfs_blk_t candidate = allocate_ordinary(&fixture);
        TEST_ASSERT(candidate != BFS_BLK_NULL && candidate > 0);
        bfs_blk_t kept = fixture.space.reserve[0];
        fixture.space.reserve_count = active_count;

        bfs_blk_t root_before = fixture.space.tree.root;
        uint32_t free_before = fixture.space.total_free;
        uint8_t root_bytes[TEST_BLOCK_SIZE];
        memcpy(root_bytes, fixture.device.bytes +
                   (size_t)root_before * TEST_BLOCK_SIZE,
               sizeof(root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        fixture.device.writes = 0;

        TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                                   candidate), BFS_OK);
        TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
        TEST_ASSERT_MEM_EQ(fixture.device.bytes +
                           (size_t)root_before * TEST_BLOCK_SIZE,
                           root_bytes, sizeof(root_bytes));
        TEST_ASSERT_EQ(fixture.space.total_free, free_before);
        TEST_ASSERT_EQ(fixture.space.reserve_count, active_count + 1);
        if (active_count == 1)
            TEST_ASSERT_EQ(fixture.space.reserve[0], kept);
        TEST_ASSERT_EQ(fixture.space.reserve[active_count], candidate);
        TEST_ASSERT_EQ(fixture.device.writes, 0);
        fixture_destroy(&fixture);
    }

    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t candidate = allocate_ordinary(&fixture);
    TEST_ASSERT(candidate != BFS_BLK_NULL && candidate > 0);
    for (uint32_t i = 0; i < fixture.space.reserve_count / 2; i++) {
        uint32_t opposite = fixture.space.reserve_count - 1 - i;
        bfs_blk_t tmp = fixture.space.reserve[i];
        fixture.space.reserve[i] = fixture.space.reserve[opposite];
        fixture.space.reserve[opposite] = tmp;
    }
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    uint8_t root_bytes[TEST_BLOCK_SIZE];
    memcpy(root_bytes, fixture.device.bytes +
               (size_t)root_before * TEST_BLOCK_SIZE,
           sizeof(root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, candidate),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_MEM_EQ(fixture.device.bytes +
                       (size_t)root_before * TEST_BLOCK_SIZE,
                       root_bytes, sizeof(root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before + 1);
    TEST_ASSERT_EQ(fixture.space.reserve[reserve_before], candidate);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    fixture_destroy(&fixture);
}

static void test_full_healthy_metadata_stock_validates_and_pops(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    while (fixture.space.reserve_count < BFS_ALLOC_RESERVE_SIZE) {
        bfs_blk_t owned = allocate_ordinary(&fixture);
        TEST_ASSERT(owned != BFS_BLK_NULL && owned > 0);
        for (uint32_t i = 0; i < fixture.space.reserve_count; i++)
            TEST_ASSERT(fixture.space.reserve[i] != owned);
        fixture.space.reserve[fixture.space.reserve_count++] = owned;
    }
    bfs_blk_t expected = fixture.space.reserve[BFS_ALLOC_RESERVE_SIZE - 1];
    TEST_ASSERT(expected != BFS_BLK_NULL && expected > 0);
    bfs_blk_t reserve_prefix[BFS_ALLOC_RESERVE_SIZE - 1];
    memcpy(reserve_prefix, fixture.space.reserve, sizeof(reserve_prefix)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint8_t root_bytes[TEST_BLOCK_SIZE];
    memcpy(root_bytes, fixture.device.bytes +
               (size_t)root_before * TEST_BLOCK_SIZE,
           sizeof(root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    fixture.device.writes = 0;

    bfs_blk_t allocated = fixture.space.iface.alloc(&fixture.space.iface);
    TEST_ASSERT_EQ(allocated, expected);
    TEST_ASSERT_EQ(fixture.space.reserve_count, BFS_ALLOC_RESERVE_SIZE - 1);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, reserve_prefix,
                       sizeof(reserve_prefix));
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_MEM_EQ(fixture.device.bytes +
                       (size_t)root_before * TEST_BLOCK_SIZE,
                       root_bytes, sizeof(root_bytes));
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    fixture_destroy(&fixture);
}

static void test_metadata_stock_fault_order_and_duplicate_positions(void)
{
    TEST_ASSERT(reserve_stock_fault_case(STOCK_DUPLICATE_FIRST_PAIR,
                                         BFS_ERR_EXISTS));
    TEST_ASSERT(reserve_stock_fault_case(STOCK_DUPLICATE_MIDDLE_PAIR,
                                         BFS_ERR_EXISTS));
    TEST_ASSERT(reserve_stock_fault_case(STOCK_DUPLICATE_NONADJACENT_PAIR,
                                         BFS_ERR_EXISTS));
    TEST_ASSERT(reserve_stock_fault_case(STOCK_DUPLICATE_BEFORE_PROTECTED,
                                         BFS_ERR_EXISTS));
    TEST_ASSERT(reserve_stock_fault_case(STOCK_PROTECTED_BEFORE_DUPLICATE,
                                         BFS_ERR_CORRUPT));
    TEST_ASSERT(reserve_stock_fault_case(STOCK_ACTIVE_EMERGENCY_ALIAS,
                                         BFS_ERR_EXISTS));
}

static void test_free_tree_overlap_is_rejected_unchanged(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t free_block;
    TEST_ASSERT(first_free_block(&fixture, &free_block));
    uint32_t search = bfs_be32(free_block), key = 0, length = 0;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.space.tree, &search, &key,
                                          &length), BFS_OK);
    TEST_ASSERT(bfs_be32(length) > 1);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    for (uint32_t i = 0; i < 2; i++) {
        fixture.device.reads = 0;
        TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                                   free_block + i),
                       BFS_ERR_EXISTS);
        TEST_ASSERT_EQ(fixture.device.reads, 1);
    }
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_live_and_committed_roots_are_never_stashed(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                               fixture.space.tree.root),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                               fixture.device.bio.block_count - 1),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_active_emergency_return_does_not_enter_ordinary_stock(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    fixture.working.emergency_pool[0] = bfs_be32(block);
    fixture.working.emergency_count = bfs_be32(1);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    TEST_ASSERT_EQ(bfs_be32(fixture.working.emergency_count), 1);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    fixture_destroy(&fixture);
}

static void test_inactive_historical_emergency_dealloc_stays_out_of_reserve(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    fixture.working.emergency_pool[BFS_EMERGENCY_POOL_SIZE - 1] = bfs_be32(block);
    fixture.working.emergency_count = bfs_be32(0);
    uint32_t reserve_before = fixture.space.reserve_count;
    uint32_t free_before = fixture.space.total_free;
    fixture.device.writes = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(bfs_be32(fixture.working.emergency_count), 1);
    TEST_ASSERT_EQ(bfs_be32(fixture.working.emergency_pool[0]), block);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    fixture_destroy(&fixture);
}

static void test_validation_read_failure_does_not_stash_or_pop(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    fixture.device.fail_read = true;
    fixture.device.reads = 0;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.reads, 1);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);

    fixture.device.fail_read = false;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    reserve_before = fixture.space.reserve_count;
    root_before = fixture.space.tree.root;
    free_before = fixture.space.total_free;
    fixture.device.fail_read = true;
    fixture.device.reads = 0;
    TEST_ASSERT_EQ(fixture.space.iface.alloc(&fixture.space.iface), BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.device.reads, 1);
    TEST_ASSERT_EQ(fixture.space.iface.error(&fixture.space.iface), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_single_block_absence_checks_only_the_height_one_root(void)
{
    const uint32_t offsets[] = {0, 4, 10}; /* below, between, after */
    for (uint32_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        allocator_fixture_t fixture;
        bfs_blk_t owned;
        TEST_ASSERT(fixture_make_gap_extents(&fixture, &owned));
        bfs_blk_t candidate = owned + offsets[i];
        bfs_blk_t root_before = fixture.space.tree.root;
        uint32_t free_before = fixture.space.total_free;
        uint32_t reserve_before = fixture.space.reserve_count;
        fixture.device.reads = 0;

        TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                                   candidate), BFS_OK);
        TEST_ASSERT_EQ(fixture.device.reads, 1);
        TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
        TEST_ASSERT_EQ(fixture.space.total_free, free_before);
        TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before + 1);
        fixture_destroy(&fixture);
    }
}

static void test_single_block_absence_rejects_bad_root_crc_on_first_read(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    uint8_t *root = fixture.device.bytes +
                    (size_t)root_before * TEST_BLOCK_SIZE;
    hdr_of(root)->crc32 ^= 1u;
    fixture.device.reads = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.device.reads, 1);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_single_block_absence_rejects_bad_key_order_on_first_read(void)
{
    allocator_fixture_t fixture;
    bfs_blk_t owned;
    TEST_ASSERT(fixture_make_gap_extents(&fixture, &owned));
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    uint8_t *root = fixture.device.bytes +
                    (size_t)root_before * TEST_BLOCK_SIZE;
    TEST_ASSERT_EQ(num_keys(root), 2);
    bfs_store_be32(node_key(&fixture.space.tree, root, 1),
                   bfs_load_be32(node_key(&fixture.space.tree, root, 0)));
    fixture_refresh_root_crc(&fixture);
    fixture.device.reads = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, owned),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.device.reads, 1);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_single_block_absence_rejects_bad_selected_extent_on_first_read(void)
{
    allocator_fixture_t fixture;
    bfs_blk_t owned;
    TEST_ASSERT(fixture_make_gap_extents(&fixture, &owned));
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    uint8_t *root = fixture.device.bytes +
                    (size_t)root_before * TEST_BLOCK_SIZE;
    bfs_store_be32(leaf_val(&fixture.space.tree, root, 0), 0);
    fixture_refresh_root_crc(&fixture);
    fixture.device.reads = 0;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface,
                                               owned + 4), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.device.reads, 1);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    fixture_destroy(&fixture);
}

static void test_write_fault_does_not_affect_write_free_metadata_stash(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    uint32_t reserve_before = fixture.space.reserve_count;
    bfs_blk_t root_before = fixture.space.tree.root;
    uint32_t free_before = fixture.space.total_free;
    fixture.device.writes = 0;
    fixture.device.fail_write = true;

    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before + 1);
    TEST_ASSERT_EQ(fixture.space.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before);
    fixture_destroy(&fixture);
}

static void test_btree_partial_write_abort_restores_owned_spare(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    const bfs_btree_ops_t ops = {
        .key_compare = bfs_cmp_be32,
        .key_size = sizeof(uint32_t), .val_size = sizeof(uint32_t),
    };
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &fixture.device.bio,
                     &fixture.space.iface, &ops, BFS_BLK_NULL, 2), BFS_OK);
    uint32_t key = bfs_be32(17), old_value = bfs_be32(23);
    uint32_t new_value = bfs_be32(91), actual;
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &old_value), BFS_OK);
    bfs_blk_t spare = allocate_ordinary(&fixture);
    TEST_ASSERT(spare != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, spare),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.space.reserve_count, HEIGHT_ONE_FLOOR + 1);
    bfs_blk_t stock[BFS_ALLOC_RESERVE_SIZE];
    memcpy(stock, fixture.space.reserve, sizeof(stock)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_blk_t root = tree.root, free_root = fixture.space.tree.root;
    uint32_t free_count = fixture.space.total_free;
    uint8_t root_bytes[TEST_BLOCK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(&fixture.device.bio, root, root_bytes), BFS_OK);
    bfs_superblock_t working = fixture.working;
    fixture.device.writes = 0;
    fixture.device.fail_write = true;
    fixture.device.partial_write = true;

    TEST_ASSERT_EQ(bfs_btree_update(&tree, &key, &new_value), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.writes, 1); /* abort stashes; no cleanup write */
    TEST_ASSERT_EQ(tree.root, root);
    TEST_ASSERT_EQ(fixture.space.tree.root, free_root);
    TEST_ASSERT_EQ(fixture.space.total_free, free_count);
    TEST_ASSERT_EQ(fixture.space.reserve_count, HEIGHT_ONE_FLOOR + 1);
    TEST_ASSERT_MEM_EQ(fixture.space.reserve, stock, sizeof(stock));
    TEST_ASSERT_MEM_EQ(&fixture.working, &working, sizeof(working));
    TEST_ASSERT_MEM_EQ(fixture.device.bytes + (size_t)root * TEST_BLOCK_SIZE,
                       root_bytes, sizeof(root_bytes));
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &actual), BFS_OK);
    TEST_ASSERT_EQ(actual, old_value);

    fixture.device.fail_write = false;
    fixture.device.partial_write = false;
    TEST_ASSERT_EQ(bfs_btree_update(&tree, &key, &new_value), BFS_OK);
    TEST_ASSERT_EQ(tree.root, spare); /* partially written scratch was reusable */
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &actual), BFS_OK);
    TEST_ASSERT_EQ(actual, new_value);
    fixture_destroy(&fixture);
}

static void test_short_transactions_keep_legacy_and_warmup_saturates(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    fixture.space.metadata_requests = 0;
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    uint32_t free_before = fixture.space.total_free;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block), BFS_OK);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before + 1);
    TEST_ASSERT(block_is_free(&fixture, block));
    for (uint32_t request = 1; request <= REUSE_WARMUP + 2; request++) {
        block = fixture.space.iface.alloc(&fixture.space.iface);
        TEST_ASSERT(block != BFS_BLK_NULL);
        TEST_ASSERT_EQ(fixture.space.metadata_requests,
                       request < REUSE_WARMUP ? request : REUSE_WARMUP);
        fixture.device.writes = 0;
        TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block), BFS_OK);
        if (request < REUSE_WARMUP) {
            TEST_ASSERT(fixture.device.writes > 0);
            TEST_ASSERT(block_is_free(&fixture, block));
        } else {
            TEST_ASSERT_EQ(fixture.device.writes, 0);
            TEST_ASSERT(!block_is_free(&fixture, block));
        }
    }
    fixture_destroy(&fixture);
}

static void test_metadata_warmup_resets_at_a_new_live_id(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block), BFS_OK);
    fixture.working.txn_id = bfs_be64(3);
    fixture.space.tree.txn_id_fallback = 3;
    block = fixture.space.iface.alloc(&fixture.space.iface);
    TEST_ASSERT(block != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fixture.space.metadata_reuse_txn, 3);
    TEST_ASSERT_EQ(fixture.space.metadata_requests, 1);
    uint32_t free_before = fixture.space.total_free;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block), BFS_OK);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before + 1);
    TEST_ASSERT(block_is_free(&fixture, block));
    fixture_destroy(&fixture);
}

typedef enum {
    FALLBACK_UNMOUNTED,
    FALLBACK_NO_WORKING_SB,
    FALLBACK_NO_COMMITTED_SB,
    FALLBACK_NOT_NEWER,
    FALLBACK_SNAPSHOTS,
    FALLBACK_SNAPSHOT_ACTIVE,
    FALLBACK_NO_SNAPSHOT_STATE,
    FALLBACK_FULL_STOCK,
} fallback_kind_t;

static bool fallback_case_uses_free_tree(fallback_kind_t kind)
{
    allocator_fixture_t fixture;
    if (!fixture_init(&fixture, TEST_BLOCK_COUNT)) return false;
    bfs_blk_t block = allocate_ordinary(&fixture);
    if (block == BFS_BLK_NULL) {
        fixture_destroy(&fixture);
        return false;
    }
    if (kind == FALLBACK_UNMOUNTED) fixture.mounted = false;
    if (kind == FALLBACK_NO_WORKING_SB) fixture.space.sb = NULL;
    if (kind == FALLBACK_NO_COMMITTED_SB) fixture.space.committed_sb = NULL;
    if (kind == FALLBACK_NOT_NEWER)
        fixture.committed.txn_id = fixture.working.txn_id;
    if (kind == FALLBACK_SNAPSHOTS)
        fixture.working.options = bfs_be32(BFS_OPT_SNAPSHOTS);
    if (kind == FALLBACK_SNAPSHOT_ACTIVE) fixture.snapshots = true;
    if (kind == FALLBACK_NO_SNAPSHOT_STATE)
        fixture.space.snapshot_state = NULL;
    if (kind == FALLBACK_FULL_STOCK) {
        uint32_t extra = BFS_ALLOC_RESERVE_SIZE - fixture.space.reserve_count;
        bfs_blk_t run = bfs_freespace_alloc(&fixture.space, extra);
        if (run == BFS_BLK_NULL) {
            fixture_destroy(&fixture);
            return false;
        }
        for (uint32_t i = 0; i < extra; i++)
            fixture.space.reserve[fixture.space.reserve_count++] = run + i;
    }
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    bfs_err_t err = fixture.space.iface.dealloc(&fixture.space.iface, block);
    bool ok = err == BFS_OK && fixture.space.total_free == free_before + 1 &&
              fixture.space.reserve_count == reserve_before &&
              block_is_free(&fixture, block);
    fixture_destroy(&fixture);
    return ok;
}

static void test_ineligible_contexts_and_full_stock_use_legacy_free_path(void)
{
    for (fallback_kind_t kind = FALLBACK_UNMOUNTED;
         kind <= FALLBACK_FULL_STOCK; kind++)
        TEST_ASSERT(fallback_case_uses_free_tree(kind));
}

static void test_deeper_free_tree_uses_legacy_free_path(void)
{
    allocator_fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, TEST_BLOCK_COUNT));
    bfs_blk_t run = bfs_freespace_alloc(&fixture.space, 260);
    TEST_ASSERT(run != BFS_BLK_NULL);
    for (uint32_t i = 0; i < 130; i++)
        TEST_ASSERT_EQ(bfs_freespace_free(&fixture.space, run + 2 * i, 1),
                       BFS_OK);
    TEST_ASSERT(fixture.space.tree.height > 1);

    bfs_blk_t block = allocate_ordinary(&fixture);
    TEST_ASSERT(block != BFS_BLK_NULL);
    uint32_t free_before = fixture.space.total_free;
    uint32_t reserve_before = fixture.space.reserve_count;
    TEST_ASSERT_EQ(fixture.space.iface.dealloc(&fixture.space.iface, block),
                   BFS_OK);
    TEST_ASSERT_EQ(fixture.space.total_free, free_before + 1);
    TEST_ASSERT_EQ(fixture.space.reserve_count, reserve_before);
    TEST_ASSERT(block_is_free(&fixture, block));
    fixture_destroy(&fixture);
}

static bool fsck_clean(bfs_fs_t *fs)
{
    bfs_fsck_report_t report;
    return bfs_fs_check(fs, false, &report) == BFS_OK && report.errors == 0;
}

typedef struct {
    bfs_blk_t block;
    uint8_t bytes[FS_BLOCK_SIZE];
} published_root_t;

static bool capture_published_root(const memory_device_t *device,
                                   uint32_t encoded_block,
                                   published_root_t *root)
{
    root->block = bfs_be32(encoded_block);
    if (root->block == BFS_BLK_NULL || root->block >= device->bio.block_count)
        return false;
    memcpy(root->bytes, device->bytes + (size_t)root->block * FS_BLOCK_SIZE,
           sizeof(root->bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return true;
}

static bool published_root_unchanged(const memory_device_t *device,
                                     const published_root_t *root)
{
    return root->block < device->bio.block_count &&
           memcmp(root->bytes,
                  device->bytes + (size_t)root->block * FS_BLOCK_SIZE,
                  sizeof(root->bytes)) == 0;
}

static bool fsck_strict_clean(bfs_fs_t *fs)
{
    bfs_fsck_report_t report = {0};
    return bfs_fs_check(fs, false, &report) == BFS_OK && report.errors == 0 &&
           report.warnings == 0 && report.leaked_blocks == 0;
}

static bool published_superblocks_unchanged(const memory_device_t *device,
                                            const uint8_t *published_image,
                                            const bfs_superblock_t *published_sb)
{
    uint64_t backup_offset =
        ((uint64_t)bfs_be32(published_sb->sb_backup_offset_hi) << 32) |
        bfs_be32(published_sb->sb_backup_offset_lo);
    uint64_t device_size = (uint64_t)device->bio.block_size *
                           device->bio.block_count;
    if (backup_offset == BFS_SB_OFFSET_A || backup_offset > device_size ||
        BFS_SB_SIZE > device_size - backup_offset)
        return false;
    return memcmp(published_image + BFS_SB_OFFSET_A,
                  device->bytes + BFS_SB_OFFSET_A, BFS_SB_SIZE) == 0 &&
           memcmp(published_image + backup_offset,
                  device->bytes + backup_offset, BFS_SB_SIZE) == 0;
}

static bool format_mount(memory_device_t *device, bfs_fs_t *fs,
                         uint32_t options)
{
    return bfs_fs_format(&device->bio, "MetadataReuse", options) == BFS_OK &&
           bfs_fs_mount(fs, &device->bio) == BFS_OK;
}

static void test_mounted_append_overwrite_commits_and_remount(void)
{
    memory_device_t device;
    bfs_fs_t fs;
    TEST_ASSERT(memory_init(&device, FS_BLOCK_SIZE, FS_BLOCK_COUNT));
    TEST_ASSERT(format_mount(&device, &fs, 0));
    fs.owned_nodes.disabled = true; /* qualifies the copy-on-write stock path */
    TEST_ASSERT(fs.freespace.mounted_state == &fs.mounted);
    TEST_ASSERT(fs.freespace.snapshot_state == &fs.has_snapshots);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "append", 6, &ino),
                   BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    uint8_t *chunk = malloc(64u * 1024u);
    TEST_ASSERT(chunk != NULL);
    for (uint32_t pass = 0; pass < 3; pass++) {
        memset(chunk, (int)(0x31u + pass), 64u * 1024u);
        uint32_t chunks = pass == 0 ? 4 : 1;
        for (uint32_t i = 0; i < chunks; i++)
            TEST_ASSERT_EQ(bfs_file_append(&file, chunk, 64u * 1024u),
                           64u * 1024u);
        if (pass == 0) {
            /* Sustained append reaches the policy warmup naturally, then a
             * retired current inode seeds an ordinary suffix spare. */
            TEST_ASSERT_EQ(fs.freespace.metadata_requests, REUSE_WARMUP);
            TEST_ASSERT(fs.freespace.reserve_count > HEIGHT_ONE_FLOOR);
        }
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_file_seek(&file, 64u * 1024u + 123, BFS_SEEK_SET),
                   64u * 1024u + 123);
    memset(chunk, 0xA7, 4096);
    TEST_ASSERT_EQ(bfs_file_write(&file, chunk, 4096), 4096);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(fsck_clean(&fs));

    bfs_bio_t *bio = &device.bio;
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    fs.owned_nodes.disabled = true; /* qualifies the copy-on-write stock path */
    TEST_ASSERT(fsck_clean(&fs));
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_seek(&file, 64u * 1024u + 123, BFS_SEEK_SET),
                   64u * 1024u + 123);
    uint8_t readback[4096];
    TEST_ASSERT_EQ(bfs_file_read(&file, readback, sizeof(readback)),
                   (int32_t)sizeof(readback));
    TEST_ASSERT_MEM_EQ(readback, chunk, sizeof(readback));
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    for (uint32_t pass = 0; pass < 6; pass++) {
        TEST_ASSERT_EQ(bfs_file_read(&file, chunk, 64u * 1024u),
                       64u * 1024u);
        for (uint32_t i = 0; i < 64u * 1024u; i++) {
            uint8_t expected = (pass == 1 && i >= 123 && i < 123 + 4096)
                             ? 0xA7 : (uint8_t)(pass < 4 ? 0x31u : 0x31u + pass - 3);
            TEST_ASSERT_EQ(chunk[i], expected);
        }
    }
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    free(chunk);
    memory_destroy(&device);
}

static void test_mounted_warmed_append_write_failure_cuts_preserve_commit(void)
{
    const size_t image_size = (size_t)FS_BLOCK_SIZE * FS_BLOCK_COUNT;
    const uint32_t chunk_size = 64u * 1024u;
    memory_device_t device;
    bfs_fs_t fs;
    TEST_ASSERT(memory_init(&device, FS_BLOCK_SIZE, FS_BLOCK_COUNT));
    TEST_ASSERT(format_mount(&device, &fs, 0));
    fs.owned_nodes.disabled = true; /* qualifies the copy-on-write stock path */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "cut", 3, &ino),
                   BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(fsck_strict_clean(&fs));

    bfs_superblock_t published_sb = fs.txn.sb;
    published_root_t roots[3];
    TEST_ASSERT(capture_published_root(&device, published_sb.free_tree_root,
                                       &roots[0]));
    TEST_ASSERT(capture_published_root(&device, published_sb.dir_tree_root,
                                       &roots[1]));
    TEST_ASSERT(capture_published_root(&device, published_sb.inode_tree_root,
                                       &roots[2]));
    uint8_t *published_image = malloc(image_size);
    uint8_t *chunk = malloc(chunk_size);
    TEST_ASSERT(published_image != NULL && chunk != NULL);
    memcpy(published_image, device.bytes, image_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_fs_abandon(&fs);

    uint32_t injected_failures = 0;
    uint32_t successful_past_end = 0;
    for (uint32_t cut = 1; cut <= 64; cut++) {
        memcpy(device.bytes, published_image, image_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        device.fail_read = false;
        device.fail_write = false;
        device.partial_write = false;
        device.fail_write_countdown = 0;
        device.failed_write_at = 0;
        device.writes = 0;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &device.bio), BFS_OK);
        fs.owned_nodes.disabled = true; /* qualifies the copy-on-write stock path */
        TEST_ASSERT_EQ(fs.txn.sb.txn_id, published_sb.txn_id);

        bfs_file_t file;
        TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
        memset(chunk, (int)(0x60u + cut), chunk_size);
        for (uint32_t i = 0; i < 4; i++) {
            TEST_ASSERT_EQ(bfs_file_append(&file, chunk, chunk_size),
                           (int32_t)chunk_size);
        }
        TEST_ASSERT_EQ(fs.freespace.metadata_requests, REUSE_WARMUP);
        TEST_ASSERT(fs.freespace.reserve_count > HEIGHT_ONE_FLOOR);
        TEST_ASSERT_EQ(fs.txn.sb.txn_id, published_sb.txn_id);
        TEST_ASSERT(published_root_unchanged(&device, &roots[0]));
        TEST_ASSERT(published_root_unchanged(&device, &roots[1]));
        TEST_ASSERT(published_root_unchanged(&device, &roots[2]));

        bfs_superblock_t current_sb;
        TEST_ASSERT_EQ(bfs_sb_read(&device.bio, &current_sb), BFS_OK);
        TEST_ASSERT_EQ(current_sb.txn_id, published_sb.txn_id);
        TEST_ASSERT_EQ(current_sb.free_tree_root, published_sb.free_tree_root);
        TEST_ASSERT_EQ(current_sb.dir_tree_root, published_sb.dir_tree_root);
        TEST_ASSERT_EQ(current_sb.inode_tree_root, published_sb.inode_tree_root);
        TEST_ASSERT(published_superblocks_unchanged(&device, published_image,
                                                     &published_sb));

        device.writes = 0;
        device.fail_write_countdown = cut;
        device.partial_write = true;
        int32_t result = bfs_file_append(&file, chunk, chunk_size);
        bool injected = device.failed_write_at != 0;
        TEST_ASSERT_EQ(result == (int32_t)chunk_size, !injected);
        if (injected) {
            injected_failures++;
            TEST_ASSERT_EQ(device.failed_write_at, cut);
            TEST_ASSERT(result == BFS_ERR_IO ||
                        (result >= 0 && result < (int32_t)chunk_size));
        } else {
            successful_past_end++;
            TEST_ASSERT(device.writes < cut);
        }

        TEST_ASSERT_EQ(fs.txn.sb.txn_id, published_sb.txn_id);
        TEST_ASSERT(published_root_unchanged(&device, &roots[0]));
        TEST_ASSERT(published_root_unchanged(&device, &roots[1]));
        TEST_ASSERT(published_root_unchanged(&device, &roots[2]));
        TEST_ASSERT_EQ(bfs_sb_read(&device.bio, &current_sb), BFS_OK);
        TEST_ASSERT_EQ(current_sb.txn_id, published_sb.txn_id);
        TEST_ASSERT_EQ(current_sb.free_tree_root, published_sb.free_tree_root);
        TEST_ASSERT_EQ(current_sb.dir_tree_root, published_sb.dir_tree_root);
        TEST_ASSERT_EQ(current_sb.inode_tree_root, published_sb.inode_tree_root);
        TEST_ASSERT(published_superblocks_unchanged(&device, published_image,
                                                     &published_sb));

        bfs_fs_abandon(&fs); /* Deliberately discard unsynced append state. */
        bfs_fs_t check_fs;
        TEST_ASSERT_EQ(bfs_fs_mount_readonly(&check_fs, &device.bio), BFS_OK);
        TEST_ASSERT(fsck_strict_clean(&check_fs));
        TEST_ASSERT_EQ(bfs_file_open(&file, &check_fs, ino), BFS_OK);
        uint8_t byte;
        TEST_ASSERT_EQ(bfs_file_read(&file, &byte, 1), 0);
        TEST_ASSERT_EQ(bfs_fs_unmount(&check_fs), BFS_OK);
    }
    TEST_ASSERT(injected_failures > 0);
    TEST_ASSERT(successful_past_end > 0);
    free(chunk);
    free(published_image);
    memory_destroy(&device);
}

/* Read inode ino through the "before" snapshot and compare it with expected. */
static void check_snapshot_contents(bfs_fs_t *fs, bfs_bio_t *bio, uint32_t ino,
                                    const uint8_t *expected, uint8_t *actual,
                                    uint32_t size)
{
    bfs_snapshot_record_t record;
    TEST_ASSERT_EQ(bfs_snapshot_find_by_name(fs, "before", NULL, &record),
                   BFS_OK);
    bfs_dir_tree_t snapshot_dir;
    bfs_btree_t snapshot_inode;
    TEST_ASSERT_EQ(bfs_snapshot_open(&record, bio,
                    bfs_freespace_allocator(&fs->freespace), &snapshot_dir,
                    &snapshot_inode), BFS_OK);
    bfs_file_t snapshot_file;
    TEST_ASSERT_EQ(bfs_file_open_readonly_view(&snapshot_file, fs,
                                               &snapshot_inode, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&snapshot_file, actual, size), (int32_t)size);
    TEST_ASSERT_MEM_EQ(actual, expected, size);
}

static void snapshot_preservation_case(uint32_t options, bool copy_on_write)
{
    memory_device_t device;
    bfs_fs_t fs;
    TEST_ASSERT(memory_init(&device, FS_BLOCK_SIZE, FS_BLOCK_COUNT));
    TEST_ASSERT(format_mount(&device, &fs, options));
    fs.owned_nodes.disabled = copy_on_write;
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "snap", 4, &ino),
                   BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    /* Enough data/metadata to exceed warmup both before snapshot creation
     * and during its graph/refcount operations on an optionless volume. */
    uint8_t original[8u * 64u * 1024u], replacement[8u * 64u * 1024u];
    uint8_t actual[8u * 64u * 1024u];
    memset(original, 0x48, sizeof(original));
    memset(replacement, 0xC2, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_file_write(&file, original, sizeof(original)),
                   (int32_t)sizeof(original));
    /* Owned rewriting avoids the metadata churn that reaches the warmup. */
    if (options == 0 && copy_on_write)
        TEST_ASSERT_EQ(fs.freespace.metadata_requests, REUSE_WARMUP);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "before"), BFS_OK);
    TEST_ASSERT(fs.has_snapshots);
    TEST_ASSERT(fs.freespace.snapshot_state == &fs.has_snapshots);

    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_write(&file, replacement, sizeof(replacement)),
                   (int32_t)sizeof(replacement));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(fsck_clean(&fs));

    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&file, actual, sizeof(actual)),
                   (int32_t)sizeof(actual));
    TEST_ASSERT_MEM_EQ(actual, replacement, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&fs, &device.bio), BFS_OK);
    TEST_ASSERT(fsck_clean(&fs));

    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&file, actual, sizeof(actual)),
                   (int32_t)sizeof(actual));
    TEST_ASSERT_MEM_EQ(actual, replacement, sizeof(replacement));
    check_snapshot_contents(&fs, &device.bio, ino, original, actual, sizeof(actual));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    memory_destroy(&device);
}

static void test_snapshot_keeps_old_data_across_overwrite_and_remount(void)
{
    /* Runtime guard, independent of the option bit; with and without owned
     * node rewriting. */
    snapshot_preservation_case(0, true);
    snapshot_preservation_case(BFS_OPT_SNAPSHOTS, true);
    snapshot_preservation_case(0, false);
    snapshot_preservation_case(BFS_OPT_SNAPSHOTS, false);
}

TEST_SUITE_BEGIN("Mounted metadata free-block reuse")
    TEST_RUN(test_dealloc_stashes_without_free_tree_mutation);
    TEST_RUN(test_alloc_pops_stashed_suffix_above_height_one_floor);
    TEST_RUN(test_alloc_stashed_stock_uses_one_root_read);
    TEST_RUN(test_alloc_never_spends_the_height_one_floor);
    TEST_RUN(test_allocator_skips_inactive_historical_emergency_slots);
    TEST_RUN(test_duplicate_and_reserve_blocks_are_rejected_unchanged);
    TEST_RUN(test_metadata_stock_accepts_healthy_active_prefixes);
    TEST_RUN(test_full_healthy_metadata_stock_validates_and_pops);
    TEST_RUN(test_metadata_stock_fault_order_and_duplicate_positions);
    TEST_RUN(test_free_tree_overlap_is_rejected_unchanged);
    TEST_RUN(test_single_block_absence_checks_only_the_height_one_root);
    TEST_RUN(test_single_block_absence_rejects_bad_root_crc_on_first_read);
    TEST_RUN(test_single_block_absence_rejects_bad_key_order_on_first_read);
    TEST_RUN(test_single_block_absence_rejects_bad_selected_extent_on_first_read);
    TEST_RUN(test_live_and_committed_roots_are_never_stashed);
    TEST_RUN(test_active_emergency_return_does_not_enter_ordinary_stock);
    TEST_RUN(test_inactive_historical_emergency_dealloc_stays_out_of_reserve);
    TEST_RUN(test_validation_read_failure_does_not_stash_or_pop);
    TEST_RUN(test_write_fault_does_not_affect_write_free_metadata_stash);
    TEST_RUN(test_btree_partial_write_abort_restores_owned_spare);
    TEST_RUN(test_short_transactions_keep_legacy_and_warmup_saturates);
    TEST_RUN(test_metadata_warmup_resets_at_a_new_live_id);
    TEST_RUN(test_ineligible_contexts_and_full_stock_use_legacy_free_path);
    TEST_RUN(test_deeper_free_tree_uses_legacy_free_path);
    TEST_RUN(test_mounted_append_overwrite_commits_and_remount);
    TEST_RUN(test_mounted_warmed_append_write_failure_cuts_preserve_commit);
    TEST_RUN(test_snapshot_keeps_old_data_across_overwrite_and_remount);
TEST_SUITE_END()
