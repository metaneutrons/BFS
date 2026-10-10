/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — A key past the end of the last leaf splits that leaf at its end, so
 * ascending inserts (inode numbers, file blocks) leave full leaves behind;
 * every other insert splits in the middle as before. Search, scan and delete
 * stay correct on both shapes.
 */

#include "test_harness.h"
#include "bfs_btree.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_btree_append_split.img"
#define BLOCK_SIZE 4096u
#define BLOCK_COUNT 65536u /* the bump allocator never reuses a block */
#define VALUE_SIZE 56u /* an inode */
#define KEYS 2000u

typedef struct {
    bfs_allocator_t iface;
    bfs_blk_t next;
} bump_t;

static bfs_blk_t bump_alloc(bfs_allocator_t *a)
{
    bump_t *bump = (bump_t *)a->ctx;
    return bump->next < BLOCK_COUNT ? bump->next++ : BFS_BLK_NULL;
}

static bfs_err_t bump_dealloc(bfs_allocator_t *a, bfs_blk_t blk)
{
    (void)a;
    (void)blk;
    return BFS_OK;
}

static const bfs_btree_ops_t ops = {
    .key_compare = bfs_btree_key_compare_be32,
    .key_size = sizeof(uint32_t),
    .val_size = VALUE_SIZE,
    .cache_key_order = true,
};

typedef struct {
    bfs_bio_t *device;
    bump_t bump;
    bfs_btree_t tree;
} fixture_t;

static bool fixture_open(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    unlink(TEST_IMG);
    f->device = bio_emu_create(TEST_IMG, BLOCK_SIZE, BLOCK_COUNT);
    if (!f->device) return false;
    f->bump.iface.alloc = bump_alloc;
    f->bump.iface.dealloc = bump_dealloc;
    f->bump.iface.ctx = &f->bump;
    f->bump.next = 2;
    return bfs_btree_init(&f->tree, f->device, &f->bump.iface, &ops, BFS_BLK_NULL, 1) == BFS_OK;
}

static void fixture_close(fixture_t *f)
{
    bfs_bio_close(f->device);
    unlink(TEST_IMG);
}

static void make_value(uint8_t *value, uint32_t key)
{
    for (uint32_t i = 0; i < VALUE_SIZE; i++) value[i] = (uint8_t)(key * 7u + i);
}

static bfs_err_t insert(fixture_t *f, uint32_t number)
{
    uint32_t key = bfs_be32(number);
    uint8_t value[VALUE_SIZE];
    make_value(value, number);
    return bfs_btree_insert(&f->tree, &key, value);
}

typedef struct {
    uint32_t nodes;
    uint32_t entries;
    uint32_t previous;
    bool ordered;
    bool values_match;
} walk_t;

static void count_node(bfs_blk_t blk, void *ctx)
{
    (void)blk;
    ((walk_t *)ctx)->nodes++;
}

static bool check_entry(const void *key, const void *value, void *ctx)
{
    walk_t *walk = (walk_t *)ctx;
    uint32_t number = bfs_load_be32(key);
    uint8_t expected[VALUE_SIZE];
    make_value(expected, number);
    if (walk->entries && number <= walk->previous) walk->ordered = false;
    if (memcmp(value, expected, VALUE_SIZE) != 0) walk->values_match = false;
    walk->previous = number;
    walk->entries++;
    return true;
}

/* The walk's result is checked by the callers through walk.ordered. */
static walk_t walk_tree(fixture_t *f)
{
    walk_t walk = { .ordered = true, .values_match = true };
    if (bfs_btree_walk(&f->tree, count_node, check_entry, &walk) != BFS_OK)
        walk.ordered = false;
    return walk;
}

static uint32_t leaf_capacity(void)
{
    return (BLOCK_SIZE - 28u) / (sizeof(uint32_t) + VALUE_SIZE);
}

static void test_ascending_inserts_fill_leaves(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    for (uint32_t number = 1; number <= KEYS; number++)
        TEST_ASSERT_EQ(insert(&f, number), BFS_OK);
    TEST_ASSERT_EQ(f.tree.height, 2);
    walk_t walk = walk_tree(&f);
    TEST_ASSERT_EQ(walk.entries, KEYS);
    TEST_ASSERT(walk.ordered && walk.values_match);
    /* Every leaf but the last keeps capacity - 1 keys: 31 leaves and a root,
     * where middle splits would leave about 60 half-full leaves. */
    uint32_t full = leaf_capacity() - 1u;
    uint32_t leaves = walk.nodes - 1u;
    TEST_ASSERT_EQ(leaves, (KEYS - 2u) / full + 1u);
    fixture_close(&f);
}

static void test_descending_inserts_still_split_in_the_middle(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    for (uint32_t number = KEYS; number >= 1; number--)
        TEST_ASSERT_EQ(insert(&f, number), BFS_OK);
    walk_t walk = walk_tree(&f);
    TEST_ASSERT_EQ(walk.entries, KEYS);
    TEST_ASSERT(walk.ordered && walk.values_match);
    TEST_ASSERT(walk.nodes - 1u >= KEYS / (leaf_capacity() / 2u + 1u));
    fixture_close(&f);
}

static void test_append_split_then_deletes_rebalance(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f));
    for (uint32_t number = 1; number <= KEYS; number++)
        TEST_ASSERT_EQ(insert(&f, number), BFS_OK);
    /* A key between existing ones lands in a full leaf: a middle split. */
    for (uint32_t number = 1; number <= KEYS; number += 97u) {
        uint32_t key = bfs_be32(number);
        TEST_ASSERT_EQ(bfs_btree_delete(&f.tree, &key), BFS_OK);
        TEST_ASSERT_EQ(insert(&f, number), BFS_OK);
    }
    /* Remove two keys of every three, from the back, which drains the short
     * last leaf first and borrows from or merges with full siblings. */
    for (uint32_t number = KEYS; number >= 1; number--) {
        if (number % 3u == 0) continue;
        uint32_t key = bfs_be32(number);
        TEST_ASSERT_EQ(bfs_btree_delete(&f.tree, &key), BFS_OK);
    }
    walk_t walk = walk_tree(&f);
    TEST_ASSERT_EQ(walk.entries, KEYS / 3u);
    TEST_ASSERT(walk.ordered && walk.values_match);
    for (uint32_t number = 1; number <= KEYS; number++) {
        uint32_t key = bfs_be32(number);
        uint8_t value[VALUE_SIZE];
        TEST_ASSERT_EQ(bfs_btree_search(&f.tree, &key, value),
                       number % 3u == 0 ? BFS_OK : BFS_ERR_NOTFOUND);
    }
    /* Appending again after the deletes still works and stays dense. */
    for (uint32_t number = KEYS + 1u; number <= 2u * KEYS; number++)
        TEST_ASSERT_EQ(insert(&f, number), BFS_OK);
    walk = walk_tree(&f);
    TEST_ASSERT_EQ(walk.entries, KEYS / 3u + KEYS);
    TEST_ASSERT(walk.ordered && walk.values_match);
    fixture_close(&f);
}

TEST_SUITE_BEGIN("B+tree append split")
    TEST_RUN(test_ascending_inserts_fill_leaves);
    TEST_RUN(test_descending_inserts_still_split_in_the_middle);
    TEST_RUN(test_append_split_then_deletes_rebalance);
TEST_SUITE_END()
