/* SPDX-License-Identifier: MPL-2.0 */

#include "posix_bio_faults.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bfs_posix_faults_t bfs_posix_faults;

void bfs_posix_faults_reset(void)
{
    memset(&bfs_posix_faults, 0, sizeof(bfs_posix_faults));
}

void *bfs_posix_test_calloc(size_t count, size_t size)
{
    if (bfs_posix_faults.calloc_error) {
        errno = ENOMEM;
        return NULL;
    }
    return calloc(count, size);
}

int bfs_posix_test_close(int fd)
{
    bfs_posix_faults.close_calls++;
    return close(fd);
}

int bfs_posix_test_fsync(int fd)
{
    bfs_posix_faults.fsync_calls++;
    if (bfs_posix_faults.fsync_eintr != 0) {
        bfs_posix_faults.fsync_eintr--;
        errno = EINTR;
        return -1;
    }
    if (bfs_posix_faults.fsync_error) {
        errno = EIO;
        return -1;
    }
    return fsync(fd);
}

int bfs_posix_test_fstat(int fd, struct stat *st)
{
    if (bfs_posix_faults.fstat_error) {
        errno = EIO;
        return -1;
    }
    return fstat(fd, st);
}

ssize_t bfs_posix_test_pread(int fd, void *buf, size_t count, off_t offset)
{
    bfs_posix_faults.pread_calls++;
    if (bfs_posix_faults.pread_eintr != 0) {
        bfs_posix_faults.pread_eintr--;
        errno = EINTR;
        return -1;
    }
    if (bfs_posix_faults.pread_error) {
        errno = EIO;
        return -1;
    }
    ssize_t result = pread(fd, buf, count, offset);
    if (result > 1 && bfs_posix_faults.pread_partial) {
        bfs_posix_faults.pread_partial = false;
        return result - 1;
    }
    return result;
}

ssize_t bfs_posix_test_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    bfs_posix_faults.pwrite_calls++;
    if (bfs_posix_faults.pwrite_eintr != 0) {
        bfs_posix_faults.pwrite_eintr--;
        errno = EINTR;
        return -1;
    }
    if (bfs_posix_faults.pwrite_error) {
        errno = EIO;
        return -1;
    }
    ssize_t result = pwrite(fd, buf, count, offset);
    if (result > 1 && bfs_posix_faults.pwrite_partial) {
        bfs_posix_faults.pwrite_partial = false;
        return result - 1;
    }
    return result;
}
