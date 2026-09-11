/* SPDX-License-Identifier: MPL-2.0 */
/* Portable structural checker for a mounted BFS filesystem. */

#ifndef BFS_FSCK_H
#define BFS_FSCK_H

#include "bfs_fs.h"

typedef struct {
    uint32_t errors;
    uint32_t warnings;
    uint32_t leaked_blocks;
    uint32_t repaired_blocks;
} bfs_fsck_report_t;

/*
 * Scan every metadata tree, file extent and snapshot root reachable from fs.
 * The default mode is strictly read-only. With repair=true, the function only
 * rebuilds free-space ownership for otherwise structurally clean leaked
 * blocks; callers must provide a writable, exclusively-owned mount.
 *
 * Returns BFS_ERR_CORRUPT when the scan found structural errors. Warnings are
 * reported in report and do not change the return value in read-only mode.
 */
bfs_err_t bfs_fs_check(bfs_fs_t *fs, bool repair, bfs_fsck_report_t *report);

#endif /* BFS_FSCK_H */
