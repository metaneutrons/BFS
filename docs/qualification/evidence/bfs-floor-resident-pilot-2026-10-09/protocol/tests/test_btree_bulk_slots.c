/* SPDX-License-Identifier: MPL-2.0 */
/* Public-operation regression tests for B-tree bulk slot shifts. */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "block_device_emu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#define TEST_IMAGE "test_btree_bulk_slots.img"
#define TEST_BLOCK_COUNT 4096u
#define TEST_SMALL_BLOCK_SIZE BFS_MIN_BLOCK_SIZE
#define TEST_LARGE_BLOCK_SIZE 4096u
#define TEST_MAX_BLOCK_SIZE 4096u
#define TEST_MAX_IDS 2048u
#define TEST_MAX_VALUE_SIZE 9u

typedef struct {
    uint32_t key_size;
    uint32_t value_size;
} test_shape_t;

static int compare_key_7(const void *left, const void *right)
{
    return memcmp(left, right, 7);
}

static int compare_key_9(const void *left, const void *right)
{
    return memcmp(left, right, 9);
}

static int compare_key_31(const void *left, const void *right)
{
    return memcmp(left, right, 31);
}

static int compare_max_key(const void *left, const void *right)
{
    return memcmp(left, right, BFS_MAX_KEY_SIZE);
}

static const bfs_btree_ops_t key7_value5_ops = {
    .key_compare = compare_key_7,
    .key_size = 7,
    .val_size = 5,
};

static const bfs_btree_ops_t key9_value3_ops = {
    .key_compare = compare_key_9,
    .key_size = 9,
    .val_size = 3,
};

static const bfs_btree_ops_t key31_value5_ops = {
    .key_compare = compare_key_31,
    .key_size = 31,
    .val_size = 5,
};

static const bfs_btree_ops_t max_key_value3_ops = {
    .key_compare = compare_max_key,
    .key_size = BFS_MAX_KEY_SIZE,
    .val_size = 3,
};

typedef struct {
    bfs_allocator_t iface;
    bfs_blk_t next_block;
    bfs_blk_t block_limit;
    bfs_blk_t free_blocks[TEST_BLOCK_COUNT];
    bool in_use[TEST_BLOCK_COUNT];
    uint32_t free_count;
    uint32_t active_count;
    bool allocator_corrupt;
} slot_allocator_t;

static bfs_blk_t slot_alloc(bfs_allocator_t *iface)
{
    slot_allocator_t *allocator = iface->ctx;
    bfs_blk_t block;

    if (allocator->free_count != 0) {
        block = allocator->free_blocks[--allocator->free_count];
    } else {
        if (allocator->next_block >= allocator->block_limit)
            return BFS_BLK_NULL;
        block = allocator->next_block++;
    }
    if (block >= allocator->block_limit || allocator->in_use[block]) {
        allocator->allocator_corrupt = true;
        return BFS_BLK_NULL;
    }
    allocator->in_use[block] = true;
    allocator->active_count++;
    return block;
}

static bfs_err_t slot_dealloc(bfs_allocator_t *iface, bfs_blk_t block)
{
    slot_allocator_t *allocator = iface->ctx;
    if (block < 2 || block >= allocator->block_limit ||
        !allocator->in_use[block] ||
        allocator->free_count >= TEST_BLOCK_COUNT) {
        allocator->allocator_corrupt = true;
        return BFS_ERR_CORRUPT;
    }
    allocator->in_use[block] = false;
    allocator->free_blocks[allocator->free_count++] = block;
    allocator->active_count--;
    return BFS_OK;
}

static bfs_err_t slot_allocator_error(bfs_allocator_t *iface)
{
    (void)iface;
    return BFS_ERR_NOSPC;
}

typedef struct {
    bfs_bio_t bio;
    bfs_bio_t *inner;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t failed_reads;
    uint32_t failed_writes;
    bool fail_next_read;
    bool fail_next_write;
} fault_bio_t;

static bfs_err_t fault_bio_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    fault_bio_t *fault = (fault_bio_t *)bio;
    fault->read_calls++;
    if (fault->fail_next_read) {
        fault->fail_next_read = false;
        fault->failed_reads++;
        return BFS_ERR_IO;
    }
    return bfs_bio_read(fault->inner, block, buffer);
}

static bfs_err_t fault_bio_write(bfs_bio_t *bio, bfs_blk_t block,
                                 const void *buffer)
{
    fault_bio_t *fault = (fault_bio_t *)bio;
    fault->write_calls++;
    if (fault->fail_next_write) {
        fault->fail_next_write = false;
        fault->failed_writes++;
        return BFS_ERR_IO;
    }
    return bfs_bio_write(fault->inner, block, buffer);
}

static bfs_err_t fault_bio_sync(bfs_bio_t *bio)
{
    fault_bio_t *fault = (fault_bio_t *)bio;
    return bfs_bio_sync(fault->inner);
}

static void fault_bio_close(bfs_bio_t *bio)
{
    fault_bio_t *fault = (fault_bio_t *)bio;
    bfs_bio_close(fault->inner);
    fault->inner = NULL;
}

static const bfs_bio_ops_t fault_bio_ops = {
    .read_block = fault_bio_read,
    .write_block = fault_bio_write,
    .sync = fault_bio_sync,
    .close = fault_bio_close,
};

typedef struct {
    fault_bio_t bio;
    slot_allocator_t allocator;
    bfs_btree_t tree;
} fixture_t;

static void allocator_init(slot_allocator_t *allocator, bfs_blk_t block_limit)
{
    memset(allocator, 0, sizeof(*allocator));
    allocator->iface.alloc = slot_alloc;
    allocator->iface.dealloc = slot_dealloc;
    allocator->iface.error = slot_allocator_error;
    allocator->iface.ctx = allocator;
    allocator->next_block = 2;
    allocator->block_limit = block_limit;
}

static bool fixture_open(fixture_t *fixture, uint32_t block_size,
                         const bfs_btree_ops_t *ops)
{
    memset(fixture, 0, sizeof(*fixture));
    unlink(TEST_IMAGE);
    fixture->bio.inner = bio_emu_create(TEST_IMAGE, block_size,
                                        TEST_BLOCK_COUNT);
    if (!fixture->bio.inner) return false;

    fixture->bio.bio.ops = &fault_bio_ops;
    fixture->bio.bio.block_size = block_size;
    fixture->bio.bio.block_count = TEST_BLOCK_COUNT;
    allocator_init(&fixture->allocator, TEST_BLOCK_COUNT);
    if (bfs_btree_init(&fixture->tree, &fixture->bio.bio,
                       &fixture->allocator.iface, ops,
                       BFS_BLK_NULL, 1) != BFS_OK) {
        bfs_bio_close(&fixture->bio.bio);
        unlink(TEST_IMAGE);
        return false;
    }
    return true;
}

static void fixture_close(fixture_t *fixture)
{
    if (fixture->bio.inner) bfs_bio_close(&fixture->bio.bio);
    unlink(TEST_IMAGE);
}

static void make_key(const test_shape_t *shape, uint32_t id,
                     uint8_t key[BFS_MAX_KEY_SIZE])
{
    uint32_t encoded_id = bfs_be32(id);
    memcpy(key, &encoded_id, sizeof(encoded_id));
    for (uint32_t i = sizeof(encoded_id); i < shape->key_size; i++)
        key[i] = (uint8_t)(1u + ((id * 37u + i * 29u) % 255u));
}

static void make_value(const test_shape_t *shape, uint32_t id,
                       uint8_t value[TEST_MAX_VALUE_SIZE])
{
    for (uint32_t i = 0; i < shape->value_size; i++)
        value[i] = (uint8_t)(1u + ((id * 19u + i * 53u) % 255u));
}

static bool insert_id(bfs_btree_t *tree, const test_shape_t *shape,
                      uint32_t id)
{
    uint8_t key[BFS_MAX_KEY_SIZE];
    uint8_t value[TEST_MAX_VALUE_SIZE];
    make_key(shape, id, key);
    make_value(shape, id, value);
    return bfs_btree_insert(tree, key, value) == BFS_OK;
}

static bool delete_id(bfs_btree_t *tree, const test_shape_t *shape,
                      uint32_t id)
{
    uint8_t key[BFS_MAX_KEY_SIZE];
    make_key(shape, id, key);
    return bfs_btree_delete(tree, key) == BFS_OK;
}

static uint32_t independent_node_crc(const uint8_t *bytes,
                                     uint32_t block_size)
{
    const uint32_t crc_offset = offsetof(bfs_btnode_hdr_t, crc32);
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < block_size; i++) {
        uint8_t byte = (i >= crc_offset &&
                        i < crc_offset + sizeof(uint32_t)) ? 0 : bytes[i];
        crc ^= byte;
        for (uint32_t bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

typedef struct {
    uint8_t minimum[BFS_MAX_KEY_SIZE];
    uint8_t maximum[BFS_MAX_KEY_SIZE];
} subtree_range_t;

static bool inspect_subtree(bfs_btree_t *tree, bfs_blk_t block,
                            uint16_t expected_level, bool seen[TEST_BLOCK_COUNT],
                            uint32_t *visited, subtree_range_t *range)
{
    uint8_t bytes[TEST_MAX_BLOCK_SIZE];
    if (block >= tree->bio->block_count || seen[block] ||
        tree->bio->block_size > sizeof(bytes))
        return false;
    seen[block] = true;
    (*visited)++;
    if (bfs_bio_read(tree->bio, block, bytes) != BFS_OK) return false;

    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)bytes;
    uint16_t level = bfs_be16(header->level);
    uint32_t count = bfs_be32(header->num_keys);
    if (bfs_be32(header->magic) != BFS_NODE_MAGIC || level != expected_level ||
        level >= BFS_BTREE_MAX_DEPTH || count == 0 ||
        bfs_be16(header->flags) != 0 ||
        bfs_be32(header->crc32) != independent_node_crc(bytes,
                                                         tree->bio->block_size))
        return false;

    uint32_t capacity = level == BFS_BTNODE_LEAF
        ? leaf_max_keys(tree) : internal_max_keys(tree);
    if (count > capacity) return false;
    for (uint32_t i = 1; i < count; i++)
        if (tree->ops->key_compare(node_key(tree, bytes, i - 1),
                                   node_key(tree, bytes, i)) >= 0)
            return false;

    if (level == BFS_BTNODE_LEAF) {
        memcpy(range->minimum, node_key(tree, bytes, 0), tree->ops->key_size);
        memcpy(range->maximum, node_key(tree, bytes, count - 1),
               tree->ops->key_size);
        return true;
    }
    if (level == 0) return false;

    uint8_t previous_maximum[BFS_MAX_KEY_SIZE];
    for (uint32_t i = 0; i <= count; i++) {
        bfs_blk_t child = get_child(tree, bytes, i);
        if (child >= tree->bio->block_count) return false;
        subtree_range_t child_range;
        if (!inspect_subtree(tree, child, (uint16_t)(level - 1), seen,
                             visited, &child_range))
            return false;
        if (i == 0) {
            memcpy(range->minimum, child_range.minimum, tree->ops->key_size);
        } else {
            const void *separator = node_key(tree, bytes, i - 1);
            if (tree->ops->key_compare(previous_maximum, separator) >= 0 ||
                tree->ops->key_compare(child_range.minimum, separator) < 0 ||
                tree->ops->key_compare(previous_maximum,
                                       child_range.minimum) >= 0)
                return false;
        }
        memcpy(previous_maximum, child_range.maximum, tree->ops->key_size);
        if (i == count)
            memcpy(range->maximum, child_range.maximum, tree->ops->key_size);
    }
    return true;
}

static void count_walk_node(bfs_blk_t block, void *ctx)
{
    uint32_t *count = ctx;
    (void)block;
    (*count)++;
}

static bool tree_layout_valid(bfs_btree_t *tree)
{
    if (tree->root == BFS_BLK_NULL) {
        uint32_t walked = 0;
        return tree->height == 0 &&
               bfs_btree_walk_nodes(tree, count_walk_node, &walked) == BFS_OK &&
               walked == 0;
    }
    if (tree->height == 0 || tree->height > BFS_BTREE_MAX_DEPTH)
        return false;
    bool seen[TEST_BLOCK_COUNT] = { false };
    uint32_t visited = 0;
    subtree_range_t range;
    if (!inspect_subtree(tree, tree->root, (uint16_t)(tree->height - 1),
                         seen, &visited, &range))
        return false;
    uint32_t walked = 0;
    return bfs_btree_walk_nodes(tree, count_walk_node, &walked) == BFS_OK &&
           walked == visited;
}

typedef struct {
    const test_shape_t *shape;
    const bool *present;
    uint32_t domain;
    uint32_t expected_count;
    uint32_t seen;
    uint32_t last_id;
    bool have_last;
    bool valid;
} scan_check_t;

static bool check_scanned_pair(const void *key, const void *value, void *ctx)
{
    scan_check_t *check = ctx;
    uint32_t id = bfs_load_be32(key);
    uint8_t expected_key[BFS_MAX_KEY_SIZE];
    uint8_t expected_value[TEST_MAX_VALUE_SIZE];
    if (check->seen >= check->expected_count || id >= check->domain ||
        !check->present[id] || (check->have_last && id <= check->last_id)) {
        check->valid = false;
        return false;
    }
    make_key(check->shape, id, expected_key);
    make_value(check->shape, id, expected_value);
    if (memcmp(key, expected_key, check->shape->key_size) != 0 ||
        memcmp(value, expected_value, check->shape->value_size) != 0) {
        check->valid = false;
        return false;
    }
    check->last_id = id;
    check->have_last = true;
    check->seen++;
    return true;
}

static bool contents_valid(bfs_btree_t *tree, const test_shape_t *shape,
                           const bool present[TEST_MAX_IDS], uint32_t domain,
                           uint32_t expected_count)
{
    if (domain > TEST_MAX_IDS || !tree_layout_valid(tree)) return false;
    scan_check_t scan = {
        .shape = shape,
        .present = present,
        .domain = domain,
        .expected_count = expected_count,
        .valid = true,
    };
    if (bfs_btree_scan(tree, NULL, check_scanned_pair, &scan) != BFS_OK ||
        !scan.valid || scan.seen != expected_count)
        return false;

    /* Check search across root, internal and leaf boundaries, plus an absent key. */
    uint32_t probes = domain < 9u ? domain : 9u;
    for (uint32_t i = 0; i < probes; i++) {
        uint32_t id = probes <= 1 ? 0 :
            (uint32_t)(((uint64_t)i * (domain - 1u)) / (probes - 1u));
        uint8_t key[BFS_MAX_KEY_SIZE];
        uint8_t value[TEST_MAX_VALUE_SIZE];
        make_key(shape, id, key);
        bfs_err_t err = bfs_btree_search(tree, key, value);
        if (present[id]) {
            uint8_t expected_value[TEST_MAX_VALUE_SIZE];
            make_value(shape, id, expected_value);
            if (err != BFS_OK ||
                memcmp(value, expected_value, shape->value_size) != 0)
                return false;
        } else if (err != BFS_ERR_NOTFOUND) {
            return false;
        }
    }
    if (domain + 1u >= TEST_MAX_IDS) return false;
    uint8_t missing_key[BFS_MAX_KEY_SIZE];
    uint8_t missing_value[TEST_MAX_VALUE_SIZE];
    make_key(shape, domain + 1u, missing_key);
    return bfs_btree_search(tree, missing_key, missing_value) == BFS_ERR_NOTFOUND;
}

#define BULK_REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ok = false; \
        goto cleanup; \
    } \
} while (0)

static bool run_leaf_slot_positions(void)
{
    fixture_t fixture;
    bool opened = false;
    bool ok = true;
    bool present[TEST_MAX_IDS] = { false };
    const test_shape_t shape = { .key_size = 7, .value_size = 5 };
    const uint32_t inserts[] = { 40, 20, 60, 50, 30, 70, 10, 80 };
    const uint32_t deletes[] = { 10, 40, 70, 30, 20, 60, 80 };
    uint32_t count = 0;

    BULK_REQUIRE(fixture_open(&fixture, TEST_SMALL_BLOCK_SIZE,
                              &key7_value5_ops));
    opened = true;
    for (uint32_t i = 0; i < sizeof(inserts) / sizeof(inserts[0]); i++) {
        BULK_REQUIRE(insert_id(&fixture.tree, &shape, inserts[i]));
        present[inserts[i]] = true;
        count++;
    }
    BULK_REQUIRE(fixture.tree.height == 1);
    BULK_REQUIRE(contents_valid(&fixture.tree, &shape, present,
                                128, count));

    for (uint32_t i = 0; i < sizeof(deletes) / sizeof(deletes[0]); i++) {
        BULK_REQUIRE(delete_id(&fixture.tree, &shape, deletes[i]));
        present[deletes[i]] = false;
        count--;
    }
    BULK_REQUIRE(contents_valid(&fixture.tree, &shape, present, 128, count));

cleanup:
    if (opened) fixture_close(&fixture);
    return ok;
}

static bool run_leaf_cow_faults(void)
{
    fixture_t fixture;
    bool opened = false;
    bool ok = true;
    bool present[TEST_MAX_IDS] = { false };
    bool old_present[TEST_MAX_IDS] = { false };
    const test_shape_t shape = { .key_size = 9, .value_size = 3 };
    const uint32_t inserts[] = { 10, 20, 30, 40, 50 };
    uint8_t root_before[TEST_MAX_BLOCK_SIZE];
    uint8_t root_after[TEST_MAX_BLOCK_SIZE];
    bfs_btree_t old_view;
    bfs_blk_t old_root;
    uint32_t count = 0;

    BULK_REQUIRE(fixture_open(&fixture, TEST_SMALL_BLOCK_SIZE,
                              &key9_value3_ops));
    opened = true;
    for (uint32_t i = 0; i < sizeof(inserts) / sizeof(inserts[0]); i++) {
        BULK_REQUIRE(insert_id(&fixture.tree, &shape, inserts[i]));
        present[inserts[i]] = true;
        count++;
    }
    BULK_REQUIRE(fixture.tree.height == 1);
    old_root = fixture.tree.root;
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_before) == BFS_OK);
    memcpy(old_present, present, sizeof(present));
    BULK_REQUIRE(bfs_btree_init(&old_view, &fixture.bio.bio,
                                &fixture.allocator.iface,
                                &key9_value3_ops, old_root, 1) == BFS_OK);
    fixture.tree.txn_id_fallback = 2;

    uint32_t active_before = fixture.allocator.active_count;
    uint32_t failed_reads_before = fixture.bio.failed_reads;
    uint8_t key[BFS_MAX_KEY_SIZE];
    uint8_t value[TEST_MAX_VALUE_SIZE];
    make_key(&shape, 25, key);
    make_value(&shape, 25, value);
    fixture.bio.fail_next_read = true;
    BULK_REQUIRE(bfs_btree_insert(&fixture.tree, key, value) == BFS_ERR_IO);
    BULK_REQUIRE(!fixture.bio.fail_next_read);
    BULK_REQUIRE(fixture.bio.failed_reads == failed_reads_before + 1u);
    BULK_REQUIRE(fixture.allocator.active_count == active_before);
    BULK_REQUIRE(fixture.tree.root == old_root);
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_after) == BFS_OK);
    BULK_REQUIRE(memcmp(root_before, root_after,
                        fixture.bio.bio.block_size) == 0);
    BULK_REQUIRE(contents_valid(&fixture.tree, &shape, present, 128, count));

    uint32_t failed_writes_before = fixture.bio.failed_writes;
    active_before = fixture.allocator.active_count;
    fixture.bio.fail_next_write = true;
    BULK_REQUIRE(bfs_btree_insert(&fixture.tree, key, value) == BFS_ERR_IO);
    BULK_REQUIRE(!fixture.bio.fail_next_write);
    BULK_REQUIRE(fixture.bio.failed_writes == failed_writes_before + 1u);
    BULK_REQUIRE(fixture.allocator.active_count == active_before);
    BULK_REQUIRE(fixture.tree.root == old_root);
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_after) == BFS_OK);
    BULK_REQUIRE(memcmp(root_before, root_after,
                        fixture.bio.bio.block_size) == 0);
    BULK_REQUIRE(contents_valid(&fixture.tree, &shape, present, 128, count));

    BULK_REQUIRE(bfs_btree_insert(&fixture.tree, key, value) == BFS_OK);
    present[25] = true;
    count++;
    BULK_REQUIRE(fixture.tree.root != old_root);
    BULK_REQUIRE(fixture.allocator.in_use[old_root]);
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_after) == BFS_OK);
    BULK_REQUIRE(memcmp(root_before, root_after,
                        fixture.bio.bio.block_size) == 0);
    BULK_REQUIRE(contents_valid(&fixture.tree, &shape, present, 128, count));
    BULK_REQUIRE(contents_valid(&old_view, &shape, old_present, 128,
                                count - 1u));
    BULK_REQUIRE(!fixture.allocator.allocator_corrupt);

cleanup:
    if (opened) fixture_close(&fixture);
    return ok;
}

static bool run_deep_width_case(const bfs_btree_ops_t *ops,
                                const test_shape_t *shape,
                                uint32_t block_size, uint32_t count,
                                uint32_t multiplier, uint32_t offset)
{
    fixture_t fixture;
    bool opened = false;
    bool ok = true;
    bool present[TEST_MAX_IDS] = { false };
    bool old_present[TEST_MAX_IDS] = { false };
    uint8_t root_before[TEST_MAX_BLOCK_SIZE];
    uint8_t root_after[TEST_MAX_BLOCK_SIZE];
    bfs_btree_t old_view;
    bfs_blk_t old_root;

    BULK_REQUIRE(count > 100u && count + 2u < TEST_MAX_IDS);
    BULK_REQUIRE(fixture_open(&fixture, block_size, ops));
    opened = true;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t id = (uint32_t)(((uint64_t)i * multiplier + offset) % count);
        BULK_REQUIRE(!present[id]);
        BULK_REQUIRE(insert_id(&fixture.tree, shape, id));
        present[id] = true;
    }
    BULK_REQUIRE(fixture.tree.height >= 3);
    BULK_REQUIRE(contents_valid(&fixture.tree, shape, present, count, count));

    old_root = fixture.tree.root;
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_before) == BFS_OK);
    memcpy(old_present, present, sizeof(present));
    BULK_REQUIRE(bfs_btree_init(&old_view, &fixture.bio.bio,
                                &fixture.allocator.iface, ops,
                                old_root, 1) == BFS_OK);
    fixture.tree.txn_id_fallback = 2;
    BULK_REQUIRE(insert_id(&fixture.tree, shape, count));
    present[count] = true;
    BULK_REQUIRE(fixture.tree.root != old_root);
    BULK_REQUIRE(fixture.allocator.in_use[old_root]);
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_after) == BFS_OK);
    BULK_REQUIRE(memcmp(root_before, root_after, block_size) == 0);
    BULK_REQUIRE(contents_valid(&fixture.tree, shape, present, count + 1u,
                                count + 1u));
    BULK_REQUIRE(contents_valid(&old_view, shape, old_present, count, count));

    for (uint32_t id = 0; id <= count; id++) {
        if (!present[id]) continue;
        BULK_REQUIRE(delete_id(&fixture.tree, shape, id));
        present[id] = false;
    }
    BULK_REQUIRE(fixture.tree.root == BFS_BLK_NULL);
    BULK_REQUIRE(fixture.tree.height == 0);
    BULK_REQUIRE(contents_valid(&fixture.tree, shape, present, count + 1u, 0));
    BULK_REQUIRE(bfs_bio_read(&fixture.bio.bio, old_root, root_after) == BFS_OK);
    BULK_REQUIRE(memcmp(root_before, root_after, block_size) == 0);
    BULK_REQUIRE(contents_valid(&old_view, shape, old_present, count, count));
    BULK_REQUIRE(!fixture.allocator.allocator_corrupt);

cleanup:
    if (opened) fixture_close(&fixture);
    return ok;
}

static bool run_odd_width_multilevel(void)
{
    const test_shape_t shape = { .key_size = 31, .value_size = 5 };
    return run_deep_width_case(&key31_value5_ops, &shape,
                               TEST_SMALL_BLOCK_SIZE, 1600u, 1001u, 37u);
}

static bool run_max_key_multilevel(void)
{
    const test_shape_t shape = {
        .key_size = BFS_MAX_KEY_SIZE,
        .value_size = 3,
    };
    return run_deep_width_case(&max_key_value3_ops, &shape,
                               TEST_LARGE_BLOCK_SIZE, 240u, 73u, 11u);
}

static void test_leaf_slot_positions_odd_widths(void)
{
    TEST_ASSERT(run_leaf_slot_positions());
}

static void test_leaf_io_failures_preserve_cow_root(void)
{
    TEST_ASSERT(run_leaf_cow_faults());
}

static void test_odd_width_multilevel_insert_delete(void)
{
    TEST_ASSERT(run_odd_width_multilevel());
}

static void test_max_key_multilevel_insert_delete(void)
{
    TEST_ASSERT(run_max_key_multilevel());
}

TEST_SUITE_BEGIN("B+tree bulk slot shifts")
    TEST_RUN(test_leaf_slot_positions_odd_widths);
    TEST_RUN(test_leaf_io_failures_preserve_cow_root);
    TEST_RUN(test_odd_width_multilevel_insert_delete);
    TEST_RUN(test_max_key_multilevel_insert_delete);
TEST_SUITE_END()
