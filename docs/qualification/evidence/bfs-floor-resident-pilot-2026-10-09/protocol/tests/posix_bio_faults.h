/* SPDX-License-Identifier: MPL-2.0 */

#ifndef BFS_POSIX_BIO_FAULTS_H
#define BFS_POSIX_BIO_FAULTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    uint32_t pread_eintr;
    uint32_t pwrite_eintr;
    uint32_t fsync_eintr;
    bool pread_partial;
    bool pwrite_partial;
    bool pread_error;
    bool pwrite_error;
    bool fsync_error;
    bool fstat_error;
    bool calloc_error;
    uint32_t pread_calls;
    uint32_t pwrite_calls;
    uint32_t fsync_calls;
    uint32_t close_calls;
} bfs_posix_faults_t;

extern bfs_posix_faults_t bfs_posix_faults;

void bfs_posix_faults_reset(void);
void *bfs_posix_test_calloc(size_t count, size_t size);
int bfs_posix_test_close(int fd);
int bfs_posix_test_fsync(int fd);
int bfs_posix_test_fstat(int fd, struct stat *st);
ssize_t bfs_posix_test_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t bfs_posix_test_pwrite(int fd, const void *buf, size_t count, off_t offset);

#endif /* BFS_POSIX_BIO_FAULTS_H */
