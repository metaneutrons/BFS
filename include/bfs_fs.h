/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Top-level filesystem handle
 *
 * Ties together all subsystems: block I/O, free space allocator,
 * directory tree, extent trees, and transaction manager.
 *
 * Usage:
 *   bfs_fs_format()  — create a fresh filesystem on a device
 *   bfs_fs_mount()   — open an existing filesystem
 *   bfs_fs_sync()    — commit current state to disk
 *   bfs_fs_unmount() — sync and close
 */

#ifndef BFS_FS_H
#define BFS_FS_H

#include "bfs_bio.h"
#include "bfs_alloc.h"
#include "bfs_dir.h"
#include "bfs_extent.h"
#include "bfs_txn.h"
#include "bfs_inode.h"
#include "bfs_refcount.h"
#include "bfs_lock.h"

#define BFS_ROOT_INO 1  /* root directory inode number */

/* Inline capacity of the per-transaction deferred-free queue. Large atomic
 * reclaim units reserve a heap buffer before mutation. Old
 * COW'd blocks park here until the next commit drains them back to the
 * allocator. The fs reserves headroom at safe entry points
 * (bfs_fs_ensure_free_headroom) so an in-COW defer() can never overflow; an
 * overflow would otherwise drop a block, leaking it until offline fsck — it is
 * NOT reclaimed automatically. */
#define BFS_PENDING_FREES_MAX 16384

/* Worst-case deferred frees one top-level metadata op can produce: at most ~5
 * trees touched (dir, inode, freespace, refcount, plus rename's second dir
 * subtree), each bounded by BFS_BTREE_MAX_OP_FREES. Reserved up front so the
 * op's in-COW defers never overflow the queue. */
#define BFS_FS_OP_FREE_RESERVE (5u * BFS_BTREE_MAX_OP_FREES)

typedef struct bfs_fs {
    bfs_bio_t        *bio;
    bfs_txn_t         txn;
    bfs_freespace_t   freespace;
    bfs_dir_tree_t    dir_tree;
    bfs_btree_t       inode_tree;  /* inode B+tree: key=inode_nr(u32), value=bfs_inode_t */
    bfs_refcount_t    refcount;    /* block refcount tree (for snapshots) */
    uint32_t           next_ino;   /* next inode number to allocate */
    uint32_t           options;    /* BFS_OPT_* flags */
    bool               mounted;
    // cppcheck-suppress unusedStructMember
    bool               read_only;  /* lifecycle forbids recovery and commits */
    bfs_err_t          recovery_error; /* nonzero: abandon/remount required */
    /* Shared between fs.c recovery and file.c handle validation. */
    // cppcheck-suppress unusedStructMember
    uint64_t           recovery_generation; /* invalidates open handles on reload */
    bool               data_checksums; /* BFS_OPT_DATA_CHECKSUMS enabled */
    /* Consulted by snapshot.c, file.c and txn.c, not just assigned by mount. */
    // cppcheck-suppress unusedStructMember
    bool               has_snapshots;  /* snapshot_tree_root != 0 */
    uint64_t           live_txn_id;    /* current transaction id (host order) */
    bfs_blk_t         pending_frees[BFS_PENDING_FREES_MAX];
    bfs_blk_t        *pending_frees_dynamic; /* large atomic reclaim units */
    uint32_t           pending_count;
    uint32_t           pending_frees_cap; /* allocated capacity; tests may lower it */
    uint8_t           *scratch;        /* pre-allocated block buffer for file I/O */
    bfs_fs_lock_t         lock;
} bfs_fs_t;

/* Effective queue capacity, including optional growth. Tests lower the inline
 * cap to exercise hard-limit failures. Zero means the inline capacity. */
static inline uint32_t bfs_fs_pending_cap(const bfs_fs_t *fs)
{
    return fs->pending_frees_cap ? fs->pending_frees_cap : BFS_PENDING_FREES_MAX;
}

static inline bfs_blk_t *bfs_fs_pending_items(bfs_fs_t *fs)
{
    return fs->pending_frees_dynamic ? fs->pending_frees_dynamic : fs->pending_frees;
}

/* Format a fresh BFS filesystem.
 * block_size: 1024..65536, power of 2
 * volname: null-terminated, max 31 chars */
bfs_err_t bfs_fs_format(bfs_bio_t *bio, const char *volname, uint32_t options);

/* Mount an existing BFS filesystem. */
bfs_err_t bfs_fs_mount(bfs_fs_t *fs, bfs_bio_t *bio);

/* Mount an existing committed BFS state without any write-side recovery or
 * commit. Close it with bfs_fs_unmount(), which only releases resources for a
 * read-only filesystem. */
bfs_err_t bfs_fs_mount_readonly(bfs_fs_t *fs, bfs_bio_t *bio);

/* Sync: commit all pending changes to disk (the full transaction commit lives in
 * txn.c as bfs_txn_commit(fs); this is just the public, lock-taking wrapper). */
bfs_err_t bfs_fs_sync(bfs_fs_t *fs);

/* Defer-free a block for reclaim at the next sync. Callers must reserve queue
 * headroom at a safe operation boundary; a full queue returns BFS_ERR_AGAIN
 * and is never drained from inside a multi-step mutation. Returns
 * BFS_ERR_UNSUPPORTED on a read-only mount. */
bfs_err_t bfs_fs_queue_pending_free(bfs_fs_t *fs, bfs_blk_t blk);

/* The deferred-free sink for this filesystem (its pending-free queue), to attach
 * to a B+tree (tree.free_sink) so the tree's COW'd blocks are reclaimed at the
 * next commit without the engine knowing anything about bfs_fs_t. */
bfs_free_sink_t bfs_fs_free_sink(bfs_fs_t *fs);

/* Ensure the deferred-free queue has room for `slots` more entries before a
 * mutation that may defer that many old blocks. Commits (draining the queue) if
 * short; returns BFS_ERR_NOSPC if still short afterwards. The caller MUST hold
 * fs->lock write and must NOT be mid-tree-mutation — this may commit. */
bfs_err_t bfs_fs_ensure_free_headroom(bfs_fs_t *fs, uint32_t slots);

/* Unmount: sync and release resources. */
bfs_err_t bfs_fs_unmount(bfs_fs_t *fs);

/* Drop a mounted in-memory state without writing it. This is only for media
 * loss/change and crash simulation; normal callers must use bfs_fs_unmount(). */
void bfs_fs_abandon(bfs_fs_t *fs);

/* ── Directory operations ──────────────────────────────────── */

/* Next inode number (simple counter stored in fs) */
uint32_t bfs_fs_alloc_ino(bfs_fs_t *fs);

/* Create a file in a directory. Returns inode number. */
bfs_err_t bfs_fs_create_file(bfs_fs_t *fs, uint32_t parent_ino,
                               const char *name, uint8_t name_len,
                               uint32_t *ino_out);

/* Create a subdirectory. Returns inode number. */
bfs_err_t bfs_fs_mkdir(bfs_fs_t *fs, uint32_t parent_ino,
                         const char *name, uint8_t name_len,
                         uint32_t *ino_out);

/* Delete a file (frees extents). */
bfs_err_t bfs_fs_delete_file(bfs_fs_t *fs, uint32_t parent_ino,
                               const char *name, uint8_t name_len);

/* Remove an empty directory. */
bfs_err_t bfs_fs_rmdir(bfs_fs_t *fs, uint32_t parent_ino,
                         const char *name, uint8_t name_len);

/* Rename/move a directory entry. */
bfs_err_t bfs_fs_rename(bfs_fs_t *fs,
                          uint32_t old_parent, const char *old_name, uint8_t old_len,
                          uint32_t new_parent, const char *new_name, uint8_t new_len);

/* Create a hard link to an existing inode. */
bfs_err_t bfs_fs_make_hardlink(bfs_fs_t *fs, uint32_t parent_ino,
                                 const char *name, uint8_t name_len,
                                 uint32_t target_ino);

/* Create a soft link (symlink). */
bfs_err_t bfs_fs_make_softlink(bfs_fs_t *fs, uint32_t parent_ino,
                                 const char *name, uint8_t name_len,
                                 const char *target_path, uint16_t path_len);

/* Set/get file comment (up to 79 chars). */
bfs_err_t bfs_fs_set_comment(bfs_fs_t *fs, uint32_t ino,
                               const char *comment, uint8_t len);
bfs_err_t bfs_fs_get_comment(bfs_fs_t *fs, uint32_t ino,
                               char *buf, uint8_t max_len);

/* Metadata space reservation (ENOSPC prevention).
 * Call before any metadata operation. Returns BFS_ERR_NOSPC if not enough
 * space. Each "item" = worst-case blocks for one tree modification. */
bfs_err_t bfs_fs_reserve(bfs_fs_t *fs, uint32_t items);
void      bfs_fs_unreserve(bfs_fs_t *fs, uint32_t items);

/* Compact a filesystem B+tree (rebuild it densely). Builds a fresh tree, swaps
 * the root, commits to make the swap durable, then frees the now-unreferenced
 * old nodes post-commit — so the old-tree mass-free can never overflow the
 * deferred-free queue mid-COW. Takes fs->lock. The only compaction entry point
 * for an fs tree. Returns BFS_ERR_UNSUPPORTED on a read-only mount. */
bfs_err_t bfs_fs_compact_tree(bfs_fs_t *fs, bfs_btree_t *tree);

#endif /* BFS_FS_H */
