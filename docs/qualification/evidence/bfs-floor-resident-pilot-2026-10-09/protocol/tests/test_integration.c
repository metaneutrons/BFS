/*
 * BFS — Integration tests: end-to-end filesystem operations
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_integration.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192

/* ── Test: full workflow — format, dirs, files, remount ────── */

static void test_full_workflow(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);

    /* Format */
    TEST_ASSERT_EQ(bfs_fs_format(bio, "WorkDisk", 0), BFS_OK);

    /* Mount */
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Create directory structure */
    uint32_t src_ino, docs_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "src", 3, &src_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "docs", 4, &docs_ino), BFS_OK);

    /* Create and write files */
    uint32_t main_ino, readme_ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, src_ino, "main.c", 6, &main_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, docs_ino, "README", 6, &readme_ino), BFS_OK);

    bfs_file_t f;
    bfs_file_open(&f, &fs, main_ino);
    const char *code = "int main(void) { return 0; }\n";
    TEST_ASSERT_EQ(bfs_file_write(&f, code, 29), 29);

    bfs_file_open(&f, &fs, readme_ino);
    const char *readme = "BFS Test Volume\n";
    TEST_ASSERT_EQ(bfs_file_write(&f, readme, 17), 17);

    /* Sync and unmount */
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);

    /* Remount and verify everything */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Verify directories */
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "src", 3, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(type, BFS_INODE_DIR);
    TEST_ASSERT_EQ(ino, src_ino);

    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "docs", 4, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(type, BFS_INODE_DIR);

    /* Verify files exist in correct directories */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, src_ino, "main.c", 6, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(type, BFS_INODE_FILE);

    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, docs_ino, "README", 6, &ino, &type), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: rename across directories then remount ──────────── */

static void test_rename_persist(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "RenTest", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    uint32_t dir_ino, file_ino;
    bfs_fs_mkdir(&fs, BFS_ROOT_INO, "archive", 7, &dir_ino);
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "temp.dat", 8, &file_ino);

    /* Rename file into subdirectory */
    TEST_ASSERT_EQ(bfs_fs_rename(&fs, BFS_ROOT_INO, "temp.dat", 8,
                                       dir_ino, "saved.dat", 9), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount and verify */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "temp.dat", 8, &ino, &type), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, dir_ino, "saved.dat", 9, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, file_ino);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: file data survives remount ──────────────────────── */

static void test_file_data_persist(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "DataTest", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Write a multi-block file with a known pattern */
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "pattern.bin", 11, &ino);

    bfs_file_t f;
    bfs_file_open(&f, &fs, ino);

    uint8_t block[BLK_SIZE];
    for (uint32_t i = 0; i < 10; i++) {
        memset(block, (uint8_t)(i * 25), BLK_SIZE);
        TEST_ASSERT_EQ(bfs_file_write(&f, block, BLK_SIZE), BLK_SIZE);
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount and verify data */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    bfs_file_open(&f, &fs, ino);

    for (uint32_t i = 0; i < 10; i++) {
        TEST_ASSERT_EQ(bfs_file_read(&f, block, BLK_SIZE), BLK_SIZE);
        TEST_ASSERT_EQ(block[0], (uint8_t)(i * 25));
        TEST_ASSERT_EQ(block[BLK_SIZE - 1], (uint8_t)(i * 25));
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: multiple block sizes ────────────────────────────── */

static void test_block_sizes(void)
{
    uint32_t sizes[] = { 1024, 2048, 4096 };

    for (int s = 0; s < 3; s++) {
        uint32_t bs = sizes[s];
        uint32_t bc = 65536 / bs * 1024; /* ~64MB */

        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, bs, bc);
        TEST_ASSERT(bio != NULL);

        TEST_ASSERT_EQ(bfs_fs_format(bio, "BSTest", 0), BFS_OK);

        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "test", 4, &ino), BFS_OK);

        bfs_file_t f;
        bfs_file_open(&f, &fs, ino);
        const char *msg = "block size test";
        TEST_ASSERT_EQ(bfs_file_write(&f, msg, 15), 15);

        bfs_fs_unmount(&fs);
        bfs_bio_close(bio);
        unlink(TEST_IMG);
    }
}

TEST_SUITE_BEGIN("Integration Tests")
    TEST_RUN(test_full_workflow);
    TEST_RUN(test_rename_persist);
    TEST_RUN(test_file_data_persist);
    TEST_RUN(test_block_sizes);
TEST_SUITE_END()
