/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — multi-block transfers
 *
 * File data moves in runs of consecutive blocks. These tests check the BIO
 * wrappers, the cache's bulk paths (which must neither bypass a deferred node
 * nor keep stale copies) and file reads that span extents and holes.
 */

#include "test_harness.h"
#include "bfs_cache.h"
#include "bfs_extent.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <unistd.h>

#define TEST_IMG   "test_block_runs.img"
#define BLK_SIZE   4096
#define MEM_BLOCKS 32

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[MEM_BLOCKS][BLK_SIZE];
    uint32_t reads;
    uint32_t writes;
    uint32_t fail_after_writes; /* 0: never */
} memory_bio_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    memory_bio_t *m = (memory_bio_t *)bio;
    if (blk >= MEM_BLOCKS) return BFS_ERR_INVAL;
    m->reads++;
    memcpy(buf, m->blocks[blk], BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    memory_bio_t *m = (memory_bio_t *)bio;
    if (blk >= MEM_BLOCKS) return BFS_ERR_INVAL;
    if (m->fail_after_writes && m->writes >= m->fail_after_writes) return BFS_ERR_IO;
    m->writes++;
    memcpy(m->blocks[blk], buf, BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_sync(bfs_bio_t *bio) { (void)bio; return BFS_OK; }
static void memory_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read, .write_block = memory_write,
    .sync = memory_sync, .close = memory_close,
};

static memory_bio_t memory;
static uint8_t run[8 * BLK_SIZE];

static void memory_reset(void)
{
    memset(&memory, 0, sizeof(memory));
    memory.bio.ops = &memory_ops;
    memory.bio.block_size = BLK_SIZE;
    memory.bio.block_count = MEM_BLOCKS;
    for (uint32_t i = 0; i < sizeof(run); i++) run[i] = (uint8_t)(i / BLK_SIZE + 0x10);
}

static bfs_err_t keep_image(const void *layout, uint32_t block_size, uint8_t *buf)
{
    (void)layout; (void)block_size; (void)buf;
    return BFS_OK;
}

static void test_wrapper_reports_written_prefix(void)
{
    memory_reset();
    memory.fail_after_writes = 2;
    uint32_t written = 99;
    TEST_ASSERT_EQ(bfs_bio_write_blocks(&memory.bio, 4, 5, run, &written), BFS_ERR_IO);
    TEST_ASSERT_EQ(written, 2);
    TEST_ASSERT_EQ(memory.blocks[5][0], 0x11);
    TEST_ASSERT_EQ(memory.blocks[6][0], 0);
    TEST_ASSERT_EQ(bfs_bio_write_blocks(&memory.bio, 30, 3, run, &written), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(written, 0);
    uint8_t back[3 * BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read_blocks(&memory.bio, 4, 2, back), BFS_OK);
    TEST_ASSERT_MEM_EQ(back, run, 2 * BLK_SIZE);
}

static void test_cache_bulk_read_bypasses_slots_but_not_deferred_nodes(void)
{
    memory_reset();
    bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 8), BFS_OK);
    bfs_cache_set_deferred_node_limit(&cache, 4);
    for (bfs_blk_t blk = 10; blk < 18; blk++) memset(memory.blocks[blk], (int)blk, BLK_SIZE);
    uint8_t back[8 * BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read_blocks(&cache.bio, 10, 8, back), BFS_OK);
    for (uint32_t i = 0; i < cache.num_slots; i++)
        TEST_ASSERT(cache.slots[i].blk == UINT32_MAX);
    TEST_ASSERT_EQ(back[3 * BLK_SIZE], 13);

    uint8_t node[BLK_SIZE];
    memset(node, 0xAB, sizeof(node));
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 12, node, keep_image, NULL), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read_blocks(&cache.bio, 10, 8, back), BFS_OK);
    TEST_ASSERT_EQ(back[2 * BLK_SIZE], 0xAB);
    TEST_ASSERT_EQ(back[3 * BLK_SIZE], 13);
    bfs_cache_destroy(&cache);
}

static void test_cache_bulk_write_refreshes_resident_copies(void)
{
    memory_reset();
    bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, &memory.bio, 8), BFS_OK);
    bfs_cache_set_deferred_node_limit(&cache, 4);
    uint8_t buf[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, buf), BFS_OK);
    memset(buf, 0x77, sizeof(buf));
    TEST_ASSERT_EQ(bfs_bio_defer_node(&cache.bio, 6, buf, keep_image, NULL), BFS_OK);
    uint32_t written;
    TEST_ASSERT_EQ(bfs_bio_write_blocks(&cache.bio, 4, 4, run, &written), BFS_OK);
    TEST_ASSERT_EQ(written, 4);
    TEST_ASSERT_EQ(cache.dirty_count, 0);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 5, buf), BFS_OK);
    TEST_ASSERT_EQ(buf[0], 0x11);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 6, buf), BFS_OK);
    TEST_ASSERT_EQ(buf[0], 0x12);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, 6));
    TEST_ASSERT_EQ(bfs_bio_flush_deferred(&cache.bio), BFS_OK);
    TEST_ASSERT_EQ(memory.blocks[6][0], 0x12);

    /* After a failed bulk write, copies past the written prefix are dropped. */
    memset(buf, 0x33, sizeof(buf));
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, 9, buf), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, 8, buf), BFS_OK);
    memory.fail_after_writes = memory.writes + 1;
    TEST_ASSERT_EQ(bfs_bio_write_blocks(&cache.bio, 8, 2, run, &written), BFS_ERR_IO);
    TEST_ASSERT_EQ(written, 1);
    bool slot8 = false, slot9 = false;
    for (uint32_t i = 0; i < cache.num_slots; i++) {
        if (cache.slots[i].blk == 8) slot8 = true;
        if (cache.slots[i].blk == 9) slot9 = true;
    }
    TEST_ASSERT(slot8);
    TEST_ASSERT(!slot9);
    bfs_cache_destroy(&cache);
}

/* ── File reads across extents and holes ───────────────────── */

static uint8_t expected[12 * BLK_SIZE];
static uint8_t actual[12 * BLK_SIZE + 3];

static void test_file_read_spans_extents_and_holes(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, 256);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "Runs", 0), BFS_OK);
    bfs_cache_t cache;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, bio, 16), BFS_OK);
    bfs_cache_set_node_write_retention(&cache, true);
    bfs_cache_set_deferred_node_limit(&cache, 8);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &cache.bio), BFS_OK);

    for (uint32_t i = 0; i < sizeof(expected); i++) expected[i] = (uint8_t)(i * 7u + i / 4096u);
    uint32_t ino, other;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "runs", 4, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "gap", 3, &other), BFS_OK);
    bfs_file_t file, filler;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&filler, &fs, other), BFS_OK);
    /* Extent 1: blocks 0-3. A filler write breaks contiguity. */
    TEST_ASSERT_EQ(bfs_file_write(&file, expected, 4 * BLK_SIZE), 4 * BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&filler, expected, 2 * BLK_SIZE), 2 * BLK_SIZE);
    /* Extent 2: blocks 4-6. */
    TEST_ASSERT_EQ(bfs_file_write(&file, expected + 4 * BLK_SIZE, 3 * BLK_SIZE),
                   3 * BLK_SIZE);
    /* Hole at blocks 7-8, then block 9-11 mapped. */
    memset(expected + 7 * BLK_SIZE, 0, 2 * BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_seek(&file, 9 * BLK_SIZE, BFS_SEEK_SET), 9 * BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&file, expected + 9 * BLK_SIZE, 3 * BLK_SIZE),
                   3 * BLK_SIZE);

    bfs_blk_t disk;
    uint32_t run_blocks;
    TEST_ASSERT_EQ(bfs_extent_lookup_run(&file.extents, 1, &disk, &run_blocks), BFS_OK);
    TEST_ASSERT_EQ(run_blocks, 3);
    TEST_ASSERT_EQ(bfs_extent_lookup_run(&file.extents, 7, &disk, &run_blocks),
                   BFS_ERR_NOTFOUND);

    for (int pass = 0; pass < 2; pass++) {
        /* Aligned whole-file read, then an unaligned one. */
        uint32_t start = pass == 0 ? 0 : 1000;
        TEST_ASSERT_EQ(bfs_file_seek(&file, start, BFS_SEEK_SET), (int64_t)start);
        uint32_t want = (uint32_t)sizeof(expected) - start;
        TEST_ASSERT_EQ(bfs_file_read(&file, actual, sizeof(actual)), (int32_t)want);
        TEST_ASSERT_MEM_EQ(actual, expected + start, want);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_cache_destroy(&cache);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Multi-block transfers")
    TEST_RUN(test_wrapper_reports_written_prefix);
    TEST_RUN(test_cache_bulk_read_bypasses_slots_but_not_deferred_nodes);
    TEST_RUN(test_cache_bulk_write_refreshes_resident_copies);
    TEST_RUN(test_file_read_spans_extents_and_holes);
TEST_SUITE_END()
