/*
 * BFS — Stress tests
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_stress.img"
#define BLK_SIZE 4096
#define BLK_COUNT 32768  /* 128MB */

static bfs_fs_t g_fs;

static bfs_fs_t *setup(const char *vol, uint32_t blk_count)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, blk_count);
    bfs_fs_format(bio, vol, 0);
    bfs_fs_mount(&g_fs, bio);
    return &g_fs;
}

static void teardown(bfs_fs_t *fs)
{
    bfs_bio_t *bio = fs->bio;
    bfs_fs_unmount(fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: large directory (2000 files) ────────────────────── */

static void test_large_directory(void)
{
    bfs_fs_t *fs = setup("LargeDir", BLK_COUNT);

    for (uint32_t i = 0; i < 2000; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%05u.dat", i);
        uint32_t ino;
        bfs_err_t err = bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
        TEST_ASSERT_EQ(err, BFS_OK);
    }

    /* Verify random lookups */
    for (uint32_t i = 0; i < 2000; i += 73) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%05u.dat", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    /* Delete half */
    for (uint32_t i = 0; i < 2000; i += 2) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%05u.dat", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    /* Verify remaining */
    for (uint32_t i = 1; i < 2000; i += 2) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%05u.dat", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    teardown(fs);
}

/* ── Test: disk full handling ──────────────────────────────── */

static void test_disk_full(void)
{
    /* Small disk: 256 blocks = 1MB */
    bfs_fs_t *fs = setup("DiskFull", 512);

    bfs_file_t f;
    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "bigfile", 7, &ino);
    bfs_file_open(&f, fs, ino);

    /* Write until disk is full */
    uint8_t data[BLK_SIZE];
    memset(data, 0xAA, BLK_SIZE);
    uint32_t blocks_written = 0;
    while (bfs_file_write(&f, data, BLK_SIZE) == BLK_SIZE)
        blocks_written++;

    /* Should have written some blocks before failing */
    TEST_ASSERT(blocks_written > 0);

    /* Filesystem should still be usable after disk-full */
    bfs_fs_sync(fs);

    /* Verify the file data is intact */
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    uint8_t readback[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_file_read(&f, readback, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_EQ(readback[0], 0xAA);

    teardown(fs);
}

/* ── Test: random insert/delete cycles ─────────────────────── */

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_random_ops(void)
{
    bfs_fs_t *fs = setup("RandomOps", BLK_COUNT);
    uint32_t rng = 12345;
    uint8_t exists[500];
    memset(exists, 0, sizeof(exists));

    /* 2000 random insert/delete operations */
    for (int op = 0; op < 2000; op++) {
        uint32_t idx = xorshift32(&rng) % 500;
        char name[32];
        int len = snprintf(name, sizeof(name), "rnd_%03u", idx);

        if (exists[idx]) {
            bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)len);
            exists[idx] = 0;
        } else {
            uint32_t ino;
            if (bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)len, &ino) == BFS_OK)
                exists[idx] = 1;
        }

        /* Periodic sync */
        if (op % 200 == 0)
            bfs_fs_sync(fs);
    }

    /* Verify consistency */
    for (uint32_t i = 0; i < 500; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "rnd_%03u", i);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type);
        if (exists[i])
            TEST_ASSERT_EQ(err, BFS_OK);
        else
            TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);
    }

    teardown(fs);
}

/* ── Test: crash recovery — synced data survives, pending frees reclaimed ── */

static void test_crash_recovery_cycles(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "CrashTest", 0);

    /* Phase 1: create files, sync, verify persistence */
    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    for (int i = 0; i < 20; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* Phase 2: more operations to generate pending frees */
    for (int i = 20; i < 30; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%02d", i);
        uint32_t ino;
        bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
    }

    /* Sync again — should reclaim pending frees from phase 2 COW */
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* Pending frees should have been reclaimed */
    TEST_ASSERT_EQ(fs.pending_count, 0);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);

    /* Phase 3: remount and verify all 30 files + inode counter persisted */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Inode counter should be persisted */
    TEST_ASSERT(fs.next_ino > 30);

    for (int i = 0; i < 30; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "file_%02d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: deep directory tree ─────────────────────────────── */

static void test_deep_dirs(void)
{
    bfs_fs_t *fs = setup("DeepDirs", BLK_COUNT);

    /* Create 20-level deep directory tree */
    uint32_t parent = BFS_ROOT_INO;
    uint32_t dir_inos[20];
    for (int depth = 0; depth < 20; depth++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "d%02d", depth);
        TEST_ASSERT_EQ(bfs_fs_mkdir(fs, parent, name, (uint8_t)len, &dir_inos[depth]), BFS_OK);
        parent = dir_inos[depth];
    }

    /* Create a file at the deepest level */
    uint32_t file_ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, parent, "deep.txt", 8, &file_ino), BFS_OK);

    /* Write data to it */
    bfs_file_t f;
    bfs_file_open(&f, fs, file_ino);
    const char *msg = "Hello from depth 20!";
    bfs_file_write(&f, msg, 20);

    /* Sync and verify */
    bfs_fs_sync(fs);

    /* Verify the file at depth 20 */
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, parent, "deep.txt", 8, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, file_ino);

    teardown(fs);
}

TEST_SUITE_BEGIN("Stress Tests")
    TEST_RUN(test_large_directory);
    TEST_RUN(test_disk_full);
    TEST_RUN(test_random_ops);
    TEST_RUN(test_crash_recovery_cycles);
    TEST_RUN(test_deep_dirs);
TEST_SUITE_END()
