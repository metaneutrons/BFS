/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Mounted SF commit persistence and fault-cut tests.
 * The BIO keeps volatile and durable images separately so power cuts can
 * discard writes not covered by a successful sync.
 */

#include "test_harness.h"
#include "bfs_file.h"
#include "bfs_fsck.h"
#include "bfs_superblock.h"

#include <stdlib.h>

#define SF_BLOCK_SIZE 4096u
#define SF_BLOCK_COUNT 4096u
#define SF_BACKUP_BLOCK (SF_BLOCK_COUNT / 2u)
#define SF_DATA_SIZE (SF_BLOCK_SIZE + 317u)
#define SF_TRACE_CAP 256u

typedef enum {
    SF_SYNC_ERROR_DROP = 0,
    SF_SYNC_ERROR_PERSIST_ALL = 1,
    SF_SYNC_ERROR_PERSIST_SB_ONLY = 2,
    SF_SYNC_ERROR_PERSIST_FIRST_NON_SB = 3
} sf_sync_error_mode_t;

typedef struct {
    char operation;
    bfs_blk_t block;
} sf_io_event_t;

typedef struct {
    bfs_bio_t bio;
    uint8_t *volatile_bytes;
    uint8_t *durable_bytes;
    bool *dirty_blocks;
    size_t storage_bytes;

    uint64_t reads;
    uint64_t writes;
    uint64_t syncs;
    uint64_t fail_read_at;
    uint64_t fail_write_at;
    uint64_t fail_sync_at;
    uint64_t injected_read_index;
    uint64_t injected_write_index;
    uint64_t injected_sync_index;
    size_t failed_write_prefix_bytes;
    sf_sync_error_mode_t sync_error_mode;
    bfs_err_t failure_error;
    uint32_t failed_sync_persisted_non_sb_blocks;
    uint32_t failed_sync_persisted_sb_blocks;

    bfs_fs_t *observed_fs;
    uint32_t write_blocks[SF_TRACE_CAP];
    sf_io_event_t events[SF_TRACE_CAP];
    uint32_t event_count;
    bool trace_overflow;
    bool failed_while_frozen;
    uint32_t frozen_writes;
    uint32_t non_sb_writes_while_frozen;
    uint32_t frozen_sb_writes;
    uint32_t sealed_metadata_syncs;
    uint32_t frozen_sb_syncs;
    bool frozen_sb_write_seen;
    bool pending_at_sealed_metadata_sync;
    bool pending_at_frozen_sb_write;
    bool pending_at_frozen_sb_sync;

    bool watch_old_root;
    bfs_blk_t old_root;
    uint8_t old_root_bytes[SF_BLOCK_SIZE];
    bool old_root_changed;
} sf_bio_t;

typedef struct {
    uint64_t reads;
    uint64_t writes;
    uint64_t syncs;
    uint32_t sealed_metadata_syncs;
    uint32_t frozen_sb_syncs;
    uint32_t write_blocks[SF_TRACE_CAP];
    sf_io_event_t events[SF_TRACE_CAP];
    uint32_t event_count;
    bool trace_overflow;
} sf_operation_trace_t;

typedef struct {
    bfs_blk_t root;
    uint32_t total_free;
    uint32_t roving;
    uint32_t reserve_count;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    uint32_t emergency_count;
    uint32_t emergency_pool[BFS_EMERGENCY_POOL_SIZE];
    uint32_t pending_count;
    bfs_blk_t *pending;
    uint64_t sb_free_root;
    uint64_t sb_free_blocks;
    uint8_t root_bytes[SF_BLOCK_SIZE];
} sf_live_snapshot_t;

typedef struct {
    sf_bio_t device;
    bfs_fs_t fs;
    bool mounted;
    uint32_t ino;
    uint64_t old_txn_id;
    uint64_t commit_txn_id;
    bfs_blk_t old_committed_free_root;
    uint8_t expected[SF_DATA_SIZE];
} sf_fixture_t;

typedef struct {
    const bfs_superblock_t *sb;
    bfs_blk_t root;
    bool disjoint;
} sf_pool_scan_t;

static sf_operation_trace_t successful_primary_trace;
static sf_operation_trace_t successful_backup_trace;
static bool primary_trace_ready;
static bool backup_trace_ready;

static bool sf_is_superblock_block(bfs_blk_t blk)
{
    return blk == 0 || blk == SF_BACKUP_BLOCK;
}

static uint8_t *sf_block_ptr(sf_bio_t *device, uint8_t *storage,
                             bfs_blk_t blk)
{
    return storage + (size_t)blk * device->bio.block_size;
}

static void sf_record_io_event(sf_bio_t *device, char operation,
                               bfs_blk_t blk)
{
    if (device->event_count < SF_TRACE_CAP) {
        device->events[device->event_count].operation = operation;
        device->events[device->event_count].block = blk;
        ++device->event_count;
    } else {
        device->trace_overflow = true;
    }
}

static void sf_commit_dirty(sf_bio_t *device)
{
    for (bfs_blk_t blk = 0; blk < device->bio.block_count; ++blk) {
        if (device->dirty_blocks[blk]) {
            memcpy(sf_block_ptr(device, device->durable_bytes, blk),
                   sf_block_ptr(device, device->volatile_bytes, blk),
                   device->bio.block_size);
            device->dirty_blocks[blk] = false;
        }
    }
}

static void sf_commit_dirty_block(sf_bio_t *device, bfs_blk_t blk)
{
    if (!device->dirty_blocks[blk]) return;
    memcpy(sf_block_ptr(device, device->durable_bytes, blk),
           sf_block_ptr(device, device->volatile_bytes, blk),
           device->bio.block_size);
    device->dirty_blocks[blk] = false;
}

static void sf_commit_dirty_superblocks(sf_bio_t *device)
{
    for (bfs_blk_t blk = 0; blk < device->bio.block_count; ++blk) {
        if (device->dirty_blocks[blk] && sf_is_superblock_block(blk)) {
            sf_commit_dirty_block(device, blk);
            ++device->failed_sync_persisted_sb_blocks;
        }
    }
}

static void sf_commit_first_dirty_non_superblock(sf_bio_t *device)
{
    for (bfs_blk_t blk = 0; blk < device->bio.block_count; ++blk) {
        if (!sf_is_superblock_block(blk) && device->dirty_blocks[blk]) {
            sf_commit_dirty_block(device, blk);
            ++device->failed_sync_persisted_non_sb_blocks;
            return;
        }
    }
}

static void sf_mark_frozen_failure(sf_bio_t *device)
{
    if (device->observed_fs &&
        device->observed_fs->freespace.allocation_frozen)
        device->failed_while_frozen = true;
}

static bfs_err_t sf_read_block(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    sf_bio_t *device = (sf_bio_t *)bio;
    if (!buf || blk >= bio->block_count) return BFS_ERR_INVAL;
    ++device->reads;
    sf_record_io_event(device, 'R', blk);
    if (device->fail_read_at && device->injected_read_index == 0 &&
        device->reads == device->fail_read_at) {
        device->injected_read_index = device->reads;
        sf_mark_frozen_failure(device);
        return device->failure_error;
    }
    memcpy(buf, sf_block_ptr(device, device->volatile_bytes, blk),
           bio->block_size);
    return BFS_OK;
}

static bfs_err_t sf_write_block(bfs_bio_t *bio, bfs_blk_t blk,
                                const void *buf)
{
    sf_bio_t *device = (sf_bio_t *)bio;
    if (!buf || blk >= bio->block_count) return BFS_ERR_INVAL;
    ++device->writes;
    sf_record_io_event(device, 'W', blk);
    if (device->writes <= SF_TRACE_CAP)
        device->write_blocks[device->writes - 1] = blk;
    else
        device->trace_overflow = true;

    if (device->watch_old_root && blk == device->old_root)
        device->old_root_changed = true;

    if (device->observed_fs &&
        device->observed_fs->freespace.allocation_frozen) {
        ++device->frozen_writes;
        if (sf_is_superblock_block(blk)) {
            ++device->frozen_sb_writes;
            device->frozen_sb_write_seen = true;
            if (device->observed_fs->pending_count != 0)
                device->pending_at_frozen_sb_write = true;
        } else {
            ++device->non_sb_writes_while_frozen;
        }
    }

    if (device->fail_write_at && device->injected_write_index == 0 &&
        device->writes == device->fail_write_at) {
        size_t prefix = device->failed_write_prefix_bytes;
        device->injected_write_index = device->writes;
        sf_mark_frozen_failure(device);
        if (prefix > bio->block_size) prefix = bio->block_size;
        if (prefix != 0) {
            memcpy(sf_block_ptr(device, device->volatile_bytes, blk), buf,
                   prefix);
            device->dirty_blocks[blk] = true;
        }
        return device->failure_error;
    }

    memcpy(sf_block_ptr(device, device->volatile_bytes, blk), buf,
           bio->block_size);
    device->dirty_blocks[blk] = true;
    return BFS_OK;
}

static bfs_err_t sf_sync(bfs_bio_t *bio)
{
    sf_bio_t *device = (sf_bio_t *)bio;
    ++device->syncs;
    sf_record_io_event(device, 'S', UINT32_MAX);
    bool frozen = device->observed_fs &&
        device->observed_fs->freespace.allocation_frozen;
    if (frozen && device->frozen_sb_write_seen) {
        ++device->frozen_sb_syncs;
        if (device->observed_fs->pending_count != 0)
            device->pending_at_frozen_sb_sync = true;
    } else if (frozen) {
        ++device->sealed_metadata_syncs;
        if (device->observed_fs->pending_count != 0)
            device->pending_at_sealed_metadata_sync = true;
    }

    if (device->fail_sync_at && device->injected_sync_index == 0 &&
        device->syncs == device->fail_sync_at) {
        device->injected_sync_index = device->syncs;
        sf_mark_frozen_failure(device);
        device->failed_sync_persisted_non_sb_blocks = 0;
        device->failed_sync_persisted_sb_blocks = 0;
        if (device->sync_error_mode == SF_SYNC_ERROR_PERSIST_ALL) {
            for (bfs_blk_t blk = 0; blk < device->bio.block_count; ++blk) {
                if (!device->dirty_blocks[blk]) continue;
                if (sf_is_superblock_block(blk))
                    ++device->failed_sync_persisted_sb_blocks;
                else
                    ++device->failed_sync_persisted_non_sb_blocks;
            }
            sf_commit_dirty(device);
        } else if (device->sync_error_mode == SF_SYNC_ERROR_PERSIST_SB_ONLY) {
            sf_commit_dirty_superblocks(device);
        } else if (device->sync_error_mode ==
                   SF_SYNC_ERROR_PERSIST_FIRST_NON_SB) {
            sf_commit_first_dirty_non_superblock(device);
        }
        return device->failure_error;
    }

    /* The model never turns a successful sync into a partial persistence. */
    sf_commit_dirty(device);
    return BFS_OK;
}

static void sf_close(bfs_bio_t *bio)
{
    (void)bio;
}

static const bfs_bio_ops_t sf_bio_ops = {
    .read_block = sf_read_block,
    .write_block = sf_write_block,
    .sync = sf_sync,
    .close = sf_close,
};

static bool sf_bio_init(sf_bio_t *device)
{
    uint64_t bytes = (uint64_t)SF_BLOCK_SIZE * SF_BLOCK_COUNT;
    memset(device, 0, sizeof(*device));
    device->volatile_bytes = (uint8_t *)calloc(1, (size_t)bytes);
    device->durable_bytes = (uint8_t *)calloc(1, (size_t)bytes);
    device->dirty_blocks = (bool *)calloc(SF_BLOCK_COUNT, sizeof(bool));
    if (!device->volatile_bytes || !device->durable_bytes ||
        !device->dirty_blocks)
        return false;
    device->storage_bytes = (size_t)bytes;
    device->bio.ops = &sf_bio_ops;
    device->bio.block_size = SF_BLOCK_SIZE;
    device->bio.block_count = SF_BLOCK_COUNT;
    device->failure_error = BFS_ERR_IO;
    return true;
}

static void sf_bio_destroy(sf_bio_t *device)
{
    free(device->volatile_bytes);
    free(device->durable_bytes);
    free(device->dirty_blocks);
    memset(device, 0, sizeof(*device));
}

static void sf_reset_trace_and_faults(sf_bio_t *device)
{
    device->reads = 0;
    device->writes = 0;
    device->syncs = 0;
    device->fail_read_at = 0;
    device->fail_write_at = 0;
    device->fail_sync_at = 0;
    device->injected_read_index = 0;
    device->injected_write_index = 0;
    device->injected_sync_index = 0;
    device->failed_write_prefix_bytes = 0;
    device->sync_error_mode = SF_SYNC_ERROR_DROP;
    device->failure_error = BFS_ERR_IO;
    device->failed_sync_persisted_non_sb_blocks = 0;
    device->failed_sync_persisted_sb_blocks = 0;
    device->trace_overflow = false;
    device->event_count = 0;
    device->failed_while_frozen = false;
    device->frozen_writes = 0;
    device->non_sb_writes_while_frozen = 0;
    device->frozen_sb_writes = 0;
    device->sealed_metadata_syncs = 0;
    device->frozen_sb_syncs = 0;
    device->frozen_sb_write_seen = false;
    device->pending_at_sealed_metadata_sync = false;
    device->pending_at_frozen_sb_write = false;
    device->pending_at_frozen_sb_sync = false;
    device->old_root_changed = false;
    memset(device->write_blocks, 0, sizeof(device->write_blocks));
    memset(device->events, 0, sizeof(device->events));
}

static void sf_arm_read_failure(sf_bio_t *device, uint64_t nth)
{
    device->fail_read_at = nth == 0 ? 0 : device->reads + nth;
    device->injected_read_index = 0;
}

static void sf_arm_write_failure(sf_bio_t *device, uint64_t nth,
                                 size_t prefix_bytes)
{
    device->fail_write_at = nth == 0 ? 0 : device->writes + nth;
    device->failed_write_prefix_bytes = prefix_bytes;
    device->injected_write_index = 0;
}

static void sf_arm_sync_failure(sf_bio_t *device, uint64_t nth,
                                sf_sync_error_mode_t mode)
{
    device->fail_sync_at = nth == 0 ? 0 : device->syncs + nth;
    device->sync_error_mode = mode;
    device->injected_sync_index = 0;
}

static void sf_power_cut(sf_bio_t *device)
{
    memcpy(device->volatile_bytes, device->durable_bytes,
           device->storage_bytes);
    memset(device->dirty_blocks, 0,
           (size_t)device->bio.block_count * sizeof(bool));
}

static void sf_capture_trace(const sf_bio_t *device,
                             sf_operation_trace_t *trace)
{
    trace->reads = device->reads;
    trace->writes = device->writes;
    trace->syncs = device->syncs;
    trace->sealed_metadata_syncs = device->sealed_metadata_syncs;
    trace->frozen_sb_syncs = device->frozen_sb_syncs;
    trace->trace_overflow = device->trace_overflow;
    trace->event_count = device->event_count;
    memcpy(trace->write_blocks, device->write_blocks,
           sizeof(trace->write_blocks));
    memcpy(trace->events, device->events, sizeof(trace->events));
}

static bool sf_fixture_init(sf_fixture_t *fixture, uint32_t options,
                            bool publish_once_before_mutation)
{
    memset(fixture, 0, sizeof(*fixture));
    if (!sf_bio_init(&fixture->device)) return false;
    if (bfs_fs_format(&fixture->device.bio, "SealPersist", options) != BFS_OK)
        goto fail;
    if (bfs_fs_mount(&fixture->fs, &fixture->device.bio) != BFS_OK) goto fail;
    fixture->mounted = true;
    fixture->device.observed_fs = &fixture->fs;

    if (publish_once_before_mutation && bfs_fs_sync(&fixture->fs) != BFS_OK)
        goto fail;

    fixture->old_txn_id = bfs_be64(fixture->fs.txn.sb.txn_id);
    fixture->commit_txn_id = bfs_txn_id(&fixture->fs.txn);
    fixture->old_committed_free_root =
        bfs_be32(fixture->fs.txn.sb.free_tree_root);
    if (fixture->old_committed_free_root == BFS_BLK_NULL ||
        fixture->old_committed_free_root >= SF_BLOCK_COUNT)
        goto fail;
    memcpy(fixture->device.old_root_bytes,
           sf_block_ptr(&fixture->device, fixture->device.durable_bytes,
                        fixture->old_committed_free_root),
           SF_BLOCK_SIZE);
    fixture->device.old_root = fixture->old_committed_free_root;
    fixture->device.watch_old_root = true;

    if (bfs_freespace_refill_reserve(&fixture->fs.freespace) != BFS_OK)
        goto fail;
    if (bfs_fs_create_file(&fixture->fs, BFS_ROOT_INO, "sealed.bin", 10,
                           &fixture->ino) != BFS_OK)
        goto fail;
    for (uint32_t i = 0; i < SF_DATA_SIZE; ++i)
        fixture->expected[i] = (uint8_t)((i * 29u + (i >> 7) * 17u) ^ (i >> 3));
    bfs_file_t file;
    if (bfs_file_open(&file, &fixture->fs, fixture->ino) != BFS_OK) goto fail;
    if (bfs_file_write(&file, fixture->expected, sizeof(fixture->expected)) !=
        (int32_t)sizeof(fixture->expected))
        goto fail;

    uint64_t root_txn = 0;
    if (fixture->fs.freespace.tree.height != 1 ||
        fixture->fs.pending_count == 0 ||
        fixture->fs.live_txn_id != fixture->commit_txn_id ||
        fixture->fs.live_txn_id <= fixture->old_txn_id ||
        bfs_btree_root_leaf_txn_id(&fixture->fs.freespace.tree, &root_txn) !=
            BFS_OK ||
        root_txn != fixture->fs.live_txn_id ||
        fixture->fs.freespace.tree.root == fixture->old_committed_free_root ||
        fixture->device.old_root_changed)
        goto fail;

    sf_reset_trace_and_faults(&fixture->device);
    return true;

fail:
    if (fixture->mounted) bfs_fs_abandon(&fixture->fs);
    sf_bio_destroy(&fixture->device);
    return false;
}

static void sf_fixture_destroy(sf_fixture_t *fixture)
{
    if (fixture->mounted) bfs_fs_abandon(&fixture->fs);
    fixture->mounted = false;
    fixture->device.observed_fs = NULL;
    sf_bio_destroy(&fixture->device);
}

static bool sf_capture_live_state(const sf_fixture_t *fixture,
                                  sf_live_snapshot_t *snapshot)
{
    const bfs_fs_t *fs = &fixture->fs;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->root = fs->freespace.tree.root;
    snapshot->total_free = fs->freespace.total_free;
    snapshot->roving = fs->freespace.roving;
    snapshot->reserve_count = fs->freespace.reserve_count;
    memcpy(snapshot->reserve, fs->freespace.reserve,
           sizeof(snapshot->reserve));
    snapshot->emergency_count = bfs_be32(fs->txn.sb_new.emergency_count);
    memcpy(snapshot->emergency_pool, fs->txn.sb_new.emergency_pool,
           sizeof(snapshot->emergency_pool));
    snapshot->pending_count = fs->pending_count;
    if (snapshot->pending_count != 0) {
        snapshot->pending = (bfs_blk_t *)malloc(
            snapshot->pending_count * sizeof(*snapshot->pending));
        if (!snapshot->pending) return false;
        memcpy(snapshot->pending, bfs_fs_pending_items((bfs_fs_t *)fs),
               snapshot->pending_count * sizeof(*snapshot->pending));
    }
    snapshot->sb_free_root = fs->txn.sb_new.free_tree_root;
    snapshot->sb_free_blocks = fs->txn.sb_new.free_blocks;
    memcpy(snapshot->root_bytes,
           sf_block_ptr((sf_bio_t *)&fixture->device,
                        fixture->device.volatile_bytes, snapshot->root),
           SF_BLOCK_SIZE);
    return true;
}

static bool sf_live_state_unchanged(const sf_fixture_t *fixture,
                                    const sf_live_snapshot_t *snapshot)
{
    const bfs_fs_t *fs = &fixture->fs;
    return fs->freespace.tree.root == snapshot->root &&
           fs->freespace.total_free == snapshot->total_free &&
           fs->freespace.roving == snapshot->roving &&
           fs->freespace.reserve_count == snapshot->reserve_count &&
           memcmp(fs->freespace.reserve, snapshot->reserve,
                  sizeof(snapshot->reserve)) == 0 &&
           bfs_be32(fs->txn.sb_new.emergency_count) ==
               snapshot->emergency_count &&
           memcmp(fs->txn.sb_new.emergency_pool, snapshot->emergency_pool,
                  sizeof(snapshot->emergency_pool)) == 0 &&
           fs->pending_count == snapshot->pending_count &&
           (snapshot->pending_count == 0 ||
            memcmp(bfs_fs_pending_items((bfs_fs_t *)fs), snapshot->pending,
                   snapshot->pending_count * sizeof(*snapshot->pending)) == 0) &&
           fs->txn.sb_new.free_tree_root == snapshot->sb_free_root &&
           fs->txn.sb_new.free_blocks == snapshot->sb_free_blocks &&
           !fs->freespace.allocation_frozen &&
           memcmp(sf_block_ptr((sf_bio_t *)&fixture->device,
                               fixture->device.volatile_bytes, snapshot->root),
                  snapshot->root_bytes, SF_BLOCK_SIZE) == 0;
}

static void sf_live_snapshot_destroy(sf_live_snapshot_t *snapshot)
{
    free(snapshot->pending);
    snapshot->pending = NULL;
}

static bool sf_check_clean(bfs_fs_t *fs)
{
    bfs_fsck_report_t report = {0};
    bfs_err_t err = bfs_fs_check(fs, false, &report);
    if (err != BFS_OK || report.errors != 0 || report.warnings != 0 ||
        report.leaked_blocks != 0) {
        fprintf(stderr,
                "  SF fsck defect: check=%d errors=%u warnings=%u leaks=%u\n",
                err, report.errors, report.warnings, report.leaked_blocks);
        return false;
    }
    return true;
}

static bool sf_check_file_data(bfs_fs_t *fs, const sf_fixture_t *fixture)
{
    bfs_file_t file;
    uint8_t actual[SF_DATA_SIZE];
    if (bfs_file_open(&file, fs, fixture->ino) != BFS_OK) return false;
    if (bfs_file_read(&file, actual, sizeof(actual)) != (int32_t)sizeof(actual))
        return false;
    return memcmp(actual, fixture->expected, sizeof(actual)) == 0;
}

static bool sf_check_pool_extent(const void *key, const void *value, void *ctx)
{
    sf_pool_scan_t *scan = (sf_pool_scan_t *)ctx;
    bfs_blk_t start = bfs_load_be32(key);
    uint32_t length = bfs_load_be32(value);
    uint32_t count = bfs_be32(scan->sb->emergency_count);
    for (uint32_t i = 0; i < count; ++i) {
        bfs_blk_t blk = bfs_be32(scan->sb->emergency_pool[i]);
        if (blk >= start && blk - start < length) scan->disjoint = false;
    }
    return true;
}

static bool sf_check_active_pool_disjoint(bfs_fs_t *fs)
{
    sf_pool_scan_t scan = {
        .sb = &fs->txn.sb,
        .root = fs->freespace.tree.root,
        .disjoint = true,
    };
    uint32_t count = bfs_be32(scan.sb->emergency_count);
    if (count > BFS_EMERGENCY_POOL_SIZE) return false;
    for (uint32_t i = 0; i < count; ++i) {
        bfs_blk_t blk = bfs_be32(scan.sb->emergency_pool[i]);
        if (blk < bfs_data_start_block(fs->bio->block_size) ||
            blk >= fs->bio->block_count || blk == scan.root)
            return false;
        for (uint32_t j = 0; j < i; ++j)
            if (bfs_be32(scan.sb->emergency_pool[j]) == blk) return false;
    }
    return bfs_btree_scan(&fs->freespace.tree, NULL, sf_check_pool_extent,
                          &scan) == BFS_OK && scan.disjoint;
}

/* Discard the failed owner, restore the durable image, and validate the
 * selected old or new committed identity with namespace and strict fsck. */
static bool sf_powercut_and_check(sf_fixture_t *fixture)
{
    uint64_t selected_id;
    sf_power_cut(&fixture->device);
    if (fixture->mounted) bfs_fs_abandon(&fixture->fs);
    fixture->mounted = false;
    fixture->device.observed_fs = NULL;
    fixture->device.watch_old_root = false;

    bfs_fs_t check_fs;
    if (bfs_fs_mount_readonly(&check_fs, &fixture->device.bio) != BFS_OK)
        return false;
    selected_id = bfs_be64(check_fs.txn.sb.txn_id);
    bool valid = selected_id == fixture->old_txn_id ||
                 selected_id == fixture->commit_txn_id;
    if (valid) valid = sf_check_clean(&check_fs);
    if (valid && selected_id == fixture->old_txn_id)
        valid = bfs_file_open(&(bfs_file_t){0}, &check_fs, fixture->ino) ==
                BFS_ERR_NOTFOUND;
    else if (valid && selected_id == fixture->commit_txn_id)
        valid = sf_check_file_data(&check_fs, fixture);
    if (bfs_fs_unmount(&check_fs) != BFS_OK) valid = false;
    return valid;
}

static bool sf_storage_best_txn_id(const sf_bio_t *device,
                                   const uint8_t *storage,
                                   uint64_t *txn_id)
{
    bfs_superblock_t primary;
    bfs_superblock_t backup;
    memcpy(&primary, storage, sizeof(primary));
    memcpy(&backup,
           storage + (size_t)SF_BACKUP_BLOCK * device->bio.block_size,
           sizeof(backup));
    bool primary_valid = bfs_sb_validate(&primary) == BFS_OK;
    bool backup_valid = bfs_sb_validate(&backup) == BFS_OK;
    if (!primary_valid && !backup_valid) return false;
    uint64_t primary_id = primary_valid ? bfs_be64(primary.txn_id) : 0;
    uint64_t backup_id = backup_valid ? bfs_be64(backup.txn_id) : 0;
    *txn_id = primary_id > backup_id ? primary_id : backup_id;
    return true;
}

static bool sf_find_sync_ordinal_after_sb_write(
    const sf_operation_trace_t *trace, bfs_blk_t sb_block,
    uint64_t *ordinal)
{
    bool sb_write_seen = false;
    uint64_t syncs = 0;
    for (uint32_t i = 0; i < trace->event_count; ++i) {
        const sf_io_event_t *event = &trace->events[i];
        if (event->operation == 'W' && event->block == sb_block)
            sb_write_seen = true;
        else if (event->operation == 'S') {
            ++syncs;
            if (sb_write_seen) {
                *ordinal = syncs;
                return true;
            }
        }
    }
    return false;
}

static bool sf_find_sync_ordinal_before_sb_write(
    const sf_operation_trace_t *trace, bfs_blk_t sb_block,
    uint64_t *ordinal)
{
    uint64_t syncs = 0;
    for (uint32_t i = 0; i < trace->event_count; ++i) {
        const sf_io_event_t *event = &trace->events[i];
        if (event->operation == 'W' && event->block == sb_block) return false;
        if (event->operation == 'S') {
            *ordinal = ++syncs;
            return true;
        }
    }
    return false;
}

static bool sf_trace_sync_before_writes(const sf_bio_t *device)
{
    uint32_t first_sync = SF_TRACE_CAP;
    uint32_t first_write = SF_TRACE_CAP;
    for (uint32_t i = 0; i < device->event_count; ++i) {
        if (device->events[i].operation == 'S' && first_sync == SF_TRACE_CAP)
            first_sync = i;
        if (device->events[i].operation == 'W' && first_write == SF_TRACE_CAP)
            first_write = i;
    }
    return first_sync < first_write;
}

static bool sf_expect_sticky_no_writes(sf_fixture_t *fixture,
                                       bfs_err_t expected_error);

/* Produce the volatile-new/durable-old state left by a dropped publication
 * sync, then abandon the original owner without simulating a power cut. */
static bool sf_prepare_failed_publication_drop(sf_fixture_t *fixture)
{
    if (!sf_fixture_init(fixture, 0, false)) return false;
    uint64_t publication_sync = 0;
    if (!primary_trace_ready ||
        !sf_find_sync_ordinal_after_sb_write(&successful_primary_trace, 0,
                                             &publication_sync)) {
        sf_fixture_destroy(fixture);
        return false;
    }
    sf_arm_sync_failure(&fixture->device, publication_sync,
                        SF_SYNC_ERROR_DROP);
    bfs_err_t err = bfs_fs_sync(&fixture->fs);
    uint64_t durable_id = 0;
    uint64_t volatile_id = 0;
    bool valid = err == BFS_ERR_IO &&
        fixture->device.injected_sync_index == publication_sync &&
        fixture->device.failed_while_frozen &&
        fixture->fs.freespace.allocation_frozen &&
        fixture->fs.recovery_error == BFS_ERR_IO &&
        sf_storage_best_txn_id(&fixture->device,
                               fixture->device.durable_bytes, &durable_id) &&
        sf_storage_best_txn_id(&fixture->device,
                               fixture->device.volatile_bytes, &volatile_id) &&
        durable_id == fixture->old_txn_id &&
        volatile_id == fixture->commit_txn_id &&
        sf_expect_sticky_no_writes(fixture, BFS_ERR_IO);
    if (!valid) {
        sf_fixture_destroy(fixture);
        return false;
    }
    bfs_fs_abandon(&fixture->fs);
    fixture->mounted = false;
    fixture->device.observed_fs = NULL;
    fixture->device.watch_old_root = false;
    sf_reset_trace_and_faults(&fixture->device);
    return true;
}

static bool sf_expect_sticky_no_writes(sf_fixture_t *fixture,
                                      bfs_err_t expected_error)
{
    uint64_t writes_before = fixture->device.writes;
    if (fixture->fs.recovery_error != expected_error) return false;
    if (bfs_freespace_alloc(&fixture->fs.freespace, 1) != BFS_BLK_NULL ||
        fixture->fs.freespace.last_error != expected_error)
        return false;
    if (fixture->device.writes != writes_before) return false;
    if (bfs_fs_sync(&fixture->fs) != expected_error) return false;
    return fixture->device.writes == writes_before;
}

static uint32_t sf_count_sb_writes(const sf_operation_trace_t *trace,
                                   uint32_t target_block,
                                   uint64_t *target_index)
{
    uint32_t count = 0;
    if (target_index) *target_index = 0;
    for (uint64_t i = 0; i < trace->writes && i < SF_TRACE_CAP; ++i) {
        if (sf_is_superblock_block(trace->write_blocks[i])) {
            ++count;
            if (target_index && trace->write_blocks[i] == target_block)
                *target_index = i + 1;
        }
    }
    return count;
}

static bool sf_run_successful_commit(bool publish_once_before_mutation,
                                     sf_operation_trace_t *trace,
                                     uint32_t *sb_target_block)
{
    sf_fixture_t fixture;
    if (!sf_fixture_init(&fixture, 0, publish_once_before_mutation))
        return false;
    if (bfs_fs_sync(&fixture.fs) != BFS_OK) {
        sf_fixture_destroy(&fixture);
        return false;
    }
    sf_capture_trace(&fixture.device, trace);
    uint64_t target_idx = 0;
    if (trace->trace_overflow || trace->writes != 2 ||
        sf_count_sb_writes(trace, publish_once_before_mutation ?
                           SF_BACKUP_BLOCK : 0, &target_idx) != 1 ||
        target_idx == 0 || fixture.device.frozen_sb_writes != 1 ||
        fixture.device.sealed_metadata_syncs != 1 ||
        fixture.device.frozen_sb_syncs != 1 ||
        fixture.device.frozen_writes != 1 ||
        fixture.device.non_sb_writes_while_frozen != 0 ||
        !fixture.device.pending_at_sealed_metadata_sync ||
        !fixture.device.pending_at_frozen_sb_write ||
        !fixture.device.pending_at_frozen_sb_sync ||
        fixture.fs.pending_count != 0 ||
        fixture.fs.freespace.reserve_count != 0 ||
        fixture.fs.freespace.allocation_frozen ||
        fixture.device.old_root_changed ||
        !sf_check_active_pool_disjoint(&fixture.fs) ||
        !sf_check_clean(&fixture.fs) || !sf_check_file_data(&fixture.fs,
                                                             &fixture)) {
        sf_fixture_destroy(&fixture);
        return false;
    }
    if (sb_target_block)
        *sb_target_block = fixture.device.write_blocks[target_idx - 1];
    bool valid = sf_powercut_and_check(&fixture);
    sf_bio_destroy(&fixture.device);
    return valid;
}

typedef enum {
    SF_CUT_READ,
    SF_CUT_WRITE,
    SF_CUT_SYNC
} sf_cut_kind_t;

static char sf_cut_operation(sf_cut_kind_t kind)
{
    return kind == SF_CUT_READ ? 'R' : kind == SF_CUT_WRITE ? 'W' : 'S';
}

static bool sf_find_cut_prefix(const sf_operation_trace_t *trace,
                               sf_cut_kind_t kind, uint64_t nth,
                               uint32_t *prefix_len)
{
    char operation = sf_cut_operation(kind);
    uint64_t seen = 0;
    if (trace->trace_overflow || nth == 0) return false;
    for (uint32_t i = 0; i < trace->event_count; ++i) {
        if (trace->events[i].operation == operation && ++seen == nth) {
            *prefix_len = i + 1;
            return true;
        }
    }
    return false;
}

static bool sf_failure_trace_stops_at(const sf_fixture_t *fixture,
                                      sf_cut_kind_t kind, uint64_t nth,
                                      bool publish_once_before_mutation)
{
    const sf_operation_trace_t *trace = publish_once_before_mutation ?
        &successful_backup_trace : &successful_primary_trace;
    bool trace_ready = publish_once_before_mutation ? backup_trace_ready :
                       primary_trace_ready;
    uint32_t prefix_len = 0;
    if (!trace_ready || !sf_find_cut_prefix(trace, kind, nth, &prefix_len) ||
        fixture->device.trace_overflow ||
        fixture->device.event_count != prefix_len)
        return false;
    return memcmp(fixture->device.events, trace->events,
                  prefix_len * sizeof(trace->events[0])) == 0;
}

static bool sf_find_read_ordinal(const sf_operation_trace_t *trace,
                                bfs_blk_t block, uint64_t *ordinal,
                                uint32_t *event_index)
{
    uint64_t reads = 0;
    for (uint32_t i = 0; i < trace->event_count; ++i) {
        if (trace->events[i].operation != 'R') continue;
        ++reads;
        if (trace->events[i].block == block) {
            *ordinal = reads;
            if (event_index) *event_index = i;
            return true;
        }
    }
    return false;
}

static bool sf_run_success_beyond_end(sf_cut_kind_t kind, uint64_t nth,
                                      bool publish_once_before_mutation);

static bool sf_run_failed_commit_cut(sf_cut_kind_t kind, uint64_t nth,
                                     bfs_err_t injected_error,
                                     sf_sync_error_mode_t sync_mode,
                                     size_t partial_prefix,
                                     bool publish_once_before_mutation,
                                     bool check_preswap_rollback,
                                     uint32_t expected_write_block)
{
    sf_fixture_t fixture;
    sf_live_snapshot_t before;
    if (!sf_fixture_init(&fixture, 0, publish_once_before_mutation))
        return false;
    if (!sf_capture_live_state(&fixture, &before)) {
        sf_fixture_destroy(&fixture);
        return false;
    }

    if (kind == SF_CUT_READ) sf_arm_read_failure(&fixture.device, nth);
    else if (kind == SF_CUT_WRITE)
        sf_arm_write_failure(&fixture.device, nth, partial_prefix);
    else sf_arm_sync_failure(&fixture.device, nth, sync_mode);
    fixture.device.failure_error = injected_error;

    bfs_err_t err = bfs_fs_sync(&fixture.fs);
    uint64_t injected = kind == SF_CUT_READ ? fixture.device.injected_read_index :
                        kind == SF_CUT_WRITE ? fixture.device.injected_write_index :
                        fixture.device.injected_sync_index;
    bool valid = err == injected_error && injected == nth;
    valid = valid && sf_failure_trace_stops_at(&fixture, kind, nth,
                                                publish_once_before_mutation);
    if (kind == SF_CUT_WRITE && expected_write_block != UINT32_MAX &&
        nth <= SF_TRACE_CAP)
        valid = valid && fixture.device.write_blocks[nth - 1] ==
                         expected_write_block;

    bool post_seal = fixture.device.failed_while_frozen ||
        bfs_be64(fixture.fs.txn.sb.txn_id) == fixture.commit_txn_id;
    if (kind == SF_CUT_SYNC && !publish_once_before_mutation) {
        uint64_t metadata_sync = 0;
        uint64_t publication_sync = 0;
        valid = valid &&
            sf_find_sync_ordinal_before_sb_write(&successful_primary_trace, 0,
                                                 &metadata_sync) &&
            sf_find_sync_ordinal_after_sb_write(&successful_primary_trace, 0,
                                                &publication_sync);
        if (nth == metadata_sync) {
            valid = valid && fixture.fs.freespace.allocation_frozen &&
                fixture.fs.pending_count == before.pending_count &&
                fixture.device.sealed_metadata_syncs == 1 &&
                fixture.device.frozen_sb_syncs == 0 &&
                fixture.device.frozen_sb_writes == 0 &&
                fixture.device.pending_at_sealed_metadata_sync &&
                !fixture.device.pending_at_frozen_sb_sync;
        } else if (nth == publication_sync) {
            valid = valid && fixture.fs.freespace.allocation_frozen &&
                fixture.fs.pending_count == before.pending_count &&
                fixture.device.sealed_metadata_syncs == 1 &&
                fixture.device.frozen_sb_syncs == 1 &&
                fixture.device.frozen_sb_writes == 1 &&
                fixture.device.pending_at_sealed_metadata_sync &&
                fixture.device.pending_at_frozen_sb_sync;
        }
    }
    bool state_unchanged = false;
    if (!post_seal)
        state_unchanged = sf_live_state_unchanged(&fixture, &before) &&
                          !fixture.device.old_root_changed;
    if (!post_seal) valid = valid && state_unchanged;
    if (check_preswap_rollback)
        valid = valid && !post_seal;
    if (post_seal) {
        bool published = bfs_be64(fixture.fs.txn.sb.txn_id) ==
                         fixture.commit_txn_id;
        valid = valid && fixture.fs.recovery_error == injected_error &&
                (published ? (fixture.fs.pending_count == 0 &&
                              !fixture.fs.freespace.allocation_frozen) :
                             (fixture.fs.pending_count == before.pending_count &&
                              fixture.fs.freespace.allocation_frozen)) &&
                sf_expect_sticky_no_writes(&fixture, injected_error);
    } else {
        valid = valid && fixture.fs.recovery_error == injected_error;
    }
    valid = valid && !fixture.device.old_root_changed;

    uint64_t expected_after_cut = UINT64_MAX;
    if (valid && kind == SF_CUT_SYNC) {
        uint64_t publication_sync = 0;
        if (!sf_find_sync_ordinal_after_sb_write(&successful_primary_trace, 0,
                                                &publication_sync)) {
            valid = false;
        } else if (sync_mode == SF_SYNC_ERROR_PERSIST_FIRST_NON_SB) {
            valid = valid &&
                fixture.device.failed_sync_persisted_non_sb_blocks == 1 &&
                fixture.device.failed_sync_persisted_sb_blocks == 0;
            expected_after_cut = fixture.old_txn_id;
        } else if (sync_mode == SF_SYNC_ERROR_PERSIST_SB_ONLY) {
            valid = valid &&
                fixture.device.failed_sync_persisted_non_sb_blocks == 0 &&
                fixture.device.failed_sync_persisted_sb_blocks == 1;
            expected_after_cut = fixture.commit_txn_id;
        } else if (sync_mode == SF_SYNC_ERROR_PERSIST_ALL) {
            expected_after_cut = nth >= publication_sync ?
                fixture.commit_txn_id : fixture.old_txn_id;
        } else {
            expected_after_cut = nth > publication_sync ?
                fixture.commit_txn_id : fixture.old_txn_id;
        }
    }

    sf_live_snapshot_destroy(&before);
    if (valid) valid = sf_powercut_and_check(&fixture);
    if (valid && expected_after_cut != UINT64_MAX) {
        uint64_t selected_id = 0;
        valid = sf_storage_best_txn_id(&fixture.device,
                    fixture.device.durable_bytes, &selected_id) &&
                selected_id == expected_after_cut;
    }
    if (!valid)
        fprintf(stderr,
                "  SF cut defect: kind=%d nth=%llu err=%d injected=%llu "
                "post_seal=%d frozen=%d recovery=%d pending=%u/%u "
                "writes=%llu reads=%llu syncs=%llu oldroot_changed=%d "
                "rollback=%d\n",
                kind, (unsigned long long)nth, err,
                (unsigned long long)injected, post_seal,
                fixture.fs.freespace.allocation_frozen,
                fixture.fs.recovery_error, fixture.fs.pending_count,
                before.pending_count, (unsigned long long)fixture.device.writes,
                (unsigned long long)fixture.device.reads,
                (unsigned long long)fixture.device.syncs,
                fixture.device.old_root_changed, state_unchanged);
    sf_bio_destroy(&fixture.device);
    return valid;
}

static void test_natural_sealed_commit_and_remount(void)
{
    sf_operation_trace_t trace;
    uint32_t target = UINT32_MAX;
    TEST_ASSERT(sf_run_successful_commit(false, &trace, &target));
    TEST_ASSERT(!trace.trace_overflow);
    TEST_ASSERT_EQ(trace.writes, 2);
    TEST_ASSERT(trace.reads > 0);
    TEST_ASSERT_EQ(trace.syncs, 3);
    TEST_ASSERT_EQ(trace.sealed_metadata_syncs, 1);
    TEST_ASSERT_EQ(trace.frozen_sb_syncs, 1);
    TEST_ASSERT_EQ(target, 0);
    successful_primary_trace = trace;
    primary_trace_ready = true;
    printf("SF primary-slot successful cut baseline: reads=%llu writes=%llu syncs=%llu (metadata=%u publication=%u)\n",
           (unsigned long long)trace.reads,
           (unsigned long long)trace.writes,
           (unsigned long long)trace.syncs,
           trace.sealed_metadata_syncs, trace.frozen_sb_syncs);
}

static void test_every_read_cut_and_beyond_end(void)
{
    TEST_ASSERT(primary_trace_ready);
    for (uint64_t i = 1; i <= successful_primary_trace.reads; ++i) {
        TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_READ, i,
                        BFS_ERR_IO, SF_SYNC_ERROR_DROP, 0, false, false,
                        UINT32_MAX));
    }
    TEST_ASSERT(sf_run_success_beyond_end(SF_CUT_READ,
                    successful_primary_trace.reads + 1, false));
}

static void test_every_write_cut_and_beyond_end(void)
{
    TEST_ASSERT(primary_trace_ready);
    for (uint64_t i = 1; i <= successful_primary_trace.writes; ++i) {
        uint32_t block = successful_primary_trace.write_blocks[i - 1];
        size_t partial = block == 0 || block == SF_BACKUP_BLOCK
                       ? BFS_SB_SIZE / 2 : SF_BLOCK_SIZE / 2;
        TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_WRITE, i,
                        BFS_ERR_IO, SF_SYNC_ERROR_DROP, partial, false,
                        !sf_is_superblock_block(block), block));
    }
    TEST_ASSERT(sf_run_success_beyond_end(SF_CUT_WRITE,
                    successful_primary_trace.writes + 1, false));
}

static void test_every_sync_cut_with_both_persistence_outcomes(void)
{
    TEST_ASSERT(primary_trace_ready);
    for (uint64_t i = 1; i <= successful_primary_trace.syncs; ++i) {
        TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_SYNC, i,
                        BFS_ERR_IO, SF_SYNC_ERROR_DROP, 0, false, false,
                        UINT32_MAX));
        TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_SYNC, i,
                        BFS_ERR_IO, SF_SYNC_ERROR_PERSIST_ALL, 0, false, false,
                        UINT32_MAX));
    }
    TEST_ASSERT(sf_run_success_beyond_end(SF_CUT_SYNC,
                    successful_primary_trace.syncs + 1, false));
}

static void test_failed_sync_partial_persistence_modes(void)
{
    TEST_ASSERT(primary_trace_ready);
    uint64_t metadata_sync = 0;
    uint64_t publication_sync = 0;
    TEST_ASSERT(sf_find_sync_ordinal_before_sb_write(&successful_primary_trace,
                    0, &metadata_sync));
    TEST_ASSERT(sf_find_sync_ordinal_after_sb_write(&successful_primary_trace,
                    0, &publication_sync));
    TEST_ASSERT(metadata_sync < publication_sync);
    TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_SYNC, metadata_sync,
                    BFS_ERR_IO, SF_SYNC_ERROR_PERSIST_FIRST_NON_SB, 0,
                    false, false, UINT32_MAX));
    TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_SYNC, publication_sync,
                    BFS_ERR_IO, SF_SYNC_ERROR_PERSIST_SB_ONLY, 0, false,
                    false, UINT32_MAX));
}

static void test_partial_backup_superblock_write(void)
{
    sf_operation_trace_t trace;
    uint32_t target = UINT32_MAX;
    TEST_ASSERT(sf_run_successful_commit(true, &trace, &target));
    TEST_ASSERT_EQ(target, SF_BACKUP_BLOCK);
    successful_backup_trace = trace;
    backup_trace_ready = true;
    uint64_t write_index = 0;
    TEST_ASSERT_EQ(sf_count_sb_writes(&trace, SF_BACKUP_BLOCK, &write_index), 1);
    TEST_ASSERT(write_index > 0);
    TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_WRITE, write_index,
                    BFS_ERR_IO, SF_SYNC_ERROR_DROP, BFS_SB_SIZE / 2, true,
                    false, SF_BACKUP_BLOCK));
    printf("SF backup-slot partial write cut: write=%llu of %llu\n",
           (unsigned long long)write_index,
           (unsigned long long)trace.writes);
}

static void test_unsupported_error_never_falls_back(void)
{
    TEST_ASSERT(primary_trace_ready);
    uint64_t sb_read_ordinal = 0;
    uint32_t sb_read_event = 0;
    uint64_t scratch_write_ordinal = 0;
    uint32_t sb_write_event = SF_TRACE_CAP;
    uint32_t first_read_event = SF_TRACE_CAP;
    uint32_t first_write_event = SF_TRACE_CAP;
    uint32_t scratch_write_event = SF_TRACE_CAP;
    bool found_scratch = false;
    for (uint32_t i = 0; i < successful_primary_trace.event_count; ++i) {
        const sf_io_event_t *event = &successful_primary_trace.events[i];
        if (event->operation == 'R' && first_read_event == SF_TRACE_CAP)
            first_read_event = i;
        if (event->operation == 'W' && first_write_event == SF_TRACE_CAP)
            first_write_event = i;
        if (event->operation == 'W' && !sf_is_superblock_block(event->block))
            scratch_write_event = i;
    }
    for (uint64_t i = 0; i < successful_primary_trace.writes; ++i) {
        uint32_t block = successful_primary_trace.write_blocks[i];
        if (!sf_is_superblock_block(block)) {
            scratch_write_ordinal = i + 1;
            found_scratch = true;
        } else if (block == 0) {
            for (uint32_t j = 0; j < successful_primary_trace.event_count; ++j) {
                if (successful_primary_trace.events[j].operation == 'W' &&
                    successful_primary_trace.events[j].block == block) {
                    sb_write_event = j;
                    break;
                }
            }
        }
    }
    TEST_ASSERT(found_scratch);
    TEST_ASSERT(first_read_event < first_write_event);
    TEST_ASSERT(first_write_event == scratch_write_event);
    TEST_ASSERT(sf_find_read_ordinal(&successful_primary_trace, 0,
                                     &sb_read_ordinal, &sb_read_event));
    TEST_ASSERT(scratch_write_event < sb_read_event);
    TEST_ASSERT(sb_write_event < successful_primary_trace.event_count);
    TEST_ASSERT(sb_read_event < sb_write_event);
    TEST_ASSERT(scratch_write_ordinal > 0);

    /* Preflight read errors must propagate and restore the unmodified owner. */
    TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_READ, 1,
                    BFS_ERR_UNSUPPORTED, SF_SYNC_ERROR_DROP, 0, false,
                    false, UINT32_MAX));

    /* A partial scratch write error must not run the legacy commit path. */
    TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_WRITE,
                    scratch_write_ordinal, BFS_ERR_UNSUPPORTED,
                    SF_SYNC_ERROR_DROP, SF_BLOCK_SIZE / 2, false, true,
                    successful_primary_trace.write_blocks[
                        scratch_write_ordinal - 1]));

    /* The post-seal SB read error is sticky and stops at that exact read. */
    TEST_ASSERT(sf_run_failed_commit_cut(SF_CUT_READ, sb_read_ordinal,
                    BFS_ERR_UNSUPPORTED, SF_SYNC_ERROR_DROP, 0, false,
                    false, UINT32_MAX));
}

static void test_writable_mount_barrier_after_dropped_publication(void)
{
    sf_fixture_t fixture;
    TEST_ASSERT(sf_prepare_failed_publication_drop(&fixture));

    bfs_fs_t remounted;
    TEST_ASSERT_EQ(bfs_fs_mount(&remounted, &fixture.device.bio), BFS_OK);
    TEST_ASSERT(!remounted.read_only);
    TEST_ASSERT_EQ(bfs_be64(remounted.txn.sb.txn_id), fixture.commit_txn_id);
    TEST_ASSERT_EQ(fixture.device.injected_sync_index, 0);
    TEST_ASSERT(sf_trace_sync_before_writes(&fixture.device));
    uint64_t durable_id = 0;
    TEST_ASSERT(sf_storage_best_txn_id(&fixture.device,
                    fixture.device.durable_bytes, &durable_id));
    TEST_ASSERT_EQ(durable_id, fixture.commit_txn_id);
    TEST_ASSERT(sf_check_clean(&remounted));
    TEST_ASSERT(sf_check_file_data(&remounted, &fixture));

    bfs_fs_abandon(&remounted);
    fixture.device.observed_fs = NULL;
    TEST_ASSERT(sf_powercut_and_check(&fixture));
    TEST_ASSERT(sf_storage_best_txn_id(&fixture.device,
                    fixture.device.durable_bytes, &durable_id));
    TEST_ASSERT_EQ(durable_id, fixture.commit_txn_id);
    sf_bio_destroy(&fixture.device);
}

static bool sf_run_failed_mount_barrier(sf_sync_error_mode_t mode,
                                        bool expect_new_durable)
{
    sf_fixture_t fixture;
    if (!sf_prepare_failed_publication_drop(&fixture)) return false;

    bfs_fs_t failed_mount;
    sf_arm_sync_failure(&fixture.device, 1, mode);
    bfs_err_t err = bfs_fs_mount(&failed_mount, &fixture.device.bio);
    uint64_t durable_id = 0;
    uint64_t volatile_id = 0;
    bool valid = err == BFS_ERR_IO &&
        fixture.device.injected_sync_index == 1 &&
        fixture.device.syncs == 1 && fixture.device.writes == 0 &&
        !failed_mount.mounted &&
        fixture.device.event_count != 0 &&
        fixture.device.events[fixture.device.event_count - 1].operation == 'S' &&
        sf_storage_best_txn_id(&fixture.device,
                               fixture.device.durable_bytes, &durable_id) &&
        sf_storage_best_txn_id(&fixture.device,
                               fixture.device.volatile_bytes, &volatile_id) &&
        durable_id == (expect_new_durable ? fixture.commit_txn_id :
                                             fixture.old_txn_id) &&
        volatile_id == fixture.commit_txn_id;
    for (uint32_t i = 0; i < fixture.device.event_count; ++i)
        if (fixture.device.events[i].operation == 'W') valid = false;
    if (!valid) {
        sf_fixture_destroy(&fixture);
        return false;
    }

    /* Read-only mount may inspect the visible new commit without flushing it. */
    sf_reset_trace_and_faults(&fixture.device);
    bfs_fs_t readonly;
    err = bfs_fs_mount_readonly(&readonly, &fixture.device.bio);
    valid = err == BFS_OK && readonly.read_only &&
        bfs_be64(readonly.txn.sb.txn_id) == fixture.commit_txn_id &&
        fixture.device.writes == 0 && fixture.device.syncs == 0 &&
        sf_check_clean(&readonly) && sf_check_file_data(&readonly, &fixture);
    if (err == BFS_OK) bfs_fs_abandon(&readonly);
    fixture.device.observed_fs = NULL;
    if (!valid) {
        sf_fixture_destroy(&fixture);
        return false;
    }

    /* A power cut selects exactly the state covered by the failed barrier. */
    valid = sf_powercut_and_check(&fixture) &&
        sf_storage_best_txn_id(&fixture.device,
                               fixture.device.durable_bytes, &durable_id) &&
        durable_id == (expect_new_durable ? fixture.commit_txn_id :
                                             fixture.old_txn_id);
    sf_bio_destroy(&fixture.device);
    return valid;
}

static void test_failed_mount_barrier_drop_and_persist_all(void)
{
    TEST_ASSERT(sf_run_failed_mount_barrier(SF_SYNC_ERROR_DROP, false));
    TEST_ASSERT(sf_run_failed_mount_barrier(SF_SYNC_ERROR_PERSIST_ALL, true));
}

static void test_ordered_preflush_failure_is_retryable(void)
{
    sf_fixture_t fixture;
    sf_live_snapshot_t before;
    TEST_ASSERT(sf_fixture_init(&fixture, BFS_OPT_DATA_ORDERED, false));
    TEST_ASSERT(sf_capture_live_state(&fixture, &before));
    sf_arm_sync_failure(&fixture.device, 1, SF_SYNC_ERROR_DROP);
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_ERR_IO);
    TEST_ASSERT_EQ(fixture.device.injected_sync_index, 1);
    TEST_ASSERT_EQ(fixture.device.writes, 0);
    TEST_ASSERT_EQ(fixture.fs.recovery_error, BFS_OK);
    TEST_ASSERT(!fixture.fs.freespace.allocation_frozen);
    TEST_ASSERT(sf_live_state_unchanged(&fixture, &before));

    sf_reset_trace_and_faults(&fixture.device);
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_OK);
    TEST_ASSERT_EQ(fixture.fs.pending_count, 0);
    TEST_ASSERT(!fixture.fs.freespace.allocation_frozen);
    TEST_ASSERT(fixture.device.frozen_sb_writes == 1);
    TEST_ASSERT(fixture.device.frozen_writes == 1);
    TEST_ASSERT(fixture.device.non_sb_writes_while_frozen == 0);
    TEST_ASSERT(fixture.device.pending_at_frozen_sb_write);
    TEST_ASSERT(sf_check_clean(&fixture.fs));
    TEST_ASSERT(sf_check_file_data(&fixture.fs, &fixture));
    sf_live_snapshot_destroy(&before);
    TEST_ASSERT(sf_powercut_and_check(&fixture));
    sf_bio_destroy(&fixture.device);
}

TEST_SUITE_BEGIN("Sealed transaction buffered persistence cuts")
    TEST_RUN(test_natural_sealed_commit_and_remount);
    TEST_RUN(test_every_read_cut_and_beyond_end);
    TEST_RUN(test_every_write_cut_and_beyond_end);
    TEST_RUN(test_every_sync_cut_with_both_persistence_outcomes);
    TEST_RUN(test_failed_sync_partial_persistence_modes);
    TEST_RUN(test_partial_backup_superblock_write);
    TEST_RUN(test_unsupported_error_never_falls_back);
    TEST_RUN(test_writable_mount_barrier_after_dropped_publication);
    TEST_RUN(test_failed_mount_barrier_drop_and_persist_all);
    TEST_RUN(test_ordered_preflush_failure_is_retryable);
TEST_SUITE_END()

static bool sf_run_success_beyond_end(sf_cut_kind_t kind, uint64_t nth,
                                      bool publish_once_before_mutation)
{
    sf_fixture_t fixture;
    if (!sf_fixture_init(&fixture, 0, publish_once_before_mutation))
        return false;
    if (kind == SF_CUT_READ) sf_arm_read_failure(&fixture.device, nth);
    else if (kind == SF_CUT_WRITE) sf_arm_write_failure(&fixture.device, nth, 0);
    else sf_arm_sync_failure(&fixture.device, nth, SF_SYNC_ERROR_DROP);
    bool valid = bfs_fs_sync(&fixture.fs) == BFS_OK;
    valid = valid && (kind == SF_CUT_READ ? fixture.device.injected_read_index == 0 :
                     kind == SF_CUT_WRITE ? fixture.device.injected_write_index == 0 :
                     fixture.device.injected_sync_index == 0);
    if (valid) sf_reset_trace_and_faults(&fixture.device);
    if (valid) valid = fixture.fs.pending_count == 0 &&
                       !fixture.fs.freespace.allocation_frozen &&
                       sf_check_clean(&fixture.fs) &&
                       sf_check_file_data(&fixture.fs, &fixture) &&
                       sf_powercut_and_check(&fixture);
    sf_bio_destroy(&fixture.device);
    return valid;
}
