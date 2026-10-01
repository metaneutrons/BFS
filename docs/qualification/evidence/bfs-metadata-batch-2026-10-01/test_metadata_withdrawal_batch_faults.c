/* SPDX-License-Identifier: MPL-2.0 */
/* Fault cuts for the first mounted metadata-withdrawal refill. */
/* The hook can select a later live FreeTree COW write after real progress, but
 * cannot isolate post-swap retirement without a production fault hook. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_ondisk.h"
#include "bfs_superblock.h"

#define TEST_BLOCK_SIZE 4096u
#define TEST_BLOCK_COUNT 4096u
#define HEIGHT_ONE_FLOOR 20u
#define BASELINE_SIZE TEST_BLOCK_SIZE
#define FRAGMENTED_OWNED_BLOCKS 64u

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    bfs_btree_t *free_tree;
    uint64_t watched_txn;
    bfs_blk_t target_block;
    bool fault_armed;
    bool partial_write;
    bool fault_seen;
    bool target_was_free_tree_leaf;
    uint32_t fail_on_free_tree_leaf_attempt;
    uint32_t successful_free_tree_node_writes;
    bool capture_after_first_leaf_success;
    bool first_leaf_state_captured;
    bfs_fs_t *observed_fs;
    bfs_blk_t root_after_first_leaf_success;
    uint32_t total_free_after_first_leaf_success;
    bfs_blk_t roving_after_first_leaf_success;
    uint32_t target_attempts;
    uint32_t free_tree_node_attempts;
    uint32_t writes;
    uint8_t target_before[TEST_BLOCK_SIZE];
    uint8_t attempted[TEST_BLOCK_SIZE];
} fault_device_t;

typedef struct {
    fault_device_t device;
    bfs_fs_t fs;
    uint32_t baseline_ino;
    uint8_t baseline[BASELINE_SIZE];
} fixture_t;

typedef struct {
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    uint32_t reserve_count;
    uint32_t emergency_pool[BFS_EMERGENCY_POOL_SIZE];
    uint32_t emergency_count;
} stock_snapshot_t;

typedef struct {
    bfs_blk_t held[FRAGMENTED_OWNED_BLOCKS];
    uint32_t held_count;
    bfs_blk_t committed_free_root;
    uint8_t committed_root_bytes[TEST_BLOCK_SIZE];
    uint64_t committed_txn;
} fragmented_fixture_t;

static bool is_watched_free_tree_leaf(const fault_device_t *device,
                                      const void *buffer)
{
    if (!device->free_tree || !buffer) return false;
    uint8_t *node = (uint8_t *)buffer;
    const bfs_btnode_hdr_t *header = (const bfs_btnode_hdr_t *)buffer;
    if (bfs_be32(header->magic) != BFS_NODE_MAGIC ||
        bfs_be64(header->txn_id) != device->watched_txn || !is_leaf(node))
        return false;

    uint32_t count = num_keys(node);
    if (count == 0 || count > leaf_max_keys(device->free_tree)) return false;
    bfs_blk_t previous_start = 0;
    bfs_blk_t previous_end = 0;
    for (uint32_t i = 0; i < count; i++) {
        bfs_blk_t start = bfs_load_be32(node_key(device->free_tree, node, i));
        uint32_t length = bfs_load_be32(leaf_val(device->free_tree, node, i));
        if (start < bfs_data_start_block(device->bio.block_size) ||
            start >= device->bio.block_count || length == 0 ||
            length > device->bio.block_count - start ||
            (i > 0 && (start <= previous_start || start < previous_end)))
            return false;
        previous_start = start;
        previous_end = start + length;
    }
    return true;
}

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    fault_device_t *device = (fault_device_t *)bio;
    if (!buffer || block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, device->bytes + (size_t)block * bio->block_size,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    fault_device_t *device = (fault_device_t *)bio;
    if (!buffer || block >= bio->block_count) return BFS_ERR_INVAL;
    device->writes++;
    if (device->capture_after_first_leaf_success &&
        !device->first_leaf_state_captured &&
        device->successful_free_tree_node_writes == 1 &&
        device->observed_fs) {
        device->root_after_first_leaf_success =
            device->observed_fs->freespace.tree.root;
        device->total_free_after_first_leaf_success =
            device->observed_fs->freespace.total_free;
        device->roving_after_first_leaf_success =
            device->observed_fs->freespace.roving;
        device->first_leaf_state_captured = true;
    }

    bool watched_leaf = is_watched_free_tree_leaf(device, buffer);
    if (watched_leaf)
        device->free_tree_node_attempts++;

    bool selected_leaf_attempt = watched_leaf && device->fault_armed &&
        device->fail_on_free_tree_leaf_attempt != 0 &&
        device->free_tree_node_attempts ==
            device->fail_on_free_tree_leaf_attempt;
    bool selected_block = device->fault_armed &&
        device->fail_on_free_tree_leaf_attempt == 0 &&
        block == device->target_block;
    if (selected_leaf_attempt || selected_block) {
        device->fault_seen = true;
        device->target_attempts++;
        device->target_block = block;
        device->target_was_free_tree_leaf = watched_leaf;
        memcpy(device->target_before,
               device->bytes + (size_t)block * bio->block_size,
               bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        memcpy(device->attempted, buffer, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        if (device->partial_write)
            memcpy(device->bytes + (size_t)block * bio->block_size,
                   buffer, bio->block_size / 2); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        return BFS_ERR_IO;
    }

    memcpy(device->bytes + (size_t)block * bio->block_size, buffer,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (watched_leaf) device->successful_free_tree_node_writes++;
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

static void fixture_destroy(fixture_t *fixture);

static bool fixture_init(fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->device.bytes = calloc(TEST_BLOCK_COUNT, TEST_BLOCK_SIZE);
    if (!fixture->device.bytes) return false;
    fixture->device.bio.ops = &memory_ops;
    fixture->device.bio.block_size = TEST_BLOCK_SIZE;
    fixture->device.bio.block_count = TEST_BLOCK_COUNT;

    if (bfs_fs_format(&fixture->device.bio, "MetadataFault", 0) != BFS_OK ||
        bfs_fs_mount(&fixture->fs, &fixture->device.bio) != BFS_OK)
        goto fail;

    if (bfs_fs_create_file(&fixture->fs, BFS_ROOT_INO, "baseline", 8,
                           &fixture->baseline_ino) != BFS_OK)
        goto fail;
    for (uint32_t i = 0; i < sizeof(fixture->baseline); i++)
        fixture->baseline[i] = (uint8_t)(0xA9u + i * 29u);
    bfs_file_t file;
    if (bfs_file_open(&file, &fixture->fs, fixture->baseline_ino) != BFS_OK ||
        bfs_file_write(&file, fixture->baseline, sizeof(fixture->baseline)) !=
            (int32_t)sizeof(fixture->baseline) ||
        bfs_fs_sync(&fixture->fs) != BFS_OK)
        goto fail;

    uint64_t root_txn = 0;
    bfs_fs_t *fs = &fixture->fs;
    bool valid = fs->freespace.mounted_owner == fs &&
           fs->freespace.tree.height == 1 &&
           bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) == BFS_OK &&
           root_txn == bfs_be64(fs->txn.sb.txn_id) &&
           fs->live_txn_id == bfs_txn_id(&fs->txn) &&
           fs->live_txn_id > bfs_be64(fs->txn.sb.txn_id) &&
           fs->freespace.reserve_count <= HEIGHT_ONE_FLOOR &&
           fs->pending_count == 0 && fs->recovery_error == BFS_OK &&
           fs->freespace.metadata_batch_txn != fs->live_txn_id;
    if (valid) return true;

fail:
    fixture_destroy(fixture);
    return false;
}

static void fixture_destroy(fixture_t *fixture)
{
    if (fixture->fs.mounted) bfs_fs_abandon(&fixture->fs);
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
}

static bfs_blk_t next_cow_scratch(const bfs_fs_t *fs)
{
    const bfs_freespace_t *space = &fs->freespace;
    if (space->reserve_count > 0)
        return space->reserve[space->reserve_count - 1];
    uint32_t emergency_count = bfs_be32(space->sb->emergency_count);
    if (emergency_count > 0)
        return bfs_be32(space->sb->emergency_pool[emergency_count - 1]);
    return BFS_BLK_NULL;
}

static bool reserve_contains(const bfs_freespace_t *space, bfs_blk_t block)
{
    for (uint32_t i = 0; i < space->reserve_count; i++)
        if (space->reserve[i] == block) return true;
    return false;
}

static bool active_emergency_contains(const bfs_freespace_t *space,
                                     bfs_blk_t block)
{
    uint32_t count = bfs_be32(space->sb->emergency_count);
    if (count > BFS_EMERGENCY_POOL_SIZE) return false;
    for (uint32_t i = 0; i < count; i++)
        if (bfs_be32(space->sb->emergency_pool[i]) == block) return true;
    return false;
}

static bool block_in_list(const bfs_blk_t *blocks, uint32_t count,
                          bfs_blk_t block)
{
    for (uint32_t i = 0; i < count; i++)
        if (blocks[i] == block) return true;
    return false;
}

static bool stock_is_distinct_and_in_range(const bfs_freespace_t *space)
{
    uint32_t emergency_count = bfs_be32(space->sb->emergency_count);
    if (space->reserve_count > BFS_ALLOC_RESERVE_SIZE ||
        emergency_count > BFS_EMERGENCY_POOL_SIZE)
        return false;
    for (uint32_t i = 0; i < space->reserve_count; i++) {
        bfs_blk_t block = space->reserve[i];
        if (block == BFS_BLK_NULL || block >= space->tree.bio->block_count)
            return false;
        for (uint32_t j = 0; j < i; j++)
            if (space->reserve[j] == block) return false;
        for (uint32_t j = 0; j < emergency_count; j++)
            if (bfs_be32(space->sb->emergency_pool[j]) == block)
                return false;
    }
    for (uint32_t i = 0; i < emergency_count; i++) {
        bfs_blk_t block = bfs_be32(space->sb->emergency_pool[i]);
        if (block == BFS_BLK_NULL || block >= space->tree.bio->block_count)
            return false;
        for (uint32_t j = 0; j < i; j++)
            if (bfs_be32(space->sb->emergency_pool[j]) == block)
                return false;
    }
    return true;
}

static bool stock_snapshot_take(const bfs_freespace_t *space,
                                stock_snapshot_t *snapshot)
{
    if (!space || !space->sb || !snapshot ||
        !stock_is_distinct_and_in_range(space))
        return false;
    snapshot->reserve_count = space->reserve_count;
    memcpy(snapshot->reserve, space->reserve, sizeof(snapshot->reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    snapshot->emergency_count = bfs_be32(space->sb->emergency_count);
    memcpy(snapshot->emergency_pool, space->sb->emergency_pool,
           sizeof(snapshot->emergency_pool)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return true;
}

/* A failed first refill must preserve every pre-existing stock block exactly
 * once. The only allowed provenance change is the popped emergency scratch
 * returning to the reserve after a failed COW write. Array order is not part
 * of the ownership oracle. */
static bool failed_refill_preserved_stock(
    const stock_snapshot_t *before, const bfs_freespace_t *space,
    bfs_blk_t scratch, bool *representation_unchanged)
{
    if (!before || !space || !representation_unchanged ||
        !stock_is_distinct_and_in_range(space))
        return false;

    stock_snapshot_t after;
    after.reserve_count = space->reserve_count;
    memcpy(after.reserve, space->reserve, sizeof(after.reserve)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    after.emergency_count = bfs_be32(space->sb->emergency_count);
    memcpy(after.emergency_pool, space->sb->emergency_pool,
           sizeof(after.emergency_pool)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory

    bool scratch_was_reserve =
        block_in_list(before->reserve, before->reserve_count, scratch);
    bfs_blk_t before_emergency[BFS_EMERGENCY_POOL_SIZE];
    for (uint32_t i = 0; i < before->emergency_count; i++)
        before_emergency[i] = bfs_be32(before->emergency_pool[i]);
    bool scratch_was_emergency =
        block_in_list(before_emergency, before->emergency_count, scratch);
    if (scratch_was_reserve == scratch_was_emergency) return false;

    bool scratch_is_reserve = reserve_contains(space, scratch);
    bool scratch_is_emergency = active_emergency_contains(space, scratch);
    if (scratch_is_reserve == scratch_is_emergency) return false;
    if (scratch_was_reserve && !scratch_is_reserve) return false;

    bool scratch_transferred = scratch_was_emergency && scratch_is_reserve;
    uint32_t expected_reserve_count = before->reserve_count +
        (scratch_transferred ? 1u : 0u);
    uint32_t expected_emergency_count = before->emergency_count -
        (scratch_transferred ? 1u : 0u);
    if (after.reserve_count != expected_reserve_count ||
        after.emergency_count != expected_emergency_count)
        return false;

    for (uint32_t i = 0; i < before->reserve_count; i++) {
        bfs_blk_t block = before->reserve[i];
        if (block != scratch && !reserve_contains(space, block)) return false;
    }
    for (uint32_t i = 0; i < before->emergency_count; i++) {
        bfs_blk_t block = bfs_be32(before->emergency_pool[i]);
        if (block != scratch && !active_emergency_contains(space, block))
            return false;
    }
    for (uint32_t i = 0; i < after.reserve_count; i++) {
        bfs_blk_t block = after.reserve[i];
        bool was_reserve = block_in_list(before->reserve,
                                         before->reserve_count, block);
        if (!was_reserve && !(block == scratch && scratch_was_emergency))
            return false;
    }
    for (uint32_t i = 0; i < after.emergency_count; i++) {
        bfs_blk_t block = bfs_be32(after.emergency_pool[i]);
        bool was_emergency = block_in_list(before_emergency,
                                           before->emergency_count, block);
        if (!was_emergency) return false;
    }

    *representation_unchanged =
        before->reserve_count == after.reserve_count &&
        memcmp(before->reserve, after.reserve, sizeof(before->reserve)) == 0 &&
        before->emergency_count == after.emergency_count &&
        memcmp(before->emergency_pool, after.emergency_pool,
               sizeof(before->emergency_pool)) == 0;
    return true;
}

static bool free_tree_contains(bfs_fs_t *fs, bfs_blk_t block)
{
    uint32_t search = bfs_be32(block), key = 0, length = 0;
    if (bfs_btree_search_floor(&fs->freespace.tree, &search, &key,
                               &length) != BFS_OK)
        return false;
    bfs_blk_t start = bfs_be32(key);
    uint32_t count = bfs_be32(length);
    return block >= start && block - start < count;
}

static bool block_is_root(const bfs_fs_t *fs, bfs_blk_t block)
{
    const bfs_superblock_t *superblocks[] = {
        &fs->txn.sb, &fs->txn.sb_new,
    };
    if (block == fs->freespace.tree.root ||
        block == fs->dir_tree.tree.root ||
        block == fs->inode_tree.root ||
        block == fs->refcount.tree.root)
        return true;
    for (size_t i = 0; i < sizeof(superblocks) / sizeof(superblocks[0]); i++) {
        const bfs_superblock_t *sb = superblocks[i];
        if (block == bfs_be32(sb->dir_tree_root) ||
            block == bfs_be32(sb->extent_tree_root) ||
            block == bfs_be32(sb->inode_tree_root) ||
            block == bfs_be32(sb->free_tree_root) ||
            block == bfs_be32(sb->refcount_tree_root) ||
            block == bfs_be32(sb->snapshot_tree_root))
            return true;
    }
    return false;
}

static bool pending_contains(const bfs_fs_t *fs, bfs_blk_t block)
{
    for (uint32_t i = 0; i < fs->pending_count; i++)
        if (bfs_fs_pending_items((bfs_fs_t *)fs)[i] == block) return true;
    return false;
}

static void sort_blocks_ascending(bfs_blk_t *blocks, uint32_t count)
{
    for (uint32_t i = 1; i < count; i++) {
        bfs_blk_t value = blocks[i];
        uint32_t j = i;
        while (j > 0 && blocks[j - 1] > value) {
            blocks[j] = blocks[j - 1];
            j--;
        }
        blocks[j] = value;
    }
}

/* Create real allocator ownership and alternate frees across the allocated
 * high-address blocks. The final operation frees the highest block, leaving a
 * one-block top extent before the batch's bounded tail withdrawal. */
static bool prepare_fragmented_fixture(fixture_t *fixture,
                                       fragmented_fixture_t *fragmented)
{
    if (!fixture || !fragmented || !fixture->fs.mounted) return false;
    memset(fragmented, 0, sizeof(*fragmented));
    bfs_fs_t *fs = &fixture->fs;
    fragmented->committed_txn = bfs_be64(fs->txn.sb.txn_id);
    fragmented->committed_free_root =
        bfs_be32(fs->txn.sb.free_tree_root);
    if (fragmented->committed_free_root == BFS_BLK_NULL ||
        fragmented->committed_free_root >= TEST_BLOCK_COUNT)
        return false;
    memcpy(fragmented->committed_root_bytes,
           fixture->device.bytes +
               (size_t)fragmented->committed_free_root * TEST_BLOCK_SIZE,
           sizeof(fragmented->committed_root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory

    bfs_blk_t owned[FRAGMENTED_OWNED_BLOCKS];
    bool ok = true;
    bfs_lock_write(&fs->lock);
    for (uint32_t i = 0; i < FRAGMENTED_OWNED_BLOCKS; i++) {
        owned[i] = bfs_freespace_alloc(&fs->freespace, 1);
        if (owned[i] == BFS_BLK_NULL || owned[i] >= TEST_BLOCK_COUNT) {
            ok = false;
            break;
        }
        for (uint32_t j = 0; j < i; j++)
            if (owned[j] == owned[i]) ok = false;
        if (!ok) break;
    }
    if (ok) {
        sort_blocks_ascending(owned, FRAGMENTED_OWNED_BLOCKS);
        for (uint32_t i = 0; i < FRAGMENTED_OWNED_BLOCKS; i++) {
            if ((i & 1u) == 0) {
                fragmented->held[fragmented->held_count++] = owned[i];
                continue;
            }
            if (bfs_freespace_free(&fs->freespace, owned[i], 1) != BFS_OK) {
                ok = false;
                break;
            }
        }
    }
    bfs_lock_unlock(&fs->lock);
    if (!ok || fragmented->held_count != FRAGMENTED_OWNED_BLOCKS / 2 ||
        fs->freespace.tree.height != 1 ||
        fs->freespace.reserve_count > HEIGHT_ONE_FLOOR ||
        fs->recovery_error != BFS_OK || fs->freespace.metadata_batch_txn ==
            fs->live_txn_id)
        return false;

    uint64_t root_txn = 0;
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK ||
        root_txn != fs->live_txn_id || !free_tree_contains(fs, owned[63]))
        return false;
    for (uint32_t i = 0; i < fragmented->held_count; i++) {
        bfs_blk_t block = fragmented->held[i];
        if (free_tree_contains(fs, block) ||
            reserve_contains(&fs->freespace, block) ||
            active_emergency_contains(&fs->freespace, block) ||
            pending_contains(fs, block) || block_is_root(fs, block))
            return false;
    }
    return true;
}

static bool mounted_file_matches(bfs_fs_t *fs, uint32_t ino,
                                 const uint8_t *expected, uint32_t length)
{
    bfs_file_t file;
    if (bfs_file_open(&file, fs, ino) != BFS_OK) return false;
    uint8_t actual[BASELINE_SIZE];
    if (length > sizeof(actual) ||
        bfs_file_read(&file, actual, length) != (int32_t)length ||
        memcmp(actual, expected, length) != 0)
        return false;
    uint8_t extra;
    return bfs_file_read(&file, &extra, 1) == 0;
}

static bool remount_committed_oracle(fixture_t *fixture, uint32_t ino,
                                     uint64_t committed_txn)
{
    bfs_fs_abandon(&fixture->fs);
    if (bfs_fs_mount_readonly(&fixture->fs, &fixture->device.bio) != BFS_OK)
        return false;
    bfs_fsck_report_t report = {0};
    bool clean = bfs_be64(fixture->fs.txn.sb.txn_id) == committed_txn &&
        bfs_fs_check(&fixture->fs, false, &report) == BFS_OK &&
        report.errors == 0 && report.warnings == 0 && report.leaked_blocks == 0 &&
        mounted_file_matches(&fixture->fs, ino, fixture->baseline,
                             sizeof(fixture->baseline));
    bfs_err_t unmount_err = bfs_fs_unmount(&fixture->fs);
    return clean && unmount_err == BFS_OK;
}

static void exercise_initial_refill_write_failure(bool partial)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_blk_t old_root = fs->freespace.tree.root;
    uint8_t old_root_bytes[TEST_BLOCK_SIZE];
    memcpy(old_root_bytes,
           fixture.device.bytes + (size_t)old_root * TEST_BLOCK_SIZE,
           sizeof(old_root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_blk_t target = next_cow_scratch(fs);
    TEST_ASSERT(target != BFS_BLK_NULL && target < TEST_BLOCK_COUNT);
    TEST_ASSERT(target != old_root);
    TEST_ASSERT(!free_tree_contains(fs, target));
    stock_snapshot_t stock_before;
    TEST_ASSERT(stock_snapshot_take(&fs->freespace, &stock_before));
    uint32_t writes_before = fixture.device.writes;
    uint8_t target_before[TEST_BLOCK_SIZE];
    memcpy(target_before,
           fixture.device.bytes + (size_t)target * TEST_BLOCK_SIZE,
           sizeof(target_before)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory

    uint64_t live_txn = fs->live_txn_id;
    uint64_t committed_txn = bfs_be64(fs->txn.sb.txn_id);
    uint32_t total_free = fs->freespace.total_free;
    bfs_blk_t roving = fs->freespace.roving;
    fixture.device.free_tree = &fs->freespace.tree;
    fixture.device.watched_txn = live_txn;
    fixture.device.target_block = target;
    fixture.device.partial_write = partial;
    fixture.device.fault_armed = true;

    bfs_allocator_t *iface = bfs_freespace_allocator(&fs->freespace);
    bfs_lock_write(&fs->lock);
    bfs_blk_t result = iface->alloc(iface);
    bfs_err_t allocation_error = iface->error(iface);
    bfs_lock_unlock(&fs->lock);
    fixture.device.fault_armed = false;

    TEST_ASSERT(fixture.device.fault_seen);
    TEST_ASSERT_EQ(fixture.device.target_attempts, 1);
    TEST_ASSERT(fixture.device.target_was_free_tree_leaf);
    TEST_ASSERT_EQ(fixture.device.free_tree_node_attempts, 1);
    TEST_ASSERT(fixture.device.writes > writes_before);
    TEST_ASSERT_EQ(result, BFS_BLK_NULL);
    TEST_ASSERT_EQ(allocation_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fs->freespace.last_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fs->freespace.metadata_batch_txn == live_txn, false);
    bool stock_representation_unchanged = false;
    TEST_ASSERT(failed_refill_preserved_stock(&stock_before, &fs->freespace,
                                              target,
                                              &stock_representation_unchanged));
    /* The allocator conservatively latches any byte-level pool representation
     * change, even when this oracle proves the emergency scratch moved to the
     * reserve exactly once. */
    TEST_ASSERT_EQ(fs->recovery_error,
                   stock_representation_unchanged ? BFS_OK : BFS_ERR_IO);
    TEST_ASSERT_EQ(fs->freespace.tree.free_sink_err, BFS_OK);
    TEST_ASSERT_EQ(fs->freespace.in_alloc, false);
    TEST_ASSERT_EQ(fs->freespace.tree.root, old_root);
    TEST_ASSERT_EQ(fs->freespace.tree.height, 1);
    TEST_ASSERT_EQ(fs->freespace.total_free, total_free);
    TEST_ASSERT_EQ(fs->freespace.roving, roving);
    TEST_ASSERT(!free_tree_contains(fs, target));
    TEST_ASSERT_MEM_EQ(fixture.device.bytes + (size_t)old_root * TEST_BLOCK_SIZE,
                       old_root_bytes, sizeof(old_root_bytes));

    const uint8_t *target_after = fixture.device.bytes +
        (size_t)target * TEST_BLOCK_SIZE;
    if (partial) {
        TEST_ASSERT_MEM_EQ(target_after, fixture.device.attempted,
                           TEST_BLOCK_SIZE / 2);
        TEST_ASSERT_MEM_EQ(target_after + TEST_BLOCK_SIZE / 2,
                           target_before + TEST_BLOCK_SIZE / 2,
                           TEST_BLOCK_SIZE / 2);
    } else {
        TEST_ASSERT_MEM_EQ(target_after, target_before, TEST_BLOCK_SIZE);
    }

    if (fs->recovery_error == BFS_OK) {
        bfs_fsck_report_t report = {0};
        TEST_ASSERT_EQ(bfs_fs_check(fs, false, &report), BFS_OK);
        TEST_ASSERT_EQ(report.errors, 0);
        TEST_ASSERT_EQ(report.warnings, 0);
        TEST_ASSERT_EQ(report.leaked_blocks, 0);
        TEST_ASSERT(mounted_file_matches(fs, fixture.baseline_ino,
                                         fixture.baseline,
                                         sizeof(fixture.baseline)));
    }
    TEST_ASSERT(remount_committed_oracle(&fixture, fixture.baseline_ino,
                                         committed_txn));
    fixture_destroy(&fixture);
}

static void test_successful_first_request_batch_with_baseline(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    uint64_t live_txn = fs->live_txn_id;
    uint32_t writes_before = fixture.device.writes;

    bfs_allocator_t *iface = bfs_freespace_allocator(&fs->freespace);
    bfs_lock_write(&fs->lock);
    bfs_blk_t allocated = iface->alloc(iface);
    bfs_lock_unlock(&fs->lock);

    TEST_ASSERT(allocated != BFS_BLK_NULL && allocated < TEST_BLOCK_COUNT);
    TEST_ASSERT_EQ(fs->freespace.last_error, BFS_OK);
    TEST_ASSERT_EQ(fs->freespace.metadata_batch_txn, live_txn);
    TEST_ASSERT_EQ(fs->live_txn_id, live_txn);
    TEST_ASSERT_EQ(fs->freespace.metadata_requests, 1);
    TEST_ASSERT(fs->freespace.reserve_count >= HEIGHT_ONE_FLOOR);
    TEST_ASSERT_EQ(fs->recovery_error, BFS_OK);
    TEST_ASSERT_EQ(fs->freespace.tree.free_sink_err, BFS_OK);
    TEST_ASSERT(fixture.device.writes > writes_before);
    TEST_ASSERT(stock_is_distinct_and_in_range(&fs->freespace));
    TEST_ASSERT(!free_tree_contains(fs, allocated));
    TEST_ASSERT(!reserve_contains(&fs->freespace, allocated));
    TEST_ASSERT(!active_emergency_contains(&fs->freespace, allocated));
    uint32_t pending_before_return = fs->pending_count;
    TEST_ASSERT(!pending_contains(fs, allocated));
    TEST_ASSERT(!block_is_root(fs, allocated));
    TEST_ASSERT(mounted_file_matches(fs, fixture.baseline_ino,
                                     fixture.baseline,
                                     sizeof(fixture.baseline)));

    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(fs, allocated), BFS_OK);
    TEST_ASSERT_EQ(fs->pending_count, pending_before_return + 1u);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_OK);
    TEST_ASSERT_EQ(fs->pending_count, 0);
    uint64_t committed_txn = bfs_be64(fs->txn.sb.txn_id);
    bfs_fsck_report_t report = {0};
    TEST_ASSERT_EQ(bfs_fs_check(fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    TEST_ASSERT_EQ(report.warnings, 0);
    TEST_ASSERT_EQ(report.leaked_blocks, 0);
    TEST_ASSERT(mounted_file_matches(fs, fixture.baseline_ino,
                                     fixture.baseline,
                                     sizeof(fixture.baseline)));
    TEST_ASSERT(remount_committed_oracle(&fixture, fixture.baseline_ino,
                                         committed_txn));
    fixture_destroy(&fixture);
}

static void test_fragmented_first_request_produces_multiple_tail_writes(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    fragmented_fixture_t fragmented;
    TEST_ASSERT(prepare_fragmented_fixture(&fixture, &fragmented));
    bfs_fs_t *fs = &fixture.fs;
    uint64_t live_txn = fs->live_txn_id;
    uint32_t total_free_before = fs->freespace.total_free;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint32_t writes_before = fixture.device.writes;
    TEST_ASSERT(fs->freespace.metadata_reuse_txn != live_txn ||
                fs->freespace.metadata_requests == 0);

    fixture.device.free_tree = &fs->freespace.tree;
    fixture.device.watched_txn = live_txn;
    fixture.device.observed_fs = fs;
    bfs_allocator_t *iface = bfs_freespace_allocator(&fs->freespace);
    bfs_lock_write(&fs->lock);
    bfs_blk_t allocated = iface->alloc(iface);
    bfs_lock_unlock(&fs->lock);

    TEST_ASSERT(allocated != BFS_BLK_NULL && allocated < TEST_BLOCK_COUNT);
    TEST_ASSERT_EQ(fs->freespace.last_error, BFS_OK);
    TEST_ASSERT_EQ(fs->freespace.metadata_batch_txn, live_txn);
    TEST_ASSERT_EQ(fs->freespace.metadata_reuse_txn, live_txn);
    TEST_ASSERT_EQ(fs->freespace.metadata_requests, 1);
    TEST_ASSERT(fs->freespace.reserve_count >= HEIGHT_ONE_FLOOR);
    TEST_ASSERT(fs->freespace.tree.root != root_before);
    TEST_ASSERT(fs->freespace.total_free < total_free_before);
    TEST_ASSERT(fixture.device.writes > writes_before);
    TEST_ASSERT(fixture.device.free_tree_node_attempts >= 2);
    TEST_ASSERT_EQ(fixture.device.successful_free_tree_node_writes,
                   fixture.device.free_tree_node_attempts);
    TEST_ASSERT(stock_is_distinct_and_in_range(&fs->freespace));
    TEST_ASSERT(!free_tree_contains(fs, allocated));
    TEST_ASSERT(!reserve_contains(&fs->freespace, allocated));
    TEST_ASSERT(!active_emergency_contains(&fs->freespace, allocated));
    TEST_ASSERT(!pending_contains(fs, allocated));
    TEST_ASSERT(!block_is_root(fs, allocated));
    for (uint32_t i = 0; i < fragmented.held_count; i++)
        TEST_ASSERT(allocated != fragmented.held[i]);
    TEST_ASSERT_MEM_EQ(fixture.device.bytes +
                           (size_t)fragmented.committed_free_root *
                               TEST_BLOCK_SIZE,
                       fragmented.committed_root_bytes,
                       sizeof(fragmented.committed_root_bytes));

    uint32_t pending_before_return = fs->pending_count;
    for (uint32_t i = 0; i < fragmented.held_count; i++)
        TEST_ASSERT_EQ(bfs_fs_queue_pending_free(fs, fragmented.held[i]),
                       BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(fs, allocated), BFS_OK);
    TEST_ASSERT_EQ(fs->pending_count,
                   pending_before_return + fragmented.held_count + 1u);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_OK);
    TEST_ASSERT_EQ(fs->pending_count, 0);
    uint64_t committed_txn = bfs_be64(fs->txn.sb.txn_id);
    bfs_fsck_report_t report = {0};
    TEST_ASSERT_EQ(bfs_fs_check(fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    TEST_ASSERT_EQ(report.warnings, 0);
    TEST_ASSERT_EQ(report.leaked_blocks, 0);
    TEST_ASSERT(mounted_file_matches(fs, fixture.baseline_ino,
                                     fixture.baseline,
                                     sizeof(fixture.baseline)));
    TEST_ASSERT(remount_committed_oracle(&fixture, fixture.baseline_ino,
                                         committed_txn));
    fixture_destroy(&fixture);
}

static void exercise_second_refill_leaf_write_failure(bool partial)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    fragmented_fixture_t fragmented;
    TEST_ASSERT(prepare_fragmented_fixture(&fixture, &fragmented));
    bfs_fs_t *fs = &fixture.fs;
    uint64_t live_txn = fs->live_txn_id;
    uint64_t root_txn = 0;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn),
                   BFS_OK);
    TEST_ASSERT_EQ(root_txn, live_txn);
    TEST_ASSERT(fs->freespace.reserve_count <= HEIGHT_ONE_FLOOR);
    TEST_ASSERT(fs->freespace.metadata_reuse_txn != live_txn ||
                fs->freespace.metadata_requests == 0);

    bfs_blk_t prebatch_root = fs->freespace.tree.root;
    uint32_t prebatch_total_free = fs->freespace.total_free;
    uint8_t prebatch_root_bytes[TEST_BLOCK_SIZE];
    memcpy(prebatch_root_bytes,
           fixture.device.bytes + (size_t)prebatch_root * TEST_BLOCK_SIZE,
           sizeof(prebatch_root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory

    fixture.device.free_tree = &fs->freespace.tree;
    fixture.device.watched_txn = live_txn;
    fixture.device.observed_fs = fs;
    fixture.device.fail_on_free_tree_leaf_attempt = 2;
    fixture.device.capture_after_first_leaf_success = true;
    fixture.device.partial_write = partial;
    fixture.device.fault_armed = true;
    bfs_allocator_t *iface = bfs_freespace_allocator(&fs->freespace);
    bfs_lock_write(&fs->lock);
    bfs_blk_t result = iface->alloc(iface);
    bfs_err_t allocation_error = iface->error(iface);
    bfs_lock_unlock(&fs->lock);
    fixture.device.fault_armed = false;

    TEST_ASSERT(fixture.device.fault_seen);
    TEST_ASSERT(fixture.device.target_was_free_tree_leaf);
    TEST_ASSERT_EQ(fixture.device.target_attempts, 1);
    TEST_ASSERT_EQ(fixture.device.free_tree_node_attempts, 2);
    TEST_ASSERT_EQ(fixture.device.successful_free_tree_node_writes, 1);
    TEST_ASSERT(fixture.device.first_leaf_state_captured);
    TEST_ASSERT(fixture.device.root_after_first_leaf_success != prebatch_root);
    TEST_ASSERT_EQ(fixture.device.total_free_after_first_leaf_success,
                   prebatch_total_free - 1u);
    TEST_ASSERT_EQ(result, BFS_BLK_NULL);
    TEST_ASSERT_EQ(allocation_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fs->freespace.last_error, BFS_ERR_IO);
    TEST_ASSERT(fs->freespace.metadata_batch_txn != live_txn);
    TEST_ASSERT_EQ(fs->recovery_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fs->freespace.tree.root,
                   fixture.device.root_after_first_leaf_success);
    TEST_ASSERT_EQ(fs->freespace.total_free,
                   fixture.device.total_free_after_first_leaf_success);
    TEST_ASSERT_EQ(fs->freespace.roving,
                   fixture.device.roving_after_first_leaf_success);
    TEST_ASSERT_EQ(fs->freespace.tree.height, 1);
    TEST_ASSERT_MEM_EQ(fixture.device.bytes +
                           (size_t)fragmented.committed_free_root *
                               TEST_BLOCK_SIZE,
                       fragmented.committed_root_bytes,
                       sizeof(fragmented.committed_root_bytes));
    TEST_ASSERT_MEM_EQ(fixture.device.bytes +
                           (size_t)prebatch_root * TEST_BLOCK_SIZE,
                       prebatch_root_bytes, sizeof(prebatch_root_bytes));

    const uint8_t *target_after = fixture.device.bytes +
        (size_t)fixture.device.target_block * TEST_BLOCK_SIZE;
    if (partial) {
        TEST_ASSERT_MEM_EQ(target_after, fixture.device.attempted,
                           TEST_BLOCK_SIZE / 2);
        TEST_ASSERT_MEM_EQ(target_after + TEST_BLOCK_SIZE / 2,
                           fixture.device.target_before + TEST_BLOCK_SIZE / 2,
                           TEST_BLOCK_SIZE / 2);
    } else {
        TEST_ASSERT_MEM_EQ(target_after, fixture.device.target_before,
                           TEST_BLOCK_SIZE);
    }

    uint32_t writes_after_fault = fixture.device.writes;
    bfs_lock_write(&fs->lock);
    bfs_blk_t retry_result = iface->alloc(iface);
    bfs_err_t retry_error = iface->error(iface);
    bfs_lock_unlock(&fs->lock);
    TEST_ASSERT_EQ(retry_result, BFS_BLK_NULL);
    TEST_ASSERT_EQ(retry_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.writes, writes_after_fault);
    TEST_ASSERT_EQ(fs->recovery_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.writes, writes_after_fault);

    TEST_ASSERT(remount_committed_oracle(&fixture, fixture.baseline_ino,
                                         fragmented.committed_txn));
    fixture_destroy(&fixture);
}

static void test_full_second_refill_leaf_write_failure_after_progress(void)
{
    exercise_second_refill_leaf_write_failure(false);
}

static void test_prefix_second_refill_leaf_write_failure_after_progress(void)
{
    exercise_second_refill_leaf_write_failure(true);
}

static void test_full_initial_refill_write_failure_preserves_root(void)
{
    exercise_initial_refill_write_failure(false);
}

static void test_prefix_initial_refill_write_failure_preserves_root(void)
{
    exercise_initial_refill_write_failure(true);
}

static void test_pending_headroom_declines_without_refill_write(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_lock_write(&fs->lock);

    /* Create real pending ownership: allocation COWs the committed Free-Tree
     * root, and the returned ordinary block is queued rather than abandoned. */
    bfs_blk_t allocated = bfs_freespace_alloc(&fs->freespace, 1);
    TEST_ASSERT(allocated != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(fs, allocated), BFS_OK);
    TEST_ASSERT(fs->pending_count > 0);
    fs->pending_frees_cap = fs->pending_count;
    TEST_ASSERT_EQ(fs->freespace.tree.height, 1);
    TEST_ASSERT(fs->freespace.reserve_count <= HEIGHT_ONE_FLOOR);

    bfs_blk_t old_root = fs->freespace.tree.root;
    uint8_t old_root_bytes[TEST_BLOCK_SIZE];
    memcpy(old_root_bytes,
           fixture.device.bytes + (size_t)old_root * TEST_BLOCK_SIZE,
           sizeof(old_root_bytes)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    uint32_t total_free = fs->freespace.total_free;
    uint32_t reserve_count = fs->freespace.reserve_count;
    uint64_t live_txn = fs->live_txn_id;
    fixture.device.writes = 0;

    bfs_allocator_t *iface = bfs_freespace_allocator(&fs->freespace);
    bfs_blk_t result = iface->alloc(iface);
    bfs_err_t allocation_error = iface->error(iface);
    TEST_ASSERT_EQ(result, BFS_BLK_NULL);
    TEST_ASSERT_EQ(allocation_error, BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fs->freespace.tree.root, old_root);
    TEST_ASSERT_MEM_EQ(fixture.device.bytes + (size_t)old_root * TEST_BLOCK_SIZE,
                       old_root_bytes, sizeof(old_root_bytes));
    TEST_ASSERT_EQ(fs->freespace.total_free, total_free);
    TEST_ASSERT_EQ(fs->freespace.reserve_count, reserve_count);
    TEST_ASSERT_EQ(fs->freespace.metadata_batch_txn == live_txn, false);
    TEST_ASSERT_EQ(fs->recovery_error, BFS_OK);
    bfs_lock_unlock(&fs->lock);

    uint64_t committed_txn = bfs_be64(fs->txn.sb.txn_id);
    TEST_ASSERT(remount_committed_oracle(&fixture, fixture.baseline_ino,
                                         committed_txn));
    fixture_destroy(&fixture);
}

TEST_SUITE_BEGIN("Metadata Withdrawal Batch Faults")
    TEST_RUN(test_successful_first_request_batch_with_baseline);
    TEST_RUN(test_fragmented_first_request_produces_multiple_tail_writes);
    TEST_RUN(test_full_initial_refill_write_failure_preserves_root);
    TEST_RUN(test_prefix_initial_refill_write_failure_preserves_root);
    TEST_RUN(test_full_second_refill_leaf_write_failure_after_progress);
    TEST_RUN(test_prefix_second_refill_leaf_write_failure_after_progress);
    TEST_RUN(test_pending_headroom_declines_without_refill_write);
TEST_SUITE_END()
