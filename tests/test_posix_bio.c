/* SPDX-License-Identifier: MPL-2.0 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_posix_bio.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define BLOCK_SIZE 4096u
#define IMAGE_BLOCKS 512u
#define IMAGE_SIZE (IMAGE_BLOCKS * BLOCK_SIZE)

static void make_image(char path[])
{
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT_EQ(ftruncate(fd, IMAGE_SIZE), 0);
    for (uint8_t block = 0; block < 4; block++) {
        uint8_t data[BLOCK_SIZE];
        memset(data, block, sizeof(data));
        TEST_ASSERT_EQ(pwrite(fd, data, sizeof(data), (off_t)block * BLOCK_SIZE),
                       (ssize_t)sizeof(data));
    }
    TEST_ASSERT_EQ(close(fd), 0);
}

static void test_readonly_subrange_tracks_no_write_or_sync(void)
{
    char path[] = "/tmp/bfs-posix-bio.XXXXXX";
    make_image(path);
    bfs_posix_bio_options_t options = {
        .byte_offset = BLOCK_SIZE,
        .byte_length = 2u * BLOCK_SIZE,
        .block_size = BLOCK_SIZE,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(path, &options);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bio->block_count, 2);

    uint8_t data[BLOCK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, 0, data), BFS_OK);
    for (size_t i = 0; i < sizeof(data); i++) TEST_ASSERT_EQ(data[i], 1);
    TEST_ASSERT_EQ(bfs_bio_write(bio, 0, data), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_ERR_INVAL);

    bfs_posix_bio_stats_t stats;
    TEST_ASSERT_EQ(bfs_posix_bio_get_stats(bio, &stats), BFS_OK);
    TEST_ASSERT_EQ(stats.read_calls, 1);
    TEST_ASSERT_EQ(stats.write_calls, 0);
    TEST_ASSERT_EQ(stats.sync_calls, 0);
    bfs_bio_close(bio);
    unlink(path);
}

static void test_writable_subrange_uses_exact_offset(void)
{
    char path[] = "/tmp/bfs-posix-bio.XXXXXX";
    make_image(path);
    bfs_posix_bio_options_t options = {
        .byte_offset = BLOCK_SIZE,
        .byte_length = 2u * BLOCK_SIZE,
        .block_size = BLOCK_SIZE,
        .writable = true,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(path, &options);
    TEST_ASSERT(bio != NULL);
    uint8_t replacement[BLOCK_SIZE];
    memset(replacement, 0xa5, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_bio_write(bio, 1, replacement), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_OK);
    bfs_bio_close(bio);

    int fd = open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0);
    uint8_t data[BLOCK_SIZE];
    TEST_ASSERT_EQ(pread(fd, data, sizeof(data), 2u * BLOCK_SIZE),
                   (ssize_t)sizeof(data));
    for (size_t i = 0; i < sizeof(data); i++) TEST_ASSERT_EQ(data[i], 0xa5);
    TEST_ASSERT_EQ(close(fd), 0);
    unlink(path);
}

static void test_rejects_invalid_range(void)
{
    char path[] = "/tmp/bfs-posix-bio.XXXXXX";
    make_image(path);
    bfs_posix_bio_options_t options = {
        .byte_offset = IMAGE_SIZE + 1u,
        .block_size = BLOCK_SIZE,
    };
    TEST_ASSERT(bfs_posix_bio_open(path, &options) == NULL);
    unlink(path);
}

static void test_lock_rejects_conflicting_alias(void)
{
    char path[] = "/tmp/bfs-posix-bio.XXXXXX";
    char alias[sizeof(path) + 8];
    make_image(path);
    TEST_ASSERT(snprintf(alias, sizeof(alias), "%s.alias", path) > 0);
    TEST_ASSERT_EQ(symlink(path, alias), 0);

    bfs_posix_bio_options_t readonly = {
        .block_size = BLOCK_SIZE,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(path, &readonly);
    TEST_ASSERT(bio != NULL);

    pid_t child = fork();
    TEST_ASSERT(child >= 0);
    if (child == 0) {
        bfs_posix_bio_options_t writable = {
            .block_size = BLOCK_SIZE,
            .writable = true,
            .lock = true,
        };
        bfs_bio_t *conflict = bfs_posix_bio_open(alias, &writable);
        if (conflict) {
            bfs_bio_close(conflict);
            _exit(1);
        }
        _exit(0);
    }
    int status;
    TEST_ASSERT_EQ(waitpid(child, &status, 0), child);
    TEST_ASSERT(WIFEXITED(status));
    TEST_ASSERT_EQ(WEXITSTATUS(status), 0);
    bfs_bio_close(bio);
    unlink(alias);
    unlink(path);
}

static void test_readonly_bfs_lifecycle_uses_no_write_or_sync(void)
{
    char path[] = "/tmp/bfs-posix-bio.XXXXXX";
    make_image(path);
    bfs_posix_bio_options_t writable = {
        .block_size = BLOCK_SIZE,
        .writable = true,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(path, &writable);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "PosixRO", 0), BFS_OK);
    bfs_bio_close(bio);

    bfs_posix_bio_options_t readonly = {
        .block_size = BLOCK_SIZE,
        .lock = true,
    };
    bio = bfs_posix_bio_open(path, &readonly);
    TEST_ASSERT(bio != NULL);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_posix_bio_stats_t stats;
    TEST_ASSERT_EQ(bfs_posix_bio_get_stats(bio, &stats), BFS_OK);
    TEST_ASSERT(stats.read_calls > 0);
    TEST_ASSERT_EQ(stats.write_calls, 0);
    TEST_ASSERT_EQ(stats.sync_calls, 0);
    bfs_bio_close(bio);
    unlink(path);
}

TEST_SUITE_BEGIN("POSIX Block Transport")
    TEST_RUN(test_readonly_subrange_tracks_no_write_or_sync);
    TEST_RUN(test_writable_subrange_uses_exact_offset);
    TEST_RUN(test_rejects_invalid_range);
    TEST_RUN(test_lock_rejects_conflicting_alias);
    TEST_RUN(test_readonly_bfs_lifecycle_uses_no_write_or_sync);
TEST_SUITE_END()
