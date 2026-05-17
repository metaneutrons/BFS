/*
 * BFS — Extent tree tests
 */

#include "test_harness.h"
#include "bfs_extent.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_extent.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192  /* 32MB */
#define DATA_START 2

static bfs_freespace_t g_fs;

static bfs_freespace_t *make_fs(bfs_bio_t *bio)
{
    bfs_freespace_init(&g_fs, bio, BFS_BLK_NULL, 1);
    bfs_freespace_add(&g_fs, DATA_START, BLK_COUNT - DATA_START);
    bfs_freespace_refill_reserve(&g_fs);
    return &g_fs;
}

/* ── Test: single extent append and lookup ─────────────────── */

static void test_single_extent(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1);

    bfs_blk_t dblk;
    TEST_ASSERT_EQ(bfs_extent_append(&et, 0, 10, &dblk), BFS_OK);
    TEST_ASSERT(dblk != BFS_BLK_NULL);

    /* Lookup each file block in the extent */
    for (uint32_t i = 0; i < 10; i++) {
        bfs_blk_t result;
        TEST_ASSERT_EQ(bfs_extent_lookup(&et, i, &result), BFS_OK);
        TEST_ASSERT_EQ(result, dblk + i);
    }

    /* Beyond extent should fail */
    bfs_blk_t result;
    TEST_ASSERT_EQ(bfs_extent_lookup(&et, 10, &result), BFS_ERR_NOTFOUND);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: multiple extents (fragmented file) ──────────────── */

static void test_fragmented_file(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1);

    bfs_blk_t dblks[5];
    /* Append 5 separate extents of 10 blocks each */
    for (uint32_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQ(bfs_extent_append(&et, i * 10, 10, &dblks[i]), BFS_OK);
    }

    /* Lookup across all extents */
    for (uint32_t i = 0; i < 50; i++) {
        bfs_blk_t result;
        TEST_ASSERT_EQ(bfs_extent_lookup(&et, i, &result), BFS_OK);
        uint32_t ext_idx = i / 10;
        uint32_t ext_off = i % 10;
        TEST_ASSERT_EQ(result, dblks[ext_idx] + ext_off);
    }

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: truncate ────────────────────────────────────────── */

static void test_truncate(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1);

    /* Create 3 extents: blocks 0-9, 10-19, 20-29 */
    bfs_blk_t d0, d1, d2;
    bfs_extent_append(&et, 0, 10, &d0);
    bfs_extent_append(&et, 10, 10, &d1);
    bfs_extent_append(&et, 20, 10, &d2);

    uint32_t free_before = fs->total_free;

    /* Truncate from block 10 — should free extents 10-19 and 20-29 */
    TEST_ASSERT_EQ(bfs_extent_truncate(&et, 10), BFS_OK);

    /* Data blocks freed, but first-transaction COW blocks aren't freed */
    TEST_ASSERT(fs->total_free >= free_before + 15);

    /* Blocks 0-9 should still be accessible */
    for (uint32_t i = 0; i < 10; i++) {
        bfs_blk_t result;
        TEST_ASSERT_EQ(bfs_extent_lookup(&et, i, &result), BFS_OK);
    }

    /* Blocks 10+ should be gone */
    bfs_blk_t result;
    TEST_ASSERT_EQ(bfs_extent_lookup(&et, 10, &result), BFS_ERR_NOTFOUND);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: truncate all (empty file) ───────────────────────── */

static void test_truncate_all(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1);

    bfs_blk_t d;
    bfs_extent_append(&et, 0, 100, &d);

    TEST_ASSERT_EQ(bfs_extent_truncate(&et, 0), BFS_OK);

    bfs_blk_t result;
    TEST_ASSERT_EQ(bfs_extent_lookup(&et, 0, &result), BFS_ERR_NOTFOUND);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: large file (many extents) ───────────────────────── */

static void test_large_file(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1);

    /* 100 extents of 10 blocks = 1000 file blocks */
    bfs_blk_t dblks[100];
    for (uint32_t i = 0; i < 100; i++) {
        TEST_ASSERT_EQ(bfs_extent_append(&et, i * 10, 10, &dblks[i]), BFS_OK);
    }

    /* Spot-check lookups */
    for (uint32_t i = 0; i < 1000; i += 37) {
        bfs_blk_t result;
        TEST_ASSERT_EQ(bfs_extent_lookup(&et, i, &result), BFS_OK);
        TEST_ASSERT_EQ(result, dblks[i / 10] + (i % 10));
    }

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Extent Tree")
    TEST_RUN(test_single_extent);
    TEST_RUN(test_fragmented_file);
    TEST_RUN(test_truncate);
    TEST_RUN(test_truncate_all);
    TEST_RUN(test_large_file);
TEST_SUITE_END()
