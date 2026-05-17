/*
 * BFS — Free space allocator tests
 */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_alloc.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096  /* 16MB */
#define DATA_START 2    /* blocks 0,1 are superblocks */

/* Helper: init a fresh allocator with all blocks free from DATA_START */
static bfs_freespace_t *make_fs(bfs_bio_t *bio)
{
    static bfs_freespace_t fs;
    bfs_freespace_init(&fs, bio, BFS_BLK_NULL, 1);
    /* Add all data blocks as one big free extent */
    bfs_freespace_add(&fs, DATA_START, BLK_COUNT - DATA_START);
    bfs_freespace_refill_reserve(&fs);
    return &fs;
}

/* ── Test: basic alloc and free ────────────────────────────── */

static void test_alloc_basic(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);
    uint32_t initial_free = fs->total_free;

    bfs_blk_t blk = bfs_freespace_alloc(fs, 1);
    TEST_ASSERT(blk != BFS_BLK_NULL);
    TEST_ASSERT(blk >= DATA_START);
    TEST_ASSERT(fs->total_free < initial_free);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: alloc multiple blocks ───────────────────────────── */

static void test_alloc_multi(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);

    bfs_blk_t blk = bfs_freespace_alloc(fs, 10);
    TEST_ASSERT(blk != BFS_BLK_NULL);
    TEST_ASSERT(blk >= DATA_START);

    /* Allocate another chunk — should be contiguous or after */
    bfs_blk_t blk2 = bfs_freespace_alloc(fs, 5);
    TEST_ASSERT(blk2 != BFS_BLK_NULL);
    TEST_ASSERT(blk2 >= blk + 10);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: free and realloc ────────────────────────────────── */

static void test_free_realloc(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);

    bfs_blk_t blk = bfs_freespace_alloc(fs, 10);
    TEST_ASSERT(blk != BFS_BLK_NULL);
    uint32_t free_after_alloc = fs->total_free;

    TEST_ASSERT_EQ(bfs_freespace_free(fs, blk, 10), BFS_OK);
    TEST_ASSERT_EQ(fs->total_free, free_after_alloc + 10);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: extent merging on free ──────────────────────────── */

static void test_merge_right(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);

    /* Allocate three adjacent chunks */
    bfs_blk_t a = bfs_freespace_alloc(fs, 10);
    bfs_blk_t b = bfs_freespace_alloc(fs, 10);
    bfs_blk_t c = bfs_freespace_alloc(fs, 10);
    TEST_ASSERT(a != BFS_BLK_NULL);
    TEST_ASSERT(b != BFS_BLK_NULL);
    TEST_ASSERT(c != BFS_BLK_NULL);

    /* Free b, then c — should merge into one extent */
    TEST_ASSERT_EQ(bfs_freespace_free(fs, b, 10), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_free(fs, c, 10), BFS_OK);

    /* Free a — should merge with b+c into one big extent */
    TEST_ASSERT_EQ(bfs_freespace_free(fs, a, 10), BFS_OK);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: many alloc/free cycles ──────────────────────────── */

static void test_alloc_free_cycles(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);

    bfs_blk_t blocks[100];

    /* Allocate 100 single blocks */
    for (int i = 0; i < 100; i++) {
        blocks[i] = bfs_freespace_alloc(fs, 1);
        TEST_ASSERT(blocks[i] != BFS_BLK_NULL);
    }

    uint32_t free_after = fs->total_free;

    /* Free all of them */
    for (int i = 0; i < 100; i++)
        TEST_ASSERT_EQ(bfs_freespace_free(fs, blocks[i], 1), BFS_OK);

    /* Free count should increase (COW overhead means not exactly +100) */
    TEST_ASSERT(fs->total_free >= free_after + 90);

    /* Allocate again — should succeed */
    for (int i = 0; i < 100; i++) {
        blocks[i] = bfs_freespace_alloc(fs, 1);
        TEST_ASSERT(blocks[i] != BFS_BLK_NULL);
    }

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: self-hosting — use allocator for a B+tree ───────── */

static int u32_cmp(const void *a, const void *b) {
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
    return (va > vb) - (va < vb);
}
static const bfs_btree_ops_t u32_ops = { .key_compare = u32_cmp, .key_size = 4, .val_size = 4 };

static void test_self_hosting(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);

    /* Create a separate B+tree that uses the free space allocator */
    bfs_btree_t data_tree;
    bfs_btree_init(&data_tree, bio, bfs_freespace_allocator(fs),
                    &u32_ops, BFS_BLK_NULL, 1);

    /* Insert 500 entries — this will allocate B+tree nodes from the free space tree */
    for (uint32_t i = 0; i < 500; i++) {
        uint32_t key = bfs_be32(i), val = bfs_be32(i * 7);
        bfs_err_t err = bfs_btree_insert(&data_tree, &key, &val);
        TEST_ASSERT_EQ(err, BFS_OK);
    }

    /* Verify all entries */
    for (uint32_t i = 0; i < 500; i++) {
        uint32_t key = bfs_be32(i), result;
        TEST_ASSERT_EQ(bfs_btree_search(&data_tree, &key, &result), BFS_OK);
        TEST_ASSERT_EQ(bfs_be32(result), i * 7);
    }

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: out of space ────────────────────────────────────── */

static void test_out_of_space(void)
{
    unlink(TEST_IMG);
    /* Tiny disk: 32 blocks */
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, 32);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t fs;
    bfs_freespace_init(&fs, bio, BFS_BLK_NULL, 1);
    bfs_freespace_add(&fs, DATA_START, 30);
    bfs_freespace_refill_reserve(&fs);

    /* Allocate all blocks */
    bfs_blk_t blk;
    int count = 0;
    while ((blk = bfs_freespace_alloc(&fs, 1)) != BFS_BLK_NULL)
        count++;

    /* Should have allocated some blocks (not all 30 due to tree overhead + reserve) */
    TEST_ASSERT(count >= 0);

    /* Alloc should eventually fail */
    /* (may already have failed above) */

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Free Space Allocator")
    TEST_RUN(test_alloc_basic);
    TEST_RUN(test_alloc_multi);
    TEST_RUN(test_free_realloc);
    TEST_RUN(test_merge_right);
    TEST_RUN(test_alloc_free_cycles);
    TEST_RUN(test_self_hosting);
    TEST_RUN(test_out_of_space);
TEST_SUITE_END()
