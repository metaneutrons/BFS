/* SPDX-License-Identifier: MPL-2.0 */
/* Shared POSIX-image opening for host BFS administration commands. */

#ifndef BFS_HOST_COMMON_H
#define BFS_HOST_COMMON_H

#include "bfs_bio.h"
#include "bfs_diagnostics.h"

bfs_bio_t *bfs_host_open_bfs_image(const char *path, bool writable,
                                    bfs_err_t *error,
                                    char diagnostic[BFS_FORMAT_ERROR_MAX]);

#endif /* BFS_HOST_COMMON_H */
