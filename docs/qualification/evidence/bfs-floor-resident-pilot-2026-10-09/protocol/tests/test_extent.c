/*
 * BFS — Extent tree tests
 */

#include "test_harness.h"
#include "bfs_extent.h"
#include "block_device_emu.h"
#include <string.h>
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

static void test_map_initialized_run(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    TEST_ASSERT_EQ(bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1), BFS_OK);
    bfs_blk_t start = bfs_freespace_alloc(fs, 4);
    TEST_ASSERT(start != BFS_BLK_NULL);
    uint8_t data[BLK_SIZE];
    memset(data, 0x5a, sizeof(data));
    for (uint32_t i = 0; i < 4; i++)
        TEST_ASSERT_EQ(bfs_bio_write(bio, start + i, data), BFS_OK);

    TEST_ASSERT_EQ(bfs_extent_map_run(&et, 0, start, 4), BFS_OK);
    bfs_extent_val_t value;
    TEST_ASSERT_EQ(bfs_extent_lookup_val(&et, 2, &value), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(value.length), 4);
    TEST_ASSERT_EQ(bfs_be32(value.disk_block), start);
    TEST_ASSERT_EQ(bfs_extent_map_run(&et, UINT32_MAX, start, 4), BFS_ERR_INVAL);
    et.data_checksums = true;
    TEST_ASSERT_EQ(bfs_extent_map_run(&et, 4, start, 2), BFS_ERR_INVAL);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: multiple extents (fragmented file) ──────────────── */

static bool count_record(const void *key, const void *val, void *ctx)
{
    (void)key; (void)val;
    (*(uint32_t *)ctx)++;
    return true;
}

static uint32_t record_count(bfs_extent_tree_t *et)
{
    uint32_t count = 0;
    if (bfs_btree_scan(&et->tree, NULL, count_record, &count) != BFS_OK) return UINT32_MAX;
    return count;
}

/* A run that continues the last tree record logically and on disk extends
 * it; a gap on either side, a checksummed record or an overlap does not. */
static void test_tree_merges_contiguous_runs(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    TEST_ASSERT_EQ(bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1), BFS_OK);
    bfs_blk_t base = bfs_freespace_alloc(fs, 64);
    TEST_ASSERT(base != BFS_BLK_NULL);

    /* A hole at block 0 starts a tree. */
    TEST_ASSERT_EQ(bfs_extent_map_run(&et, 4, base, 2), BFS_OK);
    TEST_ASSERT(et.tree.root != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_extent_map_block(&et, 6, base + 2, 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_extent_map_run(&et, 7, base + 3, 5), BFS_OK);
    TEST_ASSERT_EQ(record_count(&et), 1);
    bfs_extent_val_t value;
    TEST_ASSERT_EQ(bfs_extent_lookup_val(&et, 11, &value), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(value.disk_block), base);
    TEST_ASSERT_EQ(bfs_be32(value.length), 8);

    /* Not contiguous on disk, then not contiguous logically. */
    TEST_ASSERT_EQ(bfs_extent_map_block(&et, 12, base + 20, 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_extent_map_block(&et, 14, base + 21, 0), BFS_OK);
    TEST_ASSERT_EQ(record_count(&et), 3);

    /* Runs that touch a mapped block are refused and change nothing. */
    TEST_ASSERT_EQ(bfs_extent_map_block(&et, 5, base + 40, 0), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(bfs_extent_map_block(&et, 12, base + 40, 0), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(bfs_extent_map_run(&et, 10, base + 40, 3), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(bfs_extent_map_run(&et, 13, base + 40, 2), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(record_count(&et), 3);

    /* A checksummed record covers exactly one block. */
    et.data_checksums = true;
    TEST_ASSERT_EQ(bfs_extent_map_block(&et, 15, base + 22, 0x1234u), BFS_OK);
    TEST_ASSERT_EQ(record_count(&et), 4);
    TEST_ASSERT_EQ(bfs_extent_lookup_val(&et, 15, &value), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(value.length), 1);
    TEST_ASSERT_EQ(bfs_be32(value.data_crc32), 0x1234u);
    et.data_checksums = false;

    bfs_blk_t disk;
    for (uint32_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQ(bfs_extent_lookup(&et, 4 + i, &disk), BFS_OK);
        TEST_ASSERT_EQ(disk, base + i);
    }
    TEST_ASSERT_EQ(bfs_extent_lookup(&et, 13, &disk), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_extent_lookup(&et, 14, &disk), BFS_OK);
    TEST_ASSERT_EQ(disk, base + 21);

    /* Truncation inside the merged record keeps its prefix. */
    TEST_ASSERT_EQ(bfs_extent_truncate(&et, 6), BFS_OK);
    TEST_ASSERT_EQ(record_count(&et), 1);
    TEST_ASSERT_EQ(bfs_extent_lookup_val(&et, 5, &value), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(value.length), 2);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

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

static void test_truncate_inside_extent(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_extent_tree_t et;
    TEST_ASSERT_EQ(bfs_extent_init(&et, bio, fs, BFS_BLK_NULL, 1), BFS_OK);
    bfs_blk_t data_start;
    TEST_ASSERT_EQ(bfs_extent_append(&et, 0, 10, &data_start), BFS_OK);
    TEST_ASSERT_EQ(bfs_extent_truncate_batch(&et, 5, 0), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_extent_truncate(&et, 5), BFS_OK);

    for (uint32_t i = 0; i < 5; i++) {
        bfs_blk_t mapped = BFS_BLK_NULL;
        TEST_ASSERT_EQ(bfs_extent_lookup(&et, i, &mapped), BFS_OK);
        TEST_ASSERT_EQ(mapped, data_start + i);
    }
    bfs_blk_t mapped = BFS_BLK_NULL;
    TEST_ASSERT_EQ(bfs_extent_lookup(&et, 5, &mapped), BFS_ERR_NOTFOUND);

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
    TEST_RUN(test_map_initialized_run);
    TEST_RUN(test_tree_merges_contiguous_runs);
    TEST_RUN(test_fragmented_file);
    TEST_RUN(test_truncate);
    TEST_RUN(test_truncate_all);
    TEST_RUN(test_truncate_inside_extent);
    TEST_RUN(test_large_file);
TEST_SUITE_END()
