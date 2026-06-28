/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — core-internal declarations (NOT public API).
 *
 * Lock-free primitives shared between core translation units. The caller must
 * already hold fs->lock — the public bfs_*() wrappers in file.c / snapshot.c do.
 * These are kept out of the public headers (bfs_file.h, bfs_snapshot.h) so the
 * Amiga glue and the tools cannot accidentally bypass the filesystem lock.
 */
#ifndef BFS_INTERNAL_H
#define BFS_INTERNAL_H

#include "bfs_file.h"
#include "bfs_snapshot.h"

/* file.c — caller holds fs->lock */
bfs_err_t bfs_file_open_unlocked(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr);
int32_t   bfs_file_read_unlocked(bfs_file_t *f, void *buf, uint32_t len);
int32_t   bfs_file_write_unlocked(bfs_file_t *f, const void *buf, uint32_t len);
bfs_err_t bfs_file_truncate_unlocked(bfs_file_t *f, uint64_t new_size);

/* snapshot.c — caller holds fs->lock */
bfs_err_t bfs_snapshot_create_unlocked(bfs_fs_t *fs, const char *name);
bfs_err_t bfs_snapshot_delete_unlocked(bfs_fs_t *fs, uint32_t snapshot_id);
bfs_err_t bfs_snapshot_list_unlocked(bfs_fs_t *fs, bfs_snapshot_list_cb cb, void *ctx);
bfs_err_t bfs_snapshot_find_by_name_unlocked(bfs_fs_t *fs, const char *name,
                                             uint32_t *id_out,
                                             bfs_snapshot_record_t *rec_out);
uint32_t  bfs_snapshot_next_id_unlocked(bfs_fs_t *fs);

#endif /* BFS_INTERNAL_H */
