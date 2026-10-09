/* SPDX-License-Identifier: MPL-2.0 */
/* Adversarial tests for the validated resident-root floor-search path. */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"

#define BLOCK_SIZE 4096u
#define BLOCK_COUNT 16u
#define KEY_SIZE 3u
#define VALUE_SIZE 11u

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[BLOCK_COUNT][BLOCK_SIZE];
    uint32_t reads;
    uint32_t writes;
} memory_bio_t;

typedef struct {
    void *(*base_alloc)(bfs_bio_t *bio, size_t size);
    void (*base_free)(bfs_bio_t *bio, void *buffer);
    const void *(*base_peek)(bfs_bio_t *bio, bfs_blk_t block,
                             const bfs_node_validation_t *context);
    uint32_t calls;
    uint32_t peek_calls;
    bool fail;
} allocation_probe_t;

typedef struct {
    memory_bio_t memory;
    bfs_cache_t cache;
    bfs_bio_ops_t hooked_ops;
    const bfs_bio_ops_t *cache_ops;
    allocation_probe_t allocation;
    bfs_allocator_t allocator;
    bfs_btree_t tree;
} floor_fixture_t;

static allocation_probe_t *active_allocation_probe;

/* Deliberately not the built-in BE-u32 comparator. Ordering is by byte 1,
 * then byte 0, then byte 2; it is pure and stable for cache validation. */
static int compare_key24(const void *left, const void *right)
{
    const uint8_t *a = left;
    const uint8_t *b = right;
    static const uint8_t order[] = {1, 0, 2};
    for (uint32_t i = 0; i < sizeof(order); i++) {
        if (a[order[i]] < b[order[i]]) return -1;
        if (a[order[i]] > b[order[i]]) return 1;
    }
    return 0;
}

static bool valid_entry(const void *key_ptr, const void *value_ptr)
{
    const uint8_t *key = key_ptr;
    const uint8_t *value = value_ptr;
    return value[0] == (uint8_t)(key[0] ^ 0xa5u) &&
           value[1] == (uint8_t)(key[1] ^ 0x5au) &&
           value[2] == (uint8_t)(key[2] ^ 0x3cu);
}

static const bfs_btree_ops_t test_ops = {
    .key_compare = compare_key24,
    .key_size = KEY_SIZE,
    .val_size = VALUE_SIZE,
    .cache_key_order = true,
    .entry_ok = valid_entry,
};

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->reads++;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, memory->blocks[block], bio->block_size);
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    memory->writes++;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(memory->blocks[block], buffer, bio->block_size);
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

static void *probed_alloc(bfs_bio_t *bio, size_t size)
{
    allocation_probe_t *probe = active_allocation_probe;
    if (!probe) return NULL;
    probe->calls++;
    if (probe->fail) return NULL;
    return probe->base_alloc(bio, size);
}

static void probed_free(bfs_bio_t *bio, void *buffer)
{
    allocation_probe_t *probe = active_allocation_probe;
    if (probe) probe->base_free(bio, buffer);
}

static const void *probed_peek_valid_node(
    bfs_bio_t *bio, bfs_blk_t block,
    const bfs_node_validation_t *context)
{
    allocation_probe_t *probe = active_allocation_probe;
    if (!probe) return NULL;
    probe->peek_calls++;
    return probe->base_peek(bio, block, context);
}

static bfs_blk_t unused_alloc(bfs_allocator_t *allocator)
{
    (void)allocator;
    return BFS_BLK_NULL;
}

static bfs_err_t unused_dealloc(bfs_allocator_t *allocator, bfs_blk_t block)
{
    (void)allocator;
    (void)block;
    return BFS_OK;
}

static void make_key(uint8_t out[KEY_SIZE], uint8_t a, uint8_t b, uint8_t c)
{
    out[0] = a;
    out[1] = b;
    out[2] = c;
}

static void make_value(uint8_t out[VALUE_SIZE], const uint8_t key[KEY_SIZE],
                       uint8_t stamp)
{
    out[0] = (uint8_t)(key[0] ^ 0xa5u);
    out[1] = (uint8_t)(key[1] ^ 0x5au);
    out[2] = (uint8_t)(key[2] ^ 0x3cu);
    for (uint32_t i = 3; i < VALUE_SIZE; i++)
        out[i] = (uint8_t)(key[0] + key[1] + key[2] + stamp + i * 7u);
}

static void init_raw_node(uint8_t *buffer, uint16_t level, uint32_t count)
{
    memset(buffer, 0, BLOCK_SIZE);
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)buffer;
    header->magic = bfs_be32(BFS_NODE_MAGIC);
    header->txn_id = bfs_be64(23);
    header->num_keys = bfs_be32(count);
    header->level = bfs_be16(level);
    header->flags = 0;
    header->right_sibling = bfs_be32(BFS_BLK_NULL);
}

static void install_crc(floor_fixture_t *fixture, const bfs_btree_ops_t *ops,
                        bfs_blk_t block)
{
    bfs_btree_t layout = {.bio = &fixture->memory.bio, .ops = ops};
    bfs_btnode_hdr_t *header =
        (bfs_btnode_hdr_t *)fixture->memory.blocks[block];
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&layout,
                                              fixture->memory.blocks[block]));
}

static void finish_raw_node(floor_fixture_t *fixture,
                            const bfs_btree_ops_t *ops, bfs_blk_t block,
                            uint8_t *buffer)
{
    bfs_btree_t layout = {.bio = &fixture->memory.bio, .ops = ops};
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)buffer;
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&layout, buffer));
    memcpy(fixture->memory.blocks[block], buffer, BLOCK_SIZE);
}

static void build_leaf(floor_fixture_t *fixture, const bfs_btree_ops_t *ops,
                       bfs_blk_t block, const uint8_t *keys,
                       const uint8_t *values, uint32_t count)
{
    uint8_t buffer[BLOCK_SIZE];
    init_raw_node(buffer, BFS_BTNODE_LEAF, count);
    bfs_btree_t layout = {.bio = &fixture->memory.bio, .ops = ops};
    for (uint32_t i = 0; i < count; i++) {
        memcpy(node_key(&layout, buffer, i), keys + i * KEY_SIZE, KEY_SIZE);
        memcpy(leaf_val(&layout, buffer, i), values + i * VALUE_SIZE,
               VALUE_SIZE);
    }
    finish_raw_node(fixture, ops, block, buffer);
}

static void build_internal(floor_fixture_t *fixture,
                           const bfs_btree_ops_t *ops, bfs_blk_t block,
                           uint16_t level, const uint8_t *keys,
                           const bfs_blk_t *children, uint32_t count)
{
    uint8_t buffer[BLOCK_SIZE];
    init_raw_node(buffer, level, count);
    bfs_btree_t layout = {.bio = &fixture->memory.bio, .ops = ops};
    for (uint32_t i = 0; i < count; i++)
        memcpy(node_key(&layout, buffer, i), keys + i * KEY_SIZE, KEY_SIZE);
    for (uint32_t i = 0; i <= count; i++)
        set_child(&layout, buffer, i, children[i]);
    finish_raw_node(fixture, ops, block, buffer);
}

static bool fixture_open(floor_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->memory.bio.ops = &memory_ops;
    fixture->memory.bio.block_size = BLOCK_SIZE;
    fixture->memory.bio.block_count = BLOCK_COUNT;
    if (bfs_cache_init(&fixture->cache, &fixture->memory.bio, 4) != BFS_OK)
        return false;

    fixture->cache_ops = fixture->cache.bio.ops;
    fixture->allocation.base_alloc = fixture->cache_ops->alloc_buffer;
    fixture->allocation.base_free = fixture->cache_ops->free_buffer;
    fixture->allocation.base_peek = fixture->cache_ops->peek_valid_node;
    fixture->hooked_ops = *fixture->cache_ops;
    fixture->hooked_ops.alloc_buffer = probed_alloc;
    fixture->hooked_ops.free_buffer = probed_free;
    fixture->hooked_ops.peek_valid_node = probed_peek_valid_node;
    fixture->cache.bio.ops = &fixture->hooked_ops;
    active_allocation_probe = &fixture->allocation;

    fixture->allocator.alloc = unused_alloc;
    fixture->allocator.dealloc = unused_dealloc;
    fixture->allocator.error = NULL;
    fixture->allocator.ctx = NULL;
    return true;
}

static bool fixture_init_tree(floor_fixture_t *fixture, bfs_blk_t root)
{
    return bfs_btree_init(&fixture->tree, &fixture->cache.bio,
                          &fixture->allocator, &test_ops, root, 23) == BFS_OK;
}

static void fixture_close(floor_fixture_t *fixture)
{
    active_allocation_probe = NULL;
    fixture->cache.bio.ops = fixture->cache_ops;
    bfs_cache_destroy(&fixture->cache);
}

static void make_validation_token(const floor_fixture_t *fixture,
                                  const bfs_btree_ops_t *ops,
                                  bfs_node_validation_t *token)
{
    token->key_compare = ops->key_compare;
    token->key_size = ops->key_size;
    token->val_size = ops->val_size;
    token->block_size = fixture->memory.bio.block_size;
    token->block_count = fixture->memory.bio.block_count;
}

static bool floor_matches_tree(bfs_btree_t *tree,
                               const uint8_t key[KEY_SIZE],
                               const uint8_t expected_key[KEY_SIZE],
                               const uint8_t expected_value[VALUE_SIZE])
{
    uint8_t found_key[KEY_SIZE], found_value[VALUE_SIZE];
    return bfs_btree_search_floor(tree, key, found_key,
                                  found_value) == BFS_OK &&
           memcmp(found_key, expected_key, KEY_SIZE) == 0 &&
           memcmp(found_value, expected_value, VALUE_SIZE) == 0;
}

static bool floor_matches(floor_fixture_t *fixture,
                          const uint8_t key[KEY_SIZE],
                          const uint8_t expected_key[KEY_SIZE],
                          const uint8_t expected_value[VALUE_SIZE])
{
    return floor_matches_tree(&fixture->tree, key, expected_key,
                              expected_value);
}

static void make_three_entries(uint8_t keys[3 * KEY_SIZE],
                               uint8_t values[3 * VALUE_SIZE], uint8_t stamp)
{
    make_key(keys, 0x02, 0x10, 0xf0);
    make_key(keys + KEY_SIZE, 0x01, 0x40, 0x01);
    make_key(keys + 2 * KEY_SIZE, 0x03, 0x90, 0x00);
    for (uint32_t i = 0; i < 3; i++)
        make_value(values + i * VALUE_SIZE, keys + i * KEY_SIZE, stamp);
}

static void test_validated_root_needs_no_scratch_for_any_floor_case(void)
{
    floor_fixture_t fixture;
    uint8_t keys[3 * KEY_SIZE], values[3 * VALUE_SIZE];
    make_three_entries(keys, values, 0x11);
    TEST_ASSERT(fixture_open(&fixture));
    build_leaf(&fixture, &test_ops, 1, keys, values, 3);
    TEST_ASSERT(fixture_init_tree(&fixture, 1));

    TEST_ASSERT(floor_matches(&fixture, keys + KEY_SIZE,
                              keys + KEY_SIZE, values + VALUE_SIZE));
    bfs_node_validation_t token;
    make_validation_token(&fixture, &test_ops, &token);
    TEST_ASSERT(bfs_bio_peek_valid_node(&fixture.cache.bio, 1, &token) != NULL);

    fixture.allocation.calls = 0;
    fixture.allocation.peek_calls = 0;
    fixture.allocation.fail = true;
    uint8_t between[KEY_SIZE], below[KEY_SIZE], above[KEY_SIZE];
    make_key(between, 0x77, 0x20, 0x22);
    make_key(below, 0xff, 0x00, 0xff);
    make_key(above, 0x00, 0xf0, 0x00);
    TEST_ASSERT(floor_matches(&fixture, keys + KEY_SIZE,
                              keys + KEY_SIZE, values + VALUE_SIZE));
    TEST_ASSERT(floor_matches(&fixture, between, keys, values));
    TEST_ASSERT(floor_matches(&fixture, above, keys + 2 * KEY_SIZE,
                              values + 2 * VALUE_SIZE));

    uint8_t found_key[KEY_SIZE] = {0x9a, 0xbc, 0xde};
    uint8_t found_value[VALUE_SIZE];
    memset(found_value, 0xe7, sizeof(found_value));
    uint8_t original_key[KEY_SIZE], original_value[VALUE_SIZE];
    memcpy(original_key, found_key, sizeof(found_key));
    memcpy(original_value, found_value, sizeof(found_value));
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.tree, below, found_key,
                                          found_value), BFS_ERR_NOTFOUND);
    TEST_ASSERT_MEM_EQ(found_key, original_key, sizeof(found_key));
    TEST_ASSERT_MEM_EQ(found_value, original_value, sizeof(found_value));
    TEST_ASSERT_EQ(fixture.allocation.calls, 0);
    TEST_ASSERT_EQ(fixture.allocation.peek_calls, 4);
    fixture_close(&fixture);
}

static void test_cold_root_falls_back_and_checks_crc(void)
{
    floor_fixture_t fixture;
    uint8_t keys[3 * KEY_SIZE], values[3 * VALUE_SIZE];
    make_three_entries(keys, values, 0x21);
    TEST_ASSERT(fixture_open(&fixture));
    build_leaf(&fixture, &test_ops, 1, keys, values, 3);
    TEST_ASSERT(fixture_init_tree(&fixture, 1));
    bfs_cache_invalidate(&fixture.cache);
    fixture.allocation.calls = 0;

    uint8_t query[KEY_SIZE], found_key[KEY_SIZE], found_value[VALUE_SIZE];
    make_key(query, 0x11, 0x40, 0x02);
    fixture.allocation.fail = true;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.tree, query, found_key,
                                          found_value), BFS_ERR_NOMEM);
    TEST_ASSERT_EQ(fixture.allocation.calls, 1);

    fixture.allocation.fail = false;
    TEST_ASSERT(floor_matches(&fixture, query, keys + KEY_SIZE,
                              values + VALUE_SIZE));
    bfs_node_validation_t token;
    make_validation_token(&fixture, &test_ops, &token);
    TEST_ASSERT(bfs_bio_peek_valid_node(&fixture.cache.bio, 1, &token) != NULL);

    /* A disk mutation outside the cache followed by invalidation loses both
     * CRC and structural validation. The cold read must check the CRC again. */
    fixture.memory.blocks[1][BLOCK_SIZE - 1] ^= 1u;
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.tree, query, found_key,
                                          found_value), BFS_ERR_CORRUPT);
    fixture_close(&fixture);
}

static void test_crc_consistent_bad_order_and_entry_are_rejected(void)
{
    floor_fixture_t fixture;
    uint8_t keys[3 * KEY_SIZE], values[3 * VALUE_SIZE];
    make_three_entries(keys, values, 0x31);
    TEST_ASSERT(fixture_open(&fixture));
    build_leaf(&fixture, &test_ops, 1, keys, values, 3);
    TEST_ASSERT(fixture_init_tree(&fixture, 1));

    bfs_btree_t layout = {.bio = &fixture.memory.bio, .ops = &test_ops};
    memcpy(node_key(&layout, fixture.memory.blocks[1], 1),
           node_key(&layout, fixture.memory.blocks[1], 0), KEY_SIZE);
    install_crc(&fixture, &test_ops, 1);
    bfs_cache_invalidate(&fixture.cache);

    uint8_t found_key[KEY_SIZE], found_value[VALUE_SIZE];
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.tree, keys, found_key,
                                          found_value), BFS_ERR_CORRUPT);

    /* Restore sorted keys, then make one entry invalid while keeping a valid
     * full-block CRC. Structural validation must include entry_ok. */
    build_leaf(&fixture, &test_ops, 1, keys, values, 3);
    fixture.memory.blocks[1][(uint8_t *)leaf_val(&layout,
        fixture.memory.blocks[1], 1) - fixture.memory.blocks[1]] ^= 0x80u;
    install_crc(&fixture, &test_ops, 1);
    bfs_cache_invalidate(&fixture.cache);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.tree, keys, found_key,
                                          found_value), BFS_ERR_CORRUPT);
    fixture_close(&fixture);
}

static void test_wrong_height_leaf_and_resident_internal_root_are_corrupt(void)
{
    floor_fixture_t leaf_fixture;
    uint8_t keys[3 * KEY_SIZE], values[3 * VALUE_SIZE];
    make_three_entries(keys, values, 0x41);
    TEST_ASSERT(fixture_open(&leaf_fixture));
    build_leaf(&leaf_fixture, &test_ops, 1, keys, values, 3);
    TEST_ASSERT(fixture_init_tree(&leaf_fixture, 1));
    TEST_ASSERT(floor_matches(&leaf_fixture, keys, keys, values));
    leaf_fixture.tree.height = 2; /* Root is a leaf where level 1 is required. */
    TEST_ASSERT_EQ(bfs_btree_search_floor(&leaf_fixture.tree, keys, keys,
                                          values), BFS_ERR_CORRUPT);
    fixture_close(&leaf_fixture);

    floor_fixture_t internal_fixture;
    TEST_ASSERT(fixture_open(&internal_fixture));
    uint8_t left_keys[KEY_SIZE], split_key[KEY_SIZE], right_keys[KEY_SIZE];
    uint8_t left_value[VALUE_SIZE], right_value[VALUE_SIZE];
    make_key(left_keys, 0x02, 0x10, 0xf0);
    make_key(split_key, 0x01, 0x40, 0x01);
    make_key(right_keys, 0x03, 0x90, 0x00);
    make_value(left_value, left_keys, 0x51);
    make_value(right_value, right_keys, 0x51);
    build_leaf(&internal_fixture, &test_ops, 1, left_keys, left_value, 1);
    build_leaf(&internal_fixture, &test_ops, 3, right_keys,
               right_value, 1);
    bfs_blk_t children[] = {1, 3};
    build_internal(&internal_fixture, &test_ops, 2, 1, split_key, children, 1);
    TEST_ASSERT(fixture_init_tree(&internal_fixture, 2));
    TEST_ASSERT(floor_matches(&internal_fixture, right_keys, right_keys,
                              right_value));
    bfs_node_validation_t token;
    make_validation_token(&internal_fixture, &test_ops, &token);
    TEST_ASSERT(bfs_bio_peek_valid_node(&internal_fixture.cache.bio, 2,
                                        &token) != NULL);

    /* The cache token validates node-local structure, not traversal level.
     * A validated internal block at a height-one root must still be rejected. */
    internal_fixture.tree.height = 1;
    internal_fixture.allocation.calls = 0;
    internal_fixture.allocation.peek_calls = 0;
    internal_fixture.allocation.fail = true;
    TEST_ASSERT_EQ(bfs_btree_search_floor(&internal_fixture.tree, right_keys,
                                          keys, values), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(internal_fixture.allocation.calls, 0);
    TEST_ASSERT_EQ(internal_fixture.allocation.peek_calls, 1);
    fixture_close(&internal_fixture);
}

static void replace_value_byte(floor_fixture_t *fixture,
                               const bfs_btree_ops_t *ops, bfs_blk_t block,
                               uint32_t index, uint32_t offset, uint8_t value)
{
    bfs_btree_t layout = {.bio = &fixture->memory.bio, .ops = ops};
    ((uint8_t *)leaf_val(&layout, fixture->memory.blocks[block], index))[offset]
        = value;
    install_crc(fixture, ops, block);
}

static void test_mutation_rebind_and_invalidation_return_current_value(void)
{
    floor_fixture_t fixture;
    uint8_t keys[3 * KEY_SIZE], first_values[3 * VALUE_SIZE];
    uint8_t second_values[3 * VALUE_SIZE];
    make_three_entries(keys, first_values, 0x61);
    make_three_entries(keys, second_values, 0x71);
    TEST_ASSERT(fixture_open(&fixture));
    build_leaf(&fixture, &test_ops, 1, keys, first_values, 3);
    build_leaf(&fixture, &test_ops, 2, keys, second_values, 3);
    TEST_ASSERT(fixture_init_tree(&fixture, 1));

    uint8_t query[KEY_SIZE];
    memcpy(query, keys + KEY_SIZE, KEY_SIZE);
    TEST_ASSERT(floor_matches(&fixture, query, query,
                              first_values + VALUE_SIZE));

    /* A regular cached write refreshes the resident bytes and clears their
     * structural-validation token. The next floor read must see the new value. */
    uint8_t updated_root[BLOCK_SIZE];
    memcpy(updated_root, fixture.memory.blocks[1], sizeof(updated_root));
    bfs_btree_t layout = {.bio = &fixture.memory.bio, .ops = &test_ops};
    uint8_t *value = leaf_val(&layout, updated_root, 1);
    value[8] ^= 0x33u;
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)updated_root;
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&layout, updated_root));
    TEST_ASSERT_EQ(bfs_bio_write(&fixture.cache.bio, 1, updated_root), BFS_OK);
    first_values[VALUE_SIZE + 8] = value[8];
    fixture.allocation.fail = false;
    fixture.allocation.calls = 0;
    TEST_ASSERT(floor_matches(&fixture, query, query,
                              first_values + VALUE_SIZE));
    TEST_ASSERT(fixture.allocation.calls > 0);

    /* Rebind the same tree to another root already resident and validated. */
    bfs_btree_t other_root;
    TEST_ASSERT_EQ(bfs_btree_init(&other_root, &fixture.cache.bio,
                                  &fixture.allocator, &test_ops, 2, 23), BFS_OK);
    other_root.height = 1;
    TEST_ASSERT(floor_matches_tree(&other_root, query, query,
                                   second_values + VALUE_SIZE));
    fixture.tree.root = 2;
    fixture.allocation.calls = 0;
    fixture.allocation.fail = true;
    TEST_ASSERT(floor_matches(&fixture, query, query,
                              second_values + VALUE_SIZE));
    TEST_ASSERT_EQ(fixture.allocation.calls, 0);

    /* Change the backing device directly, invalidate the cache, and confirm
     * the subsequent cold traversal observes that new value. */
    uint8_t new_byte = (uint8_t)(second_values[VALUE_SIZE + 8] ^ 0x5au);
    replace_value_byte(&fixture, &test_ops, 2, 1, 8, new_byte);
    second_values[VALUE_SIZE + 8] = new_byte;
    bfs_cache_invalidate(&fixture.cache);
    fixture.allocation.fail = false;
    fixture.allocation.calls = 0;
    TEST_ASSERT(floor_matches(&fixture, query, query,
                              second_values + VALUE_SIZE));
    TEST_ASSERT(fixture.allocation.calls > 0);
    fixture_close(&fixture);
}

static void test_taller_tree_uses_fallback_and_turns_left_for_predecessor(void)
{
    floor_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    uint8_t left_keys[2 * KEY_SIZE], right_keys[2 * KEY_SIZE];
    uint8_t separator[KEY_SIZE], left_values[2 * VALUE_SIZE];
    uint8_t right_values[2 * VALUE_SIZE];
    make_key(left_keys, 0x02, 0x10, 0xf0);
    make_key(left_keys + KEY_SIZE, 0x05, 0x20, 0x12);
    make_key(separator, 0x07, 0x30, 0x80);
    make_key(right_keys, 0x01, 0x40, 0x01);
    make_key(right_keys + KEY_SIZE, 0x03, 0x90, 0x00);
    for (uint32_t i = 0; i < 2; i++) {
        make_value(left_values + i * VALUE_SIZE,
                   left_keys + i * KEY_SIZE, 0x81);
        make_value(right_values + i * VALUE_SIZE,
                   right_keys + i * KEY_SIZE, 0x81);
    }
    build_leaf(&fixture, &test_ops, 1, left_keys, left_values, 2);
    build_leaf(&fixture, &test_ops, 3, right_keys, right_values, 2);
    bfs_blk_t children[] = {1, 3};
    build_internal(&fixture, &test_ops, 2, 1, separator, children, 1);
    TEST_ASSERT(fixture_init_tree(&fixture, 2));

    /* Warm and validate the resident internal root and right leaf. */
    TEST_ASSERT(floor_matches(&fixture, right_keys + KEY_SIZE,
                              right_keys + KEY_SIZE,
                              right_values + VALUE_SIZE));
    bfs_node_validation_t token;
    make_validation_token(&fixture, &test_ops, &token);
    TEST_ASSERT(bfs_bio_peek_valid_node(&fixture.cache.bio, 2, &token) != NULL);

    uint8_t query[KEY_SIZE];
    make_key(query, 0x44, 0x35, 0x55);
    fixture.allocation.calls = 0;
    fixture.allocation.fail = true;
    uint8_t found_key[KEY_SIZE], found_value[VALUE_SIZE];
    TEST_ASSERT_EQ(bfs_btree_search_floor(&fixture.tree, query, found_key,
                                          found_value), BFS_ERR_NOMEM);
    TEST_ASSERT(fixture.allocation.calls > 0);

    fixture.allocation.fail = false;
    uint8_t expected_key[KEY_SIZE], expected_value[VALUE_SIZE];
    memcpy(expected_key, left_keys + KEY_SIZE, KEY_SIZE);
    memcpy(expected_value, left_values + VALUE_SIZE, VALUE_SIZE);
    TEST_ASSERT(floor_matches(&fixture, query, expected_key, expected_value));
    fixture_close(&fixture);
}

TEST_SUITE_BEGIN("B+tree resident-root floor search")
    TEST_RUN(test_validated_root_needs_no_scratch_for_any_floor_case);
    TEST_RUN(test_cold_root_falls_back_and_checks_crc);
    TEST_RUN(test_crc_consistent_bad_order_and_entry_are_rejected);
    TEST_RUN(test_wrong_height_leaf_and_resident_internal_root_are_corrupt);
    TEST_RUN(test_mutation_rebind_and_invalidation_return_current_value);
    TEST_RUN(test_taller_tree_uses_fallback_and_turns_left_for_predecessor);
TEST_SUITE_END()
