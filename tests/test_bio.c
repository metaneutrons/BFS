/*
 * BFS — Block device emulator tests
 */

#include "test_harness.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_bio.img"
#define BLK_SIZE 4096
#define BLK_COUNT 256  /* 1MB */

static void test_create_device(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bio->block_size, BLK_SIZE);
    TEST_ASSERT_EQ(bio->block_count, BLK_COUNT);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_invalid_block_size(void)
{
    TEST_ASSERT(bio_emu_create(TEST_IMG, 0, 100) == NULL);
    TEST_ASSERT(bio_emu_create(TEST_IMG, 300, 100) == NULL);   /* not power of 2 */
    TEST_ASSERT(bio_emu_create(TEST_IMG, 256, 100) == NULL);   /* below minimum */
    TEST_ASSERT(bio_emu_create(TEST_IMG, 131072, 100) == NULL); /* above maximum */
}

static void test_write_read_roundtrip(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    uint8_t wbuf[BLK_SIZE], rbuf[BLK_SIZE];
    /* Write a pattern to block 0 */
    memset(wbuf, 0xAA, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_bio_write(bio, 0, wbuf), BFS_OK);

    /* Write different pattern to last block */
    memset(wbuf, 0x55, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_bio_write(bio, BLK_COUNT - 1, wbuf), BFS_OK);

    /* Read back block 0 */
    TEST_ASSERT_EQ(bfs_bio_read(bio, 0, rbuf), BFS_OK);
    for (int i = 0; i < BLK_SIZE; i++)
        TEST_ASSERT_EQ(rbuf[i], 0xAA);

    /* Read back last block */
    TEST_ASSERT_EQ(bfs_bio_read(bio, BLK_COUNT - 1, rbuf), BFS_OK);
    for (int i = 0; i < BLK_SIZE; i++)
        TEST_ASSERT_EQ(rbuf[i], 0x55);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_out_of_bounds(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    uint8_t buf[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, BLK_COUNT, buf), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_bio_write(bio, BLK_COUNT, buf), BFS_ERR_INVAL);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reopen_persistence(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    uint8_t wbuf[BLK_SIZE], rbuf[BLK_SIZE];
    for (int i = 0; i < BLK_SIZE; i++) wbuf[i] = (uint8_t)(i & 0xFF);
    TEST_ASSERT_EQ(bfs_bio_write(bio, 42, wbuf), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_OK);
    bfs_bio_close(bio);

    /* Reopen and verify */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bio->block_count, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_bio_read(bio, 42, rbuf), BFS_OK);
    TEST_ASSERT_MEM_EQ(rbuf, wbuf, BLK_SIZE);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_sync(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_bio_sync(bio), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Block Device Emulator")
    TEST_RUN(test_create_device);
    TEST_RUN(test_invalid_block_size);
    TEST_RUN(test_write_read_roundtrip);
    TEST_RUN(test_out_of_bounds);
    TEST_RUN(test_reopen_persistence);
    TEST_RUN(test_sync);
TEST_SUITE_END()
