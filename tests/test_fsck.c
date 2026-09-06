/* SPDX-License-Identifier: MPL-2.0 */
#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_snapshot.h"
#include "bfs_crc32.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define IMAGE "test_fsck.img"

static int run_fsck(void)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        int output = open("test_fsck.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output < 0) _exit(126);
        if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0)
            _exit(126);
        close(output);
        execl("./bfsfsck", "bfsfsck", IMAGE, (char *)NULL);
        _exit(127);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static void test_clean_snapshot_and_readonly_check(void)
{
    unlink(IMAGE);
    bfs_bio_t *bio = bio_emu_create(IMAGE, 4096, 1024);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "Fsck", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&file, "shared", 6), 6);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "point"), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);

    uint8_t *before = malloc((size_t)bio->block_count * bio->block_size);
    TEST_ASSERT(before != NULL);
    for (uint32_t block = 0; block < bio->block_count; block++)
        TEST_ASSERT_EQ(bfs_bio_read(bio, block, before + (size_t)block * bio->block_size), BFS_OK);
    TEST_ASSERT_EQ(run_fsck(), 0);
    uint8_t after[4096];
    for (uint32_t block = 0; block < bio->block_count; block++) {
        TEST_ASSERT_EQ(bfs_bio_read(bio, block, after), BFS_OK);
        TEST_ASSERT_MEM_EQ(after, before + (size_t)block * bio->block_size, sizeof(after));
    }
    free(before);

    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    bfs_blk_t data;
    TEST_ASSERT_EQ(bfs_extent_lookup(&file.extents, 0, &data), BFS_OK);
    TEST_ASSERT_EQ(bfs_refcount_inc(&fs.refcount, data), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(run_fsck(), 2);
    bfs_bio_close(bio);
    unlink(IMAGE);
    unlink("test_fsck.log");
}

TEST_SUITE_BEGIN("Filesystem Checker")
    TEST_RUN(test_clean_snapshot_and_readonly_check);
TEST_SUITE_END()
