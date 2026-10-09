/* SPDX-License-Identifier: MPL-2.0 */
/* Focused public-API tests for B-tree operation scratch lifetime. */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"

#include <stdint.h>
#include <stdlib.h>

#define TEST_BLOCK_SIZE 4096u
#define TEST_BLOCK_COUNT 256u
#define TEST_KEY_SIZE BFS_MAX_KEY_SIZE
#define TEST_VALUE_SIZE 8u
#define TEST_ENTRY_COUNT 96u

typedef struct {
    bfs_bio_t bio;
    uint8_t *bytes;
    uint32_t reads;
    uint32_t write_calls;
    uint32_t successful_writes;
    uint32_t failed_writes;
    uint32_t write_fail_countdown;
} scratch_memory_bio_t;

typedef struct {
    bfs_allocator_t iface;
    bfs_blk_t next_block;
    bfs_blk_t returned[TEST_BLOCK_COUNT];
    uint32_t returned_count;
    bool owned[TEST_BLOCK_COUNT];
    int32_t fail_after_successes;
    bool injected_failure_pending;
    uint32_t invalid_allocations;
    uint32_t invalid_deallocations;
} scratch_allocator_t;

typedef struct {
    scratch_memory_bio_t memory;
    scratch_allocator_t allocator;
    bfs_btree_t tree;
} scratch_fixture_t;

static uint8_t *block_bytes(scratch_memory_bio_t *memory, bfs_blk_t block)
{
    return memory->bytes + (size_t)block * memory->bio.block_size;
}

static bfs_err_t scratch_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    scratch_memory_bio_t *memory = (scratch_memory_bio_t *)bio;
    memory->reads++;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, block_bytes(memory, block), bio->block_size);
    return BFS_OK;
}

static bfs_err_t scratch_write(bfs_bio_t *bio, bfs_blk_t block,
                               const void *buffer)
{
    scratch_memory_bio_t *memory = (scratch_memory_bio_t *)bio;
    memory->write_calls++;
    if (memory->write_fail_countdown != 0 &&
        --memory->write_fail_countdown == 0) {
        memory->failed_writes++;
        return BFS_ERR_IO;
    }
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(block_bytes(memory, block), buffer, bio->block_size);
    memory->successful_writes++;
    return BFS_OK;
}

static bfs_err_t scratch_sync(bfs_bio_t *bio)
{
    (void)bio;
    return BFS_OK;
}

static void scratch_close_bio(bfs_bio_t *bio)
{
    (void)bio;
}

static const bfs_bio_ops_t scratch_bio_ops = {
    .read_block = scratch_read,
    .write_block = scratch_write,
    .sync = scratch_sync,
    .close = scratch_close_bio,
};

static bfs_blk_t scratch_alloc(bfs_allocator_t *iface)
{
    scratch_allocator_t *allocator = (scratch_allocator_t *)iface->ctx;
    bfs_blk_t block;

    if (allocator->fail_after_successes == 0) {
        allocator->fail_after_successes = -1;
        allocator->injected_failure_pending = true;
        return BFS_BLK_NULL;
    }
    if (allocator->fail_after_successes > 0)
        allocator->fail_after_successes--;

    if (allocator->returned_count != 0) {
        block = allocator->returned[--allocator->returned_count];
    } else {
        block = allocator->next_block++;
    }
    if (block == BFS_BLK_NULL || block >= TEST_BLOCK_COUNT ||
        allocator->owned[block]) {
        allocator->invalid_allocations++;
        return BFS_BLK_NULL;
    }
    allocator->owned[block] = true;
    return block;
}

static bfs_err_t scratch_dealloc(bfs_allocator_t *iface, bfs_blk_t block)
{
    scratch_allocator_t *allocator = (scratch_allocator_t *)iface->ctx;
    if (block == BFS_BLK_NULL || block >= TEST_BLOCK_COUNT ||
        !allocator->owned[block] ||
        allocator->returned_count >= TEST_BLOCK_COUNT) {
        allocator->invalid_deallocations++;
        return BFS_ERR_CORRUPT;
    }
    allocator->owned[block] = false;
    allocator->returned[allocator->returned_count++] = block;
    return BFS_OK;
}

static bfs_err_t scratch_allocator_error(bfs_allocator_t *iface)
{
    scratch_allocator_t *allocator = (scratch_allocator_t *)iface->ctx;
    if (allocator->injected_failure_pending) {
        allocator->injected_failure_pending = false;
        return BFS_ERR_NOMEM;
    }
    return BFS_ERR_NOSPC;
}

static int compare_full_key(const void *left, const void *right)
{
    return memcmp(left, right, TEST_KEY_SIZE);
}

static const bfs_btree_ops_t full_key_ops = {
    .key_compare = compare_full_key,
    .key_size = TEST_KEY_SIZE,
    .val_size = TEST_VALUE_SIZE,
    .cache_key_order = true,
};

static bool fixture_open(scratch_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->memory.bytes = calloc(TEST_BLOCK_COUNT, TEST_BLOCK_SIZE);
    if (!fixture->memory.bytes) return false;

    fixture->memory.bio.ops = &scratch_bio_ops;
    fixture->memory.bio.block_size = TEST_BLOCK_SIZE;
    fixture->memory.bio.block_count = TEST_BLOCK_COUNT;
    fixture->allocator.iface.alloc = scratch_alloc;
    fixture->allocator.iface.dealloc = scratch_dealloc;
    fixture->allocator.iface.error = scratch_allocator_error;
    fixture->allocator.iface.ctx = &fixture->allocator;
    fixture->allocator.next_block = 1;
    fixture->allocator.fail_after_successes = -1;
    if (bfs_btree_init(&fixture->tree, &fixture->memory.bio,
                       &fixture->allocator.iface, &full_key_ops,
                       BFS_BLK_NULL, 7) != BFS_OK) {
        free(fixture->memory.bytes);
        fixture->memory.bytes = NULL;
        return false;
    }
    return true;
}

static void fixture_close(scratch_fixture_t *fixture)
{
    free(fixture->memory.bytes);
    fixture->memory.bytes = NULL;
}

static void make_key(uint8_t key[TEST_KEY_SIZE], uint32_t id)
{
    memset(key, 0xA5, TEST_KEY_SIZE);
    bfs_store_be32(key + TEST_KEY_SIZE - sizeof(uint32_t), id);
}

static void make_value(uint8_t value[TEST_VALUE_SIZE], uint32_t id)
{
    bfs_store_be32(value, id);
    bfs_store_be32(value + sizeof(uint32_t), id ^ 0x9E3779B9u);
}

static bfs_err_t insert_id(scratch_fixture_t *fixture, uint32_t id)
{
    uint8_t key[TEST_KEY_SIZE];
    uint8_t value[TEST_VALUE_SIZE];
    make_key(key, id);
    make_value(value, id);
    return bfs_btree_insert(&fixture->tree, key, value);
}

static bool search_value_is(scratch_fixture_t *fixture, uint32_t id)
{
    uint8_t key[TEST_KEY_SIZE];
    uint8_t actual[TEST_VALUE_SIZE];
    uint8_t expected[TEST_VALUE_SIZE];
    make_key(key, id);
    make_value(expected, id);
    return bfs_btree_search(&fixture->tree, key, actual) == BFS_OK &&
           memcmp(actual, expected, TEST_VALUE_SIZE) == 0;
}

typedef struct {
    uint32_t next_id;
    uint32_t expected_count;
    uint32_t seen;
    bool valid;
} scratch_scan_check_t;

static bool check_scan_entry(const void *key, const void *value, void *ctx)
{
    scratch_scan_check_t *check = (scratch_scan_check_t *)ctx;
    uint8_t expected_key[TEST_KEY_SIZE];
    uint8_t expected_value[TEST_VALUE_SIZE];

    if (check->seen >= check->expected_count) {
        check->valid = false;
        return false;
    }
    make_key(expected_key, check->next_id);
    make_value(expected_value, check->next_id);
    if (memcmp(key, expected_key, TEST_KEY_SIZE) != 0 ||
        memcmp(value, expected_value, TEST_VALUE_SIZE) != 0) {
        check->valid = false;
        return false;
    }
    check->seen++;
    check->next_id += 2;
    return true;
}

static bool stop_scan_entry(const void *key, const void *value, void *ctx)
{
    (void)key;
    (void)value;
    (void)ctx;
    return false;
}

static bool scan_ids_from(scratch_fixture_t *fixture, bool has_start,
                          uint32_t start_id, uint32_t first_id,
                          uint32_t expected_count)
{
    uint8_t start_key[TEST_KEY_SIZE];
    scratch_scan_check_t check = {
        .next_id = first_id,
        .expected_count = expected_count,
        .seen = 0,
        .valid = true,
    };
    if (has_start) make_key(start_key, start_id);
    bfs_err_t err = bfs_btree_scan(&fixture->tree,
                                   has_start ? start_key : NULL,
                                   check_scan_entry, &check);
    return err == BFS_OK && check.valid && check.seen == expected_count;
}

static bool build_even_id_tree(scratch_fixture_t *fixture, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (insert_id(fixture, i * 2) != BFS_OK) return false;
    return true;
}

static bool test_full_width_multilevel_search_and_scan(void)
{
    scratch_fixture_t fixture;
    if (!fixture_open(&fixture)) return false;
    if (!build_even_id_tree(&fixture, TEST_ENTRY_COUNT) ||
        fixture.tree.height < 3) {
        fixture_close(&fixture);
        return false;
    }

    /* The comparator consumes all 512 bytes, and IDs differ only in the last
     * four bytes. Search and scan must carry correct bounds across branches. */
    for (uint32_t i = 0; i < TEST_ENTRY_COUNT; i++) {
        if (!search_value_is(&fixture, i * 2)) {
            fixture_close(&fixture);
            return false;
        }
    }
    for (uint32_t i = TEST_ENTRY_COUNT; i-- > 0;) {
        if (!search_value_is(&fixture, i * 2)) {
            fixture_close(&fixture);
            return false;
        }
    }
    for (uint32_t i = 0; i < TEST_ENTRY_COUNT; i++) {
        uint8_t key[TEST_KEY_SIZE];
        uint8_t value[TEST_VALUE_SIZE];
        make_key(key, i * 2 + 1);
        if (bfs_btree_search(&fixture.tree, key, value) != BFS_ERR_NOTFOUND) {
            fixture_close(&fixture);
            return false;
        }
    }

    if (!scan_ids_from(&fixture, false, 0, 0, TEST_ENTRY_COUNT) ||
        !scan_ids_from(&fixture, true, 17, 18, 87) ||
        !scan_ids_from(&fixture, true, 64, 64, 64) ||
        !scan_ids_from(&fixture, true, 189, 190, 1) ||
        !scan_ids_from(&fixture, true, 191, 192, 0)) {
        fixture_close(&fixture);
        return false;
    }
    fixture_close(&fixture);
    return true;
}

static void raw_node_begin(uint8_t *node, uint16_t level, uint32_t count)
{
    memset(node, 0, TEST_BLOCK_SIZE);
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)node;
    header->magic = bfs_be32(BFS_NODE_MAGIC);
    header->crc32 = 0;
    header->txn_id = bfs_be64(7);
    header->num_keys = bfs_be32(count);
    header->level = bfs_be16(level);
    header->flags = bfs_be16(0);
    header->right_sibling = bfs_be32(BFS_BLK_NULL);
}

static void raw_node_finish(scratch_fixture_t *fixture, bfs_blk_t block,
                            uint8_t *node)
{
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)node;
    header->crc32 = 0;
    header->crc32 = bfs_be32(node_compute_crc(&fixture->tree, node));
    memcpy(block_bytes(&fixture->memory, block), node, TEST_BLOCK_SIZE);
}

static void raw_leaf(scratch_fixture_t *fixture, uint8_t *node,
                     bfs_blk_t block, const uint32_t *ids, uint32_t count)
{
    raw_node_begin(node, BFS_BTNODE_LEAF, count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t key[TEST_KEY_SIZE];
        uint8_t value[TEST_VALUE_SIZE];
        make_key(key, ids[i]);
        make_value(value, ids[i]);
        memcpy(node_key(&fixture->tree, node, i), key, TEST_KEY_SIZE);
        memcpy(leaf_val(&fixture->tree, node, i), value, TEST_VALUE_SIZE);
    }
    raw_node_finish(fixture, block, node);
}

static void raw_internal(scratch_fixture_t *fixture, uint8_t *node,
                         bfs_blk_t block, uint16_t level,
                         const uint32_t *key_ids, const bfs_blk_t *children,
                         uint32_t count)
{
    raw_node_begin(node, level, count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t key[TEST_KEY_SIZE];
        make_key(key, key_ids[i]);
        memcpy(node_key(&fixture->tree, node, i), key, TEST_KEY_SIZE);
    }
    for (uint32_t i = 0; i <= count; i++)
        set_child(&fixture->tree, node, i, children[i]);
    raw_node_finish(fixture, block, node);
}

static bool raw_crc_valid(scratch_fixture_t *fixture, bfs_blk_t block)
{
    uint8_t *node = block_bytes(&fixture->memory, block);
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)node;
    uint32_t stored = bfs_be32(header->crc32);
    return stored == node_compute_crc(&fixture->tree, node);
}

static bool test_crc_valid_child_with_wrong_inherited_bound(void)
{
    scratch_fixture_t fixture;
    if (!fixture_open(&fixture)) return false;

    uint8_t node[TEST_BLOCK_SIZE];
    const uint32_t leaf3[] = {0, 10};
    const uint32_t leaf4[] = {20, 30};
    const uint32_t leaf5[] = {50, 55};
    const uint32_t leaf7[] = {41, 55};
    const uint32_t leaf8[] = {60, 65};
    const uint32_t leaf9[] = {70, 75};
    const uint32_t leaf11[] = {81, 90};
    const uint32_t leaf12[] = {100, 105};
    const uint32_t leaf13[] = {110, 120};
    const uint32_t left_keys[] = {20, 50}; /* Violates root's upper bound 40. */
    const uint32_t middle_keys[] = {60, 70};
    const uint32_t right_keys[] = {100, 110};
    const uint32_t root_keys[] = {40, 80};
    const bfs_blk_t left_children[] = {3, 4, 5};
    const bfs_blk_t middle_children[] = {7, 8, 9};
    const bfs_blk_t right_children[] = {11, 12, 13};
    const bfs_blk_t root_children[] = {2, 6, 10};

    raw_leaf(&fixture, node, 3, leaf3, 2);
    raw_leaf(&fixture, node, 4, leaf4, 2);
    raw_leaf(&fixture, node, 5, leaf5, 2);
    raw_internal(&fixture, node, 2, 1, left_keys, left_children, 2);
    raw_leaf(&fixture, node, 7, leaf7, 2);
    raw_leaf(&fixture, node, 8, leaf8, 2);
    raw_leaf(&fixture, node, 9, leaf9, 2);
    raw_internal(&fixture, node, 6, 1, middle_keys, middle_children, 2);
    raw_leaf(&fixture, node, 11, leaf11, 2);
    raw_leaf(&fixture, node, 12, leaf12, 2);
    raw_leaf(&fixture, node, 13, leaf13, 2);
    raw_internal(&fixture, node, 10, 1, right_keys, right_children, 2);
    raw_internal(&fixture, node, 1, 2, root_keys, root_children, 2);

    if (!raw_crc_valid(&fixture, 1) || !raw_crc_valid(&fixture, 2) ||
        bfs_btree_init(&fixture.tree, &fixture.memory.bio,
                       &fixture.allocator.iface, &full_key_ops, 1, 7) != BFS_OK ||
        fixture.tree.height != 3) {
        fixture_close(&fixture);
        return false;
    }

    uint8_t key[TEST_KEY_SIZE];
    uint8_t value[TEST_VALUE_SIZE];
    make_key(key, 5);
    fixture.memory.reads = 0;
    bfs_err_t search_error = bfs_btree_search(&fixture.tree, key, value);
    uint32_t search_reads = fixture.memory.reads;
    fixture.memory.reads = 0;
    bfs_err_t scan_error = bfs_btree_scan(&fixture.tree, key,
                                          stop_scan_entry, NULL);
    uint32_t scan_reads = fixture.memory.reads;
    fixture_close(&fixture);

    /* Both traversals must reject the CRC-valid child at its inherited parent
     * bound, before reading the otherwise-valid leaf containing key 5. */
    return search_error == BFS_ERR_CORRUPT && search_reads == 2 &&
           scan_error == BFS_ERR_CORRUPT && scan_reads == 2;
}

static bool run_multilevel_update_abort(bool fail_allocation)
{
    scratch_fixture_t fixture;
    if (!fixture_open(&fixture) ||
        !build_even_id_tree(&fixture, TEST_ENTRY_COUNT) ||
        fixture.tree.height < 3) {
        if (fixture.memory.bytes) fixture_close(&fixture);
        return false;
    }

    const uint32_t id = 86;
    uint8_t key[TEST_KEY_SIZE];
    uint8_t replacement[TEST_VALUE_SIZE];
    uint8_t expected_root[TEST_BLOCK_SIZE];
    bool expected_owned[TEST_BLOCK_COUNT];
    make_key(key, id);
    make_value(replacement, id);
    replacement[0] ^= 0x5Au;

    bfs_blk_t old_root = fixture.tree.root;
    uint32_t old_height = fixture.tree.height;
    memcpy(expected_root, block_bytes(&fixture.memory, old_root), TEST_BLOCK_SIZE);
    memcpy(expected_owned, fixture.allocator.owned, sizeof(expected_owned));
    uint32_t old_write_calls = fixture.memory.write_calls;
    uint32_t old_successful_writes = fixture.memory.successful_writes;
    uint32_t old_failed_writes = fixture.memory.failed_writes;

    if (fail_allocation)
        fixture.allocator.fail_after_successes = 1;
    else
        fixture.memory.write_fail_countdown = 3;

    bfs_err_t err = bfs_btree_update(&fixture.tree, key, replacement);
    bool valid = fail_allocation ? err == BFS_ERR_NOMEM : err == BFS_ERR_IO;
    valid = valid && fixture.tree.root == old_root &&
            fixture.tree.height == old_height &&
            memcmp(block_bytes(&fixture.memory, old_root), expected_root,
                   TEST_BLOCK_SIZE) == 0 &&
            memcmp(fixture.allocator.owned, expected_owned,
                   sizeof(expected_owned)) == 0 &&
            fixture.allocator.invalid_allocations == 0 &&
            fixture.allocator.invalid_deallocations == 0;

    if (fail_allocation) {
        valid = valid && fixture.memory.write_calls == old_write_calls + 1 &&
                fixture.memory.successful_writes == old_successful_writes + 1 &&
                fixture.memory.failed_writes == old_failed_writes;
    } else {
        valid = valid && fixture.memory.write_calls == old_write_calls + 3 &&
                fixture.memory.successful_writes == old_successful_writes + 2 &&
                fixture.memory.failed_writes == old_failed_writes + 1;
    }
    valid = valid && scan_ids_from(&fixture, false, 0, 0, TEST_ENTRY_COUNT) &&
            search_value_is(&fixture, id);

    fixture_close(&fixture);
    return valid;
}

static bool test_multilevel_update_abort_restores_root_mapping_and_ownership(void)
{
    return run_multilevel_update_abort(true) &&
           run_multilevel_update_abort(false);
}

TEST_SUITE_BEGIN("B-tree scratch lifetime")
    TEST_RUN(test_full_width_multilevel_search_and_scan);
    TEST_RUN(test_crc_valid_child_with_wrong_inherited_bound);
    TEST_RUN(test_multilevel_update_abort_restores_root_mapping_and_ownership);
TEST_SUITE_END()
