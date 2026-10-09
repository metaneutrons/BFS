/* SPDX-License-Identifier: MPL-2.0 */
/* B-tree read-path tests for sparse CRCs over used leaf key bytes. */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_cache.h"

#define TEST_KEY_SIZE 264u
#define TEST_VALUE_SIZE 8u
#define TEST_BLOCK_COUNT 2u

typedef struct {
    bfs_bio_t bio;
    uint8_t blocks[TEST_BLOCK_COUNT][BFS_MAX_BLOCK_SIZE];
} memory_bio_t;

static memory_bio_t test_memory;

static bfs_err_t memory_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
    if (block >= bio->block_count) return BFS_ERR_INVAL;
    memcpy(buffer, memory->blocks[block], bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
}

static bfs_err_t memory_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    memory_bio_t *memory = (memory_bio_t *)bio;
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

static int compare_directory_keys(const void *left, const void *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    uint32_t a_parent = bfs_load_be32(a), b_parent = bfs_load_be32(b);
    if (a_parent != b_parent) return a_parent < b_parent ? -1 : 1;
    uint32_t a_hash = bfs_load_be32(a + 4), b_hash = bfs_load_be32(b + 4);
    if (a_hash != b_hash) return a_hash < b_hash ? -1 : 1;

    uint8_t a_length = a[8], b_length = b[8];
    uint8_t common_length = a_length < b_length ? a_length : b_length;
    for (uint8_t i = 0; i < common_length; i++) {
        uint8_t a_char = a[9 + i], b_char = b[9 + i];
        if (a_char >= 'a' && a_char <= 'z') a_char -= 'a' - 'A';
        if (b_char >= 'a' && b_char <= 'z') b_char -= 'a' - 'A';
        if (a_char != b_char) return a_char < b_char ? -1 : 1;
    }
    return a_length < b_length ? -1 : a_length > b_length ? 1 : 0;
}

static const bfs_btree_ops_t sparse_key_ops = {
    .key_compare = compare_directory_keys,
    .key_size = TEST_KEY_SIZE,
    .val_size = TEST_VALUE_SIZE,
    .cache_key_order = true,
    .sparse_key_crc = true,
};

static const bfs_btree_ops_t ordinary_key_ops = {
    .key_compare = compare_directory_keys,
    .key_size = TEST_KEY_SIZE,
    .val_size = TEST_VALUE_SIZE,
    .cache_key_order = true,
    .sparse_key_crc = false,
};

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

static void make_key(uint8_t key[TEST_KEY_SIZE], uint32_t hash, char name)
{
    memset(key, 0, TEST_KEY_SIZE);
    bfs_store_be32(key, 1u);       /* Parent identifier. */
    bfs_store_be32(key + 4, hash); /* Name hash. */
    key[8] = 1u;                   /* Name length. */
    key[9] = (uint8_t)name;
}

static void init_raw_leaf(memory_bio_t *memory, uint32_t block_size,
                          const bfs_btree_ops_t *ops,
                          bool nonzero_key_trailing_bytes)
{
    memset(memory->blocks, 0, sizeof(memory->blocks));
    memory->bio.ops = &memory_ops;
    memory->bio.block_size = block_size;
    memory->bio.block_count = TEST_BLOCK_COUNT;

    uint8_t *buffer = memory->blocks[1];
    bfs_btnode_hdr_t *header = (bfs_btnode_hdr_t *)buffer;
    header->magic = bfs_be32(BFS_NODE_MAGIC);
    header->crc32 = 0;
    header->txn_id = bfs_be64(7u);
    header->num_keys = bfs_be32(2u);
    header->level = bfs_be16(BFS_BTNODE_LEAF);
    header->flags = 0;
    header->right_sibling = bfs_be32(BFS_BLK_NULL);

    bfs_btree_t layout = {.bio = &memory->bio, .ops = ops};
    uint8_t key[TEST_KEY_SIZE];
    make_key(key, 1u, 'a');
    if (nonzero_key_trailing_bytes) {
        key[200] = 0x61u;
        key[250] = 0x7Du;
    }
    memcpy(node_key(&layout, buffer, 0), key, sizeof(key)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    make_key(key, 2u, 'b');
    if (nonzero_key_trailing_bytes) {
        key[200] = 0x61u;
        key[250] = 0x7Du;
    }
    memcpy(node_key(&layout, buffer, 1), key, sizeof(key)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory

    for (uint32_t i = 0; i < 2u; i++) {
        uint8_t *value = (uint8_t *)leaf_val(&layout, buffer, i);
        bfs_store_be32(value, 0x12340000u + i);
        bfs_store_be32(value + 4, 0xABCD0000u + i);
    }

    uint32_t prefix_end = sizeof(bfs_btnode_hdr_t) +
                          2u * TEST_KEY_SIZE;
    uint32_t values_start = sizeof(bfs_btnode_hdr_t) +
                            leaf_max_keys(&layout) * TEST_KEY_SIZE;
    uint32_t values_end = values_start + 2u * TEST_VALUE_SIZE;
    /* Legacy unused key and value slots may contain nonzero bytes. Their
     * exact contents remain covered by the on-disk full-block checksum. */
    buffer[prefix_end + 1u] = 0xA6u;
    buffer[prefix_end + 200u] = 0x5Bu;
    buffer[values_end + 17u] = 0xC3u;
    buffer[block_size - 1u] = 0xD7u;

    header->crc32 = bfs_be32(bfs_crc32(0, buffer, block_size));
}

enum leaf_mutation {
    LEAF_UNCHANGED,
    LEAF_MUTATE_USED_KEY_ZERO,
    LEAF_MUTATE_UNUSED_PADDING,
};

static bool count_scan_entry(const void *key, const void *value, void *context)
{
    (void)key;
    (void)value;
    (*(uint32_t *)context)++;
    return true;
}

/* Construct and initially validate a full-CRC leaf, then force the scan to
 * read it again after an optional on-disk mutation without updating its CRC. */
static bool run_scan_case(uint32_t block_size, const bfs_btree_ops_t *ops,
                          enum leaf_mutation mutation)
{
    bfs_cache_t cache;
    bfs_allocator_t allocator = {
        .alloc = unused_alloc,
        .dealloc = unused_dealloc,
        .error = NULL,
        .ctx = NULL,
    };
    bfs_btree_t tree;
    bool cache_ready = false;
    bool success = false;
    uint32_t callback_count = 0;

    init_raw_leaf(&test_memory, block_size, ops, false);
    if (bfs_cache_init(&cache, &test_memory.bio, 4u) != BFS_OK) goto done;
    cache_ready = true;
    if (bfs_btree_init(&tree, &cache.bio, &allocator, ops, 1u, 7u) != BFS_OK)
        goto done;

    if (mutation == LEAF_MUTATE_USED_KEY_ZERO) {
        /* Key bytes 200 is a used, zero-valued byte after the directory name. */
        test_memory.blocks[1][sizeof(bfs_btnode_hdr_t) + 200u] = 0x42u;
    } else if (mutation == LEAF_MUTATE_UNUSED_PADDING) {
        test_memory.blocks[1][block_size - 1u] ^= 0x01u;
    }

    bfs_cache_invalidate(&cache);
    bfs_err_t result = bfs_btree_scan(&tree, NULL, count_scan_entry,
                                      &callback_count);
    bfs_err_t expected_result = mutation == LEAF_UNCHANGED
                                    ? BFS_OK : BFS_ERR_CORRUPT;
    uint32_t expected_callbacks = mutation == LEAF_UNCHANGED ? 2u : 0u;
    success = result == expected_result &&
              callback_count == expected_callbacks;

done:
    if (cache_ready) bfs_cache_destroy(&cache);
    return success;
}

static void test_sparse_key_crc_valid_full_block_padding(void)
{
    static const uint32_t block_sizes[] = {1024u, 4096u, 65536u};
    const bfs_btree_ops_t *ops[] = {&sparse_key_ops, &ordinary_key_ops};

    for (size_t block = 0;
         block < sizeof(block_sizes) / sizeof(block_sizes[0]); block++) {
        for (size_t variant = 0; variant < sizeof(ops) / sizeof(ops[0]);
             variant++) {
            TEST_ASSERT(run_scan_case(block_sizes[block], ops[variant],
                                      LEAF_UNCHANGED));
        }
    }
}

static void test_sparse_key_crc_rejects_mutated_used_zero_byte(void)
{
    static const uint32_t block_sizes[] = {1024u, 4096u, 65536u};
    const bfs_btree_ops_t *ops[] = {&sparse_key_ops, &ordinary_key_ops};

    for (size_t block = 0;
         block < sizeof(block_sizes) / sizeof(block_sizes[0]); block++) {
        for (size_t variant = 0; variant < sizeof(ops) / sizeof(ops[0]);
             variant++) {
            TEST_ASSERT(run_scan_case(block_sizes[block], ops[variant],
                                      LEAF_MUTATE_USED_KEY_ZERO));
        }
    }
}

static void test_sparse_key_crc_rejects_mutated_padding(void)
{
    static const uint32_t block_sizes[] = {1024u, 4096u, 65536u};
    const bfs_btree_ops_t *ops[] = {&sparse_key_ops, &ordinary_key_ops};

    for (size_t block = 0;
         block < sizeof(block_sizes) / sizeof(block_sizes[0]); block++) {
        for (size_t variant = 0; variant < sizeof(ops) / sizeof(ops[0]);
             variant++) {
            TEST_ASSERT(run_scan_case(block_sizes[block], ops[variant],
                                      LEAF_MUTATE_UNUSED_PADDING));
        }
    }
}

static bool run_nonzero_key_tail_scan_case(uint32_t block_size,
                                           const bfs_btree_ops_t *ops)
{
    bfs_cache_t cache;
    bfs_allocator_t allocator = {
        .alloc = unused_alloc,
        .dealloc = unused_dealloc,
        .error = NULL,
        .ctx = NULL,
    };
    bfs_btree_t tree;
    bool cache_ready = false;
    bool success = false;
    uint32_t callback_count = 0;

    init_raw_leaf(&test_memory, block_size, ops, true);
    if (bfs_cache_init(&cache, &test_memory.bio, 4u) != BFS_OK) goto done;
    cache_ready = true;
    if (bfs_btree_init(&tree, &cache.bio, &allocator, ops, 1u, 7u) != BFS_OK)
        goto done;

    /* The initial tree read validates the full-block CRC. Scan once from the
     * validated cache entry, then invalidate and verify the same legacy key
     * bytes on a fresh read. */
    if (bfs_btree_scan(&tree, NULL, count_scan_entry, &callback_count) != BFS_OK ||
        callback_count != 2u)
        goto done;

    bfs_cache_invalidate(&cache);
    callback_count = 0;
    if (bfs_btree_scan(&tree, NULL, count_scan_entry, &callback_count) == BFS_OK &&
        callback_count == 2u)
        success = true;

done:
    if (cache_ready) bfs_cache_destroy(&cache);
    return success;
}

static void test_sparse_key_crc_accepts_nonzero_bytes_after_name(void)
{
    static const uint32_t block_sizes[] = {1024u, 4096u, 65536u};
    const bfs_btree_ops_t *ops[] = {&sparse_key_ops, &ordinary_key_ops};

    for (size_t block = 0;
         block < sizeof(block_sizes) / sizeof(block_sizes[0]); block++) {
        for (size_t variant = 0; variant < sizeof(ops) / sizeof(ops[0]);
             variant++) {
            TEST_ASSERT(run_nonzero_key_tail_scan_case(block_sizes[block],
                                                       ops[variant]));
        }
    }
}

TEST_SUITE_BEGIN("B-tree sparse key CRC")
    TEST_RUN(test_sparse_key_crc_valid_full_block_padding);
    TEST_RUN(test_sparse_key_crc_rejects_mutated_used_zero_byte);
    TEST_RUN(test_sparse_key_crc_rejects_mutated_padding);
    TEST_RUN(test_sparse_key_crc_accepts_nonzero_bytes_after_name);
TEST_SUITE_END()
