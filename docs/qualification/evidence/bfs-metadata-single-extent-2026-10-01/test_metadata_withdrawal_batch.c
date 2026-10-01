/* SPDX-License-Identifier: MPL-2.0 */
/* Focused mounted ownership and convergence tests for metadata batching. */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "bfs_file.h"
#include "bfs_fsck.h"
#include "bfs_superblock.h"

#include <stdlib.h>

#define SMALL_VOLUME_BLOCKS 256u
#define CHURN_BLOCKS_1024 4096u
#define CHURN_BLOCKS_4096 1024u
#define FRAGMENTED_BLOCKS 4096u
#define LOW_SPACE_BLOCKS 512u
#define HEIGHT_ONE_FLOOR 20u
#define METADATA_BATCH_EXTRA 8u
#define NO_FIT_BLOCKS_1024 8192u

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    uint64_t reads;
    uint64_t writes;
    uint64_t syncs;
    bfs_blk_t last_write_block;

    bfs_fs_t *observed_fs;
    bool watch_commit;
    bool saw_commit_superblock;
    uint32_t commit_pre_superblock_writes;
    uint32_t commit_superblock_writes;
    uint32_t commit_frozen_writes;
    uint32_t commit_frozen_superblock_writes;
} memory_device_t;

typedef struct {
    memory_device_t device;
    bfs_fs_t fs;
} fixture_t;

typedef struct {
    uint32_t count;
    uint64_t total;
    uint32_t largest_length;
    bfs_blk_t largest_start;
    uint32_t highest_length;
    bfs_blk_t highest_start;
} extent_summary_t;

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
    device->last_write_block = block;
    if (device->watch_commit) {
        bfs_fs_t *fs = device->observed_fs;
        uint64_t backup_offset = fs
            ? bfs_sb_backup_offset(&fs->txn.sb_new) : 0;
        bool superblock = block == 0 ||
            (fs && backup_offset % bio->block_size == 0 &&
             block == backup_offset / bio->block_size);
        if (fs && fs->freespace.allocation_frozen) {
            device->commit_frozen_writes++;
            if (superblock) device->commit_frozen_superblock_writes++;
        }
        if (superblock) {
            device->saw_commit_superblock = true;
            device->commit_superblock_writes++;
        } else if (!device->saw_commit_superblock) {
            device->commit_pre_superblock_writes++;
        }
    }
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

static bool fixture_init(fixture_t *fixture, uint32_t block_size,
                         bfs_blk_t block_count, uint32_t options,
                         bool read_only)
{
    memset(fixture, 0, sizeof(*fixture));
    if ((size_t)block_count > SIZE_MAX / block_size) return false;
    fixture->device.bytes = calloc((size_t)block_count, block_size);
    if (!fixture->device.bytes) return false;
    fixture->device.bio.ops = &memory_ops;
    fixture->device.bio.block_size = block_size;
    fixture->device.bio.block_count = block_count;

    if (bfs_fs_format(&fixture->device.bio, "MetadataBatch", options) != BFS_OK)
        goto fail;
    bfs_err_t err = read_only
        ? bfs_fs_mount_readonly(&fixture->fs, &fixture->device.bio)
        : bfs_fs_mount(&fixture->fs, &fixture->device.bio);
    if (err != BFS_OK) goto fail;
    fixture->device.observed_fs = &fixture->fs;
    fixture->device.reads = 0;
    fixture->device.writes = 0;
    fixture->device.syncs = 0;
    return true;

fail:
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
    return false;
}

static void fixture_destroy(fixture_t *fixture)
{
    if (fixture->fs.mounted) bfs_fs_abandon(&fixture->fs);
    fixture->device.observed_fs = NULL;
    free(fixture->device.bytes);
    fixture->device.bytes = NULL;
}

static bool summarize_extent(const void *key, const void *value, void *context)
{
    extent_summary_t *summary = (extent_summary_t *)context;
    bfs_blk_t start = bfs_load_be32(key);
    uint32_t length = bfs_load_be32(value);
    summary->count++;
    summary->total += length;
    if (summary->count == 1 || start > summary->highest_start) {
        summary->highest_start = start;
        summary->highest_length = length;
    }
    if (length > summary->largest_length) {
        summary->largest_length = length;
        summary->largest_start = start;
    }
    return true;
}

static bool extent_summary(bfs_fs_t *fs, extent_summary_t *summary)
{
    memset(summary, 0, sizeof(*summary));
    return bfs_btree_scan(&fs->freespace.tree, NULL, summarize_extent,
                          summary) == BFS_OK;
}

typedef struct {
    bfs_blk_t *starts;
    uint32_t count;
    uint32_t capacity;
    bool overflow;
} singleton_capture_t;

static bool capture_singleton_extent(const void *key, const void *value,
                                     void *context)
{
    singleton_capture_t *capture = (singleton_capture_t *)context;
    if (bfs_load_be32(value) != 1) return true;
    if (capture->count == capture->capacity) {
        capture->overflow = true;
        return false;
    }
    capture->starts[capture->count++] = bfs_load_be32(key);
    return true;
}

static bool capture_singleton_extents(bfs_fs_t *fs, bfs_blk_t *starts,
                                      uint32_t capacity,
                                      uint32_t *count_out)
{
    singleton_capture_t capture = {
        .starts = starts, .capacity = capacity,
    };
    bfs_err_t err = bfs_btree_scan(&fs->freespace.tree, NULL,
                                   capture_singleton_extent, &capture);
    *count_out = capture.count;
    return err == BFS_OK && !capture.overflow;
}

typedef struct {
    uint32_t min_length;
    uint32_t fitting_count;
    uint32_t length;
    bfs_blk_t start;
} fitting_extent_t;

static bool capture_highest_fitting_extent(const void *key, const void *value,
                                           void *context)
{
    fitting_extent_t *fit = (fitting_extent_t *)context;
    uint32_t start = bfs_load_be32(key);
    uint32_t length = bfs_load_be32(value);
    if (length < fit->min_length) return true;
    fit->fitting_count++;
    if (fit->fitting_count == 1 || start > fit->start) {
        fit->start = start;
        fit->length = length;
    }
    return true;
}

static bool highest_fitting_extent(bfs_fs_t *fs, uint32_t min_length,
                                   fitting_extent_t *fit)
{
    memset(fit, 0, sizeof(*fit));
    fit->min_length = min_length;
    return bfs_btree_scan(&fs->freespace.tree, NULL,
                          capture_highest_fitting_extent, fit) == BFS_OK &&
           fit->fitting_count > 0;
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

static bool reserve_contains(const bfs_freespace_t *space, bfs_blk_t block)
{
    for (uint32_t i = 0; i < space->reserve_count; i++)
        if (space->reserve[i] == block) return true;
    return false;
}

static bool active_pool_contains(const bfs_superblock_t *sb, bfs_blk_t block)
{
    uint32_t active = bfs_be32(sb->emergency_count);
    if (active > BFS_EMERGENCY_POOL_SIZE) return true;
    for (uint32_t i = 0; i < active; i++)
        if (bfs_be32(sb->emergency_pool[i]) == block) return true;
    return false;
}

static bool allocator_accounting_valid(bfs_fs_t *fs)
{
    extent_summary_t summary;
    if (!extent_summary(fs, &summary) || summary.total != fs->freespace.total_free ||
        fs->freespace.reserve_count > BFS_ALLOC_RESERVE_SIZE)
        return false;
    for (uint32_t i = 0; i < fs->freespace.reserve_count; i++) {
        bfs_blk_t block = fs->freespace.reserve[i];
        if (block == BFS_BLK_NULL || block >= fs->bio->block_count ||
            block == fs->freespace.tree.root || free_tree_contains(fs, block) ||
            active_pool_contains(&fs->txn.sb_new, block))
            return false;
        for (uint32_t j = 0; j < i; j++)
            if (fs->freespace.reserve[j] == block) return false;
    }
    return true;
}

static bool strict_fsck(bfs_fs_t *fs)
{
    bfs_fsck_report_t report = {0};
    bfs_err_t err = bfs_fs_check(fs, false, &report);
    if (err != BFS_OK || report.errors != 0 || report.warnings != 0 ||
        report.leaked_blocks != 0) {
        fprintf(stderr,
                "  strict fsck: err=%d errors=%u warnings=%u leaked=%u\n",
                err, report.errors, report.warnings, report.leaked_blocks);
        return false;
    }
    return true;
}

static bool read_file_matches(bfs_fs_t *fs, uint32_t ino,
                              const uint8_t *expected, uint32_t length);

static bool readonly_remount_is_clean(fixture_t *fixture)
{
    bfs_bio_t *bio = &fixture->device.bio;
    if (fixture->fs.mounted) bfs_fs_abandon(&fixture->fs);
    bfs_fs_t readonly_fs;
    bfs_err_t err = bfs_fs_mount_readonly(&readonly_fs, bio);
    if (err != BFS_OK) {
        fprintf(stderr, "  readonly remount failed: %d\n", err);
        return false;
    }
    bool clean = strict_fsck(&readonly_fs);
    err = bfs_fs_unmount(&readonly_fs);
    return clean && err == BFS_OK;
}

static bool readonly_remount_check_file(fixture_t *fixture, const char *name,
                                        uint8_t name_len, uint32_t expected_ino,
                                        const uint8_t *data, uint32_t length,
                                        bool should_exist)
{
    bfs_bio_t *bio = &fixture->device.bio;
    if (fixture->fs.mounted) bfs_fs_abandon(&fixture->fs);
    bfs_fs_t readonly_fs;
    bfs_err_t err = bfs_fs_mount_readonly(&readonly_fs, bio);
    if (err != BFS_OK) return false;
    bool valid = strict_fsck(&readonly_fs);
    uint32_t ino = 0, type = 0;
    err = bfs_dir_lookup(&readonly_fs.dir_tree, BFS_ROOT_INO, name, name_len,
                         &ino, &type);
    if (should_exist) {
        valid = valid && err == BFS_OK && ino == expected_ino &&
            type == BFS_INODE_FILE && read_file_matches(&readonly_fs, ino,
                                                        data, length);
    } else {
        valid = valid && err == BFS_ERR_NOTFOUND;
    }
    if (bfs_fs_unmount(&readonly_fs) != BFS_OK) valid = false;
    if (!valid || bfs_fs_mount(&fixture->fs, bio) != BFS_OK) return false;
    fixture->device.observed_fs = &fixture->fs;
    return true;
}

static bool queue_owned_block(bfs_fs_t *fs, bfs_blk_t block)
{
    return block != BFS_BLK_NULL &&
           bfs_fs_queue_pending_free(fs, block) == BFS_OK;
}

static bool queue_owned_range(bfs_fs_t *fs, bfs_blk_t start, uint32_t count)
{
    if (start == BFS_BLK_NULL || count == 0 ||
        start >= fs->bio->block_count || count > fs->bio->block_count - start)
        return false;
    for (uint32_t i = 0; i < count; i++)
        if (!queue_owned_block(fs, start + i)) return false;
    return true;
}

static bool run_first_request_case(uint32_t block_size)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, block_size, SMALL_VOLUME_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    uint64_t live = fs->live_txn_id;
    uint64_t root_txn = 0;
    bfs_blk_t returned[3] = {BFS_BLK_NULL, BFS_BLK_NULL, BFS_BLK_NULL};

    if (fs->freespace.mounted_owner != fs ||
        fs->freespace.tree.height != 1 ||
        fs->freespace.tree.root == BFS_BLK_NULL ||
        live != bfs_txn_id(&fs->txn) ||
        live <= bfs_be64(fs->txn.sb.txn_id) ||
        fs->freespace.metadata_requests != 0 ||
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK ||
        root_txn >= live) {
        fprintf(stderr, "  %u-byte setup did not expose an eligible mounted root\n",
                block_size);
        goto done;
    }

    bfs_free_sink_t expected_sink = bfs_fs_free_sink(fs);
    if (fs->freespace.tree.free_sink.ctx != expected_sink.ctx ||
        fs->freespace.tree.free_sink.defer != expected_sink.defer ||
        fs->freespace.tree.free_sink.headroom != expected_sink.headroom ||
        fs->freespace.tree.free_sink.reserve != expected_sink.reserve ||
        fs->freespace.tree.free_sink.capacity != expected_sink.capacity) {
        fprintf(stderr, "  %u-byte free sink is not owner-bound\n", block_size);
        goto done;
    }

    uint32_t initial_free = fs->freespace.total_free;
    uint32_t initial_stock = fs->freespace.reserve_count;
    uint32_t initial_emergency = bfs_be32(fs->txn.sb_new.emergency_count);
    uint32_t initial_pending = fs->pending_count;
    bfs_blk_t initial_root = fs->freespace.tree.root;
    uint32_t batch_target = HEIGHT_ONE_FLOOR + METADATA_BATCH_EXTRA;
    if (initial_stock > HEIGHT_ONE_FLOOR ||
        batch_target > BFS_ALLOC_RESERVE_SIZE) {
        fprintf(stderr, "  %u-byte stock setup mismatch: stock=%u target=%u capacity=%u\n",
                block_size, initial_stock, batch_target,
                BFS_ALLOC_RESERVE_SIZE);
        goto done;
    }
    uint64_t expected_draw = (uint64_t)batch_target - initial_stock;
    if (root_txn < live && initial_stock > 0) expected_draw++;
    if (root_txn == live && initial_stock == 0) expected_draw--;
    if (initial_free <= expected_draw) {
        fprintf(stderr, "  %u-byte insufficient draw headroom: free=%u draw=%llu\n",
                block_size, initial_free,
                (unsigned long long)expected_draw);
        goto done;
    }
    bfs_lock_write(&fs->lock);
    for (uint32_t i = 0; i < 3; i++) {
        uint64_t writes_before = fixture.device.writes;
        uint32_t free_before = fs->freespace.total_free;
        uint32_t stock_before = fs->freespace.reserve_count;
        uint32_t pending_before = fs->pending_count;
        returned[i] = fs->freespace.iface.alloc(&fs->freespace.iface);
        if (returned[i] == BFS_BLK_NULL || free_tree_contains(fs, returned[i]) ||
            reserve_contains(&fs->freespace, returned[i])) {
            fprintf(stderr, "  %u-byte request %u returned no distinct block\n",
                    block_size, i + 1);
            bfs_lock_unlock(&fs->lock);
            goto done;
        }
        if (i == 0) {
            uint64_t current_root_txn = 0;
            if (fs->freespace.metadata_batch_txn != live ||
                fs->freespace.metadata_reuse_txn != live ||
                fs->freespace.metadata_requests != 1 ||
                fs->freespace.reserve_count != batch_target - 1u ||
                fs->freespace.reserve_count >= BFS_ALLOC_RESERVE_SIZE ||
                bfs_btree_root_leaf_txn_id(&fs->freespace.tree,
                                            &current_root_txn) != BFS_OK ||
                current_root_txn != live ||
                fs->freespace.tree.root == initial_root ||
                fs->freespace.tree.root ==
                    bfs_be32(fs->txn.sb.free_tree_root) ||
                fs->freespace.total_free != initial_free - expected_draw ||
                bfs_be32(fs->txn.sb_new.emergency_count) !=
                    initial_emergency - (initial_stock == 0 ? 1u : 0u) ||
                fs->pending_count != initial_pending + 1u ||
                fixture.device.writes != writes_before + 1u ||
                fixture.device.last_write_block != fs->freespace.tree.root ||
                !allocator_accounting_valid(fs)) {
                fprintf(stderr,
                        "  %u-byte first request did not establish a checked batch\n",
                        block_size);
                bfs_lock_unlock(&fs->lock);
                goto done;
            }
            if (fs->freespace.roving != returned[i] + 1) {
                fprintf(stderr, "  %u-byte batch pop did not apply roving rule\n",
                        block_size);
                bfs_lock_unlock(&fs->lock);
                goto done;
            }
        } else if (fs->freespace.metadata_batch_txn != live ||
                   fs->freespace.metadata_requests != i + 1 ||
                   fs->freespace.reserve_count + 1 != stock_before ||
                   fs->freespace.total_free != free_before ||
                   fixture.device.writes != writes_before ||
                   fs->pending_count != pending_before ||
                   fs->freespace.reserve_count < HEIGHT_ONE_FLOOR ||
                   !allocator_accounting_valid(fs)) {
            fprintf(stderr,
                    "  %u-byte repeat request %u did not use ordinary surplus\n",
                    block_size, i + 1);
            bfs_lock_unlock(&fs->lock);
            goto done;
        }
        for (uint32_t j = 0; j < i; j++)
            if (returned[j] == returned[i]) {
                fprintf(stderr, "  %u-byte repeated pops were not distinct\n",
                        block_size);
                bfs_lock_unlock(&fs->lock);
                goto done;
            }
        if (!queue_owned_block(fs, returned[i])) {
            fprintf(stderr, "  %u-byte raw allocation could not be deferred\n",
                    block_size);
            bfs_lock_unlock(&fs->lock);
            goto done;
        }
    }
    bfs_lock_unlock(&fs->lock);

    if (bfs_fs_sync(fs) != BFS_OK || fs->pending_count != 0 ||
        fs->recovery_error != BFS_OK || !strict_fsck(fs)) {
        fprintf(stderr, "  %u-byte first batch did not settle cleanly\n",
                block_size);
        goto done;
    }

    uint64_t next_live = fs->live_txn_id;
    if (next_live <= live || fs->freespace.metadata_batch_txn == next_live) {
        fprintf(stderr, "  %u-byte batch permission did not expire at sync\n",
                block_size);
        goto done;
    }

    bfs_blk_t next = BFS_BLK_NULL;
    bfs_lock_write(&fs->lock);
    next = fs->freespace.iface.alloc(&fs->freespace.iface);
    bool new_epoch = next != BFS_BLK_NULL &&
        fs->freespace.metadata_batch_txn == next_live &&
        fs->freespace.metadata_reuse_txn == next_live &&
        fs->freespace.metadata_requests == 1 &&
        fs->freespace.reserve_count >= HEIGHT_ONE_FLOOR &&
        !free_tree_contains(fs, next) && !reserve_contains(&fs->freespace, next) &&
        allocator_accounting_valid(fs);
    if (new_epoch) new_epoch = queue_owned_block(fs, next);
    bfs_lock_unlock(&fs->lock);
    if (!new_epoch || bfs_fs_sync(fs) != BFS_OK || !strict_fsck(fs)) {
        fprintf(stderr, "  %u-byte next transaction did not get one new epoch\n",
                block_size);
        goto done;
    }
    ok = readonly_remount_is_clean(&fixture);

done:
    fixture_destroy(&fixture);
    return ok;
}

static void test_first_request_batch_all_supported_block_sizes(void)
{
    static const uint32_t block_sizes[] = {
        1024u, 2048u, 4096u, 8192u, 16384u, 32768u, 65536u,
    };
    for (uint32_t i = 0; i < sizeof(block_sizes) / sizeof(block_sizes[0]); i++)
        TEST_ASSERT(run_first_request_case(block_sizes[i]));
}

static bool read_file_matches(bfs_fs_t *fs, uint32_t ino,
                              const uint8_t *expected, uint32_t length)
{
    uint8_t *actual = malloc(length ? length : 1);
    if (!actual) return false;
    bfs_file_t file;
    bfs_err_t err = bfs_file_open(&file, fs, ino);
    bool matches = err == BFS_OK && file.size == length &&
        bfs_file_read(&file, actual, length) == (int32_t)length &&
        memcmp(actual, expected, length) == 0;
    free(actual);
    return matches;
}

static bool run_churn_case(uint32_t block_size, bfs_blk_t block_count)
{
    fixture_t fixture;
    uint32_t options = BFS_OPT_DATA_CHECKSUMS | BFS_OPT_DATA_ORDERED;
    if (!fixture_init(&fixture, block_size, block_count, options, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    uint32_t length = block_size + 97u;
    uint8_t *data_a = malloc(length);
    uint8_t *data_b = malloc(length);
    bool ok = false;
    if (!data_a || !data_b) goto done;

    for (uint32_t cycle = 0; cycle < 3; cycle++) {
        memset(data_a, (int)(0x31u + cycle), length);
        memset(data_b, (int)(0xA7u + cycle), length);
        uint32_t ino = 0;
        uint64_t create_txn = fs->live_txn_id;
        if (bfs_fs_create_file(fs, BFS_ROOT_INO, "batch-cycle", 11, &ino) != BFS_OK ||
            ino == 0 || fs->freespace.metadata_batch_txn != create_txn) {
            fprintf(stderr, "  %u-byte cycle %u create missed its first batch\n",
                    block_size, cycle);
            goto done;
        }
        bfs_file_t file;
        if (bfs_file_open(&file, fs, ino) != BFS_OK ||
            bfs_file_write(&file, data_a, length) != (int32_t)length ||
            !read_file_matches(fs, ino, data_a, length) ||
            bfs_fs_sync(fs) != BFS_OK || !strict_fsck(fs)) {
            fprintf(stderr, "  %u-byte cycle %u create/write commit failed\n",
                    block_size, cycle);
            goto done;
        }
        if (fs->live_txn_id <= create_txn ||
            fs->freespace.metadata_batch_txn == fs->live_txn_id ||
            !readonly_remount_check_file(&fixture, "batch-cycle", 11, ino,
                                         data_a, length, true)) {
            fprintf(stderr, "  %u-byte cycle %u persisted create data failed\n",
                    block_size, cycle);
            goto done;
        }
        uint64_t overwrite_txn = fs->live_txn_id;
        if (bfs_file_open(&file, fs, ino) != BFS_OK ||
            bfs_file_write(&file, data_b, length) != (int32_t)length ||
            fs->freespace.metadata_batch_txn != overwrite_txn ||
            !read_file_matches(fs, ino, data_b, length) ||
            bfs_fs_sync(fs) != BFS_OK || !strict_fsck(fs)) {
            fprintf(stderr, "  %u-byte cycle %u overwrite epoch failed\n",
                    block_size, cycle);
            goto done;
        }
        if (fs->live_txn_id <= overwrite_txn ||
            fs->freespace.metadata_batch_txn == fs->live_txn_id ||
            !readonly_remount_check_file(&fixture, "batch-cycle", 11, ino,
                                         data_b, length, true)) {
            fprintf(stderr, "  %u-byte cycle %u persisted overwrite failed\n",
                    block_size, cycle);
            goto done;
        }
        uint64_t delete_txn = fs->live_txn_id;
        if (bfs_fs_delete_file(fs, BFS_ROOT_INO, "batch-cycle", 11) != BFS_OK ||
            fs->freespace.metadata_batch_txn != delete_txn ||
            bfs_fs_sync(fs) != BFS_OK || !strict_fsck(fs) ||
            fs->pending_count != 0 || fs->recovery_error != BFS_OK) {
            fprintf(stderr, "  %u-byte cycle %u delete commit failed\n",
                    block_size, cycle);
            goto done;
        }
        if (fs->live_txn_id <= delete_txn ||
            !readonly_remount_check_file(&fixture, "batch-cycle", 11, 0,
                                         NULL, 0, false)) {
            fprintf(stderr, "  %u-byte cycle %u persisted delete failed\n",
                    block_size, cycle);
            goto done;
        }
    }

    ok = readonly_remount_is_clean(&fixture);
done:
    free(data_b);
    free(data_a);
    fixture_destroy(&fixture);
    return ok;
}

static void test_repeated_create_overwrite_delete_sync(void)
{
    TEST_ASSERT(run_churn_case(1024u, CHURN_BLOCKS_1024));
    TEST_ASSERT(run_churn_case(4096u, CHURN_BLOCKS_4096));
}

static bool run_disabled_context_case(uint32_t shape)
{
    enum {
        OWNER_NULL,
        OWNER_MISMATCH,
        SINK_TUPLE_MISMATCH,
        SNAPSHOT_OPTION,
        SNAPSHOT_RUNTIME,
        REFCOUNT_ROOT,
        LEGACY_EXTENT_ROOT,
        CONTEXT_COUNT,
    };
    fixture_t fixture;
    fixture_t foreign_fixture;
    memset(&foreign_fixture, 0, sizeof(foreign_fixture));
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool foreign_ready = false;
    if (shape == OWNER_MISMATCH) {
        if (!fixture_init(&foreign_fixture, 1024u, FRAGMENTED_BLOCKS, 0, false)) {
            fixture_destroy(&fixture);
            return false;
        }
        foreign_ready = true;
    }
    bfs_fs_t *saved_owner = fs->freespace.mounted_owner;
    bfs_free_sink_t saved_sink = fs->freespace.tree.free_sink;
    uint32_t saved_options = fs->txn.sb_new.options;
    uint32_t saved_refcount_root = fs->txn.sb_new.refcount_tree_root;
    uint32_t saved_extent_root = fs->txn.sb_new.extent_tree_root;
    bool saved_snapshots = fs->has_snapshots;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;

    bfs_lock_write(&fs->lock);
    switch (shape) {
    case OWNER_NULL:
        fs->freespace.mounted_owner = NULL;
        break;
    case OWNER_MISMATCH:
        fs->freespace.mounted_owner = &foreign_fixture.fs;
        break;
    case SINK_TUPLE_MISMATCH:
        fs->freespace.tree.free_sink.capacity++;
        break;
    case SNAPSHOT_OPTION:
        fs->txn.sb_new.options = bfs_be32(
            bfs_be32(fs->txn.sb_new.options) | BFS_OPT_SNAPSHOTS);
        break;
    case SNAPSHOT_RUNTIME:
        fs->has_snapshots = true;
        break;
    case REFCOUNT_ROOT:
        fs->txn.sb_new.refcount_tree_root = fs->dir_tree.tree.root;
        break;
    case LEGACY_EXTENT_ROOT:
        fs->txn.sb_new.extent_tree_root = fs->dir_tree.tree.root;
        break;
    default:
        bfs_lock_unlock(&fs->lock);
        goto done;
    }

    uint64_t live = fs->live_txn_id;
    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    fs->freespace.mounted_owner = saved_owner;
    fs->freespace.tree.free_sink = saved_sink;
    fs->txn.sb_new.options = saved_options;
    fs->txn.sb_new.refcount_tree_root = saved_refcount_root;
    fs->txn.sb_new.extent_tree_root = saved_extent_root;
    fs->has_snapshots = saved_snapshots;
    if (allocated == BFS_BLK_NULL || fs->freespace.metadata_batch_txn == live ||
        free_tree_contains(fs, allocated) || reserve_contains(&fs->freespace, allocated) ||
        !queue_owned_block(fs, allocated)) {
        bfs_lock_unlock(&fs->lock);
        goto done;
    }
    bfs_lock_unlock(&fs->lock);
    ok = bfs_fs_sync(fs) == BFS_OK && fs->pending_count == 0 &&
         fs->recovery_error == BFS_OK && strict_fsck(fs);

done:
    if (foreign_ready) fixture_destroy(&foreign_fixture);
    fixture_destroy(&fixture);
    return ok;
}

static void test_owner_and_snapshot_gates_fall_back_to_normal_allocator(void)
{
    for (uint32_t shape = 0; shape < 7; shape++)
        TEST_ASSERT(run_disabled_context_case(shape));
}

static void test_readonly_frozen_and_recovery_gates_do_not_allocate(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, 1024u, SMALL_VOLUME_BLOCKS, 0, true));
    bfs_freespace_t *space = &fixture.fs.freespace;
    TEST_ASSERT(fixture.fs.read_only);
    uint64_t writes = fixture.device.writes;
    TEST_ASSERT_EQ(space->iface.alloc(&space->iface), BFS_BLK_NULL);
    TEST_ASSERT_EQ(space->iface.error(&space->iface), BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(fixture.device.writes, writes);
    fixture_destroy(&fixture);

    TEST_ASSERT(fixture_init(&fixture, 1024u, SMALL_VOLUME_BLOCKS, 0, false));
    space = &fixture.fs.freespace;
    bfs_lock_write(&fixture.fs.lock);
    space->allocation_frozen = true;
    writes = fixture.device.writes;
    TEST_ASSERT_EQ(space->iface.alloc(&space->iface), BFS_BLK_NULL);
    TEST_ASSERT_EQ(space->iface.error(&space->iface), BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(fixture.device.writes, writes);
    space->allocation_frozen = false;
    fixture.fs.recovery_error = BFS_ERR_IO;
    TEST_ASSERT_EQ(space->iface.alloc(&space->iface), BFS_BLK_NULL);
    TEST_ASSERT_EQ(space->iface.error(&space->iface), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.writes, writes);
    fixture.fs.recovery_error = BFS_OK;
    bfs_lock_unlock(&fixture.fs.lock);
    fixture_destroy(&fixture);
}

static bool future_root_is_rejected_before_write(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t temporary = BFS_BLK_NULL;
    uint64_t old_root_txn = 0, future_root_txn = 0;

    bfs_lock_write(&fs->lock);
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree,
                                    &old_root_txn) != BFS_OK)
        goto unlock;
    fs->live_txn_id = old_root_txn + 2;
    fs->txn.sb_new.txn_id = bfs_be64(fs->live_txn_id);
    temporary = bfs_freespace_alloc(&fs->freespace, 1);
    if (temporary == BFS_BLK_NULL ||
        bfs_freespace_free(&fs->freespace, temporary, 1) != BFS_OK)
        goto unlock;
    temporary = BFS_BLK_NULL; /* It is back in the Free-Tree. */
    fs->live_txn_id = old_root_txn + 1;
    fs->txn.sb_new.txn_id = bfs_be64(fs->live_txn_id);
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree,
                                    &future_root_txn) != BFS_OK ||
        future_root_txn != old_root_txn + 2)
        goto unlock;
    fixture.device.writes = 0;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint32_t free_before = fs->freespace.total_free;
    uint32_t stock_before = fs->freespace.reserve_count;
    bfs_blk_t allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = allocated == BFS_BLK_NULL &&
         fs->freespace.iface.error(&fs->freespace.iface) == BFS_ERR_CORRUPT &&
         fixture.device.writes == 0 && fs->freespace.tree.root == root_before &&
         fs->freespace.total_free == free_before &&
         fs->freespace.reserve_count == stock_before &&
         fs->freespace.metadata_batch_txn == 0 &&
         fs->recovery_error == BFS_OK;

unlock:
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
    return ok;
}

static void test_future_leaf_tag_is_rejected_before_optional_write(void)
{
    TEST_ASSERT(future_root_is_rejected_before_write());
}

static bool committed_root_equal_live_tag_is_rejected_unchanged(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bool bytes_restored = false;
    uint8_t *original = malloc(fs->bio->block_size);
    bfs_blk_t *pending_snapshot = NULL;
    if (!original) {
        fixture_destroy(&fixture);
        return false;
    }

    bfs_lock_write(&fs->lock);
    bfs_blk_t root = fs->freespace.tree.root;
    bfs_blk_t committed_root = bfs_be32(fs->txn.sb.free_tree_root);
    uint64_t old_tag = 0;
    uint64_t live = fs->live_txn_id;
    uint32_t metadata_requests_before = fs->freespace.metadata_requests;
    uint64_t metadata_reuse_txn_before = fs->freespace.metadata_reuse_txn;
    if (fs->freespace.tree.height != 1 || root == BFS_BLK_NULL ||
        root != committed_root ||
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &old_tag) != BFS_OK ||
        old_tag >= live || fs->freespace.metadata_batch_txn != 0 ||
        metadata_requests_before != 0 || metadata_reuse_txn_before == live) {
        fprintf(stderr,
                "  committed-root setup mismatch: root=%u committed=%u "
                "height=%u old=%llu live=%llu batch=%llu requests=%u reuse=%llu\n",
                root, committed_root, fs->freespace.tree.height,
                (unsigned long long)old_tag, (unsigned long long)live,
                (unsigned long long)fs->freespace.metadata_batch_txn,
                metadata_requests_before,
                (unsigned long long)metadata_reuse_txn_before);
        goto unlock;
    }

    size_t root_offset = (size_t)root * fs->bio->block_size;
    uint8_t *root_bytes = fixture.device.bytes + root_offset;
    memcpy(original, root_bytes, fs->bio->block_size);
    bfs_btnode_hdr_t *header = hdr_of(root_bytes);
    header->txn_id = bfs_be64(live);
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&fs->freespace.tree,
                                               root_bytes));
    uint64_t forged_tag = 0;
    if (bfs_be32(header->crc32) !=
            node_compute_crc(&fs->freespace.tree, root_bytes) ||
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &forged_tag) != BFS_OK ||
        forged_tag != live) {
        fprintf(stderr, "  forged node did not retain a valid CRC/live tag\n");
        goto restore;
    }

    bfs_blk_t reserve_before[BFS_ALLOC_RESERVE_SIZE];
    uint32_t emergency_pool_before[BFS_EMERGENCY_POOL_SIZE];
    memcpy(reserve_before, fs->freespace.reserve,
           sizeof(reserve_before));
    memcpy(emergency_pool_before, fs->txn.sb_new.emergency_pool,
           sizeof(emergency_pool_before));
    uint32_t reserve_count_before = fs->freespace.reserve_count;
    uint32_t emergency_count_before = fs->txn.sb_new.emergency_count;
    uint32_t pending_cap_before = fs->pending_frees_cap;
    bfs_blk_t *pending_storage_before = bfs_fs_pending_items(fs);
    uint32_t total_free_before = fs->freespace.total_free;
    bfs_blk_t roving_before = fs->freespace.roving;
    bfs_err_t sink_error_before = fs->freespace.tree.free_sink_err;
    uint32_t pending_before = fs->pending_count;
    if (pending_before > 0) {
        pending_snapshot = malloc((size_t)pending_before *
                                  sizeof(*pending_snapshot));
        if (!pending_snapshot) goto restore;
        memcpy(pending_snapshot, bfs_fs_pending_items(fs),
               (size_t)pending_before * sizeof(*pending_snapshot));
    }
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint64_t batch_tag_before = fs->freespace.metadata_batch_txn;
    uint64_t writes_before = fixture.device.writes;
    bool frozen_before = fs->freespace.allocation_frozen;

    bfs_blk_t allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = allocated == BFS_BLK_NULL &&
         fs->freespace.iface.error(&fs->freespace.iface) == BFS_ERR_CORRUPT &&
         fixture.device.writes == writes_before &&
         fs->freespace.reserve_count == reserve_count_before &&
         memcmp(fs->freespace.reserve, reserve_before,
                sizeof(reserve_before)) == 0 &&
         fs->txn.sb_new.emergency_count == emergency_count_before &&
         memcmp(fs->txn.sb_new.emergency_pool, emergency_pool_before,
                sizeof(emergency_pool_before)) == 0 &&
         fs->pending_frees_cap == pending_cap_before &&
         bfs_fs_pending_items(fs) == pending_storage_before &&
         fs->freespace.total_free == total_free_before &&
         fs->freespace.roving == roving_before &&
         fs->freespace.tree.free_sink_err == sink_error_before &&
         fs->pending_count == pending_before &&
         (!pending_before ||
          memcmp(bfs_fs_pending_items(fs), pending_snapshot,
                 (size_t)pending_before * sizeof(*pending_snapshot)) == 0) &&
         fs->freespace.tree.root == root_before &&
         fs->freespace.metadata_batch_txn == batch_tag_before &&
         fs->freespace.metadata_reuse_txn == live &&
         fs->freespace.metadata_requests == metadata_requests_before + 1u &&
         fs->freespace.allocation_frozen == frozen_before &&
         fs->recovery_error == BFS_OK;
    if (!ok)
        fprintf(stderr,
                "  forged committed root was not rejected unchanged: block=%u "
                "error=%d writes=%llu/%llu free=%u/%u stock=%u/%u "
                "roving=%u/%u pending=%u/%u root=%u/%u batch=%llu\n",
                allocated, fs->freespace.iface.error(&fs->freespace.iface),
                (unsigned long long)fixture.device.writes,
                (unsigned long long)writes_before,
                fs->freespace.total_free, total_free_before,
                fs->freespace.reserve_count, reserve_count_before,
                fs->freespace.roving, roving_before,
                fs->pending_count, pending_before,
                fs->freespace.tree.root, root_before,
                (unsigned long long)fs->freespace.metadata_batch_txn);

restore:
    memcpy(root_bytes, original, fs->bio->block_size);
    bytes_restored = memcmp(root_bytes, original, fs->bio->block_size) == 0;
unlock:
    bfs_lock_unlock(&fs->lock);
    free(pending_snapshot);
    free(original);
    bool restored_clean = strict_fsck(fs) &&
                          readonly_remount_is_clean(&fixture);
    ok = ok && bytes_restored && restored_clean;
    fixture_destroy(&fixture);
    return ok;
}

static void test_crc_valid_equal_live_tag_on_committed_root_is_corrupt(void)
{
    TEST_ASSERT(committed_root_equal_live_tag_is_rejected_unchanged());
}

static bool prime_current_root(fixture_t *fixture)
{
    bfs_fs_t *fs = &fixture->fs;
    bfs_blk_t block = bfs_freespace_alloc(&fs->freespace, 1);
    if (block == BFS_BLK_NULL) return false;
    if (bfs_freespace_free(&fs->freespace, block, 1) != BFS_OK) return false;
    uint64_t root_txn = 0;
    return fs->freespace.tree.height == 1 &&
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) == BFS_OK &&
        root_txn == fs->live_txn_id &&
        fs->freespace.reserve_count == HEIGHT_ONE_FLOOR;
}

static bool run_zero_stock_emergency_batch_case(bool current_root_case)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t held[BFS_ALLOC_RESERVE_SIZE];
    uint32_t held_count = 0;
    bfs_blk_t allocated = BFS_BLK_NULL;

    bfs_lock_write(&fs->lock);
    if ((current_root_case && !prime_current_root(&fixture)) ||
        fs->freespace.metadata_requests != 0 ||
        fs->freespace.metadata_batch_txn != 0 ||
        fs->freespace.reserve_count !=
            (current_root_case ? HEIGHT_ONE_FLOOR : 0u) ||
        !allocator_accounting_valid(fs))
        goto unlock;

    uint64_t live = fs->live_txn_id;
    uint64_t root_txn = 0;
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK ||
        (root_txn == live) != current_root_case ||
        (current_root_case && fs->freespace.tree.root ==
             bfs_be32(fs->txn.sb.free_tree_root)) ||
        (!current_root_case && fs->freespace.tree.root !=
             bfs_be32(fs->txn.sb.free_tree_root)))
        goto unlock;

    extent_summary_t summary;
    uint32_t target = HEIGHT_ONE_FLOOR + METADATA_BATCH_EXTRA;
    uint64_t draw = target;
    if (current_root_case) draw--;
    if (target > BFS_ALLOC_RESERVE_SIZE || !extent_summary(fs, &summary) ||
        summary.largest_length <= draw ||
        (uint64_t)fs->freespace.total_free <=
            (uint64_t)fs->freespace.global_reserve + 1u + draw ||
        bfs_be32(fs->txn.sb_new.emergency_count) == 0) {
        fprintf(stderr,
                "  zero-stock %s draw precondition failed: target=%u largest=%u "
                "draw=%llu free=%u global=%u emergency=%u\n",
                current_root_case ? "current-root" : "older-root", target,
                summary.largest_length, (unsigned long long)draw,
                fs->freespace.total_free, fs->freespace.global_reserve,
                bfs_be32(fs->txn.sb_new.emergency_count));
        goto unlock;
    }

    uint32_t free_before = fs->freespace.total_free;
    uint32_t emergency_before = bfs_be32(fs->txn.sb_new.emergency_count);
    uint32_t pending_before = fs->pending_count;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint64_t writes_before = fixture.device.writes;

    /* Transfer the actual stock blocks to a local caller-owned holding list;
     * every one is queued before this fixture is committed. */
    held_count = fs->freespace.reserve_count;
    memcpy(held, fs->freespace.reserve,
           (size_t)held_count * sizeof(*held));
    for (uint32_t i = 0; i < held_count; i++)
        fs->freespace.reserve[i] = BFS_BLK_NULL;
    fs->freespace.reserve_count = 0;

    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    uint64_t new_root_txn = 0;
    uint32_t expected_pending = pending_before +
        (current_root_case ? 0u : 1u);
    ok = allocated != BFS_BLK_NULL &&
         fs->freespace.metadata_batch_txn == live &&
         fs->freespace.metadata_reuse_txn == live &&
         fs->freespace.metadata_requests == 1 &&
         fs->freespace.tree.root != root_before &&
         fs->freespace.tree.root != bfs_be32(fs->txn.sb.free_tree_root) &&
         bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &new_root_txn) == BFS_OK &&
         new_root_txn == live &&
         fs->freespace.reserve_count == target - 1u &&
         fs->freespace.total_free == free_before - draw &&
         bfs_be32(fs->txn.sb_new.emergency_count) == emergency_before - 1u &&
         fs->pending_count == expected_pending &&
         fixture.device.writes == writes_before + 1u &&
         fixture.device.last_write_block == fs->freespace.tree.root &&
         fs->freespace.roving == allocated + 1u &&
         !free_tree_contains(fs, allocated) &&
         !reserve_contains(&fs->freespace, allocated) &&
         allocator_accounting_valid(fs);
    if (!ok)
        fprintf(stderr,
                "  zero-stock %s mismatch: block=%u draw=%llu free=%u/%u "
                "stock=%u target=%u emergency=%u/%u pending=%u/%u writes=%llu/%llu\n",
                current_root_case ? "current-root" : "older-root", allocated,
                (unsigned long long)draw, fs->freespace.total_free, free_before,
                fs->freespace.reserve_count, target,
                bfs_be32(fs->txn.sb_new.emergency_count), emergency_before,
                fs->pending_count, expected_pending,
                (unsigned long long)fixture.device.writes,
                (unsigned long long)writes_before);
    if (ok) ok = queue_owned_block(fs, allocated);
    for (uint32_t i = 0; ok && i < held_count; i++)
        ok = queue_owned_block(fs, held[i]);

unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) {
        bfs_err_t err = bfs_fs_sync(fs);
        ok = err == BFS_OK && fs->pending_count == 0 &&
             fs->recovery_error == BFS_OK && strict_fsck(fs) &&
             readonly_remount_is_clean(&fixture);
    }
    fixture_destroy(&fixture);
    return ok;
}

static void test_zero_stock_emergency_draw_accounts_for_root_age(void)
{
    TEST_ASSERT(run_zero_stock_emergency_batch_case(false));
    TEST_ASSERT(run_zero_stock_emergency_batch_case(true));
}

static bool alias_is_rejected(uint32_t kind)
{
    enum { CURRENT_ROOT, COMMITTED_ROOT, WORKING_BACKUP,
           COMMITTED_BACKUP, DUPLICATE_RESERVE, ACTIVE_EMERGENCY };
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_lock_write(&fs->lock);
    if (!prime_current_root(&fixture)) goto unlock;
    bfs_blk_t alias;
    switch (kind) {
    case CURRENT_ROOT:
        alias = fs->freespace.tree.root;
        break;
    case COMMITTED_ROOT:
        alias = bfs_be32(fs->txn.sb.free_tree_root);
        if (alias == fs->freespace.tree.root) goto unlock;
        break;
    case WORKING_BACKUP:
        alias = (bfs_blk_t)(bfs_sb_backup_offset(&fs->txn.sb_new) /
                            fs->bio->block_size);
        break;
    case COMMITTED_BACKUP:
        alias = (bfs_blk_t)(bfs_sb_backup_offset(&fs->txn.sb) /
                            fs->bio->block_size);
        break;
    case DUPLICATE_RESERVE:
        alias = fs->freespace.reserve[1];
        break;
    case ACTIVE_EMERGENCY:
        alias = bfs_be32(fs->txn.sb_new.emergency_pool[0]);
        break;
    default:
        goto unlock;
    }
    bfs_blk_t saved = fs->freespace.reserve[0];
    fs->freespace.reserve[0] = alias;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint32_t total_before = fs->freespace.total_free;
    uint32_t count_before = fs->freespace.reserve_count;
    fixture.device.writes = 0;
    bfs_blk_t block = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = block == BFS_BLK_NULL &&
         fs->freespace.iface.error(&fs->freespace.iface) == BFS_ERR_CORRUPT &&
         fixture.device.writes == 0 && fs->freespace.tree.root == root_before &&
         fs->freespace.total_free == total_before &&
         fs->freespace.reserve_count == count_before &&
         fs->freespace.metadata_batch_txn == 0 && fs->recovery_error == BFS_OK;
    fs->freespace.reserve[0] = saved;
unlock:
    bfs_lock_unlock(&fs->lock);
    fixture_destroy(&fixture);
    return ok;
}

static void test_live_stock_aliases_are_errors_before_write(void)
{
    for (uint32_t kind = 0; kind < 6; kind++)
        TEST_ASSERT(alias_is_rejected(kind));
}

static bool post_batch_alias_is_rejected(uint32_t alias_kind, bool stash)
{
    enum { CURRENT_ROOT, WORKING_BACKUP, COMMITTED_BACKUP };
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t owned = BFS_BLK_NULL;
    bfs_lock_write(&fs->lock);
    uint64_t live = fs->live_txn_id;
    owned = fs->freespace.iface.alloc(&fs->freespace.iface);
    if (owned == BFS_BLK_NULL || fs->freespace.metadata_batch_txn != live ||
        fs->freespace.tree.height != 1 || fs->freespace.reserve_count <=
            HEIGHT_ONE_FLOOR)
        goto unlock;

    bfs_blk_t alias;
    switch (alias_kind) {
    case CURRENT_ROOT:
        alias = fs->freespace.tree.root;
        break;
    case WORKING_BACKUP:
        alias = (bfs_blk_t)(bfs_sb_backup_offset(&fs->txn.sb_new) /
                            fs->bio->block_size);
        break;
    case COMMITTED_BACKUP:
        alias = (bfs_blk_t)(bfs_sb_backup_offset(&fs->txn.sb) /
                            fs->bio->block_size);
        break;
    default:
        goto unlock;
    }
    bfs_blk_t saved = fs->freespace.reserve[0];
    uint32_t stock_count = fs->freespace.reserve_count;
    uint32_t total_free = fs->freespace.total_free;
    uint32_t writes = (uint32_t)fixture.device.writes;
    fs->freespace.reserve[0] = alias;

    if (stash) {
        ok = fs->freespace.iface.dealloc(&fs->freespace.iface, owned) ==
                 BFS_ERR_CORRUPT &&
             fs->freespace.last_error == BFS_ERR_CORRUPT;
    } else {
        bfs_blk_t second = fs->freespace.iface.alloc(&fs->freespace.iface);
        ok = second == BFS_BLK_NULL &&
             fs->freespace.iface.error(&fs->freespace.iface) ==
                 BFS_ERR_CORRUPT;
    }
    ok = ok && fixture.device.writes == writes &&
         fs->freespace.reserve_count == stock_count &&
         fs->freespace.total_free == total_free &&
         fs->freespace.metadata_batch_txn == live &&
         fs->recovery_error == BFS_OK;
    fs->freespace.reserve[0] = saved;
    if (ok) ok = queue_owned_block(fs, owned);

unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) ok = bfs_fs_sync(fs) == BFS_OK && fs->pending_count == 0 &&
                 fs->recovery_error == BFS_OK && strict_fsck(fs);
    bool remounted_clean = ok && readonly_remount_is_clean(&fixture);
    fixture_destroy(&fixture);
    return remounted_clean;
}

static void test_success_batch_keeps_root_and_backup_aliases_out_of_reuse(void)
{
    for (uint32_t alias = 0; alias < 3; alias++) {
        TEST_ASSERT(post_batch_alias_is_rejected(alias, false));
        TEST_ASSERT(post_batch_alias_is_rejected(alias, true));
    }
}

static bool post_batch_live_namespace_root_alias_is_rejected(bool stash)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    uint32_t ino = 0;
    bfs_blk_t owned = BFS_BLK_NULL;
    uint64_t live = fs->live_txn_id;

    bfs_lock_write(&fs->lock);
    owned = fs->freespace.iface.alloc(&fs->freespace.iface);
    bool batch_succeeded = owned != BFS_BLK_NULL &&
        fs->freespace.metadata_batch_txn == live &&
        fs->freespace.reserve_count > HEIGHT_ONE_FLOOR;
    bfs_lock_unlock(&fs->lock);
    if (!batch_succeeded ||
        bfs_fs_create_file(fs, BFS_ROOT_INO, "live-root", 9, &ino) != BFS_OK)
        goto done;

    bfs_lock_write(&fs->lock);
    bfs_blk_t dir_current = fs->dir_tree.tree.root;
    bfs_blk_t dir_working = bfs_be32(fs->txn.sb_new.dir_tree_root);
    bfs_blk_t dir_committed = bfs_be32(fs->txn.sb.dir_tree_root);
    bfs_blk_t inode_current = fs->inode_tree.root;
    bfs_blk_t inode_working = bfs_be32(fs->txn.sb_new.inode_tree_root);
    bfs_blk_t inode_committed = bfs_be32(fs->txn.sb.inode_tree_root);
    bfs_blk_t alias = BFS_BLK_NULL;
    if (dir_current != dir_working && dir_current != dir_committed)
        alias = dir_current;
    else if (inode_current != inode_working && inode_current != inode_committed)
        alias = inode_current;
    if (alias == BFS_BLK_NULL || fs->freespace.metadata_batch_txn != live ||
        fs->freespace.reserve_count <= HEIGHT_ONE_FLOOR)
        goto unlock;

    bfs_blk_t saved = fs->freespace.reserve[0];
    uint32_t stock_count = fs->freespace.reserve_count;
    uint32_t total_free = fs->freespace.total_free;
    uint32_t writes = (uint32_t)fixture.device.writes;
    fs->freespace.reserve[0] = alias;
    if (stash) {
        ok = fs->freespace.iface.dealloc(&fs->freespace.iface, owned) ==
                 BFS_ERR_CORRUPT &&
             fs->freespace.last_error == BFS_ERR_CORRUPT;
    } else {
        bfs_blk_t second = fs->freespace.iface.alloc(&fs->freespace.iface);
        ok = second == BFS_BLK_NULL &&
             fs->freespace.iface.error(&fs->freespace.iface) ==
                 BFS_ERR_CORRUPT;
    }
    ok = ok && fixture.device.writes == writes &&
         fs->freespace.reserve_count == stock_count &&
         fs->freespace.total_free == total_free &&
         fs->freespace.metadata_batch_txn == live &&
         fs->recovery_error == BFS_OK;
    fs->freespace.reserve[0] = saved;
    if (ok) ok = queue_owned_block(fs, owned);
unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) {
        bfs_err_t err = bfs_fs_sync(fs);
        ok = err == BFS_OK && fs->pending_count == 0 &&
             fs->recovery_error == BFS_OK && strict_fsck(fs) &&
             readonly_remount_check_file(&fixture, "live-root", 9, ino,
                                         NULL, 0, true);
    }
done:
    fixture_destroy(&fixture);
    return ok;
}

static void test_success_batch_protects_unsynced_directory_root(void)
{
    TEST_ASSERT(post_batch_live_namespace_root_alias_is_rejected(false));
    TEST_ASSERT(post_batch_live_namespace_root_alias_is_rejected(true));
}

static bool inactive_historical_alias_falls_back(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;
    bfs_lock_write(&fs->lock);
    if (!prime_current_root(&fixture)) goto unlock;
    extent_summary_t summary;
    if (!extent_summary(fs, &summary) || summary.largest_length < 2)
        goto unlock;
    bfs_blk_t historical = summary.largest_start;
    if (!free_tree_contains(fs, historical)) goto unlock;
    uint32_t old_slot = fs->txn.sb_new.emergency_pool[BFS_EMERGENCY_POOL_SIZE - 1];
    uint32_t old_count = fs->txn.sb_new.emergency_count;
    fs->txn.sb_new.emergency_pool[BFS_EMERGENCY_POOL_SIZE - 1] =
        bfs_be32(historical);
    fs->txn.sb_new.emergency_count = bfs_be32(0);
    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    fs->txn.sb_new.emergency_pool[BFS_EMERGENCY_POOL_SIZE - 1] = old_slot;
    fs->txn.sb_new.emergency_count = old_count;
    ok = allocated != BFS_BLK_NULL &&
         !free_tree_contains(fs, allocated) &&
         !reserve_contains(&fs->freespace, allocated) &&
         fs->freespace.metadata_batch_txn == 0 &&
         queue_owned_block(fs, allocated);
unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) ok = bfs_fs_sync(fs) == BFS_OK && fs->pending_count == 0 &&
                 fs->recovery_error == BFS_OK && strict_fsck(fs);
    bool remounted_clean = ok && readonly_remount_is_clean(&fixture);
    fixture_destroy(&fixture);
    return remounted_clean;
}

static void test_inactive_historical_emergency_slot_uses_ordinary_fallback(void)
{
    TEST_ASSERT(inactive_historical_alias_falls_back());
}

static bool capacity_declines_batch(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;
    bfs_lock_write(&fs->lock);
    if (!prime_current_root(&fixture)) goto unlock;
    uint32_t extra = BFS_ALLOC_RESERVE_SIZE - fs->freespace.reserve_count - 1;
    bfs_blk_t run = bfs_freespace_alloc(&fs->freespace, extra);
    if (run == BFS_BLK_NULL) goto unlock;
    for (uint32_t i = 0; i < extra; i++)
        fs->freespace.reserve[fs->freespace.reserve_count++] = run + i;
    if (fs->freespace.reserve_count != BFS_ALLOC_RESERVE_SIZE - 1)
        goto unlock;
    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = allocated != BFS_BLK_NULL &&
         fs->freespace.metadata_batch_txn == 0 &&
         fs->freespace.reserve_count <= BFS_ALLOC_RESERVE_SIZE &&
         queue_owned_block(fs, allocated);
unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) ok = bfs_fs_sync(fs) == BFS_OK && fs->pending_count == 0 &&
                 fs->recovery_error == BFS_OK && strict_fsck(fs);
    bool remounted_clean = ok && readonly_remount_is_clean(&fixture);
    fixture_destroy(&fixture);
    return remounted_clean;
}

static void test_one_free_reserve_slot_cannot_start_a_batch(void)
{
    TEST_ASSERT(capacity_declines_batch());
}

static bool drain_to_low_space(fixture_t *fixture, uint32_t above_guard)
{
    bfs_fs_t *fs = &fixture->fs;
    uint64_t target = (uint64_t)fs->freespace.global_reserve + 1 + above_guard;
    if ((uint64_t)fs->freespace.total_free <= target) return false;
    while ((uint64_t)fs->freespace.total_free > target) {
        extent_summary_t summary;
        if (!extent_summary(fs, &summary) || summary.largest_length == 0)
            return false;
        uint64_t needed = fs->freespace.total_free - target;
        uint32_t count = needed < summary.largest_length
            ? (uint32_t)needed : summary.largest_length;
        bfs_blk_t start = bfs_freespace_alloc(&fs->freespace, count);
        if (start == BFS_BLK_NULL) return false;
        for (uint32_t i = 0; i < count; i++)
            if (!queue_owned_block(fs, start + i)) return false;
    }
    return fs->freespace.total_free == target &&
           fs->freespace.tree.height == 1;
}

static bool low_space_declines_before_optional_refill(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, LOW_SPACE_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;
    bfs_lock_write(&fs->lock);
    if (!prime_current_root(&fixture) || !drain_to_low_space(&fixture, 1u)) {
        fprintf(stderr, "  low-space drain failed: free=%u reserve=%u pending=%u\n",
                fs->freespace.total_free, fs->freespace.reserve_count,
                fs->pending_count);
        goto unlock;
    }
    if (fs->freespace.reserve_count != HEIGHT_ONE_FLOOR ||
        fs->freespace.total_free - fs->freespace.global_reserve - 1 != 1u) {
        fprintf(stderr, "  low-space target mismatch: free=%u global=%u reserve=%u\n",
                fs->freespace.total_free, fs->freespace.global_reserve,
                fs->freespace.reserve_count);
        goto unlock;
    }
    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = allocated != BFS_BLK_NULL &&
         fs->freespace.metadata_batch_txn == 0 &&
         fs->freespace.reserve_count >= HEIGHT_ONE_FLOOR &&
         queue_owned_block(fs, allocated);
    if (!ok)
        fprintf(stderr, "  low-space alloc mismatch: block=%u err=%d free=%u stock=%u batch=%llu\n",
                allocated, fs->freespace.iface.error(&fs->freespace.iface),
                fs->freespace.total_free, fs->freespace.reserve_count,
                (unsigned long long)fs->freespace.metadata_batch_txn);
unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) {
        bfs_err_t err = bfs_fs_sync(fs);
        ok = err == BFS_OK && fs->pending_count == 0 &&
             fs->recovery_error == BFS_OK && strict_fsck(fs);
        if (!ok) fprintf(stderr, "  low-space sync failed: err=%d pending=%u recovery=%d\n",
                         err, fs->pending_count, fs->recovery_error);
    }
    bool remounted_clean = ok && readonly_remount_is_clean(&fixture);
    fixture_destroy(&fixture);
    return remounted_clean;
}

static void test_low_space_uses_ordinary_allocation_without_batch_permission(void)
{
    TEST_ASSERT(low_space_declines_before_optional_refill());
}

static bool tight_pending_headroom_keeps_ordinary_single_alloc_available(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;
    uint32_t saved_cap = fs->pending_frees_cap;

    bfs_lock_write(&fs->lock);
    if (!prime_current_root(&fixture) ||
        fs->freespace.metadata_requests != 0 ||
        fs->freespace.metadata_batch_txn != 0 ||
        fs->freespace.reserve_count != HEIGHT_ONE_FLOOR ||
        fs->pending_count >= BFS_PENDING_FREES_MAX ||
        fs->freespace.total_free == 0)
        goto unlock;

    uint64_t live = fs->live_txn_id;
    uint64_t root_txn = 0;
    if (bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK ||
        root_txn != live)
        goto unlock;

    uint32_t pending_before = fs->pending_count;
    uint32_t one_slot_cap = pending_before + 1u;
    uint32_t free_before = fs->freespace.total_free;
    if (fs->freespace.tree.root ==
            bfs_be32(fs->txn.sb.free_tree_root) ||
        (uint64_t)free_before <=
            (uint64_t)fs->freespace.global_reserve + 1u + 8u)
        goto unlock;
    fs->pending_frees_cap = one_slot_cap;
    if (bfs_fs_pending_cap(fs) - fs->pending_count != 1u ||
        bfs_fs_pending_cap(fs) - fs->pending_count >=
            BFS_BTREE_DELETE_FREE_BUDGET(fs->freespace.tree.height))
        goto restore_cap;

    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = allocated != BFS_BLK_NULL &&
         fs->freespace.metadata_batch_txn == 0 &&
         fs->freespace.metadata_requests == 1 &&
         fs->freespace.metadata_reuse_txn == live &&
         fs->freespace.total_free == free_before - 1u &&
         fs->freespace.reserve_count == HEIGHT_ONE_FLOOR &&
         fs->freespace.roving == allocated + 1u &&
         fs->pending_count >= pending_before &&
         fs->pending_count <= one_slot_cap &&
         fs->recovery_error == BFS_OK;
    if (!ok)
        fprintf(stderr,
                "  tight-headroom alloc mismatch: block=%u free=%u/%u "
                "stock=%u pending=%u/%u batch=%llu requests=%u roving=%u\n",
                allocated, fs->freespace.total_free, free_before,
                fs->freespace.reserve_count, fs->pending_count, pending_before,
                (unsigned long long)fs->freespace.metadata_batch_txn,
                fs->freespace.metadata_requests, fs->freespace.roving);

restore_cap:
    fs->pending_frees_cap = saved_cap;
    if (ok) ok = queue_owned_block(fs, allocated);
unlock:
    fs->pending_frees_cap = saved_cap;
    bfs_lock_unlock(&fs->lock);
    if (ok) {
        bfs_err_t err = bfs_fs_sync(fs);
        ok = err == BFS_OK && fs->pending_count == 0 &&
             fs->recovery_error == BFS_OK && strict_fsck(fs) &&
             readonly_remount_is_clean(&fixture);
    }
    fixture_destroy(&fixture);
    return ok;
}

static void test_one_pending_slot_declines_batch_but_allows_single_alloc(void)
{
    TEST_ASSERT(tight_pending_headroom_keeps_ordinary_single_alloc_available());
}

static void test_tiny_pending_capacity_rejects_before_metadata_mutation(void)
{
    fixture_t fixture;
    TEST_ASSERT(fixture_init(&fixture, 1024u, SMALL_VOLUME_BLOCKS, 0, false));
    bfs_fs_t *fs = &fixture.fs;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint64_t writes = fixture.device.writes;
    uint32_t ino = 0;
    fs->pending_frees_cap = 1;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "tiny", 4, &ino),
                   BFS_ERR_NOSPC);
    TEST_ASSERT_EQ(ino, 0);
    TEST_ASSERT_EQ(fs->freespace.metadata_batch_txn, 0);
    TEST_ASSERT_EQ(fs->pending_count, 0);
    TEST_ASSERT_EQ(fs->freespace.tree.root, root_before);
    TEST_ASSERT_EQ(fixture.device.writes, writes);
    fs->pending_frees_cap = BFS_PENDING_FREES_MAX;
    fixture_destroy(&fixture);
}

static bool build_full_root_leaf(fixture_t *fixture)
{
    bfs_fs_t *fs = &fixture->fs;
    extent_summary_t summary;
    if (fs->freespace.tree.height != 1 || !extent_summary(fs, &summary)) {
        fprintf(stderr, "  full-leaf setup root=%u height=%u\n",
                fs->freespace.tree.root, fs->freespace.tree.height);
        return false;
    }
    uint32_t capacity = bfs_btree_leaf_capacity(&fs->freespace.tree);
    if (summary.count >= capacity) {
        fprintf(stderr, "  full-leaf initial extents=%u capacity=%u\n",
                summary.count, capacity);
        return false;
    }
    uint32_t initial_count = summary.count;
    uint32_t new_extents = capacity - summary.count;
    uint32_t allocated_count = 2u * new_extents;
    if ((uint64_t)fs->freespace.total_free <
        (uint64_t)allocated_count + fs->freespace.global_reserve + 1u)
        return false;
    bfs_blk_t *allocated = malloc((size_t)allocated_count * sizeof(*allocated));
    bfs_blk_t *crumbs = malloc((size_t)new_extents * sizeof(*crumbs));
    if (!allocated || !crumbs) {
        free(crumbs);
        free(allocated);
        return false;
    }

    /* Repeated single-block withdrawals use the highest tail. Returning every
     * other held block creates high-key one-block crumbs above a lower fitting
     * run, which witnesses reverse selection rather than a highest-key draw. */
    for (uint32_t i = 0; i < allocated_count; i++) {
        allocated[i] = bfs_freespace_alloc(&fs->freespace, 1);
        if (allocated[i] == BFS_BLK_NULL) {
            free(crumbs);
            free(allocated);
            fprintf(stderr, "  full-leaf tail allocation failed: i=%u err=%d\n",
                    i, fs->freespace.last_error);
            return false;
        }
    }
    for (uint32_t i = 0; i < new_extents; i++)
        crumbs[i] = allocated[2u * (new_extents - 1u - i)];
    bfs_err_t err = bfs_freespace_free_sorted_blocks(&fs->freespace, crumbs,
                                                     new_extents);
    free(crumbs);
    if (err != BFS_OK) {
        fprintf(stderr, "  full-leaf sorted free failed: err=%d\n", err);
        free(allocated);
        return false;
    }
    if (!extent_summary(fs, &summary) || summary.count > capacity ||
        fs->freespace.tree.height != 1) {
        fprintf(stderr, "  full-leaf result extents=%u expected=%u initial=%u "
                "new=%u allocated=%u height=%u\n",
                summary.count, capacity, initial_count, new_extents,
                allocated_count, fs->freespace.tree.height);
        free(allocated);
        return false;
    }

    while (summary.count < capacity) {
        uint32_t count_before = summary.count;
        bfs_blk_t low = bfs_freespace_alloc(&fs->freespace, 2u);
        if (low == BFS_BLK_NULL ||
            !queue_owned_block(fs, low + 1u) ||
            bfs_freespace_free(&fs->freespace, low, 1) != BFS_OK ||
            !extent_summary(fs, &summary) ||
            summary.count != count_before + 1u ||
            fs->freespace.tree.height != 1) {
            fprintf(stderr,
                    "  full-leaf supplemental crumb failed: low=%u "
                    "count=%u before=%u err=%d\n",
                    low, summary.count, count_before,
                    fs->freespace.last_error);
            free(allocated);
            return false;
        }
    }

    /* Every block not returned to the Free-Tree remains caller-owned until
     * commit; record it as pending before any later sync. */
    for (uint32_t i = 0; i < new_extents; i++) {
        if (!queue_owned_block(fs, allocated[2u * i + 1u])) {
            free(allocated);
            return false;
        }
    }
    free(allocated);
    return true;
}

static bool force_deeper_fragmented_root(bfs_fs_t *fs)
{
    const uint32_t count = 17;
    bfs_blk_t first = bfs_freespace_alloc(&fs->freespace, count);
    if (first == BFS_BLK_NULL) {
        fprintf(stderr, "  deeper-root allocation failed: err=%d free=%u stock=%u\n",
                fs->freespace.iface.error(&fs->freespace.iface),
                fs->freespace.total_free, fs->freespace.reserve_count);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if ((i & 1u) != 0) {
            if (bfs_freespace_free(&fs->freespace, first + i, 1) != BFS_OK) {
                fprintf(stderr, "  deeper-root free failed: i=%u\n", i);
                return false;
            }
        } else if (!queue_owned_block(fs, first + i)) {
            fprintf(stderr, "  deeper-root defer failed: i=%u\n", i);
            return false;
        }
    }
    if (fs->freespace.tree.height <= 1)
        fprintf(stderr, "  fragmented frees retained root height=%u\n",
                fs->freespace.tree.height);
    return fs->freespace.tree.height > 1;
}

static bool deeper_root_declines_optional_batch(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;
    bfs_lock_write(&fs->lock);
    if (!build_full_root_leaf(&fixture) ||
        !force_deeper_fragmented_root(fs) || fs->freespace.tree.height <= 1)
        goto unlock;
    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    ok = allocated != BFS_BLK_NULL &&
         fs->freespace.metadata_batch_txn == 0 &&
         fs->freespace.reserve_count >= HEIGHT_ONE_FLOOR &&
         !free_tree_contains(fs, allocated) &&
         !reserve_contains(&fs->freespace, allocated) &&
         queue_owned_block(fs, allocated);
unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) ok = bfs_fs_sync(fs) == BFS_OK && fs->pending_count == 0 &&
                 fs->recovery_error == BFS_OK && strict_fsck(fs);
    bool remounted_clean = ok && readonly_remount_is_clean(&fixture);
    fixture_destroy(&fixture);
    return remounted_clean;
}

static void test_deeper_free_tree_does_not_start_optional_batch(void)
{
    TEST_ASSERT(deeper_root_declines_optional_batch());
}

static bool fragmented_full_leaf_uses_real_commit_fallback(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, FRAGMENTED_BLOCKS, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t raw_metadata_block = BFS_BLK_NULL;

    bfs_lock_write(&fs->lock);
    if (!build_full_root_leaf(&fixture)) goto unlock;
    uint64_t live = fs->live_txn_id;
    extent_summary_t before;
    bfs_blk_t singleton_starts[256];
    uint32_t singleton_count = 0;
    uint64_t root_txn = 0;
    if (!extent_summary(fs, &before) ||
        before.count != bfs_btree_leaf_capacity(&fs->freespace.tree) ||
        !capture_singleton_extents(fs, singleton_starts,
                                   sizeof(singleton_starts) /
                                       sizeof(singleton_starts[0]),
                                   &singleton_count) ||
        singleton_count == 0 || singleton_count >= before.count ||
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK ||
        root_txn != live || fs->freespace.reserve_count != HEIGHT_ONE_FLOOR)
        goto unlock;

    uint32_t target = HEIGHT_ONE_FLOOR + METADATA_BATCH_EXTRA;
    uint32_t reserve_before = fs->freespace.reserve_count;
    uint64_t draw = (uint64_t)target - reserve_before;
    if (root_txn < live && reserve_before > 0) draw++;
    if (root_txn == live && reserve_before == 0) draw--;
    fitting_extent_t selected;
    if (target > BFS_ALLOC_RESERVE_SIZE || draw == 0 || draw > UINT32_MAX ||
        !highest_fitting_extent(fs, (uint32_t)draw, &selected) ||
        selected.length <= draw)
        goto unlock;
    uint64_t selected_end = (uint64_t)selected.start + selected.length;
    uint32_t high_singleton_count = 0;
    for (uint32_t i = 0; i < singleton_count; i++) {
        if (singleton_starts[i] >= selected_end) high_singleton_count++;
        if (!free_tree_contains(fs, singleton_starts[i]))
            goto unlock;
    }
    if (high_singleton_count == 0) goto unlock;
    uint32_t free_before = fs->freespace.total_free;
    uint32_t emergency_before = bfs_be32(fs->txn.sb_new.emergency_count);
    uint32_t pending_before = fs->pending_count;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint64_t writes_before = fixture.device.writes;
    raw_metadata_block = fs->freespace.iface.alloc(&fs->freespace.iface);
    extent_summary_t after;
    fitting_extent_t selected_after;
    uint64_t new_root_txn = 0;
    if (raw_metadata_block == BFS_BLK_NULL ||
        fs->freespace.metadata_batch_txn != live ||
        fs->freespace.metadata_reuse_txn != live ||
        fs->freespace.metadata_requests != 1 ||
        fs->freespace.tree.height != 1 ||
        fs->freespace.tree.root == root_before ||
        fs->freespace.tree.root == bfs_be32(fs->txn.sb.free_tree_root) ||
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &new_root_txn) != BFS_OK ||
        new_root_txn != live ||
        fs->freespace.reserve_count != target - 1u ||
        fs->freespace.total_free != free_before - draw ||
        bfs_be32(fs->txn.sb_new.emergency_count) != emergency_before -
            (reserve_before == 0 ? 1u : 0u) ||
        fs->pending_count != pending_before + (root_txn < live ? 1u : 0u) ||
        fixture.device.writes != writes_before + 1u ||
        fixture.device.last_write_block != fs->freespace.tree.root ||
        !extent_summary(fs, &after) || after.count != before.count ||
        !highest_fitting_extent(fs, (uint32_t)draw, &selected_after) ||
        selected_after.start != selected.start ||
        selected_after.length != selected.length - draw ||
        !free_tree_contains(fs, selected.start) ||
        free_tree_contains(fs, (bfs_blk_t)(selected_end - draw)) ||
        !allocator_accounting_valid(fs))
        goto unlock;
    for (uint32_t i = 0; i < singleton_count; i++)
        if (!free_tree_contains(fs, singleton_starts[i])) goto unlock;
    if (!queue_owned_block(fs, raw_metadata_block) ||
        !force_deeper_fragmented_root(fs))
        goto unlock;

    fixture.device.commit_pre_superblock_writes = 0;
    fixture.device.commit_superblock_writes = 0;
    fixture.device.commit_frozen_writes = 0;
    fixture.device.commit_frozen_superblock_writes = 0;
    fixture.device.saw_commit_superblock = false;
    fixture.device.watch_commit = true;
    bfs_lock_unlock(&fs->lock);

    bfs_err_t err = bfs_fs_sync(fs);
    fixture.device.watch_commit = false;
    if (err != BFS_OK || fs->pending_count != 0 ||
        fs->recovery_error != BFS_OK || fs->freespace.allocation_frozen ||
        fixture.device.commit_pre_superblock_writes == 0 ||
        fixture.device.commit_superblock_writes == 0 ||
        fixture.device.commit_frozen_writes != 0 ||
        fixture.device.commit_frozen_superblock_writes != 0 ||
        !strict_fsck(fs)) {
        fprintf(stderr,
                "  fragmented fallback failed: err=%d pending=%u pre-sb=%u "
                "sb=%u frozen=%u frozen-sb=%u\n",
                err, fs->pending_count,
                fixture.device.commit_pre_superblock_writes,
                fixture.device.commit_superblock_writes,
                fixture.device.commit_frozen_writes,
                fixture.device.commit_frozen_superblock_writes);
        goto done;
    }
    ok = readonly_remount_is_clean(&fixture);
done:
    fixture_destroy(&fixture);
    return ok;

unlock:
    bfs_lock_unlock(&fs->lock);
    goto done;
}

static void test_full_leaf_fragmentation_and_commit_fallback_converge(void)
{
    TEST_ASSERT(fragmented_full_leaf_uses_real_commit_fallback());
}

typedef struct {
    bfs_blk_t start;
    uint32_t length;
} held_range_t;

static bool build_no_fit_leaf(fixture_t *fixture, held_range_t *held_ranges,
                              uint32_t held_capacity,
                              uint32_t *held_count_out,
                              uint32_t *small_extent_count_out)
{
    bfs_fs_t *fs = &fixture->fs;
    extent_summary_t initial;
    if (fs->freespace.tree.height != 1 ||
        !extent_summary(fs, &initial)) {
        fprintf(stderr, "  no-fit setup lacks height-one Free-Tree\n");
        return false;
    }
    uint32_t capacity = bfs_btree_leaf_capacity(&fs->freespace.tree);
    if (initial.count >= capacity) {
        fprintf(stderr, "  no-fit initial leaf already full: %u/%u\n",
                initial.count, capacity);
        return false;
    }
    uint32_t small_extent_count = capacity - initial.count;
    if ((uint64_t)small_extent_count * 7u <=
        (uint64_t)fs->freespace.global_reserve + 1u +
            HEIGHT_ONE_FLOOR + METADATA_BATCH_EXTRA) {
        fprintf(stderr, "  no-fit short extents lack global headroom: n=%u total=%u global=%u\n",
                small_extent_count, small_extent_count * 7u,
                fs->freespace.global_reserve);
        return false;
    }

    uint32_t allocation_count = small_extent_count * 8u;
    uint32_t free_count = small_extent_count * 7u;
    if ((uint64_t)fs->freespace.total_free <
        (uint64_t)allocation_count + fs->freespace.global_reserve + 1u) {
        fprintf(stderr, "  no-fit fragmentation allocation lacks headroom: free=%u need=%u\n",
                fs->freespace.total_free, allocation_count);
        return false;
    }
    bfs_blk_t *free_blocks = malloc((size_t)free_count * sizeof(*free_blocks));
    if (!free_blocks) return false;
    bfs_blk_t first = bfs_freespace_alloc(&fs->freespace, allocation_count);
    if (first == BFS_BLK_NULL) {
        fprintf(stderr, "  no-fit bulk allocation failed: count=%u err=%d\n",
                allocation_count, fs->freespace.last_error);
        free(free_blocks);
        return false;
    }

    for (uint32_t i = 0; i < small_extent_count; i++)
        for (uint32_t j = 0; j < 7u; j++)
            free_blocks[i * 7u + j] = first + i * 8u + j;
    bfs_err_t err = bfs_freespace_free_sorted_blocks(&fs->freespace,
                                                     free_blocks, free_count);
    free(free_blocks);
    if (err != BFS_OK) {
        fprintf(stderr, "  no-fit sorted free failed: err=%d\n", err);
        return false;
    }

    /* The final block of each eight-block group remains caller-owned and
     * separates neighboring seven-block free extents. */
    for (uint32_t i = 0; i < small_extent_count; i++)
        if (!queue_owned_block(fs, first + i * 8u + 7u)) return false;

    extent_summary_t fragmented;
    if (!extent_summary(fs, &fragmented) ||
        fragmented.count > capacity || fs->freespace.tree.height != 1) {
        fprintf(stderr, "  no-fit fragmented leaf mismatch: %u/%u height=%u\n",
                fragmented.count, capacity, fs->freespace.tree.height);
        return false;
    }

    while (fragmented.count < capacity) {
        uint32_t count_before = fragmented.count;
        bfs_blk_t run = bfs_freespace_alloc(&fs->freespace, 8u);
        if (run == BFS_BLK_NULL) {
            fprintf(stderr, "  no-fit supplemental extent allocation failed: err=%d\n",
                    fs->freespace.last_error);
            return false;
        }
        bfs_blk_t small_blocks[7];
        for (uint32_t i = 0; i < 7u; i++) small_blocks[i] = run + i;
        err = bfs_freespace_free_sorted_blocks(&fs->freespace, small_blocks, 7u);
        if (err != BFS_OK || !queue_owned_block(fs, run + 7u) ||
            !extent_summary(fs, &fragmented) ||
            fragmented.count != count_before + 1u ||
            fs->freespace.tree.height != 1) {
            fprintf(stderr,
                    "  no-fit supplemental seven-block extent failed: run=%u "
                    "count=%u before=%u err=%d\n",
                    run, fragmented.count, count_before, err);
            return false;
        }
        small_extent_count++;
    }

    uint32_t held_count = 0;
    while (fragmented.largest_length > 7u) {
        if (held_count == held_capacity) {
            fprintf(stderr, "  no-fit held-range capacity exhausted\n");
            return false;
        }
        uint32_t length = fragmented.largest_length;
        bfs_blk_t start = bfs_freespace_alloc(&fs->freespace, length);
        if (start == BFS_BLK_NULL) {
            fprintf(stderr, "  no-fit large-extent withdrawal failed: len=%u err=%d\n",
                    length, fs->freespace.last_error);
            return false;
        }
        held_ranges[held_count++] = (held_range_t){ start, length };
        if (!extent_summary(fs, &fragmented) ||
            fragmented.count + held_count != capacity) {
            fprintf(stderr, "  no-fit shrink mismatch: extents=%u held=%u capacity=%u\n",
                    fragmented.count, held_count, capacity);
            return false;
        }
    }
    if (fragmented.count != small_extent_count ||
        fragmented.largest_length != 7u ||
        fragmented.total != (uint64_t)small_extent_count * 7u ||
        fs->freespace.total_free != fragmented.total) {
        fprintf(stderr, "  no-fit terminal shape mismatch: extents=%u expected=%u "
                "largest=%u total=%llu free=%u held=%u\n",
                fragmented.count, small_extent_count,
                fragmented.largest_length,
                (unsigned long long)fragmented.total,
                fs->freespace.total_free, held_count);
        return false;
    }

    *held_count_out = held_count;
    *small_extent_count_out = small_extent_count;
    return true;
}

static bool no_fitting_extent_declines_before_mutation(void)
{
    fixture_t fixture;
    if (!fixture_init(&fixture, 1024u, NO_FIT_BLOCKS_1024, 0, false))
        return false;
    bfs_fs_t *fs = &fixture.fs;
    bool ok = false;
    bfs_blk_t allocated = BFS_BLK_NULL;
    held_range_t held_ranges[8];
    uint32_t held_count = 0;
    uint32_t small_extent_count = 0;

    bfs_lock_write(&fs->lock);
    if (!build_no_fit_leaf(&fixture, held_ranges,
                           sizeof(held_ranges) / sizeof(held_ranges[0]),
                           &held_count, &small_extent_count))
        goto unlock;

    uint64_t live = fs->live_txn_id;
    uint64_t root_txn = 0;
    extent_summary_t before;
    uint32_t stock_before = fs->freespace.reserve_count;
    uint32_t target = HEIGHT_ONE_FLOOR + METADATA_BATCH_EXTRA;
    uint32_t expected_draw = target - stock_before;
    if (fs->freespace.metadata_requests != 0 ||
        fs->freespace.metadata_batch_txn != 0 ||
        stock_before != HEIGHT_ONE_FLOOR ||
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) != BFS_OK ||
        root_txn != live ||
        fs->freespace.tree.root == bfs_be32(fs->txn.sb.free_tree_root) ||
        !extent_summary(fs, &before) || before.count != small_extent_count ||
        before.largest_length >= expected_draw ||
        (uint64_t)fs->freespace.total_free <=
            (uint64_t)fs->freespace.global_reserve + 1u + expected_draw)
        goto unlock;

    uint32_t free_before = fs->freespace.total_free;
    uint32_t emergency_before = bfs_be32(fs->txn.sb_new.emergency_count);
    uint32_t pending_before = fs->pending_count;
    bfs_blk_t root_before = fs->freespace.tree.root;
    uint64_t writes_before = fixture.device.writes;
    allocated = fs->freespace.iface.alloc(&fs->freespace.iface);
    extent_summary_t after;
    uint64_t new_root_txn = 0;
    ok = allocated != BFS_BLK_NULL &&
         fs->freespace.metadata_batch_txn == 0 &&
         fs->freespace.metadata_reuse_txn == live &&
         fs->freespace.metadata_requests == 1 &&
         fs->freespace.tree.root != root_before &&
         bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &new_root_txn) == BFS_OK &&
         new_root_txn == live &&
         fs->freespace.reserve_count == stock_before &&
         fs->freespace.total_free == free_before - 1u &&
         bfs_be32(fs->txn.sb_new.emergency_count) == emergency_before &&
         fs->pending_count == pending_before &&
         fixture.device.writes == writes_before + 1u &&
         fixture.device.last_write_block == fs->freespace.tree.root &&
         fs->freespace.roving == allocated + 1u &&
         !free_tree_contains(fs, allocated) &&
         !reserve_contains(&fs->freespace, allocated) &&
         extent_summary(fs, &after) && after.count == before.count &&
         allocator_accounting_valid(fs);
    if (!ok)
        fprintf(stderr,
                "  no-fit ordinary fallback mismatch: block=%u free=%u/%u "
                "stock=%u/%u pending=%u/%u batch=%llu writes=%llu/%llu\n",
                allocated, fs->freespace.total_free, free_before,
                fs->freespace.reserve_count, stock_before,
                fs->pending_count, pending_before,
                (unsigned long long)fs->freespace.metadata_batch_txn,
                (unsigned long long)fixture.device.writes,
                (unsigned long long)writes_before);
    if (ok) ok = queue_owned_block(fs, allocated);
    for (uint32_t i = 0; ok && i < held_count; i++)
        ok = queue_owned_range(fs, held_ranges[i].start,
                               held_ranges[i].length);

unlock:
    bfs_lock_unlock(&fs->lock);
    if (ok) {
        bfs_err_t err = bfs_fs_sync(fs);
        ok = err == BFS_OK && fs->pending_count == 0 &&
             fs->recovery_error == BFS_OK && strict_fsck(fs) &&
             readonly_remount_is_clean(&fixture);
    }
    fixture_destroy(&fixture);
    return ok;
}

static void test_no_fitting_extent_uses_ordinary_single_allocation(void)
{
    TEST_ASSERT(no_fitting_extent_declines_before_mutation());
}

TEST_SUITE_BEGIN("Metadata withdrawal batch")
    TEST_RUN(test_first_request_batch_all_supported_block_sizes);
    TEST_RUN(test_owner_and_snapshot_gates_fall_back_to_normal_allocator);
    TEST_RUN(test_readonly_frozen_and_recovery_gates_do_not_allocate);
    TEST_RUN(test_future_leaf_tag_is_rejected_before_optional_write);
    TEST_RUN(test_crc_valid_equal_live_tag_on_committed_root_is_corrupt);
    TEST_RUN(test_live_stock_aliases_are_errors_before_write);
    TEST_RUN(test_success_batch_keeps_root_and_backup_aliases_out_of_reuse);
    TEST_RUN(test_success_batch_protects_unsynced_directory_root);
    TEST_RUN(test_zero_stock_emergency_draw_accounts_for_root_age);
    TEST_RUN(test_inactive_historical_emergency_slot_uses_ordinary_fallback);
    TEST_RUN(test_one_free_reserve_slot_cannot_start_a_batch);
    TEST_RUN(test_low_space_uses_ordinary_allocation_without_batch_permission);
    TEST_RUN(test_one_pending_slot_declines_batch_but_allows_single_alloc);
    TEST_RUN(test_tiny_pending_capacity_rejects_before_metadata_mutation);
    TEST_RUN(test_repeated_create_overwrite_delete_sync);
    TEST_RUN(test_deeper_free_tree_does_not_start_optional_batch);
    TEST_RUN(test_full_leaf_fragmentation_and_commit_fallback_converge);
    TEST_RUN(test_no_fitting_extent_uses_ordinary_single_allocation);
TEST_SUITE_END()
