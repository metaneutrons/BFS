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

static int run_fsck(bool fix)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        int output = open("test_fsck.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output < 0) _exit(126);
        if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0)
            _exit(126);
        close(output);
        /* Fixed executable/arguments in the test build directory, with no shell. */
        execl("./bfsfsck", "bfsfsck", IMAGE, fix ? "--fix" : (char *)NULL, /* Flawfinder: ignore */
              (char *)NULL);
        _exit(127);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static int run_bfs_check(bool repair)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        int output = open("test_fsck.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output < 0) _exit(126);
        if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0)
            _exit(126);
        close(output);
        execl("./bfs", "bfs", "check", IMAGE, repair ? "--repair" : (char *)NULL,
              (char *)NULL); /* Flawfinder: ignore */
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
    TEST_ASSERT_EQ(run_bfs_check(false), 0);
    uint8_t after[4096];
    for (uint32_t block = 0; block < bio->block_count; block++) {
        TEST_ASSERT_EQ(bfs_bio_read(bio, block, after), BFS_OK);
        TEST_ASSERT_MEM_EQ(after, before + (size_t)block * bio->block_size, sizeof(after));
    }
    TEST_ASSERT_EQ(run_bfs_check(true), 0);
    for (uint32_t block = 0; block < bio->block_count; block++) {
        TEST_ASSERT_EQ(bfs_bio_read(bio, block, after), BFS_OK);
        TEST_ASSERT_MEM_EQ(after, before + (size_t)block * bio->block_size, sizeof(after));
    }
    TEST_ASSERT_EQ(run_fsck(false), 0);
    free(before);

    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    bfs_blk_t data;
    TEST_ASSERT_EQ(bfs_extent_lookup(&file.extents, 0, &data), BFS_OK);
    TEST_ASSERT_EQ(bfs_refcount_inc(&fs.refcount, data), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(run_bfs_check(false), 2);
    TEST_ASSERT_EQ(run_fsck(false), 2);
    bfs_bio_close(bio);
    unlink(IMAGE);
    unlink("test_fsck.log");
}

static void test_unsupported_format_never_repaired(void)
{
    for (unsigned option = 0; option < 2; option++) {
        for (unsigned slot = 0; slot < 2; slot++) {
            unlink(IMAGE);
            bfs_bio_t *bio = bio_emu_create(IMAGE, 4096, 256);
            TEST_ASSERT(bio != NULL);
            TEST_ASSERT_EQ(bfs_fs_format(bio, "Future", 0), BFS_OK);
            bfs_superblock_t sb;
            TEST_ASSERT_EQ(bfs_sb_read(bio, &sb), BFS_OK);
            if (option) sb.options = bfs_be32(0x80000000u);
            else sb.version = bfs_be32(BFS_SB_VERSION + 1);
            sb.crc32 = bfs_be32(bfs_sb_compute_crc(&sb));
            uint64_t offset = slot ? bfs_default_backup_offset(256, 4096) : 0;
            TEST_ASSERT_EQ(bfs_sb_write_raw(bio, offset, &sb), BFS_OK);
            TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_OK);
            uint8_t *before = malloc(4096 * 256);
            TEST_ASSERT(before != NULL);
            for (unsigned block = 0; block < 256; block++)
                TEST_ASSERT_EQ(bfs_bio_read(bio, block, before + block * 4096), BFS_OK);
            TEST_ASSERT_EQ(run_fsck(false), 1);
            TEST_ASSERT_EQ(run_fsck(true), 1);
            FILE *log = fopen("test_fsck.log", "r");
            TEST_ASSERT(log != NULL);
            char message[256];
            TEST_ASSERT(fgets(message, sizeof(message), log) != NULL);
            TEST_ASSERT_EQ(fclose(log), 0);
            TEST_ASSERT(strstr(message, option ? "version 2 uses unsupported options 0x80000000" :
                                                "version 3 is too new") != NULL);
            uint8_t after[4096];
            for (unsigned block = 0; block < 256; block++) {
                TEST_ASSERT_EQ(bfs_bio_read(bio, block, after), BFS_OK);
                TEST_ASSERT_MEM_EQ(after, before + block * 4096, sizeof(after));
            }
            free(before);
            bfs_bio_close(bio);
            unlink(IMAGE);
            unlink("test_fsck.log");
        }
    }
}

static void test_retained_open_inode_is_checker_visible_until_recovery(void)
{
    unlink(IMAGE);
    bfs_bio_t *bio = bio_emu_create(IMAGE, 4096, 1024);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "FsckOrphan", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino, orphan;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "open", 4, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&file, "retained", 8), 8);
    TEST_ASSERT_EQ(bfs_fs_unlink_open_file(&fs, BFS_ROOT_INO, "open", 4, &orphan), BFS_OK);
    TEST_ASSERT_EQ(orphan, ino);
    TEST_ASSERT_EQ(bfs_file_mark_unlinked(&file), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* A read-only check must keep ownership of the retained inode's extents. */
    bfs_fs_abandon(&fs);
    TEST_ASSERT_EQ(run_fsck(false), 0);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(IMAGE);
    unlink("test_fsck.log");
}

static void test_canonical_repair_reclaims_only_leaks(void)
{
    unlink(IMAGE);
    bfs_bio_t *bio = bio_emu_create(IMAGE, 4096, 1024);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "Repair", 0), BFS_OK);

    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT(bfs_freespace_alloc(&fs.freespace, 1) != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);

    TEST_ASSERT_EQ(run_bfs_check(false), 1);
    TEST_ASSERT_EQ(run_bfs_check(true), 1);
    TEST_ASSERT_EQ(run_bfs_check(false), 0);

    bfs_bio_close(bio);
    unlink(IMAGE);
    unlink("test_fsck.log");
}

TEST_SUITE_BEGIN("Filesystem Checker")
    TEST_RUN(test_clean_snapshot_and_readonly_check);
    TEST_RUN(test_unsupported_format_never_repaired);
    TEST_RUN(test_retained_open_inode_is_checker_visible_until_recovery);
    TEST_RUN(test_canonical_repair_reclaims_only_leaks);
TEST_SUITE_END()
