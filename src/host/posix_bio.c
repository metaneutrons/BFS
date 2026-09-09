/* SPDX-License-Identifier: MPL-2.0 */

#include "bfs_posix_bio.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif

typedef struct {
    bfs_bio_t base;
    int fd;
    uint64_t byte_offset;
    uint64_t byte_length;
    bool writable;
    bool locked;
    bfs_posix_bio_stats_t stats;
} posix_bio_t;

static bool add_overflows(uint64_t a, uint64_t b)
{
    return b > UINT64_MAX - a;
}

static bool offset_valid(const posix_bio_t *bio, bfs_blk_t block, uint64_t *offset_out)
{
    uint64_t relative = (uint64_t)block * bio->base.block_size;
    if (block >= bio->base.block_count || relative > bio->byte_length ||
        bio->base.block_size > bio->byte_length - relative ||
        add_overflows(bio->byte_offset, relative))
        return false;
    uint64_t offset = bio->byte_offset + relative;
    if (offset > (uint64_t)INT64_MAX) return false;
    *offset_out = offset;
    return true;
}

static bfs_err_t transfer_exact(int fd, void *buf, size_t length, uint64_t offset, bool write)
{
    size_t done = 0;
    while (done < length) {
        uint64_t current = offset + done;
        if (current > (uint64_t)INT64_MAX) return BFS_ERR_OVERFLOW;
        ssize_t count = write
            ? pwrite(fd, (const uint8_t *)buf + done, length - done, (off_t)current)
            : pread(fd, (uint8_t *)buf + done, length - done, (off_t)current);
        if (count < 0) {
            if (errno == EINTR) continue;
            return BFS_ERR_IO;
        }
        if (count == 0) return BFS_ERR_IO;
        done += (size_t)count;
    }
    return BFS_OK;
}

static bfs_err_t posix_read(bfs_bio_t *base, bfs_blk_t block, void *buf)
{
    posix_bio_t *bio = (posix_bio_t *)base;
    uint64_t offset;
    if (!buf || !offset_valid(bio, block, &offset)) return BFS_ERR_INVAL;
    bio->stats.read_calls++;
    return transfer_exact(bio->fd, buf, base->block_size, offset, false);
}

static bfs_err_t posix_write(bfs_bio_t *base, bfs_blk_t block, const void *buf)
{
    posix_bio_t *bio = (posix_bio_t *)base;
    uint64_t offset;
    if (!bio->writable || !buf || !offset_valid(bio, block, &offset)) return BFS_ERR_INVAL;
    bio->stats.write_calls++;
    return transfer_exact(bio->fd, (void *)buf, base->block_size, offset, true);
}

static bfs_err_t posix_sync(bfs_bio_t *base)
{
    posix_bio_t *bio = (posix_bio_t *)base;
    if (!bio->writable) return BFS_ERR_UNSUPPORTED;
    bio->stats.sync_calls++;
    while (fsync(bio->fd) != 0) {
        if (errno != EINTR) return BFS_ERR_IO;
    }
    return BFS_OK;
}

static void posix_close(bfs_bio_t *base)
{
    posix_bio_t *bio = (posix_bio_t *)base;
    if (bio->fd >= 0) {
        if (bio->locked) {
            struct flock lock = { .l_type = F_UNLCK, .l_whence = SEEK_SET };
            (void)fcntl(bio->fd, F_SETLK, &lock);
        }
        (void)close(bio->fd);
    }
    free(bio);
}

static const bfs_bio_ops_t posix_readonly_ops = {
    .read_block = posix_read,
    .close = posix_close,
};

static const bfs_bio_ops_t posix_writable_ops = {
    .read_block = posix_read,
    .write_block = posix_write,
    .sync = posix_sync,
    .close = posix_close,
};

static bool backing_size(int fd, const struct stat *st, uint64_t *size_out)
{
    if (S_ISREG(st->st_mode)) {
        if (st->st_size < 0) return false;
        *size_out = (uint64_t)st->st_size;
        return true;
    }
#if defined(__linux__)
    if (S_ISBLK(st->st_mode)) {
        unsigned long long bytes = 0;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) return false;
        *size_out = bytes;
        return true;
    }
#else
    (void)fd;
#endif
    return false;
}

bfs_bio_t *bfs_posix_bio_open(const char *path, const bfs_posix_bio_options_t *options)
{
    if (!path || !options || !bfs_block_size_valid(options->block_size)) {
        errno = EINVAL;
        return NULL;
    }
    int flags = (options->writable ? O_RDWR : O_RDONLY) | O_CLOEXEC;
    int fd;
    do {
        fd = open(path, flags);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) return NULL;

    struct stat st;
    uint64_t backing_bytes;
    if (fstat(fd, &st) != 0 || !backing_size(fd, &st, &backing_bytes) ||
        options->byte_offset > backing_bytes) {
        (void)close(fd);
        errno = EINVAL;
        return NULL;
    }
    /* Raw-device writes need a platform mount-table check in addition to an
     * advisory lock. Until that policy exists, accept write mode for explicit
     * regular images only. */
    if (options->writable && S_ISBLK(st.st_mode)) {
        (void)close(fd);
        errno = EPERM;
        return NULL;
    }
    uint64_t available = backing_bytes - options->byte_offset;
    uint64_t selected = options->byte_length ? options->byte_length : available;
    if (selected == 0 || selected > available || selected < options->block_size) {
        (void)close(fd);
        errno = EINVAL;
        return NULL;
    }

    bool locked = false;
    if (options->lock) {
        struct flock lock = {
            .l_type = options->writable ? F_WRLCK : F_RDLCK,
            .l_whence = SEEK_SET,
            .l_start = 0,
            .l_len = 0,
        };
        if (fcntl(fd, F_SETLK, &lock) != 0) {
            (void)close(fd);
            return NULL;
        }
        locked = true;
    }

    posix_bio_t *bio = calloc(1, sizeof(*bio));
    if (!bio) {
        if (locked) {
            struct flock lock = { .l_type = F_UNLCK, .l_whence = SEEK_SET };
            (void)fcntl(fd, F_SETLK, &lock);
        }
        (void)close(fd);
        return NULL;
    }
    uint64_t blocks = selected / options->block_size;
    if (blocks > UINT32_MAX) {
        free(bio);
        if (locked) {
            struct flock lock = { .l_type = F_UNLCK, .l_whence = SEEK_SET };
            (void)fcntl(fd, F_SETLK, &lock);
        }
        (void)close(fd);
        errno = EOVERFLOW;
        return NULL;
    }
    bio->base.ops = options->writable ? &posix_writable_ops : &posix_readonly_ops;
    bio->base.block_size = options->block_size;
    bio->base.block_count = (bfs_blk_t)blocks;
    bio->fd = fd;
    bio->byte_offset = options->byte_offset;
    bio->byte_length = selected;
    bio->writable = options->writable;
    bio->locked = locked;
    return &bio->base;
}

bfs_err_t bfs_posix_bio_get_stats(const bfs_bio_t *base, bfs_posix_bio_stats_t *stats)
{
    if (!base || !stats || (base->ops != &posix_readonly_ops &&
                           base->ops != &posix_writable_ops))
        return BFS_ERR_INVAL;
    *stats = ((const posix_bio_t *)base)->stats;
    return BFS_OK;
}

bfs_err_t bfs_posix_bio_get_range(const bfs_bio_t *base, uint64_t *byte_offset,
                                   uint64_t *byte_length)
{
    if (!base || !byte_offset || !byte_length ||
        (base->ops != &posix_readonly_ops && base->ops != &posix_writable_ops))
        return BFS_ERR_INVAL;
    const posix_bio_t *bio = (const posix_bio_t *)base;
    *byte_offset = bio->byte_offset;
    *byte_length = bio->byte_length;
    return BFS_OK;
}
