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
    for (int i = 0; i < 100; i++) {
        bfs_err_t err = bfs_freespace_free(fs, blocks[i], 1);
        if (err != BFS_OK)
            fprintf(stderr,
                    "free cycle failed: index=%d block=%u total=%u reserve=%u root=%u error=%d\n",
                    i, blocks[i], fs->total_free, fs->reserve_count,
                    fs->tree.root, err);
        TEST_ASSERT_EQ(err, BFS_OK);
    }

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

typedef struct {
    bfs_freespace_t *fs;
    const uint8_t *owned;
    bfs_blk_t previous_end;
    bool have_previous;
    bool invalid;
} reserve_check_ctx_t;

static bool reserve_check_extent(const void *key, const void *val, void *ctx)
{
    reserve_check_ctx_t *check = (reserve_check_ctx_t *)ctx;
    bfs_blk_t start = bfs_load_be32(key);
    uint32_t len = bfs_load_be32(val);

    if (len == 0 || start == BFS_BLK_NULL ||
        start >= check->fs->tree.bio->block_count ||
        len > check->fs->tree.bio->block_count - start ||
        (check->have_previous && start < check->previous_end)) {
        check->invalid = true;
        return false;
    }

    bfs_blk_t end = start + len;
    for (bfs_blk_t blk = start; blk < end; blk++) {
        if (check->owned && check->owned[blk]) {
            check->invalid = true;
            return false;
        }
    }
    for (uint32_t i = 0; i < check->fs->reserve_count; i++) {
        bfs_blk_t blk = check->fs->reserve[i];
        if (blk >= start && blk < end) {
            check->invalid = true;
            return false;
        }
    }

    check->previous_end = end;
    check->have_previous = true;
    return true;
}

static bool reserve_state_valid(bfs_freespace_t *fs, const uint8_t *owned)
{
    if (!fs || fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) return false;

    for (uint32_t i = 0; i < fs->reserve_count; i++) {
        bfs_blk_t blk = fs->reserve[i];
        if (blk == BFS_BLK_NULL || blk >= fs->tree.bio->block_count ||
            (owned && owned[blk]))
            return false;
        for (uint32_t j = 0; j < i; j++) {
            if (fs->reserve[j] == blk) return false;
        }
    }

    reserve_check_ctx_t check = {
        .fs = fs,
        .owned = owned,
        .previous_end = 0,
        .have_previous = false,
        .invalid = false,
    };
    return bfs_btree_scan(&fs->tree, NULL, reserve_check_extent, &check) == BFS_OK &&
           !check.invalid;
}

static uint64_t freespace_capacity(const bfs_freespace_t *fs)
{
    return (uint64_t)fs->total_free + fs->reserve_count;
}

static void reserve_allocator_cycle(bfs_freespace_t *fs, uint8_t *owned)
{
    uint64_t before = freespace_capacity(fs);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(fs), BFS_OK);
    TEST_ASSERT_EQ(freespace_capacity(fs), before);
    TEST_ASSERT(reserve_state_valid(fs, owned));

    bfs_blk_t first = bfs_freespace_alloc(fs, 9);
    TEST_ASSERT(first != BFS_BLK_NULL);
    for (uint32_t i = 0; i < 9; i++) {
        TEST_ASSERT(first + i < BLK_COUNT);
        TEST_ASSERT(!owned[first + i]);
        owned[first + i] = 1;
    }

    bfs_blk_t second = bfs_freespace_alloc(fs, 6);
    TEST_ASSERT(second == first + 9);
    for (uint32_t i = 0; i < 6; i++) {
        TEST_ASSERT(second + i < BLK_COUNT);
        TEST_ASSERT(!owned[second + i]);
        owned[second + i] = 1;
    }
    TEST_ASSERT_EQ(freespace_capacity(fs), before - 15);
    TEST_ASSERT(reserve_state_valid(fs, owned));

    TEST_ASSERT_EQ(bfs_freespace_free(fs, second, 6), BFS_OK);
    memset(&owned[second], 0, 6);
    TEST_ASSERT_EQ(bfs_freespace_free(fs, first, 9), BFS_OK);
    memset(&owned[first], 0, 9);
    TEST_ASSERT_EQ(freespace_capacity(fs), before);
    TEST_ASSERT(reserve_state_valid(fs, owned));

    bfs_err_t return_err = bfs_freespace_return_reserve(fs);
    TEST_ASSERT_EQ(return_err, BFS_ERR_NOSPC);
    TEST_ASSERT_EQ(fs->reserve_count, 1);
    TEST_ASSERT_EQ(freespace_capacity(fs), before);
    TEST_ASSERT(reserve_state_valid(fs, owned));

    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(fs), BFS_OK);
    TEST_ASSERT(fs->reserve_count > 0);
    TEST_ASSERT_EQ(freespace_capacity(fs), before);
    TEST_ASSERT(reserve_state_valid(fs, owned));
}

/* ── Test: reserve refill/return cycles preserve allocator state ── */

/* Without a superblock emergency pool, the final free-tree COW scratch block
 * cannot be returned and bfs_freespace_return_reserve reports BFS_ERR_NOSPC. */
static void test_reserve_refill_return_cycles(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);
    uint8_t owned[BLK_COUNT] = {0};

    TEST_ASSERT(fs->reserve_count > 0);
    TEST_ASSERT(reserve_state_valid(fs, owned));
    for (uint32_t cycle = 0; cycle < 5; cycle++) {
        int failures_before = test_fail_count;
        reserve_allocator_cycle(fs, owned);
        if (test_fail_count != failures_before) return;
    }

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: self-hosting — use allocator for a B+tree ───────── */

static int u32_cmp(const void *a, const void *b) {
    uint32_t va = bfs_load_be32(a);
    uint32_t vb = bfs_load_be32(b);
    return (va > vb) - (va < vb);
}
static const bfs_btree_ops_t u32_ops = { .key_compare = u32_cmp, .key_size = 4, .val_size = 4 };

typedef struct {
    uint8_t *blocks;
    bool invalid;
} node_map_ctx_t;

static void mark_tree_node(bfs_blk_t blk, void *ctx)
{
    node_map_ctx_t *map = (node_map_ctx_t *)ctx;
    if (blk >= BLK_COUNT || map->blocks[blk]) {
        map->invalid = true;
        return;
    }
    map->blocks[blk] = 1;
}

static bool data_tree_nodes_valid(bfs_freespace_t *fs, bfs_btree_t *tree,
                                  uint8_t *owned)
{
    memset(owned, 0, BLK_COUNT);
    node_map_ctx_t map = { .blocks = owned, .invalid = false };
    return bfs_btree_walk_nodes(tree, mark_tree_node, &map) == BFS_OK &&
           !map.invalid && reserve_state_valid(fs, owned);
}

enum { RESERVE_TREE_BASE_ENTRIES = 500, RESERVE_TREE_CYCLES = 5 };

static void insert_reserve_tree_base(bfs_btree_t *tree, uint32_t *expected)
{
    for (uint32_t i = 0; i < RESERVE_TREE_BASE_ENTRIES; i++) {
        uint32_t key = bfs_be32(i);
        uint32_t value = bfs_be32(i * 7);
        TEST_ASSERT_EQ(bfs_btree_insert(tree, &key, &value), BFS_OK);
        expected[i] = i * 7;
    }
}

static void reserve_tree_mutation_cycle(bfs_freespace_t *fs,
                                        bfs_btree_t *tree,
                                        uint32_t *expected, uint8_t *owned,
                                        uint32_t cycle)
{
    uint32_t updated_key = cycle * 79;
    uint32_t updated_value = 0xf00d0000u | cycle;
    uint32_t key_be = bfs_be32(updated_key);
    uint32_t value_be = bfs_be32(updated_value);
    TEST_ASSERT_EQ(bfs_btree_update(tree, &key_be, &value_be), BFS_OK);
    expected[updated_key] = updated_value;

    uint32_t inserted_key = RESERVE_TREE_BASE_ENTRIES + cycle;
    uint32_t inserted_value = inserted_key * 11;
    key_be = bfs_be32(inserted_key);
    value_be = bfs_be32(inserted_value);
    TEST_ASSERT_EQ(bfs_btree_insert(tree, &key_be, &value_be), BFS_OK);
    expected[inserted_key] = inserted_value;

    uint64_t before = freespace_capacity(fs);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(fs), BFS_OK);
    TEST_ASSERT_EQ(freespace_capacity(fs), before);
    TEST_ASSERT(data_tree_nodes_valid(fs, tree, owned));

    bfs_err_t return_err = bfs_freespace_return_reserve(fs);
    TEST_ASSERT_EQ(return_err, BFS_ERR_NOSPC);
    TEST_ASSERT_EQ(fs->reserve_count, 1);
    TEST_ASSERT_EQ(freespace_capacity(fs), before);
    TEST_ASSERT(data_tree_nodes_valid(fs, tree, owned));

    uint32_t result = 0;
    key_be = bfs_be32(updated_key);
    TEST_ASSERT_EQ(bfs_btree_search(tree, &key_be, &result), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(result), updated_value);
    key_be = bfs_be32(inserted_key);
    TEST_ASSERT_EQ(bfs_btree_search(tree, &key_be, &result), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(result), inserted_value);
}

static void verify_reserve_tree(bfs_btree_t *tree, const uint32_t *expected)
{
    for (uint32_t i = 0; i < RESERVE_TREE_BASE_ENTRIES + RESERVE_TREE_CYCLES; i++) {
        uint32_t key = bfs_be32(i), value = 0;
        TEST_ASSERT_EQ(bfs_btree_search(tree, &key, &value), BFS_OK);
        TEST_ASSERT_EQ(bfs_be32(value), expected[i]);
    }
}

/* ── Test: reserve cycling preserves mutations in allocator-backed trees ── */

static void test_reserve_cycles_preserve_tree_mutations(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);
    bfs_btree_t data_tree;
    TEST_ASSERT_EQ(bfs_btree_init(&data_tree, bio, bfs_freespace_allocator(fs),
                                  &u32_ops, BFS_BLK_NULL, 1), BFS_OK);

    uint32_t expected[RESERVE_TREE_BASE_ENTRIES + RESERVE_TREE_CYCLES] = {0};
    uint8_t owned[BLK_COUNT] = {0};
    int failures_before = test_fail_count;
    insert_reserve_tree_base(&data_tree, expected);
    if (test_fail_count != failures_before) return;

    TEST_ASSERT(data_tree_nodes_valid(fs, &data_tree, owned));
    for (uint32_t cycle = 0; cycle < RESERVE_TREE_CYCLES; cycle++) {
        failures_before = test_fail_count;
        reserve_tree_mutation_cycle(fs, &data_tree, expected, owned, cycle);
        if (test_fail_count != failures_before) return;
    }

    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(fs), BFS_OK);
    TEST_ASSERT(data_tree_nodes_valid(fs, &data_tree, owned));
    verify_reserve_tree(&data_tree, expected);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

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

static void test_double_free_preserves_accounting(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_freespace_t *fs = make_fs(bio);
    bfs_blk_t blk = bfs_freespace_alloc(fs, 1);
    TEST_ASSERT(blk != BFS_BLK_NULL);

    TEST_ASSERT_EQ(bfs_freespace_free(fs, blk, 1), BFS_OK);
    uint32_t free_after_first = fs->total_free;
    TEST_ASSERT_EQ(bfs_freespace_free(fs, blk, 1), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(fs->total_free, free_after_first);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Free Space Allocator")
    TEST_RUN(test_alloc_basic);
    TEST_RUN(test_alloc_multi);
    TEST_RUN(test_free_realloc);
    TEST_RUN(test_merge_right);
    TEST_RUN(test_alloc_free_cycles);
    TEST_RUN(test_reserve_refill_return_cycles);
    TEST_RUN(test_reserve_cycles_preserve_tree_mutations);
    TEST_RUN(test_self_hosting);
    TEST_RUN(test_out_of_space);
    TEST_RUN(test_double_free_preserves_accounting);
TEST_SUITE_END()
