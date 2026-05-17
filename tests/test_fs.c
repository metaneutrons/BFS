/*
 * BFS — Filesystem format/mount/unmount tests
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_fs.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096  /* 16MB */

/* ── Test: format and mount ────────────────────────────────── */

static void test_format_mount(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_format(bio, "TestVol", 0), BFS_OK);

    /* Verify superblock is valid */
    bfs_superblock_t sb;
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(sb.magic), BFS_SB_MAGIC);
    TEST_ASSERT_MEM_EQ(sb.volname, "TestVol", 7);
    TEST_ASSERT(bfs_be32(sb.dir_tree_root) != 0);
    TEST_ASSERT(bfs_be32(sb.free_tree_root) != 0);

    /* Mount */
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT(fs.mounted);
    TEST_ASSERT(fs.freespace.total_free > 0);

    /* Root dir should exist */
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, 0, "/", 1, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, BFS_ROOT_INO);
    TEST_ASSERT_EQ(type, BFS_INODE_DIR);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: format, create files, remount, verify ───────────── */

static void test_format_create_remount(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_format(bio, "MyDisk", 0), BFS_OK);

    /* Mount and create some files */
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "readme.txt", 10, 100, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "data.bin", 8, 101, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "subdir", 6, 102, BFS_INODE_DIR), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);

    /* Reopen and verify */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "readme.txt", 10, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 100);
    TEST_ASSERT_EQ(type, BFS_INODE_FILE);

    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "data.bin", 8, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 101);

    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "subdir", 6, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 102);
    TEST_ASSERT_EQ(type, BFS_INODE_DIR);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: crash recovery — uncommitted changes lost ───────── */

static void test_crash_recovery(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_format(bio, "CrashTest", 0), BFS_OK);

    /* Mount and create a file, then sync */
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "saved.txt", 9, 200, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* Create another file but DON'T sync (simulate crash) */
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "lost.txt", 8, 201, BFS_INODE_FILE), BFS_OK);
    /* No sync — just close the bio (simulating power loss) */
    bfs_bio_close(bio);

    /* Reopen — should recover to last committed state */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    uint32_t ino, type;
    /* saved.txt should exist (was synced) */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "saved.txt", 9, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 200);

    /* lost.txt should NOT exist (was not synced) */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "lost.txt", 8, &ino, &type), BFS_ERR_NOTFOUND);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: multiple sync cycles ────────────────────────────── */

static void test_multiple_syncs(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_format(bio, "MultiSync", 0), BFS_OK);

    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Create files across multiple sync cycles */
    for (uint32_t i = 0; i < 50; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%03u", i);
        TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, 300 + i, BFS_INODE_FILE), BFS_OK);

        if (i % 10 == 9)
            TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    }

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);

    /* Reopen and verify all 50 files */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    for (uint32_t i = 0; i < 50; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%03u", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
        TEST_ASSERT_EQ(ino, 300 + i);
    }

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: superblock alternation across syncs ─────────────── */

static void test_superblock_alternation(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    TEST_ASSERT_EQ(bfs_fs_format(bio, "AltTest", 0), BFS_OK);

    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Do several syncs and verify txn_id increases */
    uint64_t prev_txn = 0;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

        bfs_superblock_t sb;
        TEST_ASSERT_EQ(bfs_sb_read(bio, &sb), BFS_OK);
        uint64_t cur_txn = bfs_be64(sb.txn_id);
        TEST_ASSERT(cur_txn > prev_txn);
        prev_txn = cur_txn;
    }

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Filesystem")
    TEST_RUN(test_format_mount);
    TEST_RUN(test_format_create_remount);
    TEST_RUN(test_crash_recovery);
    TEST_RUN(test_multiple_syncs);
    TEST_RUN(test_superblock_alternation);
TEST_SUITE_END()
