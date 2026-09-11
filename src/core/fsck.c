/* SPDX-License-Identifier: MPL-2.0 */
/* Portable offline/in-handler BFS structural checker. */

#include "bfs_fsck.h"

#include "bfs_alloc.h"
#include "bfs_btree.h"
#include "bfs_dir.h"
#include "bfs_extent.h"
#include "bfs_inode.h"
#include "bfs_ondisk.h"
#include "bfs_refcount.h"
#include "bfs_snapshot.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    bfs_fs_t *fs;
    bfs_fsck_report_t report;
    uint8_t *block_map;
    uint8_t *reference_map;
    uint32_t block_count;
    bool reference_saturated;
} check_state_t;

typedef struct {
    check_state_t *state;
} check_context_t;

#define BFS_REFERENCE_MAX 255u

static const bfs_btree_ops_t snapshot_ops = {
    .key_compare = bfs_cmp_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_snapshot_record_t),
};

static void check_error(check_state_t *state)
{
    state->report.errors++;
}

static void check_warning(check_state_t *state)
{
    state->report.warnings++;
}

static void mark(check_state_t *state, uint32_t block, uint8_t type)
{
    if (block >= state->block_count) {
        check_error(state);
        return;
    }
    uint8_t previous = state->block_map[block] & 0x7fu;
    if (previous && previous != type) check_error(state);
    state->block_map[block] = (state->block_map[block] & 0x80u) | type;
}

static void mark_reference(check_state_t *state, uint32_t block, uint8_t type)
{
    mark(state, block, type);
    if (block >= state->block_count) return;
    if (state->reference_map[block] == BFS_REFERENCE_MAX) {
        state->reference_saturated = true;
        return;
    }
    state->reference_map[block]++;
}

static void mark_range(check_state_t *state, uint32_t start, uint32_t count,
                       uint8_t type, bool reference)
{
    uint64_t end = (uint64_t)start + count;
    if (start >= state->block_count || end > state->block_count) {
        check_error(state);
        if (start >= state->block_count) return;
        count = state->block_count - start;
    }
    for (uint32_t index = 0; index < count; index++) {
        if (reference) mark_reference(state, start + index, type);
        else mark(state, start + index, type);
    }
}

static void node_cb(bfs_blk_t block, void *context)
{
    mark((check_state_t *)context, block, 2);
}

static void reference_node_cb(bfs_blk_t block, void *context)
{
    mark_reference((check_state_t *)context, block, 2);
}

static bool free_cb(const void *key, const void *value, void *context)
{
    check_state_t *state = (check_state_t *)context;
    mark_range(state, bfs_load_be32(key), bfs_load_be32(value), 1, false);
    return true;
}

static bool dir_cb(const char *name, uint8_t name_length, uint32_t inode,
                   uint32_t type, void *context)
{
    (void)type;
    check_state_t *state = (check_state_t *)context;
    bfs_inode_t node;

    if (name_length == 2 && name[0] == '.' && name[1] == '.') return true;
    if (bfs_inode_read(&state->fs->inode_tree, inode, &node) != BFS_OK)
        check_error(state);
    return true;
}

static bool extent_data_cb(const void *key, const void *value, void *context)
{
    (void)key;
    check_state_t *state = (check_state_t *)context;
    const bfs_extent_val_t *extent = (const bfs_extent_val_t *)value;
    mark_range(state, bfs_be32(extent->disk_block), bfs_be32(extent->length), 3, true);
    return true;
}

static void mark_extent_tree(check_state_t *state, bfs_blk_t root)
{
    if (root == BFS_BLK_NULL) return;

    bfs_extent_tree_t tree;
    if (bfs_extent_init(&tree, state->fs->bio, &state->fs->freespace,
                        root, bfs_txn_id(&state->fs->txn)) != BFS_OK) {
        check_error(state);
        return;
    }
    if (bfs_btree_walk_nodes(&tree.tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    if (bfs_btree_scan(&tree.tree, NULL, extent_data_cb, state) != BFS_OK)
        check_error(state);
}

static bool inode_extent_cb(const void *key, const void *value, void *context)
{
    (void)key;
    check_context_t *check = (check_context_t *)context;
    const bfs_inode_t *inode = (const bfs_inode_t *)value;
    mark_extent_tree(check->state, bfs_be32(inode->extent_root));
    return true;
}

static void mark_inode_payloads(check_state_t *state, bfs_btree_t *tree)
{
    check_context_t context = { .state = state };
    if (bfs_btree_scan(tree, NULL, inode_extent_cb, &context) != BFS_OK)
        check_error(state);
}

static bool snapshot_mark_cb(uint32_t id, const bfs_snapshot_record_t *record, void *context)
{
    (void)id;
    check_state_t *state = (check_state_t *)context;
    uint64_t transaction_id = bfs_snapshot_record_txn_id(record);
    bfs_dir_tree_t dir_tree;
    bfs_btree_t inode_tree;

    if (bfs_dir_init(&dir_tree, state->fs->bio, bfs_freespace_allocator(&state->fs->freespace),
                     bfs_be32(record->dir_tree_root), transaction_id) != BFS_OK ||
        bfs_btree_walk_nodes(&dir_tree.tree, reference_node_cb, state) != BFS_OK) {
        check_error(state);
    }
    if (bfs_inode_init(&inode_tree, state->fs->bio, bfs_freespace_allocator(&state->fs->freespace),
                       bfs_be32(record->inode_tree_root), transaction_id) != BFS_OK) {
        check_error(state);
        return true;
    }
    if (bfs_btree_walk_nodes(&inode_tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    mark_inode_payloads(state, &inode_tree);
    return true;
}

static bool refcount_check_cb(const void *key, const void *value, void *context)
{
    check_state_t *state = (check_state_t *)context;
    bfs_blk_t block = bfs_load_be32(key);
    uint32_t stored = bfs_load_be32(value);

    if (block == BFS_BLK_NULL || block >= state->block_count || stored < 2) {
        check_error(state);
    } else if (state->reference_map[block] <= 1 ||
               (state->reference_map[block] != BFS_REFERENCE_MAX &&
                stored != state->reference_map[block])) {
        check_error(state);
    }
    if (block < state->block_count) state->block_map[block] |= 0x80u;
    return true;
}

static void scan(check_state_t *state)
{
    bfs_fs_t *fs = state->fs;
    const bfs_superblock_t *superblock = &fs->txn.sb;
    uint32_t data_start = bfs_data_start_block(fs->bio->block_size);
    uint64_t backup_offset = ((uint64_t)bfs_be32(superblock->sb_backup_offset_hi) << 32) |
                             bfs_be32(superblock->sb_backup_offset_lo);
    uint64_t backup_block = backup_offset / fs->bio->block_size;

    mark_range(state, 0, data_start, 2, false);
    if (backup_block >= state->block_count) check_error(state);
    else mark(state, (uint32_t)backup_block, 2);
    for (uint32_t index = 0; index < bfs_be32(superblock->emergency_count); index++)
        mark(state, bfs_be32(superblock->emergency_pool[index]), 2);
    for (uint32_t index = 0; index < fs->freespace.reserve_count; index++)
        mark(state, fs->freespace.reserve[index], 2);

    if (bfs_btree_walk_nodes(&fs->freespace.tree, node_cb, state) != BFS_OK)
        check_error(state);
    if (bfs_btree_walk_nodes(&fs->dir_tree.tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    if (bfs_btree_walk_nodes(&fs->inode_tree, reference_node_cb, state) != BFS_OK)
        check_error(state);
    if (fs->has_snapshots &&
        bfs_btree_walk_nodes(&fs->refcount.tree, node_cb, state) != BFS_OK)
        check_error(state);

    bfs_blk_t snapshot_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    if (snapshot_root != BFS_BLK_NULL) {
        bfs_btree_t snapshot_tree;
        if (bfs_btree_init(&snapshot_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                           &snapshot_ops, snapshot_root, bfs_txn_id(&fs->txn)) != BFS_OK ||
            bfs_btree_walk_nodes(&snapshot_tree, node_cb, state) != BFS_OK ||
            bfs_snapshot_list(fs, snapshot_mark_cb, state) != BFS_OK)
            check_error(state);
    }

    if (bfs_btree_scan(&fs->freespace.tree, NULL, free_cb, state) != BFS_OK)
        check_error(state);
    mark_inode_payloads(state, &fs->inode_tree);
    if (bfs_dir_scan(&fs->dir_tree, BFS_ROOT_INO, dir_cb, state) != BFS_OK)
        check_error(state);

    if (fs->has_snapshots) {
        if (bfs_btree_scan(&fs->refcount.tree, NULL, refcount_check_cb, state) != BFS_OK)
            check_error(state);
        for (uint32_t block = 0; block < state->block_count; block++) {
            if (state->reference_map[block] > 1 && !(state->block_map[block] & 0x80u))
                check_error(state);
        }
        if (state->reference_saturated) check_warning(state);
    }
}

bfs_err_t bfs_fs_check(bfs_fs_t *fs, bool repair, bfs_fsck_report_t *report)
{
    check_state_t state;
    if (report) memset(report, 0, sizeof(*report));
    memset(&state, 0, sizeof(state));
    if (!fs || !fs->mounted || !fs->bio || !report) return BFS_ERR_INVAL;
    if (repair && fs->read_only) return BFS_ERR_UNSUPPORTED;

    state.fs = fs;
    state.block_count = fs->bio->block_count;
    state.block_map = calloc(state.block_count, 1);
    state.reference_map = calloc(state.block_count, 1);
    if (!state.block_map || !state.reference_map) {
        free(state.reference_map);
        free(state.block_map);
        return BFS_ERR_NOMEM;
    }

    scan(&state);
    for (uint32_t block = 0; block < state.block_count; block++) {
        if ((state.block_map[block] & 0x7fu) == 0) state.report.leaked_blocks++;
    }
    if (state.report.leaked_blocks) check_warning(&state);

    bfs_err_t result = state.report.errors ? BFS_ERR_CORRUPT : BFS_OK;
    if (repair && result == BFS_OK) {
        for (uint32_t block = 0; block < state.block_count; block++) {
            if ((state.block_map[block] & 0x7fu) != 0) continue;
            result = bfs_freespace_free(&fs->freespace, block, 1);
            if (result != BFS_OK) break;
            state.report.repaired_blocks++;
        }
        if (result == BFS_OK && state.report.repaired_blocks)
            result = bfs_fs_sync(fs);
    }

    *report = state.report;
    free(state.reference_map);
    free(state.block_map);
    return result;
}
