/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Snapshot management
 *
 * Snapshots are frozen point-in-time copies of the filesystem tree roots.
 * They share blocks with the live filesystem via reference counting.
 * Creating a snapshot is O(1) — just saves the current roots.
 * Deleting a snapshot is O(n) — must walk and decrement refcounts.
 */

#ifndef BFS_SNAPSHOT_H
#define BFS_SNAPSHOT_H

#include "bfs_fs.h"

#define BFS_SNAPSHOT_NAME_MAX 32

/* On-disk snapshot record (stored as value in snapshot B+tree) */
typedef struct BFS_PACKED {
    uint32_t dir_tree_root;
    uint32_t inode_tree_root;
    uint32_t txn_id_hi;
    uint32_t txn_id_lo;
    uint32_t timestamp;          /* AmigaOS DateStamp days since 1978 */
    uint8_t  name[BFS_SNAPSHOT_NAME_MAX];
} bfs_snapshot_record_t;

/* Create a snapshot of the current filesystem state. */
bfs_err_t bfs_snapshot_create(bfs_fs_t *fs, const char *name);

/* Delete a snapshot by ID. Decrements refcounts and frees unreferenced blocks. */
bfs_err_t bfs_snapshot_delete(bfs_fs_t *fs, uint32_t snapshot_id);

/* Callback for listing snapshots. Return false to stop iteration. */
typedef bool (*bfs_snapshot_list_cb)(uint32_t id, const bfs_snapshot_record_t *rec, void *ctx);

/* List all snapshots. */
bfs_err_t bfs_snapshot_list(bfs_fs_t *fs, bfs_snapshot_list_cb cb, void *ctx);

/* Find a snapshot by name. */
bfs_err_t bfs_snapshot_find_by_name(bfs_fs_t *fs, const char *name,
                                      uint32_t *id_out,
                                      bfs_snapshot_record_t *rec_out);

/* Get the next available snapshot ID. */
uint32_t bfs_snapshot_next_id(bfs_fs_t *fs);

/* Reconstruct the 64-bit transaction id stored (as hi/lo halves) in a record. */
uint64_t bfs_snapshot_record_txn_id(const bfs_snapshot_record_t *rec);

/* Open a snapshot's read-only directory and inode trees from its record. Either
 * out pointer may be NULL; `alloc` is attached to the trees (a read-only/no-op
 * allocator for a mounted snapshot, or the live freespace allocator for internal
 * traversal). Keeps the record's on-disk layout out of the Amiga glue. */
bfs_err_t bfs_snapshot_open(const bfs_snapshot_record_t *rec, bfs_bio_t *bio,
                            bfs_allocator_t *alloc,
                            bfs_dir_tree_t *dir_out, bfs_btree_t *inode_out);

/* Resume any interrupted snapshot deletions. */
bfs_err_t bfs_snapshot_resume_deletions(bfs_fs_t *fs);

#endif /* BFS_SNAPSHOT_H */
