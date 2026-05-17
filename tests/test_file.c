/*
 * BFS — File I/O tests
 */

#include "test_harness.h"
#include "bfs_file.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_file.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192

static bfs_fs_t g_fs;

static bfs_fs_t *setup(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "FileTest", 0);
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

/* ── Test: write and read back ─────────────────────────────── */

static void test_write_read(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "test", 4, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    /* Write "Hello, BFS!" */
    const char *msg = "Hello, BFS!";
    TEST_ASSERT_EQ(bfs_file_write(&f, msg, 12), 12);
    TEST_ASSERT_EQ(f.size, 12);

    /* Seek back and read */
    TEST_ASSERT_EQ(bfs_file_seek(&f, 0, BFS_SEEK_SET), 0);
    char buf[32] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&f, buf, 32), 12);
    TEST_ASSERT_MEM_EQ(buf, "Hello, BFS!", 12);

    teardown(fs);
}

/* ── Test: write across block boundary ─────────────────────── */

static void test_cross_block_write(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "test", 4, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    /* Write a pattern larger than one block */
    uint8_t pattern[BLK_SIZE + 100];
    for (uint32_t i = 0; i < sizeof(pattern); i++)
        pattern[i] = (uint8_t)(i & 0xFF);

    TEST_ASSERT_EQ(bfs_file_write(&f, pattern, sizeof(pattern)), (int32_t)sizeof(pattern));

    /* Read back */
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    uint8_t readback[BLK_SIZE + 100];
    TEST_ASSERT_EQ(bfs_file_read(&f, readback, sizeof(readback)), (int32_t)sizeof(readback));
    TEST_ASSERT_MEM_EQ(readback, pattern, sizeof(pattern));

    teardown(fs);
}

/* ── Test: seek modes ──────────────────────────────────────── */

static void test_seek(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "test", 4, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    uint8_t data[100];
    memset(data, 'A', 100);
    bfs_file_write(&f, data, 100);

    TEST_ASSERT_EQ(bfs_file_seek(&f, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_seek(&f, 50, BFS_SEEK_SET), 50);
    TEST_ASSERT_EQ(bfs_file_seek(&f, 10, BFS_SEEK_CUR), 60);
    TEST_ASSERT_EQ(bfs_file_seek(&f, -5, BFS_SEEK_END), 95);
    TEST_ASSERT_EQ(bfs_file_seek(&f, -200, BFS_SEEK_SET), BFS_ERR_INVAL);

    teardown(fs);
}

/* ── Test: truncate ────────────────────────────────────────── */

static void test_truncate(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "test", 4, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    /* Write 3 blocks worth of data */
    uint8_t data[BLK_SIZE];
    memset(data, 0xBB, BLK_SIZE);
    for (int i = 0; i < 3; i++)
        bfs_file_write(&f, data, BLK_SIZE);

    TEST_ASSERT_EQ(f.size, 3 * BLK_SIZE);

    /* Truncate to 1 block */
    TEST_ASSERT_EQ(bfs_file_truncate(&f, BLK_SIZE), BFS_OK);
    TEST_ASSERT_EQ(f.size, (uint64_t)BLK_SIZE);

    /* Read first block — should still have data */
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    uint8_t readback[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_file_read(&f, readback, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_EQ(readback[0], 0xBB);

    /* Truncate to 0 */
    TEST_ASSERT_EQ(bfs_file_truncate(&f, 0), BFS_OK);
    TEST_ASSERT_EQ(f.size, 0);

    teardown(fs);
}

/* ── Test: large file (many blocks) ────────────────────────── */

static void test_large_file(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "test", 4, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    /* Write 100 blocks */
    uint8_t data[BLK_SIZE];
    for (uint32_t i = 0; i < 100; i++) {
        memset(data, (uint8_t)(i & 0xFF), BLK_SIZE);
        TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);
    }

    /* Spot-check reads */
    for (uint32_t i = 0; i < 100; i += 17) {
        bfs_file_seek(&f, (int64_t)i * BLK_SIZE, BFS_SEEK_SET);
        TEST_ASSERT_EQ(bfs_file_read(&f, data, BLK_SIZE), BLK_SIZE);
        TEST_ASSERT_EQ(data[0], (uint8_t)(i & 0xFF));
        TEST_ASSERT_EQ(data[BLK_SIZE - 1], (uint8_t)(i & 0xFF));
    }

    teardown(fs);
}

TEST_SUITE_BEGIN("File I/O")
    TEST_RUN(test_write_read);
    TEST_RUN(test_cross_block_write);
    TEST_RUN(test_seek);
    TEST_RUN(test_truncate);
    TEST_RUN(test_large_file);
TEST_SUITE_END()
