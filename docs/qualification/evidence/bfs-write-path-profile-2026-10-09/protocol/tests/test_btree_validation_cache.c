/* SPDX-License-Identifier: MPL-2.0 */
/* Focused tests for B-tree structural validation memoization. */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"

#define BLOCK_SIZE BFS_MIN_BLOCK_SIZE
#define BLOCK_COUNT 16
#define LEAF_KEYS 64

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[BLOCK_COUNT][BLOCK_SIZE];
    uint32_t reads;
    uint32_t writes;
    bool fail_read;
    bool fail_write;
} memory_bio_t;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->reads++;
    if (memory->fail_read) return BFS_ERR_IO;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, memory->blocks[block], bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->writes++;
    if (memory->fail_write) return BFS_ERR_IO;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(memory->blocks[block], buffer, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_sync(bfs_bio_t *bio)
{
    (void)bio;
    return BFS_OK;
}

static void memory_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t memory_ops = {
    .read_block = memory_read,
    .write_block = memory_write,
    .sync = memory_sync,
    .close = memory_close,
};

static uint32_t compare_a_calls;
static uint32_t compare_b_calls;
static uint32_t stateful_compare_calls;
static bool stateful_reverse;

static int compare_u32_a(const void *left, const void *right)
{
    compare_a_calls++;
    uint32_t a = bfs_load_be32(left), b = bfs_load_be32(right);
    return a < b ? -1 : a > b ? 1 : 0;
}

static int cmp_u32b(const void *left, const void *right)
{
    compare_b_calls++;
    uint32_t a = bfs_load_be32(left), b = bfs_load_be32(right);
    return a < b ? -1 : a > b ? 1 : 0;
}

/* Side-effecting on purpose: arbitrary default comparators are not memoized. */
static int compare_u32_stateful(const void *left, const void *right)
{
    stateful_compare_calls++;
    uint32_t a = bfs_load_be32(left), b = bfs_load_be32(right);
    int order = a < b ? -1 : a > b ? 1 : 0;
    return stateful_reverse ? -order : order;
}

static const bfs_btree_ops_t ops_a = {
    .key_compare = compare_u32_a,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = true,
};

static const bfs_btree_ops_t ops_b = {
    .key_compare = cmp_u32b,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = true,
};

static const bfs_btree_ops_t ops_stateful = {
    .key_compare = compare_u32_stateful,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = false,
};

typedef struct {
    bfs_bio_t bio;
    bfs_cache_t cache;
    bfs_allocator_t allocator;
} cache_fixture_t;

static bfs_blk_t unused_alloc(bfs_allocator_t *allocator)
{
    (void)allocator;
    return BFS_BLK_NULL;
}

static bfs_err_t unused_dealloc(bfs_allocator_t *allocator, bfs_blk_t blk)
{
    (void)allocator;
    (void)blk;
    return BFS_OK;
}

static void memory_init(memory_bio_t *memory)
{
    stateful_reverse = false;
    memset(memory, 0, sizeof(*memory));
    memory->bio.ops = &memory_ops;
    memory->bio.block_size = BLOCK_SIZE;
    memory->bio.block_count = BLOCK_COUNT;
}

static bool cache_fixture_init(cache_fixture_t *fixture, memory_bio_t *memory,
                               uint32_t slots)
{
    memory_init(memory);
    if (bfs_cache_init(&fixture->cache, &memory->bio, slots) != BFS_OK)
        return false;
    fixture->allocator.alloc = unused_alloc;
    fixture->allocator.dealloc = unused_dealloc;
    fixture->allocator.error = NULL;
    fixture->allocator.ctx = NULL;
    return true;
}

static void cache_fixture_destroy(cache_fixture_t *fixture)
{
    bfs_cache_destroy(&fixture->cache);
}

static bool init_tree(cache_fixture_t *fixture, bfs_btree_t *tree,
                      const bfs_btree_ops_t *ops, bfs_blk_t root)
{
    return bfs_btree_init(tree, &fixture->cache.bio, &fixture->allocator,
                          ops, root, 7) == BFS_OK;
}

static void finish_raw_node(memory_bio_t *memory, const bfs_btree_ops_t *ops,
                            bfs_blk_t block, uint8_t *buffer)
{
    bfs_btree_t layout = {.bio = &memory->bio, .ops = ops};
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)buffer;
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&layout, buffer));
    memcpy(memory->blocks[block], buffer, BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
}

static void init_raw_header(uint8_t *buffer, uint16_t level, uint32_t count)
{
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)buffer;
    memset(buffer, 0, BLOCK_SIZE);
    header->magic = bfs_be32(BFS_NODE_MAGIC);
    header->crc32 = 0;
    header->txn_id = bfs_be64(7);
    header->num_keys = bfs_be32(count);
    header->level = bfs_be16(level);
    header->flags = 0;
    header->right_sibling = bfs_be32(BFS_BLK_NULL);
}

static void build_leaf(memory_bio_t *memory, const bfs_btree_ops_t *ops,
                       bfs_blk_t block, const uint32_t *keys,
                       const uint32_t *values, uint32_t count)
{
    uint8_t buffer[BLOCK_SIZE];
    init_raw_header(buffer, BFS_BTNODE_LEAF, count);
    bfs_btree_t layout = {.bio = &memory->bio, .ops = ops};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key = bfs_be32(keys[i]);
        uint32_t value = bfs_be32(values[i]);
        memcpy(node_key(&layout, buffer, i), &key, sizeof(key)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        memcpy(leaf_val(&layout, buffer, i), &value, sizeof(value)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    }
    finish_raw_node(memory, ops, block, buffer);
}

static void build_internal_at(memory_bio_t *memory, const bfs_btree_ops_t *ops,
                              bfs_blk_t block, uint16_t level, const uint32_t *keys,
                              const bfs_blk_t *children, uint32_t count)
{
    uint8_t buffer[BLOCK_SIZE];
    init_raw_header(buffer, level, count);
    bfs_btree_t layout = {.bio = &memory->bio, .ops = ops};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key = bfs_be32(keys[i]);
        memcpy(node_key(&layout, buffer, i), &key, sizeof(key)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    }
    for (uint32_t i = 0; i <= count; i++)
        set_child(&layout, buffer, i, children[i]);
    finish_raw_node(memory, ops, block, buffer);
}

static void build_internal(memory_bio_t *memory, const bfs_btree_ops_t *ops,
                           bfs_blk_t block, const uint32_t *keys,
                           const bfs_blk_t *children, uint32_t count)
{
    build_internal_at(memory, ops, block, 1, keys, children, count);
}

static void make_validation_token(const bfs_btree_ops_t *ops,
                                  const memory_bio_t *memory,
                                  bfs_node_validation_t *token)
{
    token->key_compare = ops->key_compare;
    token->key_size = ops->key_size;
    token->val_size = ops->val_size;
    token->block_size = memory->bio.block_size;
    token->block_count = memory->bio.block_count;
}

static void build_counted_leaf(memory_bio_t *memory,
                               const bfs_btree_ops_t *ops,
                               bfs_blk_t block, uint32_t count)
{
    uint32_t keys[LEAF_KEYS], values[LEAF_KEYS];
    for (uint32_t i = 0; i < count; i++) {
        keys[i] = i * 2;
        values[i] = i + 100;
    }
    build_leaf(memory, ops, block, keys, values, count);
}

static bool open_counted_leaf(cache_fixture_t *fixture, memory_bio_t *memory,
                              bfs_btree_t *tree, const bfs_btree_ops_t *ops,
                              uint32_t count)
{
    if (!cache_fixture_init(fixture, memory, 4)) return false;
    build_counted_leaf(memory, ops, 1, count);
    return init_tree(fixture, tree, ops, 1);
}

static void test_opt_in_cache_skips_repeated_key_order_scan(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    TEST_ASSERT(open_counted_leaf(&fixture, &memory, &tree, &ops_a, LEAF_KEYS));

    bfs_cache_invalidate(&fixture.cache);
    compare_a_calls = 0;
    uint32_t key = bfs_be32(62), value;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_OK);
    uint32_t after_first = compare_a_calls;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_OK);
    uint32_t after_second = compare_a_calls;
    uint32_t second_delta = after_second - after_first;

    TEST_ASSERT(after_first >= LEAF_KEYS - 1);
    TEST_ASSERT(second_delta > 0);
    TEST_ASSERT(second_delta < LEAF_KEYS - 1);
    TEST_ASSERT_EQ(memory.reads, 2); /* init read, then one post-invalidate read */
    TEST_ASSERT_EQ(value, bfs_be32(131));
    cache_fixture_destroy(&fixture);
}

static void test_default_stateful_comparator_revalidates_each_read(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    TEST_ASSERT(open_counted_leaf(&fixture, &memory, &tree,
                                  &ops_stateful, LEAF_KEYS));
    bfs_node_validation_t token;
    make_validation_token(&ops_stateful, &memory, &token);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));

    bfs_cache_invalidate(&fixture.cache);
    stateful_compare_calls = 0;
    uint32_t key = bfs_be32(40), value;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_OK);
    uint32_t after_first = stateful_compare_calls;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_OK);
    uint32_t after_second = stateful_compare_calls;

    TEST_ASSERT(after_first >= LEAF_KEYS - 1);
    TEST_ASSERT(after_second - after_first >= LEAF_KEYS - 1);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));
    cache_fixture_destroy(&fixture);
}

static void test_comparator_function_change_revalidates_node(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    TEST_ASSERT(open_counted_leaf(&fixture, &memory, &tree, &ops_a, LEAF_KEYS));
    bfs_node_validation_t token_a, token_b;
    make_validation_token(&ops_a, &memory, &token_a);
    make_validation_token(&ops_b, &memory, &token_b);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token_a));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token_b));

    compare_b_calls = 0;
    tree.ops = &ops_b;
    uint32_t key = bfs_be32(50), value;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_OK);
    uint32_t after_first = compare_b_calls;
    TEST_ASSERT(after_first >= LEAF_KEYS - 1);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token_b));
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_OK);
    TEST_ASSERT(compare_b_calls - after_first < LEAF_KEYS - 1);
    cache_fixture_destroy(&fixture);
}

static void test_default_comparator_semantics_change_revalidates(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    TEST_ASSERT(open_counted_leaf(&fixture, &memory, &tree,
                                  &ops_stateful, LEAF_KEYS));
    uint32_t reads = memory.reads, writes = memory.writes;
    TEST_ASSERT(bfs_bio_node_crc_valid(&fixture.cache.bio, tree.root));
    stateful_reverse = true;
    uint32_t key = bfs_be32(20), value;
    bfs_err_t err = bfs_btree_search(&tree, &key, &value);
    stateful_reverse = false;
    TEST_ASSERT_EQ(err, BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(memory.reads, reads);
    TEST_ASSERT_EQ(memory.writes, writes);
    cache_fixture_destroy(&fixture);
}

static void test_mutated_ops_object_revalidates_actual_tree_read(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    bfs_btree_ops_t mutable_ops = ops_a;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 4));
    build_counted_leaf(&memory, &ops_a, 1, LEAF_KEYS);
    TEST_ASSERT(init_tree(&fixture, &tree, &mutable_ops, 1));

    bfs_node_validation_t original_token;
    make_validation_token(&mutable_ops, &memory, &original_token);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1,
                                              &original_token));

    /* Keep the ops object's address fixed while changing the leaf capacity.
     * The cached structural result must not bypass the new shape check. */
    mutable_ops.val_size = 16;
    uint32_t key = bfs_be32(20), value;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_ERR_CORRUPT);

    bfs_node_validation_t changed_token;
    make_validation_token(&mutable_ops, &memory, &changed_token);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1,
                                               &changed_token));
    cache_fixture_destroy(&fixture);
}

static void test_validation_token_compares_each_field(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 2));
    build_counted_leaf(&memory, &ops_a, 1, LEAF_KEYS);
    uint8_t buffer[BLOCK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(&fixture.cache.bio, 1, buffer), BFS_OK);

    bfs_node_validation_t base;
    make_validation_token(&ops_a, &memory, &base);
    bfs_bio_mark_node_crc_valid(&fixture.cache.bio, 1);
    bfs_bio_mark_node_structure_valid(&fixture.cache.bio, 1, &base);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &base));

    bfs_node_validation_t changed = base;
    changed.key_compare = cmp_u32b;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &changed));
    changed = base;
    changed.key_size++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &changed));
    changed = base;
    changed.val_size++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &changed));
    changed = base;
    changed.block_size++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &changed));
    changed = base;
    changed.block_count++;
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &changed));
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &base));
    cache_fixture_destroy(&fixture);
}

static bool corrupt_order_with_valid_crc(memory_bio_t *memory,
                                         const bfs_btree_ops_t *ops,
                                         bfs_blk_t block, uint8_t *bad)
{
    memcpy(bad, memory->blocks[block], BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_btree_t layout = {.bio = &memory->bio, .ops = ops};
    memcpy(node_key(&layout, bad, 1), node_key(&layout, bad, 0),
           ops->key_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    finish_raw_node(memory, ops, block, bad);
    return bfs_be32(((bfs_btnode_hdr_t *)bad)->crc32) ==
           node_compute_crc(&layout, bad);
}

static bool test_corrupt_write_case(bool trusted_node_write)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    if (!cache_fixture_init(&fixture, &memory, 4)) return false;
    build_counted_leaf(&memory, &ops_a, 1, LEAF_KEYS);
    if (!init_tree(&fixture, &tree, &ops_a, 1)) return false;
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);
    if (!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token)) {
        cache_fixture_destroy(&fixture);
        return false;
    }
    if (trusted_node_write)
        bfs_cache_set_node_write_retention(&fixture.cache, true);

    uint8_t bad[BLOCK_SIZE];
    if (!corrupt_order_with_valid_crc(&memory, &ops_a, 1, bad)) {
        cache_fixture_destroy(&fixture);
        return false;
    }
    bfs_err_t write_err = trusted_node_write
        ? bfs_bio_write_node(&fixture.cache.bio, 1, bad)
        : bfs_bio_write(&fixture.cache.bio, 1, bad);
    bool invalidated = !bfs_bio_node_structure_valid(&fixture.cache.bio, 1,
                                                       &token);
    bool crc_state = bfs_bio_node_crc_valid(&fixture.cache.bio, 1);
    uint32_t key = bfs_be32(0), value;
    compare_a_calls = 0;
    bfs_err_t search_err = bfs_btree_search(&tree, &key, &value);
    /* The deliberately duplicated first pair is rejected at the first
     * comparison, but only after structure validation ran again. */
    bool revalidated = compare_a_calls > 0;
    cache_fixture_destroy(&fixture);
    return write_err == BFS_OK && invalidated &&
           crc_state == trusted_node_write && search_err == BFS_ERR_CORRUPT &&
           revalidated;
}

static void test_ordinary_write_clears_structure_and_bad_order_fails(void)
{
    TEST_ASSERT(test_corrupt_write_case(false));
}

static void test_trusted_write_keeps_only_crc_validation(void)
{
    TEST_ASSERT(test_corrupt_write_case(true));
}

static void test_stateful_comparator_rejects_crc_valid_bad_order(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    TEST_ASSERT(open_counted_leaf(&fixture, &memory, &tree,
                                  &ops_stateful, LEAF_KEYS));
    bfs_node_validation_t token;
    make_validation_token(&ops_stateful, &memory, &token);
    TEST_ASSERT(bfs_bio_node_crc_valid(&fixture.cache.bio, 1));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));

    uint8_t bad[BLOCK_SIZE];
    TEST_ASSERT(corrupt_order_with_valid_crc(&memory, &ops_stateful, 1, bad));
    bfs_cache_set_node_write_retention(&fixture.cache, true);
    TEST_ASSERT_EQ(bfs_bio_write_node(&fixture.cache.bio, 1, bad), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&fixture.cache.bio, 1));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));

    stateful_compare_calls = 0;
    uint32_t key = bfs_be32(0), value;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_ERR_CORRUPT);
    TEST_ASSERT(stateful_compare_calls > 0);
    cache_fixture_destroy(&fixture);
}

static bool test_failed_write_clears_case(bool trusted_node_write)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    bfs_btree_t tree;
    if (!cache_fixture_init(&fixture, &memory, 4)) return false;
    build_counted_leaf(&memory, &ops_a, 1, LEAF_KEYS);
    if (!init_tree(&fixture, &tree, &ops_a, 1)) return false;
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);
    if (!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token)) {
        cache_fixture_destroy(&fixture);
        return false;
    }
    if (trusted_node_write)
        bfs_cache_set_node_write_retention(&fixture.cache, true);
    uint8_t same[BLOCK_SIZE];
    memcpy(same, memory.blocks[1], BLOCK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    memory.fail_write = true;
    bfs_err_t write_err = trusted_node_write
        ? bfs_bio_write_node(&fixture.cache.bio, 1, same)
        : bfs_bio_write(&fixture.cache.bio, 1, same);
    memory.fail_write = false;
    bool invalidated = !bfs_bio_node_structure_valid(&fixture.cache.bio, 1,
                                                       &token);
    uint32_t reads_before_search = memory.reads;
    uint32_t key = bfs_be32(20), value;
    compare_a_calls = 0;
    bfs_err_t search_err = bfs_btree_search(&tree, &key, &value);
    bool reloaded = memory.reads == reads_before_search + 1;
    bool structurally_revalidated = compare_a_calls >= LEAF_KEYS - 1;
    cache_fixture_destroy(&fixture);
    return write_err == BFS_ERR_IO && invalidated && reloaded &&
           search_err == BFS_OK && bfs_load_be32(&value) == 110 &&
           structurally_revalidated;
}

static void test_failed_ordinary_and_trusted_writes_evict_validation(void)
{
    TEST_ASSERT(test_failed_write_clears_case(false));
    TEST_ASSERT(test_failed_write_clears_case(true));
}

static void test_eviction_and_invalidate_clear_structure(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 1));
    build_counted_leaf(&memory, &ops_a, 1, LEAF_KEYS);
    build_counted_leaf(&memory, &ops_a, 2, LEAF_KEYS);
    uint8_t buffer[BLOCK_SIZE];
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);

    TEST_ASSERT_EQ(bfs_bio_read(&fixture.cache.bio, 1, buffer), BFS_OK);
    bfs_bio_mark_node_crc_valid(&fixture.cache.bio, 1);
    bfs_bio_mark_node_structure_valid(&fixture.cache.bio, 1, &token);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));
    TEST_ASSERT_EQ(bfs_bio_read(&fixture.cache.bio, 2, buffer), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));

    bfs_bio_mark_node_crc_valid(&fixture.cache.bio, 2);
    bfs_bio_mark_node_structure_valid(&fixture.cache.bio, 2, &token);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 2, &token));
    bfs_cache_set_node_write_retention(&fixture.cache, true);
    TEST_ASSERT_EQ(bfs_bio_write_node(&fixture.cache.bio, 1, memory.blocks[1]),
                   BFS_OK);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 2, &token));
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));
    bfs_btree_t tree;
    TEST_ASSERT(init_tree(&fixture, &tree, &ops_a, 1));
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));
    /* A media change with equal geometry is detectable only after the caller
     * invalidates. The new bytes must undergo the full node-local checks. */
    uint8_t changed[BLOCK_SIZE];
    TEST_ASSERT(corrupt_order_with_valid_crc(&memory, &ops_a, 1, changed));
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));
    TEST_ASSERT(!bfs_bio_node_crc_valid(&fixture.cache.bio, 1));
    uint32_t key = bfs_be32(0), value;
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &value), BFS_ERR_CORRUPT);
    cache_fixture_destroy(&fixture);
}

static void test_trusted_count_zero_is_not_structurally_cached(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 2));
    bfs_cache_set_node_write_retention(&fixture.cache, true);
    uint8_t empty[BLOCK_SIZE];
    init_raw_header(empty, BFS_BTNODE_LEAF, 0);
    finish_raw_node(&memory, &ops_a, 1, empty);
    TEST_ASSERT_EQ(bfs_bio_write_node(&fixture.cache.bio, 1, empty), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&fixture.cache.bio, 1));
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));

    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &fixture.cache.bio,
                                  &fixture.allocator, &ops_a, 1, 7),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT(!bfs_bio_node_structure_valid(&fixture.cache.bio, 1, &token));
    cache_fixture_destroy(&fixture);
}

static void test_expected_level_checked_for_warmed_child(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 8));
    uint32_t left_keys[] = {10, 20}, left_values[] = {110, 120};
    uint32_t right_keys[] = {35, 40}, right_values[] = {135, 140};
    uint32_t outer_right_keys[] = {60, 70}, outer_right_values[] = {160, 170};
    build_leaf(&memory, &ops_a, 3, left_keys, left_values, 2);
    build_leaf(&memory, &ops_a, 4, right_keys, right_values, 2);
    build_leaf(&memory, &ops_a, 5, outer_right_keys, outer_right_values, 2);
    uint32_t child_separator[] = {30};
    bfs_blk_t child_children[] = {3, 4};
    build_internal(&memory, &ops_a, 2, child_separator, child_children, 1);
    uint32_t parent_separator[] = {50};
    bfs_blk_t parent_children[] = {2, 5};
    build_internal(&memory, &ops_a, 1, parent_separator, parent_children, 1);

    bfs_btree_t standalone, parent;
    TEST_ASSERT(init_tree(&fixture, &standalone, &ops_a, 2));
    uint32_t key = bfs_be32(10), value;
    TEST_ASSERT_EQ(bfs_btree_search(&standalone, &key, &value), BFS_OK);
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 2, &token));

    TEST_ASSERT(init_tree(&fixture, &parent, &ops_a, 1));
    TEST_ASSERT_EQ(bfs_btree_search(&parent, &key, &value), BFS_ERR_CORRUPT);
    cache_fixture_destroy(&fixture);
}

/* Searches use validated resident nodes in place. A warmed leaf reached where
 * the parent expects an internal node must still be rejected. */
static void test_warmed_leaf_at_internal_level_rejected(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 8));
    uint32_t leaf_keys[] = {10, 20}, leaf_values[] = {110, 120};
    uint32_t right_keys[] = {60, 70}, right_values[] = {160, 170};
    build_leaf(&memory, &ops_a, 3, leaf_keys, leaf_values, 2);
    build_leaf(&memory, &ops_a, 4, right_keys, right_values, 2);
    uint32_t inner_separator[] = {65};
    bfs_blk_t inner_children[] = {4, 4};
    build_internal(&memory, &ops_a, 2, inner_separator, inner_children, 1);
    uint32_t root_separator[] = {50};
    bfs_blk_t root_children[] = {3, 2};
    build_internal_at(&memory, &ops_a, 1, 2, root_separator, root_children, 1);

    bfs_btree_t standalone, root;
    TEST_ASSERT(init_tree(&fixture, &standalone, &ops_a, 3));
    uint32_t key = bfs_be32(10), value;
    TEST_ASSERT_EQ(bfs_btree_search(&standalone, &key, &value), BFS_OK);
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 3, &token));

    TEST_ASSERT(init_tree(&fixture, &root, &ops_a, 1));
    TEST_ASSERT_EQ(bfs_btree_search(&root, &key, &value), BFS_ERR_CORRUPT);
    uint32_t found_key;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&root, &key, &found_key, &value),
                   BFS_ERR_CORRUPT);
    cache_fixture_destroy(&fixture);
}

static void test_parent_bounds_checked_for_warmed_child(void)
{
    cache_fixture_t fixture;
    memory_bio_t memory;
    TEST_ASSERT(cache_fixture_init(&fixture, &memory, 8));
    uint32_t left_keys[] = {10, 20}, left_values[] = {110, 120};
    uint32_t bad_right_keys[] = {40, 45}, bad_right_values[] = {140, 145};
    build_leaf(&memory, &ops_a, 3, left_keys, left_values, 2);
    build_leaf(&memory, &ops_a, 2, bad_right_keys, bad_right_values, 2);
    uint32_t separator[] = {50};
    bfs_blk_t children[] = {3, 2};
    build_internal(&memory, &ops_a, 1, separator, children, 1);

    bfs_btree_t standalone, parent;
    TEST_ASSERT(init_tree(&fixture, &standalone, &ops_a, 2));
    uint32_t warm_key = bfs_be32(40), value;
    TEST_ASSERT_EQ(bfs_btree_search(&standalone, &warm_key, &value), BFS_OK);
    bfs_node_validation_t token;
    make_validation_token(&ops_a, &memory, &token);
    TEST_ASSERT(bfs_bio_node_structure_valid(&fixture.cache.bio, 2, &token));

    TEST_ASSERT(init_tree(&fixture, &parent, &ops_a, 1));
    uint32_t route_right = bfs_be32(60);
    TEST_ASSERT_EQ(bfs_btree_search(&parent, &route_right, &value),
                   BFS_ERR_CORRUPT);
    cache_fixture_destroy(&fixture);
}

TEST_SUITE_BEGIN("B+tree structure validation cache")
    TEST_RUN(test_opt_in_cache_skips_repeated_key_order_scan);
    TEST_RUN(test_default_stateful_comparator_revalidates_each_read);
    TEST_RUN(test_comparator_function_change_revalidates_node);
    TEST_RUN(test_default_comparator_semantics_change_revalidates);
    TEST_RUN(test_mutated_ops_object_revalidates_actual_tree_read);
    TEST_RUN(test_validation_token_compares_each_field);
    TEST_RUN(test_ordinary_write_clears_structure_and_bad_order_fails);
    TEST_RUN(test_trusted_write_keeps_only_crc_validation);
    TEST_RUN(test_stateful_comparator_rejects_crc_valid_bad_order);
    TEST_RUN(test_failed_ordinary_and_trusted_writes_evict_validation);
    TEST_RUN(test_eviction_and_invalidate_clear_structure);
    TEST_RUN(test_trusted_count_zero_is_not_structurally_cached);
    TEST_RUN(test_expected_level_checked_for_warmed_child);
    TEST_RUN(test_parent_bounds_checked_for_warmed_child);
    TEST_RUN(test_warmed_leaf_at_internal_level_rejected);
TEST_SUITE_END()
