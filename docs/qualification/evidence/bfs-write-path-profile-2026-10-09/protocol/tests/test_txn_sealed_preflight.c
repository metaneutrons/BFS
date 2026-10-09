/* SPDX-License-Identifier: MPL-2.0 */
/* Focused mounted preflight and allocation-freeze tests for sealed commits. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_dir.h"
#include "bfs_fs.h"
#include "bfs_internal.h"
#include "bfs_inode.h"
#include "bfs_ondisk.h"
#include "bfs_superblock.h"

#define TEST_BLOCK_SIZE 1024u
#define TEST_BLOCK_COUNT 4096u

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    uint32_t reads;
    uint32_t writes;
    uint32_t syncs;
} memory_device_t;

typedef struct {
    memory_device_t device;
    bfs_fs_t fs;
} fixture_t;

typedef struct {
    bfs_superblock_t working_sb;
    bfs_superblock_t committed_sb;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    bfs_blk_t pending[BFS_PENDING_FREES_MAX];
    uint32_t reserve_count;
    uint32_t pending_count;
    bfs_blk_t root;
    uint32_t height;
    uint32_t total_free;
    bfs_blk_t roving;
    uint64_t live_txn_id;
    uint32_t options;
    bool has_snapshots;
    bool allocation_frozen;
    bool in_alloc;
    bfs_err_t free_sink_err;
    bfs_err_t recovery_error;
} allocator_snapshot_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    if (!buffer || block >= bio->block_count) return BFS_ERR_INVAL;
    device->reads++;
    memcpy(buffer, device->bytes + (size_t)block * bio->block_size,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    memory_device_t *device = (memory_device_t *)bio;
    if (!buffer || block >= bio->block_count) return BFS_ERR_INVAL;
    device->writes++;
    memcpy(device->bytes + (size_t)block * bio->block_size, buffer,
           bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_sync(bfs_bio_t *bio)
{
    memory_device_t *device = (memory_device_t *)bio;
    device->syncs++;
    return BFS_OK;
}

static void memory_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
};

static bool fixture_init(fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->device.bytes = calloc(TEST_BLOCK_COUNT, TEST_BLOCK_SIZE);
    if (!fixture->device.bytes) return false;
    fixture->device.bio.ops = &memory_ops;
    fixture->device.bio.block_size = TEST_BLOCK_SIZE;
    fixture->device.bio.block_count = TEST_BLOCK_COUNT;

    if (bfs_fs_format(&fixture->device.bio, "SealPreflight", 0) != BFS_OK ||
        bfs_fs_mount(&fixture->fs, &fixture->device.bio) != BFS_OK)
        return false;

    /* A real mounted allocator mutation gives the exact live, height-one
     * Free-Tree root required by the SF eligibility gate. */
    bfs_lock_write(&fixture->fs.lock);
    bfs_blk_t allocated = bfs_freespace_alloc(&fixture->fs.freespace, 1);
    uint64_t root_txn = 0;
    bool exact_live_root = allocated != BFS_BLK_NULL &&
        fixture->fs.freespace.tree.height == 1 &&
        bfs_btree_root_leaf_txn_id(&fixture->fs.freespace.tree,
                                   &root_txn) == BFS_OK &&
        root_txn == fixture->fs.live_txn_id &&
        fixture->fs.live_txn_id == bfs_txn_id(&fixture->fs.txn) &&
        fixture->fs.live_txn_id > bfs_be64(fixture->fs.txn.sb.txn_id);
    bfs_lock_unlock(&fixture->fs.lock);
    if (!exact_live_root) return false;

    /* Discard setup I/O from the per-call no-write oracles. */
    fixture->device.writes = 0;
    return true;
}

static void fixture_destroy(fixture_t *fixture)
{
    if (fixture->fs.mounted) bfs_fs_abandon(&fixture->fs);
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
}

static void take_snapshot(const bfs_fs_t *fs, allocator_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->working_sb = fs->txn.sb_new;
    snapshot->committed_sb = fs->txn.sb;
    memcpy(snapshot->reserve, fs->freespace.reserve,
           sizeof(snapshot->reserve));
    memcpy(snapshot->pending, fs->pending_frees,
           sizeof(snapshot->pending));
    snapshot->reserve_count = fs->freespace.reserve_count;
    snapshot->pending_count = fs->pending_count;
    snapshot->root = fs->freespace.tree.root;
    snapshot->height = fs->freespace.tree.height;
    snapshot->total_free = fs->freespace.total_free;
    snapshot->roving = fs->freespace.roving;
    snapshot->live_txn_id = fs->live_txn_id;
    snapshot->options = fs->options;
    snapshot->has_snapshots = fs->has_snapshots;
    snapshot->allocation_frozen = fs->freespace.allocation_frozen;
    snapshot->in_alloc = fs->freespace.in_alloc;
    snapshot->free_sink_err = fs->freespace.tree.free_sink_err;
    snapshot->recovery_error = fs->recovery_error;
}

static bool snapshot_matches(const bfs_fs_t *fs,
                             const allocator_snapshot_t *snapshot)
{
    return memcmp(&fs->txn.sb_new, &snapshot->working_sb,
                  sizeof(snapshot->working_sb)) == 0 &&
           memcmp(&fs->txn.sb, &snapshot->committed_sb,
                  sizeof(snapshot->committed_sb)) == 0 &&
           memcmp(fs->freespace.reserve, snapshot->reserve,
                  sizeof(snapshot->reserve)) == 0 &&
           memcmp(fs->pending_frees, snapshot->pending,
                  sizeof(snapshot->pending)) == 0 &&
           fs->freespace.reserve_count == snapshot->reserve_count &&
           fs->pending_count == snapshot->pending_count &&
           fs->freespace.tree.root == snapshot->root &&
           fs->freespace.tree.height == snapshot->height &&
           fs->freespace.total_free == snapshot->total_free &&
           fs->freespace.roving == snapshot->roving &&
           fs->live_txn_id == snapshot->live_txn_id &&
           fs->options == snapshot->options &&
           fs->has_snapshots == snapshot->has_snapshots &&
           fs->freespace.allocation_frozen == snapshot->allocation_frozen &&
           fs->freespace.in_alloc == snapshot->in_alloc &&
           fs->freespace.tree.free_sink_err == snapshot->free_sink_err &&
           fs->recovery_error == snapshot->recovery_error;
}

static bool check_preflight(fixture_t *fixture, bfs_err_t expected,
                            bool expected_sealed)
{
    allocator_snapshot_t before;
    take_snapshot(&fixture->fs, &before);
    uint32_t writes = fixture->device.writes;
    bool sealed = !expected_sealed;
    bfs_err_t err = bfs_freespace_seal_commit(&fixture->fs, &sealed);
    return err == expected && sealed == expected_sealed &&
           fixture->device.writes == writes &&
           fixture->fs.recovery_error == BFS_OK &&
           snapshot_matches(&fixture->fs, &before);
}

typedef struct {
    uint32_t count;
    uint32_t first_key;
    uint32_t first_length;
} extent_summary_t;

static bool summarize_extent(const void *key, const void *value, void *ctx)
{
    extent_summary_t *summary = (extent_summary_t *)ctx;
    if (summary->count == 0) {
        summary->first_key = bfs_load_be32(key);
        summary->first_length = bfs_load_be32(value);
    }
    summary->count++;
    return true;
}

static bool extent_summary(bfs_fs_t *fs, extent_summary_t *summary)
{
    memset(summary, 0, sizeof(*summary));
    return bfs_btree_scan(&fs->freespace.tree, NULL, summarize_extent,
                          summary) == BFS_OK;
}

static bool pending_contains(const bfs_fs_t *fs, bfs_blk_t block)
{
    const bfs_blk_t *pending = fs->pending_frees_dynamic
        ? fs->pending_frees_dynamic : fs->pending_frees;
    for (uint32_t i = 0; i < fs->pending_count; i++)
        if (pending[i] == block) return true;
    return false;
}

static bool pool_contains(const bfs_superblock_t *sb, bfs_blk_t block)
{
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        if (bfs_be32(sb->emergency_pool[i]) == block) return true;
    return false;
}

static bool reserve_contains(const bfs_freespace_t *space, bfs_blk_t block)
{
    for (uint32_t i = 0; i < space->reserve_count; i++)
        if (space->reserve[i] == block) return true;
    return false;
}

static bool superblock_root_contains(const bfs_superblock_t *sb,
                                     bfs_blk_t block)
{
    return block == bfs_be32(sb->dir_tree_root) ||
           block == bfs_be32(sb->extent_tree_root) ||
           block == bfs_be32(sb->inode_tree_root) ||
           block == bfs_be32(sb->free_tree_root) ||
           block == bfs_be32(sb->refcount_tree_root) ||
           block == bfs_be32(sb->snapshot_tree_root);
}

static bool root_contains(const bfs_fs_t *fs, bfs_blk_t block)
{
    return block == fs->freespace.tree.root ||
           block == fs->dir_tree.tree.root ||
           block == fs->inode_tree.root ||
           block == fs->refcount.tree.root ||
           superblock_root_contains(&fs->txn.sb_new, block) ||
           superblock_root_contains(&fs->txn.sb, block);
}

static bool current_leaf_contains(bfs_fs_t *fs, bfs_blk_t block);

static bool ordinary_candidate(bfs_fs_t *fs, bfs_blk_t block)
{
    return block >= bfs_data_start_block(fs->bio->block_size) &&
           block < fs->bio->block_count &&
           !current_leaf_contains(fs, block) &&
           !pool_contains(&fs->txn.sb_new, block) &&
           !pool_contains(&fs->txn.sb, block) &&
           !reserve_contains(&fs->freespace, block) &&
           !pending_contains(fs, block) && !root_contains(fs, block);
}

static void set_backup_block_quotient(bfs_superblock_t *sb,
                                      uint64_t block, uint32_t block_size)
{
    uint64_t offset = block * block_size;
    sb->sb_backup_offset_hi = bfs_be32((uint32_t)(offset >> 32));
    sb->sb_backup_offset_lo = bfs_be32((uint32_t)offset);
}

static bool allocate_ordinary_alias(bfs_fs_t *fs, bfs_blk_t excluded,
                                    bfs_blk_t *alias)
{
    bfs_blk_t first = bfs_freespace_alloc(&fs->freespace, 2);
    if (first == BFS_BLK_NULL) return false;
    *alias = BFS_BLK_NULL;
    for (uint32_t i = 0; i < 2; i++) {
        bfs_blk_t candidate = first + i;
        if (candidate != excluded && ordinary_candidate(fs, candidate)) {
            *alias = candidate;
            return true;
        }
    }
    return false;
}

static bool setup_distinct_backup_positions(fixture_t *fixture,
                                            bfs_blk_t *working_backup,
                                            bfs_blk_t *committed_backup)
{
    bfs_fs_t *fs = &fixture->fs;
    uint32_t block_size = fs->bio->block_size;
    uint64_t working_offset = bfs_sb_backup_offset(&fs->txn.sb_new);
    if (working_offset != bfs_default_backup_offset(fs->bio->block_count,
                                                    block_size) ||
        working_offset % block_size != 0)
        return false;

    *working_backup = (bfs_blk_t)(working_offset / block_size);
    /* Allocate through the public allocator so the committed target is a
     * genuinely allocated data block, rather than a synthetic in-range value. */
    if (!allocate_ordinary_alias(fs, *working_backup, committed_backup) ||
        !ordinary_candidate(fs, *working_backup))
        return false;

    set_backup_block_quotient(&fs->txn.sb, *committed_backup, block_size);
    return bfs_sb_backup_offset(&fs->txn.sb) != working_offset;
}

static bfs_superblock_t *selected_backup_superblock(bfs_fs_t *fs,
                                                    bool committed)
{
    return committed ? &fs->txn.sb : &fs->txn.sb_new;
}

static bool current_leaf_contains(bfs_fs_t *fs, bfs_blk_t block)
{
    uint32_t search = bfs_be32(block), key_be = 0, length_be = 0;
    if (bfs_btree_search_floor(&fs->freespace.tree, &search,
                               &key_be, &length_be) != BFS_OK)
        return false;
    bfs_blk_t start = bfs_be32(key_be);
    uint32_t length = bfs_be32(length_be);
    return block >= start && block - start < length;
}

static bfs_blk_t first_ordinary_free_block(bfs_fs_t *fs)
{
    extent_summary_t summary;
    if (!extent_summary(fs, &summary) || summary.count == 0)
        return BFS_BLK_NULL;
    bfs_blk_t end = summary.first_key + summary.first_length;
    for (bfs_blk_t block = summary.first_key; block < end; block++) {
        bool in_reserve = false;
        for (uint32_t i = 0; i < fs->freespace.reserve_count; i++)
            if (fs->freespace.reserve[i] == block) in_reserve = true;
        if (!in_reserve && !pool_contains(fs->freespace.sb, block) &&
            block != fs->freespace.tree.root &&
            block != fs->dir_tree.tree.root && block != fs->inode_tree.root)
            return block;
    }
    return BFS_BLK_NULL;
}

static bool setup_future_free_root(fixture_t *fixture)
{
    bfs_fs_t *fs = &fixture->fs;
    uint64_t root_txn = 0;
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK)
        return false;
    fs->live_txn_id = root_txn + 2;
    fs->txn.sb_new.txn_id = bfs_be64(fs->live_txn_id);
    if (bfs_freespace_alloc(&fs->freespace, 1) == BFS_BLK_NULL) return false;
    uint64_t future_txn = 0;
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &future_txn) != BFS_OK ||
        future_txn != root_txn + 2)
        return false;
    fs->live_txn_id = root_txn + 1;
    fs->txn.sb_new.txn_id = bfs_be64(fs->live_txn_id);
    fixture->device.writes = 0;
    return fs->live_txn_id > bfs_be64(fs->txn.sb.txn_id);
}

static bool make_deeper_free_tree(fixture_t *fixture)
{
    bfs_fs_t *fs = &fixture->fs;
    extent_summary_t summary;
    if (fs->freespace.tree.height != 1 ||
        !extent_summary(fs, &summary)) return false;
    uint32_t capacity = bfs_btree_leaf_capacity(&fs->freespace.tree);
    uint32_t needed = capacity > summary.count
        ? capacity - summary.count + 3u : 8u;
    uint32_t allocation_count = 2u * needed + 1u;
    bfs_blk_t first = bfs_freespace_alloc(&fs->freespace, allocation_count);
    if (first == BFS_BLK_NULL) return false;
    for (uint32_t i = 0; i < needed; i++) {
        bfs_err_t err = bfs_freespace_free(&fs->freespace,
                                            first + 1u + 2u * i, 1);
        if (err != BFS_OK) return false;
    }
    fixture->device.writes = 0;
    return fs->freespace.tree.height > 1;
}

static bool make_full_free_leaf(fixture_t *fixture)
{
    bfs_fs_t *fs = &fixture->fs;
    extent_summary_t summary;
    if (fs->freespace.tree.height != 1 || !extent_summary(fs, &summary))
        return false;
    uint32_t capacity = bfs_btree_leaf_capacity(&fs->freespace.tree);
    if (summary.count >= capacity) return false;
    uint32_t needed = capacity - summary.count;
    uint32_t allocation_count = 2u * needed + 1u;
    bfs_blk_t *blocks = malloc((size_t)needed * sizeof(*blocks));
    if (!blocks) return false;
    bfs_blk_t first = bfs_freespace_alloc(&fs->freespace, allocation_count);
    if (first == BFS_BLK_NULL) { free(blocks); return false; }
    for (uint32_t i = 0; i < needed; i++)
        blocks[i] = first + 1u + 2u * i;
    bfs_err_t err = bfs_freespace_free_sorted_blocks(&fs->freespace, blocks,
                                                      needed);
    free(blocks);
    if (err != BFS_OK) return false;
    fixture->device.writes = 0;
    return fs->freespace.tree.height == 1 &&
           extent_summary(fs, &summary) && summary.count == capacity;
}

static void test_snapshot_contexts_decline_without_writes(void)
{
    enum {
        SNAP_OPTION_RUNTIME,
        SNAP_OPTION_WORKING,
        SNAP_OPTIONLESS_RUNTIME,
        SNAP_WORKING_SNAPSHOT_ROOT,
        SNAP_WORKING_REFCOUNT_ROOT,
        SNAP_COMMITTED_SNAPSHOT_ROOT,
        SNAP_COMMITTED_REFCOUNT_ROOT,
        SNAP_COMMITTED_OPTION,
        SNAP_WORKING_LEGACY_EXTENT_ROOT,
        SNAP_COMMITTED_LEGACY_EXTENT_ROOT,
        SNAP_CONTEXT_COUNT
    };
    for (int shape = 0; shape < SNAP_CONTEXT_COUNT; shape++) {
        fixture_t fixture;
        TEST_ASSERT(fixture_init(&fixture));
        bfs_fs_t *fs = &fixture.fs;
        bfs_lock_write(&fs->lock);
        switch (shape) {
        case SNAP_OPTION_RUNTIME:
            fs->options |= BFS_OPT_SNAPSHOTS;
            fs->has_snapshots = true;
            break;
        case SNAP_OPTION_WORKING:
            fs->txn.sb_new.options = bfs_be32(BFS_OPT_SNAPSHOTS);
            break;
        case SNAP_OPTIONLESS_RUNTIME:
            fs->has_snapshots = true;
            break;
        case SNAP_WORKING_SNAPSHOT_ROOT:
            fs->txn.sb_new.snapshot_tree_root =
                bfs_be32(fs->dir_tree.tree.root);
            break;
        case SNAP_WORKING_REFCOUNT_ROOT:
            fs->txn.sb_new.refcount_tree_root =
                bfs_be32(fs->dir_tree.tree.root);
            break;
        case SNAP_COMMITTED_SNAPSHOT_ROOT:
            fs->txn.sb.snapshot_tree_root =
                bfs_be32(fs->dir_tree.tree.root);
            break;
        case SNAP_COMMITTED_REFCOUNT_ROOT:
            fs->txn.sb.refcount_tree_root =
                bfs_be32(fs->dir_tree.tree.root);
            break;
        case SNAP_COMMITTED_OPTION:
            fs->txn.sb.options = bfs_be32(BFS_OPT_SNAPSHOTS);
            break;
        case SNAP_WORKING_LEGACY_EXTENT_ROOT:
            fs->txn.sb_new.extent_tree_root =
                bfs_be32(fs->dir_tree.tree.root);
            break;
        case SNAP_COMMITTED_LEGACY_EXTENT_ROOT:
            fs->txn.sb.extent_tree_root =
                bfs_be32(fs->dir_tree.tree.root);
            break;
        }
        TEST_ASSERT(check_preflight(&fixture, BFS_OK, false));
        bfs_lock_unlock(&fs->lock);
        fixture_destroy(&fixture);
    }
}

static void test_older_future_deeper_and_full_leaf_shapes(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    uint64_t root_txn = 0;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&fs->freespace.tree,
                                               &root_txn), BFS_OK);
    bfs_lock_write(&fs->lock);
    fs->live_txn_id = root_txn + 1;
    fs->txn.sb_new.txn_id = bfs_be64(fs->live_txn_id);
    fixture.device.writes = 0;
    TEST_ASSERT(check_preflight(&fixture, BFS_OK, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    TEST_ASSERT(setup_future_free_root(&fixture));
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    TEST_ASSERT(make_deeper_free_tree(&fixture));
    TEST_ASSERT(check_preflight(&fixture, BFS_OK, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    TEST_ASSERT(make_full_free_leaf(&fixture));
    TEST_ASSERT(check_preflight(&fixture, BFS_OK, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
}

static void test_missing_scratch_and_inactive_pool_alias_decline(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->freespace.sb->emergency_count = 0;
    TEST_ASSERT(check_preflight(&fixture, BFS_OK, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    uint32_t active = bfs_be32(fs->freespace.sb->emergency_count);
    TEST_ASSERT(active < BFS_EMERGENCY_POOL_SIZE);
    bfs_blk_t alias = first_ordinary_free_block(fs);
    TEST_ASSERT(alias != BFS_BLK_NULL);
    bfs_err_t expected_error;
    uint32_t original_pool[BFS_EMERGENCY_POOL_SIZE];
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        original_pool[i] = fs->freespace.sb->emergency_pool[i];
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        fs->freespace.sb->emergency_pool[i] = bfs_be32(alias);
        expected_error = i < active ? BFS_ERR_CORRUPT : BFS_OK;
        TEST_ASSERT(check_preflight(&fixture, expected_error, false));
        fs->freespace.sb->emergency_pool[i] = original_pool[i];
    }
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
}

static void test_backup_block_and_current_root_rejections(void)
{
    for (uint32_t target_index = 0; target_index < 2; target_index++) {
        for (uint32_t shape = 0; shape < 4; shape++) {
            fixture_t fixture;
            TEST_ASSERT(fixture_init(&fixture));
            bfs_fs_t *fs = &fixture.fs;
            bfs_lock_write(&fs->lock);
            bfs_blk_t working_backup, committed_backup;
            TEST_ASSERT(setup_distinct_backup_positions(&fixture,
                &working_backup, &committed_backup));
            bfs_blk_t backup = target_index == 0
                ? working_backup : committed_backup;
            switch (shape) {
            case 0:
                fs->pending_frees[0] = backup;
                fs->pending_count = 1;
                break;
            case 1:
                TEST_ASSERT(fs->freespace.reserve_count <
                            BFS_ALLOC_RESERVE_SIZE);
                fs->freespace.reserve[fs->freespace.reserve_count++] = backup;
                break;
            case 2: {
                uint32_t active = bfs_be32(fs->freespace.sb->emergency_count);
                TEST_ASSERT(active < BFS_EMERGENCY_POOL_SIZE);
                fs->freespace.sb->emergency_pool[active] = bfs_be32(backup);
                break;
            }
            case 3:
                TEST_ASSERT_EQ(bfs_freespace_free(&fs->freespace, backup, 1),
                               BFS_OK);
                TEST_ASSERT(current_leaf_contains(fs, backup));
                break;
            }
            fixture.device.writes = 0;
            TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
            bfs_lock_unlock(&fs->lock);
            fixture_destroy(&fixture);
        }
    }

    fixture_t fixture;
    bfs_fs_t *fs;
    for (uint32_t root_case = 0; root_case < 2; root_case++) {
        TEST_ASSERT(fixture_init(&fixture));
        fs = &fixture.fs;
        bfs_lock_write(&fs->lock);
        fs->pending_frees[0] = root_case == 0
            ? fs->dir_tree.tree.root : fs->inode_tree.root;
        fs->pending_count = 1;
        TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
        bfs_lock_unlock(&fs->lock);
        fixture_destroy(&fixture);
    }
}

static uint64_t high_backup_quotient(bfs_blk_t alias)
{
    return (UINT64_C(1) << 32) + alias;
}

static void test_high_backup_quotients_do_not_alias_pending_blocks(void)
{
    for (uint32_t committed = 0; committed < 2; committed++) {
        fixture_t fixture;
        TEST_ASSERT(fixture_init(&fixture));
        bfs_fs_t *fs = &fixture.fs;
        bfs_lock_write(&fs->lock);

        uint64_t default_offset = bfs_sb_backup_offset(&fs->txn.sb_new);
        TEST_ASSERT_EQ(default_offset,
            bfs_default_backup_offset(fs->bio->block_count,
                                      fs->bio->block_size));
        TEST_ASSERT_EQ(default_offset % fs->bio->block_size, 0);
        bfs_blk_t default_backup =
            (bfs_blk_t)(default_offset / fs->bio->block_size);
        bfs_blk_t alias;
        TEST_ASSERT(allocate_ordinary_alias(fs, default_backup, &alias));
        TEST_ASSERT(ordinary_candidate(fs, alias));

        uint64_t high = high_backup_quotient(alias);
        bfs_superblock_t *sb = selected_backup_superblock(fs, committed != 0);
        set_backup_block_quotient(sb, high, fs->bio->block_size);
        TEST_ASSERT_EQ(bfs_sb_backup_offset(sb) / fs->bio->block_size, high);
        TEST_ASSERT(bfs_sb_backup_offset(sb) / fs->bio->block_size != alias);
        fs->pending_frees[0] = alias;
        fs->pending_count = 1;
        TEST_ASSERT_EQ(fs->pending_frees[0], alias);
        TEST_ASSERT(alias >= bfs_data_start_block(fs->bio->block_size));
        TEST_ASSERT(alias < fs->bio->block_count);
        TEST_ASSERT(!pool_contains(&fs->txn.sb_new, alias));
        TEST_ASSERT(!pool_contains(&fs->txn.sb, alias));
        TEST_ASSERT(!reserve_contains(&fs->freespace, alias));
        TEST_ASSERT(!current_leaf_contains(fs, alias));
        TEST_ASSERT(!root_contains(fs, alias));

        allocator_snapshot_t before;
        take_snapshot(fs, &before);
        uint32_t writes_before = fixture.device.writes;
        bool sealed = false;
        bfs_err_t err = bfs_freespace_seal_commit(fs, &sealed);
        TEST_ASSERT_EQ(err, BFS_OK);
        TEST_ASSERT(sealed);
        TEST_ASSERT_EQ(fixture.device.writes, writes_before + 1u);
        TEST_ASSERT(fs->freespace.allocation_frozen);
        TEST_ASSERT_EQ(fs->recovery_error, BFS_OK);
        TEST_ASSERT_EQ(fs->pending_count, before.pending_count);
        TEST_ASSERT_MEM_EQ(fs->pending_frees, before.pending,
                           sizeof(before.pending));
        TEST_ASSERT_MEM_EQ(&fs->txn.sb, &before.committed_sb,
                           sizeof(before.committed_sb));
        TEST_ASSERT_EQ(bfs_sb_backup_offset(&fs->txn.sb_new),
                       bfs_sb_backup_offset(&before.working_sb));
        TEST_ASSERT_EQ(fs->live_txn_id, before.live_txn_id);
        TEST_ASSERT_EQ(fs->options, before.options);
        TEST_ASSERT_EQ(fs->has_snapshots, before.has_snapshots);
        TEST_ASSERT_EQ(fs->freespace.tree.free_sink_err,
                       before.free_sink_err);
        TEST_ASSERT_EQ(bfs_sb_backup_offset(sb) / fs->bio->block_size, high);
        TEST_ASSERT(pending_contains(fs, alias));
        TEST_ASSERT(current_leaf_contains(fs, alias));
        TEST_ASSERT(!pool_contains(&fs->txn.sb_new, alias));
        TEST_ASSERT(!reserve_contains(&fs->freespace, alias));
        TEST_ASSERT(fs->mounted);

        /* Keep the malformed offset in memory only. fixture_destroy abandons
         * this mounted instance; this test never commits or remounts it. */
        bfs_lock_unlock(&fs->lock);
        fixture_destroy(&fixture);
    }
}

static void test_high_backup_quotients_cast_only_for_old_leaf_checks(void)
{
    for (uint32_t committed = 0; committed < 2; committed++) {
        fixture_t fixture;
        TEST_ASSERT(fixture_init(&fixture));
        bfs_fs_t *fs = &fixture.fs;
        bfs_lock_write(&fs->lock);

        uint64_t default_offset = bfs_sb_backup_offset(&fs->txn.sb_new);
        TEST_ASSERT_EQ(default_offset % fs->bio->block_size, 0);
        bfs_blk_t default_backup =
            (bfs_blk_t)(default_offset / fs->bio->block_size);
        bfs_blk_t alias;
        TEST_ASSERT(allocate_ordinary_alias(fs, default_backup, &alias));
        TEST_ASSERT(ordinary_candidate(fs, alias));

        uint64_t high = high_backup_quotient(alias);
        bfs_superblock_t *sb = selected_backup_superblock(fs, committed != 0);
        set_backup_block_quotient(sb, high, fs->bio->block_size);
        TEST_ASSERT_EQ(bfs_sb_backup_offset(sb) / fs->bio->block_size, high);
        TEST_ASSERT_EQ(bfs_freespace_free(&fs->freespace, alias, 1), BFS_OK);
        TEST_ASSERT(current_leaf_contains(fs, alias));
        fixture.device.writes = 0;
        TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));

        /* As above, do not publish or mount the deliberately malformed SB. */
        bfs_lock_unlock(&fs->lock);
        fixture_destroy(&fixture);
    }
}

static void test_invalid_candidates_fail_before_write(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->pending_frees[0] = fixture.device.bio.block_count;
    fs->pending_count = 1;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    bfs_blk_t allocated = bfs_freespace_alloc(&fs->freespace, 1);
    TEST_ASSERT(allocated != BFS_BLK_NULL);
    fs->pending_frees[0] = allocated;
    fs->pending_frees[1] = allocated;
    fs->pending_count = 2;
    fixture.device.writes = 0;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    bfs_blk_t free_block = first_ordinary_free_block(fs);
    TEST_ASSERT(free_block != BFS_BLK_NULL);
    fs->pending_frees[0] = free_block;
    fs->pending_count = 1;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->pending_frees[0] = fs->dir_tree.tree.root;
    fs->pending_count = 1;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->freespace.sb->emergency_pool[BFS_EMERGENCY_POOL_SIZE - 1] =
        bfs_be32(fixture.device.bio.block_count);
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
}

static void test_cross_source_duplicates_and_bounds_fail_before_write(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    TEST_ASSERT(fs->freespace.reserve_count > 0);
    fs->pending_frees[0] = fs->freespace.reserve[0];
    fs->pending_count = 1;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->pending_frees[0] = fs->freespace.tree.root;
    fs->pending_count = 1;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->freespace.reserve_count = BFS_ALLOC_RESERVE_SIZE + 1u;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    fs->pending_count = BFS_PENDING_FREES_MAX + 1u;
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture));
    fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    uint32_t active = bfs_be32(fs->freespace.sb->emergency_count);
    TEST_ASSERT(active > 0 && active < BFS_EMERGENCY_POOL_SIZE);
    uint32_t inactive_value = fs->freespace.sb->emergency_pool[active];
    fs->freespace.sb->emergency_pool[active] =
        fs->freespace.sb->emergency_pool[0];
    TEST_ASSERT(check_preflight(&fixture, BFS_ERR_CORRUPT, false));
    fs->freespace.sb->emergency_pool[active] = inactive_value;
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
}

typedef struct {
    uint32_t count;
    bfs_blk_t blocks[BFS_ALLOC_RESERVE_SIZE + BFS_EMERGENCY_POOL_SIZE + 1u];
} expected_pool_t;

static void expected_pool_add(expected_pool_t *expected, bfs_blk_t block)
{
    for (uint32_t i = 0; i < expected->count; i++)
        if (expected->blocks[i] == block) return;
    expected->blocks[expected->count++] = block;
}

static bool expected_pool_contains(const expected_pool_t *expected,
                                   bfs_blk_t block)
{
    for (uint32_t i = 0; i < expected->count; i++)
        if (expected->blocks[i] == block) return true;
    return false;
}

static bool block_array_contains(const bfs_blk_t *blocks, uint32_t count,
                                 bfs_blk_t block)
{
    for (uint32_t i = 0; i < count; i++)
        if (blocks[i] == block) return true;
    return false;
}

static int historical_pool_index(const bfs_blk_t pool[BFS_EMERGENCY_POOL_SIZE],
                                 bfs_blk_t block)
{
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        if (pool[i] == block) return (int)i;
    return -1;
}

static bool make_committed_dir_root_pool_origin(fixture_t *fixture)
{
    bfs_fs_t *fs = &fixture->fs;
    bfs_blk_t saved_reserve[BFS_ALLOC_RESERVE_SIZE];
    uint32_t saved_reserve_count = fs->freespace.reserve_count;
    memcpy(saved_reserve, fs->freespace.reserve, sizeof(saved_reserve));

    /* Exercise the allocator's legitimate emergency-origin branch for both
     * namespace roots while temporarily withholding ordinary reserve stock. */
    fs->freespace.reserve_count = 0;
    fs->freespace.in_alloc = true;
    bfs_err_t err = bfs_dir_insert(&fs->dir_tree, BFS_ROOT_INO, "seed", 4,
                                    BFS_ROOT_INO + 1, BFS_INODE_FILE);
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(BFS_ROOT_INO + 1);
    inode.type = bfs_be32(BFS_INODE_FILE);
    inode.link_count = bfs_be32(1);
    if (err == BFS_OK)
        err = bfs_inode_write(&fs->inode_tree, BFS_ROOT_INO + 1, &inode);
    fs->freespace.in_alloc = false;
    memcpy(fs->freespace.reserve, saved_reserve, sizeof(saved_reserve));
    fs->freespace.reserve_count = saved_reserve_count;
    if (err != BFS_OK) return false;
    fs->next_ino = BFS_ROOT_INO + 2;

    bfs_blk_t seeded_root = fs->dir_tree.tree.root;
    uint32_t emergency_count = bfs_be32(fs->txn.sb_new.emergency_count);
    bool in_pool_history = false;
    for (uint32_t i = emergency_count; i < BFS_EMERGENCY_POOL_SIZE; i++)
        if (bfs_be32(fs->txn.sb_new.emergency_pool[i]) == seeded_root)
            in_pool_history = true;
    if (!in_pool_history) return false;

    if (bfs_txn_commit(fs) != BFS_OK ||
        bfs_be32(fs->txn.sb.dir_tree_root) != seeded_root)
        return false;
    emergency_count = bfs_be32(fs->txn.sb.emergency_count);
    in_pool_history = false;
    for (uint32_t i = emergency_count; i < BFS_EMERGENCY_POOL_SIZE; i++)
        if (bfs_be32(fs->txn.sb.emergency_pool[i]) == seeded_root)
            in_pool_history = true;
    if (!in_pool_history) return false;

    if (bfs_freespace_alloc(&fs->freespace, 1) == BFS_BLK_NULL) return false;
    uint64_t root_txn = 0;
    return fs->freespace.tree.height == 1 &&
           bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) == BFS_OK &&
           root_txn == fs->live_txn_id &&
           fs->live_txn_id == bfs_txn_id(&fs->txn);
}

static void test_success_seals_mixed_provenance_and_freezes_mutation(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    TEST_ASSERT(make_committed_dir_root_pool_origin(&fixture));

    /* The namespace COW naturally retires the old committed directory root;
     * a separate ordinary block joins it in the pending batch. */
    bfs_blk_t committed_dir_root = bfs_be32(fs->txn.sb.dir_tree_root);
    uint32_t new_ino = bfs_fs_alloc_ino(fs);
    TEST_ASSERT(new_ino != 0);
    TEST_ASSERT_EQ(bfs_dir_insert(&fs->dir_tree, BFS_ROOT_INO, "entry", 5,
                                 new_ino, BFS_INODE_FILE), BFS_OK);
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(new_ino);
    inode.type = bfs_be32(BFS_INODE_FILE);
    inode.link_count = bfs_be32(1);
    TEST_ASSERT_EQ(bfs_inode_write(&fs->inode_tree, new_ino, &inode), BFS_OK);
    TEST_ASSERT(fs->dir_tree.tree.root != committed_dir_root);
    TEST_ASSERT(pending_contains(fs, committed_dir_root));
    bfs_blk_t ordinary = bfs_freespace_alloc(&fs->freespace, 1);
    TEST_ASSERT(ordinary != BFS_BLK_NULL);
    TEST_ASSERT(!pool_contains(fs->freespace.sb, ordinary));
    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(fs, ordinary), BFS_OK);

    uint64_t root_txn = 0;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&fs->freespace.tree,
                                               &root_txn), BFS_OK);
    TEST_ASSERT_EQ(root_txn, fs->live_txn_id);
    TEST_ASSERT(fs->freespace.tree.height == 1);

    bfs_freespace_t *space = &fs->freespace;
    uint32_t original_ec = bfs_be32(space->sb->emergency_count);
    TEST_ASSERT(original_ec > 0 && original_ec <= BFS_EMERGENCY_POOL_SIZE);
    bfs_blk_t old_free_root = space->tree.root;
    bfs_blk_t original_pool[BFS_EMERGENCY_POOL_SIZE];
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        original_pool[i] = bfs_be32(space->sb->emergency_pool[i]);
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        TEST_ASSERT(original_pool[i] != BFS_BLK_NULL);
        TEST_ASSERT(original_pool[i] < fixture.device.bio.block_count);
        for (uint32_t j = 0; j < i; j++)
            TEST_ASSERT(original_pool[i] != original_pool[j]);
    }
    bfs_blk_t pool_before_permutation[BFS_EMERGENCY_POOL_SIZE];
    memcpy(pool_before_permutation, original_pool,
           sizeof(pool_before_permutation));
    int committed_dir_index = historical_pool_index(original_pool,
                                                     committed_dir_root);
    TEST_ASSERT(committed_dir_index >= (int)original_ec);
    TEST_ASSERT(historical_pool_index(original_pool, old_free_root) >=
                (int)original_ec);
    /* Preserve the entire historical pool and active prefix while placing
     * this naturally pending retired root in the last inactive slot. */
    uint32_t committed_dir_slot = (uint32_t)committed_dir_index;
    uint32_t last_slot = BFS_EMERGENCY_POOL_SIZE - 1u;
    uint32_t slot_value = space->sb->emergency_pool[last_slot];
    space->sb->emergency_pool[last_slot] =
        space->sb->emergency_pool[committed_dir_slot];
    space->sb->emergency_pool[committed_dir_slot] = slot_value;
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        original_pool[i] = bfs_be32(space->sb->emergency_pool[i]);
    TEST_ASSERT_EQ(historical_pool_index(original_pool, committed_dir_root),
                   BFS_EMERGENCY_POOL_SIZE - 1u);
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        bfs_blk_t expected = pool_before_permutation[i];
        if (i == committed_dir_slot)
            expected = pool_before_permutation[last_slot];
        else if (i == last_slot)
            expected = pool_before_permutation[committed_dir_slot];
        TEST_ASSERT_EQ(original_pool[i], expected);
    }
    TEST_ASSERT(historical_pool_index(original_pool, old_free_root) >=
                (int)original_ec);
    bfs_blk_t scratch = bfs_be32(space->sb->emergency_pool[original_ec - 1]);
    uint32_t original_pending_count = fs->pending_count;
    bfs_blk_t original_pending[BFS_PENDING_FREES_MAX];
    memcpy(original_pending, fs->pending_frees, sizeof(original_pending));
    bfs_blk_t original_reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(original_reserve, space->reserve, sizeof(original_reserve));
    bfs_blk_t expected_staged_reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(expected_staged_reserve, original_reserve,
           sizeof(expected_staged_reserve));
    uint32_t original_reserve_count = space->reserve_count;
    uint32_t original_total_free = space->total_free;
    uint32_t writes_before = fixture.device.writes;

    bool sealed = false;
    TEST_ASSERT_EQ(bfs_freespace_seal_commit(fs, &sealed), BFS_OK);
    TEST_ASSERT(sealed);
    TEST_ASSERT(space->allocation_frozen);
    TEST_ASSERT_EQ(space->tree.root, scratch);
    TEST_ASSERT_EQ(space->tree.height, 1);
    TEST_ASSERT_EQ(space->reserve_count, 0);
    TEST_ASSERT_EQ(fixture.device.writes - writes_before, 1);
    TEST_ASSERT_EQ(fs->pending_count, original_pending_count);
    TEST_ASSERT_MEM_EQ(fs->pending_frees, original_pending,
                       sizeof(original_pending));
    expected_staged_reserve[0] = scratch;
    TEST_ASSERT_MEM_EQ(expected_staged_reserve, space->reserve,
                       sizeof(expected_staged_reserve));
    TEST_ASSERT(original_reserve_count > 0);
    TEST_ASSERT_EQ(old_free_root != scratch, true);

    /* Every pool-origin input in all 32 historical slots is returned to the
     * working pool; scratch leaves it. Ordinary pending blocks stay in leaf. */
    expected_pool_t expected = {0};
    uint32_t ordinary_capacity = original_pending_count +
                                 original_reserve_count + 1u;
    bfs_blk_t ordinary_candidates[ordinary_capacity];
    uint32_t ordinary_count = 0;
    for (uint32_t i = 0; i < original_ec; i++)
        if (original_pool[i] != scratch)
            expected_pool_add(&expected, original_pool[i]);
    const bfs_blk_t *pending = fs->pending_frees_dynamic
        ? fs->pending_frees_dynamic : fs->pending_frees;
    for (uint32_t i = 0; i < original_pending_count; i++) {
        int index = historical_pool_index(original_pool, pending[i]);
        TEST_ASSERT(index < 0 || index >= (int)original_ec);
        if (index >= (int)original_ec)
            expected_pool_add(&expected, pending[i]);
        else if (index < 0)
            ordinary_candidates[ordinary_count++] = pending[i];
    }
    for (uint32_t i = 0; i < original_reserve_count; i++) {
        int index = historical_pool_index(original_pool, original_reserve[i]);
        TEST_ASSERT(index < 0 || index >= (int)original_ec);
        if (index >= (int)original_ec)
            expected_pool_add(&expected, original_reserve[i]);
        else if (index < 0)
            ordinary_candidates[ordinary_count++] = original_reserve[i];
    }
    int old_root_pool_index = historical_pool_index(original_pool, old_free_root);
    TEST_ASSERT(old_root_pool_index < 0 ||
                old_root_pool_index >= (int)original_ec);
    if (old_root_pool_index >= (int)original_ec)
        expected_pool_add(&expected, old_free_root);
    else if (old_root_pool_index < 0)
        ordinary_candidates[ordinary_count++] = old_free_root;

    uint32_t final_ec = bfs_be32(space->sb->emergency_count);
    TEST_ASSERT_EQ(final_ec, expected.count);
    bfs_blk_t final_pool[BFS_EMERGENCY_POOL_SIZE];
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        final_pool[i] = bfs_be32(space->sb->emergency_pool[i]);
        TEST_ASSERT(final_pool[i] != BFS_BLK_NULL);
        TEST_ASSERT(final_pool[i] < fixture.device.bio.block_count);
        TEST_ASSERT(!block_array_contains(final_pool, i, final_pool[i]));
        TEST_ASSERT(block_array_contains(original_pool,
                                         BFS_EMERGENCY_POOL_SIZE,
                                         final_pool[i]));
    }
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        TEST_ASSERT(block_array_contains(final_pool, BFS_EMERGENCY_POOL_SIZE,
                                         original_pool[i]));
    /* The final scratch is no longer active, but remains in the inactive tail;
     * in this case it was displaced from the active boundary by reactivating
     * the historical slot-31 directory root. */
    TEST_ASSERT(historical_pool_index(final_pool, scratch) >= (int)final_ec);

    for (uint32_t i = 0; i < final_ec; i++) {
        bfs_blk_t block = final_pool[i];
        TEST_ASSERT(expected_pool_contains(&expected, block));
        TEST_ASSERT(!current_leaf_contains(fs, block));
        TEST_ASSERT(block != space->tree.root);
        for (uint32_t j = 0; j < i; j++)
            TEST_ASSERT(block != final_pool[j]);
    }
    for (uint32_t i = 0; i < expected.count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < final_ec; j++)
            if (expected.blocks[i] == final_pool[j])
                found = true;
        TEST_ASSERT(found);
    }
    TEST_ASSERT_EQ(space->total_free,
                   original_total_free + ordinary_count);
    for (uint32_t i = 0; i < ordinary_count; i++) {
        TEST_ASSERT(!pool_contains(space->sb, ordinary_candidates[i]));
        TEST_ASSERT(current_leaf_contains(fs, ordinary_candidates[i]));
    }
    TEST_ASSERT(current_leaf_contains(fs, ordinary));
    TEST_ASSERT(historical_pool_index(original_pool, committed_dir_root) ==
                (int)BFS_EMERGENCY_POOL_SIZE - 1);
    TEST_ASSERT(!current_leaf_contains(fs, committed_dir_root));
    if (historical_pool_index(original_pool, old_free_root) < 0)
        TEST_ASSERT(current_leaf_contains(fs, old_free_root));
    else
        TEST_ASSERT(!current_leaf_contains(fs, old_free_root));

    allocator_snapshot_t frozen;
    take_snapshot(fs, &frozen);
    uint32_t sealed_write_count = fixture.device.writes;
    bfs_allocator_t *iface = bfs_freespace_allocator(space);
    bfs_free_sink_t sink = bfs_fs_free_sink(fs);
    TEST_ASSERT(bfs_freespace_alloc(space, 1) == BFS_BLK_NULL);
    TEST_ASSERT(iface->alloc(iface) == BFS_BLK_NULL);
    TEST_ASSERT_EQ(iface->error(iface), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(iface->dealloc(iface, ordinary), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_freespace_free(space, ordinary, 1), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_freespace_add(space, ordinary, 1), BFS_ERR_AGAIN);
    bfs_blk_t one_free[1] = { ordinary };
    TEST_ASSERT_EQ(bfs_freespace_free_sorted_blocks(space, one_free, 1),
                   BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(space), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(space), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_freespace_settle_reserve(space), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(fs, ordinary), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_fs_reserve_pending(fs, 1), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(bfs_fs_ensure_free_headroom(fs, 1), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(sink.defer(sink.ctx, ordinary), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(sink.reserve(sink.ctx, 1), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(sink.headroom(sink.ctx), 0);
    TEST_ASSERT_EQ(bfs_fs_alloc_ino(fs), 0);
    TEST_ASSERT_EQ(fixture.device.writes, sealed_write_count);
    TEST_ASSERT(snapshot_matches(fs, &frozen));

    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
}

static void test_api_error_is_caller_latched(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture));
    bfs_fs_t *fs = &fixture.fs;
    bfs_lock_write(&fs->lock);
    TEST_ASSERT(setup_future_free_root(&fixture));
    bool sealed = true;
    TEST_ASSERT_EQ(bfs_freespace_seal_commit(fs, &sealed), BFS_ERR_CORRUPT);
    TEST_ASSERT(!sealed);
    TEST_ASSERT_EQ(fs->recovery_error, BFS_OK);
    TEST_ASSERT_EQ(fixture.device.writes, 0);

    TEST_ASSERT_EQ(bfs_txn_commit(fs), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fs->recovery_error, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
}

TEST_SUITE_BEGIN("Sealed commit preflight")
    TEST_RUN(test_snapshot_contexts_decline_without_writes);
    TEST_RUN(test_older_future_deeper_and_full_leaf_shapes);
    TEST_RUN(test_missing_scratch_and_inactive_pool_alias_decline);
    TEST_RUN(test_backup_block_and_current_root_rejections);
    TEST_RUN(test_high_backup_quotients_do_not_alias_pending_blocks);
    TEST_RUN(test_high_backup_quotients_cast_only_for_old_leaf_checks);
    TEST_RUN(test_invalid_candidates_fail_before_write);
    TEST_RUN(test_cross_source_duplicates_and_bounds_fail_before_write);
    TEST_RUN(test_success_seals_mixed_provenance_and_freezes_mutation);
    TEST_RUN(test_api_error_is_caller_latched);
TEST_SUITE_END()
