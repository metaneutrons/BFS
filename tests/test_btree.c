/*
 * BFS — B+tree tests: search, insert, split, scan, COW
 */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
#include "bfs_alloc.h"
#include "bfs_cache.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdlib.h>

/* Forward declarations for bootstrap allocator */
typedef struct {
    bfs_allocator_t base;
    bfs_blk_t next_block;
    bfs_blk_t max_block;
    bfs_blk_t freed[4096];
    uint32_t freed_count;
} bootstrap_alloc_t;
extern bootstrap_alloc_t *bootstrap_create(bfs_blk_t start, bfs_blk_t max);

#define TEST_IMG "test_btree.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096  /* 16MB — enough for thousands of inserts */

/* Simple uint32 key, uint32 value for testing */
static int u32_compare(const void *a, const void *b)
{
    uint32_t va = bfs_load_be32(a);
    uint32_t vb = bfs_load_be32(b);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static const bfs_btree_ops_t u32_ops = {
    .key_compare = u32_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
};

static const bfs_btree_ops_t u32_u64_ops = {
    .key_compare = u32_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint64_t),
};

static const bfs_btree_ops_t u32_u40_ops = {
    .key_compare = u32_compare,
    .key_size = sizeof(uint32_t),
    .val_size = 5,
};

static void make_key(uint32_t *k, uint32_t v) { *k = bfs_be32(v); }
static uint32_t read_key(const void *k) { return bfs_load_be32(k); }
static void make_u64_value(uint64_t *v, uint64_t value) { *v = bfs_be64(value); }
static uint64_t read_u64_value(const void *v) { return bfs_load_be64(v); }
static void make_u40_value(uint8_t v[5], uint64_t value)
{
    for (uint32_t i = 0; i < 5; i++)
        v[i] = (uint8_t)(value >> ((4u - i) * 8u));
}
static uint64_t read_u40_value(const uint8_t v[5])
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 5; i++) value = (value << 8) | v[i];
    return value;
}

static uint32_t reference_node_crc(const uint8_t *buf, uint32_t block_size)
{
    const uint32_t crc_offset = offsetof(bfs_btnode_hdr_t, crc32);
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < block_size; i++) {
        uint8_t byte = (i >= crc_offset && i < crc_offset + sizeof(uint32_t))
                           ? 0 : buf[i];
        crc ^= byte;
        for (uint32_t bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static bool node_padding_ranges(const bfs_btree_t *tree, const uint8_t *buf,
                                uint32_t *gap_start, uint32_t *values_start,
                                uint32_t *values_end)
{
    const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
    uint16_t level = bfs_be16(hdr->level);
    uint32_t count = bfs_be32(hdr->num_keys);
    bool leaf = level == BFS_BTNODE_LEAF;
    uint32_t capacity = leaf ? leaf_max_keys(tree) : internal_max_keys(tree);
    if (level >= BFS_BTREE_MAX_DEPTH || count > capacity) return false;
    *gap_start = sizeof(*hdr) + count * tree->ops->key_size;
    *values_start = sizeof(*hdr) + capacity * tree->ops->key_size;
    uint32_t live_values = leaf ? count * tree->ops->val_size
                                : (count + 1u) * sizeof(uint32_t);
    if (*gap_start > *values_start || *values_start > tree->bio->block_size ||
        live_values > tree->bio->block_size - *values_start)
        return false;
    *values_end = *values_start + live_values;
    return true;
}

static bool range_is_zero(const uint8_t *buf, uint32_t start, uint32_t end)
{
    for (uint32_t i = start; i < end; i++)
        if (buf[i] != 0) return false;
    return true;
}

static bool range_has_nonzero(const uint8_t *buf, uint32_t start,
                              uint32_t end)
{
    for (uint32_t i = start; i < end; i++)
        if (buf[i] != 0) return true;
    return false;
}

static void fill_nonzero_range(uint8_t *buf, uint32_t start, uint32_t end,
                               uint32_t seed)
{
    for (uint32_t i = start; i < end; i++)
        buf[i] = (uint8_t)(1u + ((i + seed) % 255u));
}

static bool seed_legacy_node_padding(bfs_btree_t *tree, bfs_blk_t blk,
                                     uint8_t *buf, uint32_t seed)
{
    uint32_t gap_start, values_start, values_end;
    if (bfs_bio_read(tree->bio, blk, buf) != BFS_OK ||
        !node_padding_ranges(tree, buf, &gap_start, &values_start, &values_end))
        return false;
    fill_nonzero_range(buf, gap_start, values_start, seed);
    fill_nonzero_range(buf, values_end, tree->bio->block_size, seed + 71u);
    ((bfs_btnode_hdr_t *)buf)->crc32 =
        bfs_be32(reference_node_crc(buf, tree->bio->block_size));
    return bfs_bio_write(tree->bio, blk, buf) == BFS_OK;
}

static bool node_has_canonical_padding(const bfs_btree_t *tree,
                                      const uint8_t *buf)
{
    uint32_t gap_start, values_start, values_end;
    const bfs_btnode_hdr_t *hdr = (const bfs_btnode_hdr_t *)buf;
    return node_padding_ranges(tree, buf, &gap_start, &values_start,
                               &values_end) &&
           bfs_be32(hdr->crc32) ==
               reference_node_crc(buf, tree->bio->block_size) &&
           range_is_zero(buf, gap_start, values_start) &&
           range_is_zero(buf, values_end, tree->bio->block_size);
}

typedef struct {
    bfs_blk_t blocks[64];
    uint32_t count;
    bool overflow;
} collected_nodes_t;

static void collect_node_block(bfs_blk_t blk, void *ctx)
{
    collected_nodes_t *collected = ctx;
    if (collected->count == sizeof(collected->blocks) / sizeof(collected->blocks[0])) {
        collected->overflow = true;
        return;
    }
    collected->blocks[collected->count++] = blk;
}

static bool open_padding_fixture(uint32_t block_size,
                                 const bfs_btree_ops_t *ops,
                                 bfs_bio_t **bio_out,
                                 bootstrap_alloc_t **ba_out,
                                 bfs_btree_t *tree)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, block_size, 128);
    if (!bio) return false;
    bootstrap_alloc_t *ba = bootstrap_create(2, 128);
    if (!ba) {
        bfs_bio_close(bio);
        unlink(TEST_IMG);
        return false;
    }
    if (bfs_btree_init(tree, bio, &ba->base, ops, BFS_BLK_NULL, 9) != BFS_OK) {
        free(ba);
        bfs_bio_close(bio);
        unlink(TEST_IMG);
        return false;
    }
    *bio_out = bio;
    *ba_out = ba;
    return true;
}

static void close_padding_fixture(bfs_bio_t *bio, bootstrap_alloc_t *ba)
{
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static bfs_bio_t *fail_write_target;
static const bfs_bio_ops_t *fail_write_original_ops;
static bool fail_next_write;
static uint32_t observed_write_calls;
static bfs_err_t test_read_block(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    (void)bio;
    return fail_write_original_ops->read_block(fail_write_target, blk, buf);
}
static bfs_err_t test_write_block(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    (void)bio;
    observed_write_calls++;
    if (fail_next_write) { fail_next_write = false; return BFS_ERR_IO; }
    return fail_write_original_ops->write_block(fail_write_target, blk, buf);
}

static const bfs_bio_ops_t fail_write_ops = {
    .read_block = test_read_block, .write_block = test_write_block,
    .sync = NULL, .close = NULL
};

#define TRACKED_BLOCK_MAX 16
typedef struct {
    bfs_allocator_t iface;
    bfs_allocator_t *wrapped;
    bfs_blk_t allocated[TRACKED_BLOCK_MAX];
    uint32_t alloc_count;
    bfs_blk_t deallocated[TRACKED_BLOCK_MAX];
    uint32_t dealloc_count;
} tracked_allocator_t;

static bfs_blk_t tracked_alloc(bfs_allocator_t *allocator)
{
    tracked_allocator_t *tracked = allocator->ctx;
    bfs_blk_t blk = tracked->wrapped->alloc(tracked->wrapped);
    if (tracked->alloc_count < TRACKED_BLOCK_MAX)
        tracked->allocated[tracked->alloc_count] = blk;
    tracked->alloc_count++;
    return blk;
}

static bfs_err_t tracked_dealloc(bfs_allocator_t *allocator, bfs_blk_t blk)
{
    tracked_allocator_t *tracked = allocator->ctx;
    if (tracked->dealloc_count < TRACKED_BLOCK_MAX)
        tracked->deallocated[tracked->dealloc_count] = blk;
    tracked->dealloc_count++;
    return tracked->wrapped->dealloc(tracked->wrapped, blk);
}

static bfs_err_t tracked_allocator_error(bfs_allocator_t *allocator)
{
    tracked_allocator_t *tracked = allocator->ctx;
    return tracked->wrapped->error
        ? tracked->wrapped->error(tracked->wrapped) : BFS_ERR_NOSPC;
}

static void tracked_allocator_init(tracked_allocator_t *tracked,
                                   bfs_allocator_t *wrapped)
{
    memset(tracked, 0, sizeof(*tracked));
    tracked->wrapped = wrapped;
    tracked->iface.alloc = tracked_alloc;
    tracked->iface.dealloc = tracked_dealloc;
    tracked->iface.error = tracked_allocator_error;
    tracked->iface.ctx = tracked;
}

static void tracked_allocator_reset(tracked_allocator_t *tracked)
{
    tracked->alloc_count = 0;
    tracked->dealloc_count = 0;
}

static bool tracked_has_block(const bfs_blk_t *blocks, uint32_t count,
                              bfs_blk_t target)
{
    uint32_t limit = count < TRACKED_BLOCK_MAX ? count : TRACKED_BLOCK_MAX;
    for (uint32_t i = 0; i < limit; i++)
        if (blocks[i] == target) return true;
    return false;
}

typedef struct {
    bfs_blk_t deferred[TRACKED_BLOCK_MAX];
    uint32_t defer_count;
    uint32_t available;
} test_free_sink_t;

static bfs_err_t test_defer_free(void *ctx, bfs_blk_t blk)
{
    test_free_sink_t *sink = ctx;
    if (sink->defer_count < TRACKED_BLOCK_MAX)
        sink->deferred[sink->defer_count] = blk;
    sink->defer_count++;
    return BFS_OK;
}

static uint32_t test_free_headroom(void *ctx)
{
    return ((test_free_sink_t *)ctx)->available;
}

static bool open_owned_root_fixture(bfs_bio_t **bio_out,
                                    bootstrap_alloc_t **ba_out,
                                    bfs_btree_t *tree,
                                    tracked_allocator_t *tracked,
                                    uint64_t txn_id)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    if (!bio) return false;
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    if (!ba) {
        bfs_bio_close(bio);
        unlink(TEST_IMG);
        return false;
    }
    tracked_allocator_init(tracked, &ba->base);
    if (bfs_btree_init(tree, bio, &tracked->iface, &u32_ops,
                       BFS_BLK_NULL, txn_id) != BFS_OK) {
        free(ba);
        bfs_bio_close(bio);
        unlink(TEST_IMG);
        return false;
    }
    uint32_t key, val;
    make_key(&key, 1);
    make_key(&val, 10);
    if (bfs_btree_insert(tree, &key, &val) != BFS_OK) {
        free(ba);
        bfs_bio_close(bio);
        unlink(TEST_IMG);
        return false;
    }
    *bio_out = bio;
    *ba_out = ba;
    return true;
}

static void close_owned_root_fixture(bfs_bio_t *bio, bootstrap_alloc_t *ba)
{
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: empty tree search ───────────────────────────────── */

static void test_empty_tree_search(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val;
    make_key(&key, 42);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_ERR_NOTFOUND);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: owned nodes ─────────────────────────────────────── */

static void read_block(bfs_bio_t *bio, bfs_blk_t blk, uint8_t *buf)
{
    TEST_ASSERT_EQ(bfs_bio_read(bio, blk, buf), BFS_OK);
}

/* Insert key k with value k * 10. */
static bfs_err_t insert_tenfold(bfs_btree_t *tree, uint32_t k)
{
    uint32_t key, val;
    make_key(&key, k);
    val = bfs_be32(k * 10u);
    return bfs_btree_insert(tree, &key, &val);
}

static void test_owned_rewrite_requires_registration(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    bfs_btree_owned_t owned = {0};
    tree.free_sink.owned = &owned;

    TEST_ASSERT_EQ(insert_tenfold(&tree, 1), BFS_OK);
    bfs_blk_t root = tree.root;
    bfs_blk_t next = ba->next_block;

    /* A node the live transaction allocated is rewritten in place. */
    TEST_ASSERT_EQ(insert_tenfold(&tree, 2), BFS_OK);
    TEST_ASSERT_EQ(tree.root, root);
    TEST_ASSERT_EQ(ba->next_block, next);

    /* The same txn_id without registration, as on a damaged image, is not
     * proof of ownership: the node is copied and its bytes stay intact. */
    bfs_btree_owned_reset(&owned);
    uint8_t before[BLK_SIZE], after[BLK_SIZE];
    read_block(bio, root, before);
    TEST_ASSERT_EQ(insert_tenfold(&tree, 3), BFS_OK);
    TEST_ASSERT(tree.root != root);
    read_block(bio, root, after);
    TEST_ASSERT_MEM_EQ(after, before, BLK_SIZE);

    /* The copy is registered and rewritten in place again. */
    root = tree.root;
    TEST_ASSERT_EQ(insert_tenfold(&tree, 4), BFS_OK);
    TEST_ASSERT_EQ(tree.root, root);

    /* A different transaction id invalidates every entry. */
    tree.txn_id_fallback = 2;
    TEST_ASSERT_EQ(insert_tenfold(&tree, 5), BFS_OK);
    TEST_ASSERT(tree.root != root);

    /* The disabled switch keeps every change copy-on-write. */
    root = tree.root;
    owned.disabled = true;
    TEST_ASSERT_EQ(insert_tenfold(&tree, 6), BFS_OK);
    TEST_ASSERT(tree.root != root);

    uint32_t key, val;
    for (uint32_t i = 1; i <= 6; i++) {
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_OK);
        TEST_ASSERT_EQ(bfs_load_be32(&val), i * 10);
    }
    bfs_btree_owned_destroy(&owned);
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_owned_rewrite_publishes_after_fallible_steps(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    bfs_btree_owned_t owned = {0};
    tree.free_sink.owned = &owned;

    /* Fill one owned root leaf to capacity; the next insert must split. */
    uint32_t capacity = bfs_btree_leaf_capacity(&tree);
    uint32_t key, val;
    for (uint32_t i = 0; i < capacity; i++) {
        make_key(&key, i * 2u); val = bfs_be32(i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    }
    TEST_ASSERT_EQ(tree.height, 1);
    bfs_blk_t root = tree.root;
    uint8_t before[BLK_SIZE], after[BLK_SIZE];
    read_block(bio, root, before);

    /* The split cannot allocate: the owned leaf must keep its old bytes. */
    ba->max_block = ba->next_block;
    make_key(&key, 1u); val = bfs_be32(99);
    TEST_ASSERT(bfs_btree_insert(&tree, &key, &val) != BFS_OK);
    TEST_ASSERT_EQ(tree.root, root);
    TEST_ASSERT_EQ(tree.height, 1);
    read_block(bio, root, after);
    TEST_ASSERT_MEM_EQ(after, before, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_ERR_NOTFOUND);
    for (uint32_t i = 0; i < capacity; i++) {
        make_key(&key, i * 2u);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_OK);
    }

    /* With space again the same insert succeeds. */
    ba->max_block = BLK_COUNT;
    make_key(&key, 1u); val = bfs_be32(99);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    TEST_ASSERT_EQ(tree.height, 2);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_OK);
    bfs_btree_owned_destroy(&owned);
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static int wide_compare(const void *a, const void *b)
{
    return u32_compare(a, b);
}

static const bfs_btree_ops_t wide_ops = {
    .key_compare = wide_compare,
    .key_size = BFS_MAX_KEY_SIZE,
    .val_size = sizeof(uint32_t),
};

static void wide_key(uint8_t *key, uint32_t value)
{
    memset(key, 0, BFS_MAX_KEY_SIZE);
    make_key((uint32_t *)(void *)key, value);
}

/* Insert ascending keys until the tree reaches height three and report the
 * key count at which that happened. */
static void wide_height_three_at(bfs_bio_t *bio, uint32_t *count_out)
{
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &wide_ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    uint8_t key[BFS_MAX_KEY_SIZE];
    uint32_t val = 0, count = 0;
    *count_out = 0;
    while (tree.height < 3 && count < 1000) {
        wide_key(key, ++count);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, key, &val), BFS_OK);
    }
    free(ba);
    *count_out = count;
}

static void test_owned_rewrite_abort_after_staging_keeps_bytes(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    uint32_t split_at;
    wide_height_three_at(bio, &split_at);
    TEST_ASSERT(split_at > 1 && split_at < 1000);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &wide_ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    bfs_btree_owned_t owned = {0};
    tree.free_sink.owned = &owned;
    uint8_t key[BFS_MAX_KEY_SIZE];
    uint32_t val = 0;
    for (uint32_t i = 1; i < split_at; i++) {
        wide_key(key, i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, key, &val), BFS_OK);
    }
    TEST_ASSERT_EQ(tree.height, 2);

    /* The rightmost leaf and the root are full and owned. Allow only the leaf
     * split: the owned leaf is staged before the root split fails. */
    bfs_blk_t root = tree.root;
    uint8_t root_buf[BLK_SIZE];
    read_block(bio, root, root_buf);
    uint32_t root_keys = bfs_load_be32(&((bfs_btnode_hdr_t *)(void *)root_buf)->num_keys);
    bfs_blk_t leaf = get_child(&tree, root_buf, root_keys);
    uint8_t leaf_before[BLK_SIZE], root_before[BLK_SIZE], after[BLK_SIZE];
    read_block(bio, leaf, leaf_before);
    read_block(bio, root, root_before);
    uint32_t freed_before = ba->freed_count;
    ba->max_block = ba->next_block + 1;

    wide_key(key, split_at);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, key, &val), BFS_ERR_NOSPC);
    TEST_ASSERT_EQ(tree.root, root);
    TEST_ASSERT_EQ(tree.height, 2);
    TEST_ASSERT_EQ(ba->freed_count, freed_before + 1);
    read_block(bio, leaf, after);
    TEST_ASSERT_MEM_EQ(after, leaf_before, BLK_SIZE);
    read_block(bio, root, after);
    TEST_ASSERT_MEM_EQ(after, root_before, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, key, &val), BFS_ERR_NOTFOUND);
    for (uint32_t i = 1; i < split_at; i++) {
        wide_key(key, i);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, key, &val), BFS_OK);
    }
    bfs_btree_owned_destroy(&owned);
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: single insert and search ────────────────────────── */

static void test_single_insert_search(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val, result;
    make_key(&key, 100);
    make_key(&val, 999);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(bio, tree.root));

    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_OK);
    TEST_ASSERT_EQ(read_key(&result), 999);

    /* Not found */
    make_key(&key, 101);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_ERR_NOTFOUND);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: duplicate insert rejected ───────────────────────── */

static void test_duplicate_insert(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val;
    make_key(&key, 50);
    make_key(&val, 1);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_ERR_EXISTS);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: many sequential inserts (triggers splits) ───────── */

static void test_sequential_inserts(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    /* Insert 1000 sequential keys */
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_key(&val, i * 10);
        bfs_err_t err = bfs_btree_insert(&tree, &key, &val);
        TEST_ASSERT_EQ(err, BFS_OK);
    }

    /* Verify all lookups */
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t key, result;
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_OK);
        TEST_ASSERT_EQ(read_key(&result), i * 10);
    }

    /* Tree should have grown beyond 1 level */
    TEST_ASSERT(tree.height > 1);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: reverse order inserts ───────────────────────────── */

static void test_reverse_inserts(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    for (int i = 499; i >= 0; i--) {
        uint32_t key, val;
        make_key(&key, (uint32_t)i);
        make_key(&val, (uint32_t)(i + 1000));
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    }

    for (uint32_t i = 0; i < 500; i++) {
        uint32_t key, result;
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_OK);
        TEST_ASSERT_EQ(read_key(&result), i + 1000);
    }

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: scan ────────────────────────────────────────────── */

typedef struct {
    uint32_t keys[2000];
    uint32_t count;
} scan_ctx_t;

static bool scan_collector(const void *key, const void *val, void *ctx)
{
    (void)val;
    scan_ctx_t *sc = (scan_ctx_t *)ctx;
    if (sc->count < 2000)
        sc->keys[sc->count++] = read_key(key);
    return true;
}

static void test_scan_all(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    /* Insert 200 keys in pseudo-random order */
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t k = (i * 97) % 200;  /* simple permutation */
        uint32_t key, val;
        make_key(&key, k);
        make_key(&val, k);
        bfs_btree_insert(&tree, &key, &val);
    }

    /* Scan all — should return keys in sorted order */
    scan_ctx_t sc = { .count = 0 };
    TEST_ASSERT_EQ(bfs_btree_scan(&tree, NULL, scan_collector, &sc), BFS_OK);
    TEST_ASSERT_EQ(sc.count, 200);

    for (uint32_t i = 0; i < 200; i++)
        TEST_ASSERT_EQ(sc.keys[i], i);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_scan_from_key(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    for (uint32_t i = 0; i < 100; i++) {
        uint32_t key, val;
        make_key(&key, i * 2);  /* even numbers only: 0,2,4,...,198 */
        make_key(&val, i);
        bfs_btree_insert(&tree, &key, &val);
    }

    /* Scan from key 50 */
    uint32_t start;
    make_key(&start, 50);
    scan_ctx_t sc = { .count = 0 };
    TEST_ASSERT_EQ(bfs_btree_scan(&tree, &start, scan_collector, &sc), BFS_OK);

    /* Should get keys 50,52,54,...,198 = 75 keys */
    TEST_ASSERT_EQ(sc.count, 75);
    TEST_ASSERT_EQ(sc.keys[0], 50);
    TEST_ASSERT_EQ(sc.keys[74], 198);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: scans across leaves of a three-level tree ───────── */

#define SCAN_IMG "test_btree_scan.img"
#define SCAN_BLK_SIZE 1024
#define SCAN_BLK_COUNT 262144
#define SCAN_KEYS 16000u

typedef struct {
    uint32_t *keys;
    uint32_t count;
    uint32_t capacity;
} scan_list_t;

static bool scan_list_add(const void *key, const void *val, void *ctx)
{
    (void)val;
    scan_list_t *list = ctx;
    if (list->count < list->capacity) list->keys[list->count] = read_key(key);
    list->count++;
    return true;
}

static bfs_freespace_t scan_space;

/* Even keys 0 .. 2 * (SCAN_KEYS - 1) in a tree on 1 KiB blocks, three levels deep. */
static void build_scan_tree(bfs_btree_t *tree, bfs_bio_t *bio)
{
    TEST_ASSERT_EQ(bfs_freespace_init(&scan_space, bio, BFS_BLK_NULL, 1), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_add(&scan_space, 2, SCAN_BLK_COUNT - 2), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&scan_space), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_init(tree, bio, bfs_freespace_allocator(&scan_space), &u32_ops,
                                  BFS_BLK_NULL, 1), BFS_OK);
    for (uint32_t i = 0; i < SCAN_KEYS; i++) {
        uint32_t k = (i * 7919u) % SCAN_KEYS;
        uint32_t key, val;
        make_key(&key, 2 * k);
        make_key(&val, k);
        TEST_ASSERT_EQ(bfs_btree_insert(tree, &key, &val), BFS_OK);
    }
    TEST_ASSERT(tree->height >= 3);
}

/* Scans from every kind of start key return exactly the keys at or after it,
 * read through the device or through validated cache views. */
static void test_scan_three_levels(void)
{
    unlink(SCAN_IMG);
    bfs_bio_t *raw = bio_emu_create(SCAN_IMG, SCAN_BLK_SIZE, SCAN_BLK_COUNT);
    TEST_ASSERT(raw != NULL);
    bfs_btree_t tree;
    build_scan_tree(&tree, raw);
    static uint32_t keys[SCAN_KEYS];
    static bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, raw, 64), BFS_OK);
    static const uint32_t starts[] = {0, 1, 2, 247, 248, 249, 9999, 15000, 31996, 31997, 31998, 31999, 40000};
    for (int via_cache = 0; via_cache < 2; via_cache++) {
        tree.bio = via_cache ? &cache.bio : raw;
        for (int pass = 0; pass < 2; pass++) {
            scan_list_t list = { keys, 0, SCAN_KEYS };
            TEST_ASSERT_EQ(bfs_btree_scan(&tree, NULL, scan_list_add, &list), BFS_OK);
            TEST_ASSERT_EQ(list.count, SCAN_KEYS);
            for (uint32_t i = 0; i < SCAN_KEYS; i++) TEST_ASSERT_EQ(keys[i], 2 * i);
        }
        for (size_t s = 0; s < sizeof(starts) / sizeof(starts[0]); s++) {
            uint32_t start;
            make_key(&start, starts[s]);
            scan_list_t list = { keys, 0, SCAN_KEYS };
            TEST_ASSERT_EQ(bfs_btree_scan(&tree, &start, scan_list_add, &list), BFS_OK);
            uint32_t first = (starts[s] + 1) / 2;
            uint32_t expected = first < SCAN_KEYS ? SCAN_KEYS - first : 0;
            TEST_ASSERT_EQ(list.count, expected);
            for (uint32_t i = 0; i < list.count; i++) TEST_ASSERT_EQ(keys[i], 2 * (first + i));
        }
    }
    bfs_cache_destroy(&cache);
    bfs_bio_close(raw);
    unlink(SCAN_IMG);
}

typedef struct {
    bfs_btree_t *tree;
    uint32_t previous;
    uint32_t visited;
    bool any;
    bool ordered;
    uint8_t *seen;      /* by key value */
    bool *deleted;      /* by key value */
} mutating_scan_t;

/* At every visited key, delete the even key four ahead and insert odd keys
 * behind and ahead of the scan position. */
static bool mutate_while_scanning(const void *key, const void *val, void *ctx)
{
    (void)val;
    mutating_scan_t *scan = ctx;
    uint32_t k = read_key(key);
    if (scan->any && k <= scan->previous) scan->ordered = false;
    scan->previous = k;
    scan->any = true;
    if (k < 2 * SCAN_KEYS + 8) scan->seen[k]++;
    scan->visited++;
    if (k % 2 == 0 && k % 10 == 0) {
        uint32_t victim, behind, ahead, v;
        make_key(&victim, k + 4);
        if (k + 4 < 2 * SCAN_KEYS && bfs_btree_delete(scan->tree, &victim) == BFS_OK)
            scan->deleted[k + 4] = true;
        make_key(&behind, k > 1 ? k - 1 : 1);
        make_key(&ahead, k + 7);
        make_key(&v, 0);
        (void)bfs_btree_insert(scan->tree, &behind, &v);
        (void)bfs_btree_insert(scan->tree, &ahead, &v);
    }
    return true;
}

/* A callback may change the scanned tree. The scan then still returns keys
 * in strictly ascending order, each at most once, never a key deleted before
 * the scan reached it, and every original key that was not deleted. */
static void test_scan_survives_callback_changes(void)
{
    unlink(SCAN_IMG);
    bfs_bio_t *raw = bio_emu_create(SCAN_IMG, SCAN_BLK_SIZE, SCAN_BLK_COUNT);
    TEST_ASSERT(raw != NULL);
    bfs_btree_t tree;
    build_scan_tree(&tree, raw);
    static bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, raw, 64), BFS_OK);
    tree.bio = &cache.bio;
    static uint8_t seen[2 * SCAN_KEYS + 8];
    static bool deleted[2 * SCAN_KEYS + 8];
    mutating_scan_t scan = { .tree = &tree, .ordered = true, .seen = seen, .deleted = deleted };
    TEST_ASSERT_EQ(bfs_btree_scan(&tree, NULL, mutate_while_scanning, &scan), BFS_OK);
    TEST_ASSERT(scan.ordered);
    for (uint32_t k = 0; k < 2 * SCAN_KEYS; k += 2) {
        TEST_ASSERT(seen[k] <= 1);
        if (deleted[k]) TEST_ASSERT_EQ(seen[k], 0);
        else TEST_ASSERT_EQ(seen[k], 1);
    }
    bfs_cache_destroy(&cache);
    bfs_bio_close(raw);
    unlink(SCAN_IMG);
}

/* ── Test: resuming scans from a cursor ────────────────────── */

typedef struct {
    bfs_bio_t bio;
    bfs_bio_t *dev;
    unsigned reads;
} counting_bio_t;

static bfs_err_t counting_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    counting_bio_t *c = (counting_bio_t *)bio;
    c->reads++;
    return bfs_bio_read(c->dev, blk, buf);
}

static bfs_err_t counting_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    return bfs_bio_write(((counting_bio_t *)bio)->dev, blk, buf);
}

static bfs_err_t counting_sync(bfs_bio_t *bio)
{
    return bfs_bio_sync(((counting_bio_t *)bio)->dev);
}

static void counting_close(bfs_bio_t *bio)
{
    (void)bio;
}

static const bfs_bio_ops_t counting_ops = {
    .read_block = counting_read,
    .write_block = counting_write,
    .sync = counting_sync,
    .close = counting_close,
};

typedef struct {
    uint32_t key;
    bool got;
} next_key_t;

static bool take_one_after(const void *key, const void *val, void *ctx)
{
    (void)val;
    next_key_t *next = ctx;
    next->key = read_key(key);
    next->got = true;
    return false;
}

/* The next key after k (exclusive), one scan call per key as ExNext does. */
static bool cursor_next(bfs_btree_t *tree, bfs_btree_cursor_t *cursor, uint32_t k,
                        bool first, uint32_t *out)
{
    uint32_t start;
    make_key(&start, first ? 0 : k + 1);
    next_key_t next = { 0, false };
    if (bfs_btree_scan_cursor(tree, cursor, &start, take_one_after, &next) != BFS_OK)
        return false;
    *out = next.key;
    return next.got;
}

/* Key-by-key iteration with a cursor equals a full scan, reads the device
 * only at leaf boundaries, and stays correct when the tree changes between
 * calls. */
static void test_scan_cursor_resumes_in_leaf(void)
{
    unlink(SCAN_IMG);
    bfs_bio_t *raw = bio_emu_create(SCAN_IMG, SCAN_BLK_SIZE, SCAN_BLK_COUNT);
    TEST_ASSERT(raw != NULL);
    bfs_btree_t tree;
    build_scan_tree(&tree, raw);
    counting_bio_t counting = { .bio = { .ops = &counting_ops, .block_size = SCAN_BLK_SIZE,
                                          .block_count = SCAN_BLK_COUNT }, .dev = raw };
    tree.bio = &counting.bio;
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);

    uint32_t k = 0, count = 0, reads_in_leaf = 0;
    bool first = true;
    while (cursor_next(&tree, &cursor, k, first, &k)) {
        TEST_ASSERT_EQ(k, 2 * count);
        count++;
        first = false;
        unsigned before = counting.reads;
        uint32_t peek;
        /* Asking again from inside the same leaf needs no device read. */
        if (k > 0 && cursor_next(&tree, &cursor, k - 2, false, &peek)) {
            TEST_ASSERT_EQ(peek, k);
            if (counting.reads == before) reads_in_leaf++;
        }
    }
    TEST_ASSERT_EQ(count, SCAN_KEYS);
    /* All but the first key of each leaf are found again without a read. */
    TEST_ASSERT(reads_in_leaf > SCAN_KEYS - SCAN_KEYS / 50);

    /* A change between calls invalidates the copy. */
    uint32_t next;
    TEST_ASSERT(cursor_next(&tree, &cursor, 98, false, &next));
    TEST_ASSERT_EQ(next, 100);
    uint32_t victim;
    make_key(&victim, 102);
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &victim), BFS_OK);
    TEST_ASSERT(cursor_next(&tree, &cursor, 100, false, &next));
    TEST_ASSERT_EQ(next, 104);
    uint32_t added, value;
    make_key(&added, 105);
    make_key(&value, 0);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &added, &value), BFS_OK);
    TEST_ASSERT(cursor_next(&tree, &cursor, 104, false, &next));
    TEST_ASSERT_EQ(next, 105);

    bfs_btree_cursor_release(&cursor);
    TEST_ASSERT(cursor.leaf == NULL);
    bfs_bio_close(raw);
    unlink(SCAN_IMG);
}

static bool stop_at_first(const void *key, const void *val, void *ctx)
{
    (void)key;
    (void)val;
    (void)ctx;
    return false;
}

typedef struct {
    uint32_t count;
    uint32_t last;
    bool ordered;
} order_check_t;

static bool check_order(const void *key, const void *val, void *ctx)
{
    (void)val;
    order_check_t *check = ctx;
    uint32_t k = read_key(key);
    if (check->count > 0 && k <= check->last) check->ordered = false;
    check->last = k;
    check->count++;
    return true;
}

static bool key_value(bfs_btree_t *tree, uint32_t k, uint32_t *value)
{
    uint32_t key, val;
    make_key(&key, k);
    if (bfs_btree_search(tree, &key, &val) != BFS_OK) return false;
    *value = read_key(&val);
    return true;
}

static bool key_present(bfs_btree_t *tree, uint32_t k)
{
    uint32_t value;
    return key_value(tree, k, &value);
}

static bfs_err_t move_key(bfs_btree_t *tree, uint32_t from, uint32_t to, uint32_t value)
{
    uint32_t old_key, new_key, val;
    make_key(&old_key, from);
    make_key(&new_key, to);
    make_key(&val, value);
    return bfs_btree_update_key(tree, &old_key, &new_key, &val);
}

/* A key moves in place only while it stays between its neighbours and below
 * the separator that routes to its leaf; otherwise the tree is unchanged. */
static void test_update_key_stays_inside_its_leaf(void)
{
    unlink(SCAN_IMG);
    bfs_bio_t *raw = bio_emu_create(SCAN_IMG, SCAN_BLK_SIZE, SCAN_BLK_COUNT);
    TEST_ASSERT(raw != NULL);
    bfs_btree_t tree;
    build_scan_tree(&tree, raw);

    TEST_ASSERT_EQ(move_key(&tree, 4, 5, 77), BFS_OK);
    uint32_t value;
    TEST_ASSERT(key_value(&tree, 5, &value));
    TEST_ASSERT_EQ(value, 77);
    TEST_ASSERT(!key_present(&tree, 4));
    TEST_ASSERT_EQ(move_key(&tree, 2, 6, 1), BFS_ERR_UNSUPPORTED);
    TEST_ASSERT(key_present(&tree, 2));
    TEST_ASSERT_EQ(move_key(&tree, 3, 7, 1), BFS_ERR_NOTFOUND);

    /* The last key of the first leaf; the next leaf starts two above it. */
    bfs_btree_cursor_t cursor;
    bfs_btree_cursor_init(&cursor);
    TEST_ASSERT_EQ(bfs_btree_scan_cursor(&tree, &cursor, NULL, stop_at_first, NULL), BFS_OK);
    uint32_t n = bfs_be32(((bfs_btnode_hdr_t *)cursor.leaf)->num_keys);
    TEST_ASSERT(n > 1);
    uint32_t last = read_key(node_key(&tree, cursor.leaf, n - 1));
    bfs_btree_cursor_release(&cursor);
    TEST_ASSERT(key_present(&tree, last + 2));
    TEST_ASSERT_EQ(move_key(&tree, last, last + 1, 5), BFS_OK);

    /* With the next leaf's first key gone, its separator still bounds this
     * leaf, although no stored key lies between. */
    uint32_t gone;
    make_key(&gone, last + 2);
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &gone), BFS_OK);
    TEST_ASSERT_EQ(move_key(&tree, last + 1, last + 3, 5), BFS_ERR_UNSUPPORTED);
    TEST_ASSERT(key_present(&tree, last + 1));
    TEST_ASSERT(!key_present(&tree, last + 3));

    order_check_t check = { 0, 0, true };
    TEST_ASSERT_EQ(bfs_btree_scan(&tree, NULL, check_order, &check), BFS_OK);
    TEST_ASSERT(check.ordered);
    TEST_ASSERT_EQ(check.count, SCAN_KEYS - 1);
    bfs_bio_close(raw);
    unlink(SCAN_IMG);
}

/* ── Test: searches reuse the last leaf only while it is current ── */

static bool search_value(bfs_btree_t *tree, uint32_t k, uint32_t *value)
{
    uint32_t key, raw;
    make_key(&key, k);
    if (bfs_btree_search(tree, &key, &raw) != BFS_OK) return false;
    *value = read_key(&raw);
    return true;
}

static void test_search_hint_follows_changes(void)
{
    unlink(SCAN_IMG);
    bfs_bio_t *raw = bio_emu_create(SCAN_IMG, SCAN_BLK_SIZE, SCAN_BLK_COUNT);
    TEST_ASSERT(raw != NULL);
    bfs_btree_t tree;
    build_scan_tree(&tree, raw);
    static bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, raw, 64), BFS_OK);
    tree.bio = &cache.bio;

    uint32_t value;
    for (uint32_t k = 0; k < 2 * SCAN_KEYS; k += 2) {
        TEST_ASSERT(search_value(&tree, k, &value));
        TEST_ASSERT_EQ(value, k / 2);
        /* An absent key inside the same leaf is absent. */
        TEST_ASSERT(!search_value(&tree, k + 1, &value));
    }
    TEST_ASSERT(!search_value(&tree, 2 * SCAN_KEYS + 1, &value));

    /* Each change makes the remembered leaf stale; the old copy of a
     * rewritten leaf stays resident and must not answer. */
    TEST_ASSERT(search_value(&tree, 1000, &value));
    uint32_t key, replacement;
    make_key(&key, 1002);
    make_key(&replacement, 7777);
    TEST_ASSERT_EQ(bfs_btree_update(&tree, &key, &replacement), BFS_OK);
    TEST_ASSERT(search_value(&tree, 1002, &value));
    TEST_ASSERT_EQ(value, 7777);
    make_key(&key, 1004);
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key), BFS_OK);
    TEST_ASSERT(!search_value(&tree, 1004, &value));
    TEST_ASSERT(search_value(&tree, 1006, &value));
    TEST_ASSERT_EQ(value, 503);
    make_key(&key, 1005);
    make_key(&replacement, 4242);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &replacement), BFS_OK);
    TEST_ASSERT(search_value(&tree, 1005, &value));
    TEST_ASSERT_EQ(value, 4242);

    bfs_cache_destroy(&cache);
    bfs_bio_close(raw);
    unlink(SCAN_IMG);
}

/* ── Test: specialized search for big-endian u32 keys ──────── */

static const bfs_btree_ops_t be32_ops = {
    .key_compare = bfs_btree_key_compare_be32,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(uint32_t),
    .cache_key_order = true,
};

/* Keys spread over the whole unsigned range, including values with the top
 * bit set, are found exactly; the gaps between them are not. */
static void test_be32_search_matches_comparator(void)
{
    unlink(SCAN_IMG);
    bfs_bio_t *raw = bio_emu_create(SCAN_IMG, SCAN_BLK_SIZE, SCAN_BLK_COUNT);
    TEST_ASSERT(raw != NULL);
    TEST_ASSERT_EQ(bfs_freespace_init(&scan_space, raw, BFS_BLK_NULL, 1), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_add(&scan_space, 2, SCAN_BLK_COUNT - 2), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&scan_space), BFS_OK);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, raw, bfs_freespace_allocator(&scan_space), &be32_ops,
                                  BFS_BLK_NULL, 1), BFS_OK);
    const uint32_t count = 3000;
    const uint32_t step = 0x00155555u; /* 3000 steps cover most of 2^32 */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = ((i * 1237u) % count) * step + 7u, key, val;
        make_key(&key, k);
        make_key(&val, k ^ 0xA5A5A5A5u);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    }
    TEST_ASSERT(tree.height >= 2);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = i * step + 7u, key, val;
        make_key(&key, k);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_OK);
        TEST_ASSERT_EQ(read_key(&val), k ^ 0xA5A5A5A5u);
        make_key(&key, k + 1u);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_ERR_NOTFOUND);
        make_key(&key, k - 1u);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &val), BFS_ERR_NOTFOUND);
    }
    uint32_t key, val, found_key;
    make_key(&key, 0x80000000u);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&tree, &key, &found_key, &val), BFS_OK);
    TEST_ASSERT(read_key(&found_key) < 0x80000000u);
    TEST_ASSERT(read_key(&found_key) + step > 0x80000000u);
    bfs_bio_close(raw);
    unlink(SCAN_IMG);
}

/* ── Test: COW preserves old root ──────────────────────────── */

static void test_cow_old_root_preserved(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    /* Insert some keys */
    for (uint32_t i = 0; i < 50; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_key(&val, i);
        bfs_btree_insert(&tree, &key, &val);
    }

    bfs_blk_t old_root = tree.root;

    /* Insert more keys — root should change due to COW */
    for (uint32_t i = 50; i < 100; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_key(&val, i);
        bfs_btree_insert(&tree, &key, &val);
    }

    /* Root should have changed (COW) */
    TEST_ASSERT(tree.root != old_root);

    /* Old root should still be readable (not overwritten) */
    uint8_t buf[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, buf), BFS_OK);
    /* It should still have valid magic */
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    TEST_ASSERT_EQ(bfs_be32(hdr->magic), BFS_NODE_MAGIC);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: single delete ────────────────────────────────────── */

static void test_single_delete(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val, result;
    make_key(&key, 42);
    make_key(&val, 100);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_ERR_NOTFOUND);

    /* Delete non-existent key */
    TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key), BFS_ERR_NOTFOUND);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: delete all keys ─────────────────────────────────── */

static void test_delete_all(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    /* Insert 500 keys */
    for (uint32_t i = 0; i < 500; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_key(&val, i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    }

    /* Delete all in forward order */
    for (uint32_t i = 0; i < 500; i++) {
        uint32_t key;
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key), BFS_OK);
    }

    /* Tree should be empty */
    TEST_ASSERT_EQ(tree.root, BFS_BLK_NULL);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: delete in reverse order ─────────────────────────── */

static void test_delete_reverse(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    for (uint32_t i = 0; i < 500; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_key(&val, i);
        bfs_btree_insert(&tree, &key, &val);
    }

    /* Delete in reverse order */
    for (int i = 499; i >= 0; i--) {
        uint32_t key;
        make_key(&key, (uint32_t)i);
        TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key), BFS_OK);

        /* Verify remaining keys still findable */
        if (i > 0 && (i % 100 == 0)) {
            uint32_t check_key, result;
            make_key(&check_key, 0);
            TEST_ASSERT_EQ(bfs_btree_search(&tree, &check_key, &result), BFS_OK);
        }
    }

    TEST_ASSERT_EQ(tree.root, BFS_BLK_NULL);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: interleaved insert/delete ───────────────────────── */

static void test_insert_delete_interleaved(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    /* Insert 0..199, delete even numbers, verify odd numbers remain */
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t key, val;
        make_key(&key, i);
        make_key(&val, i * 10);
        bfs_btree_insert(&tree, &key, &val);
    }

    for (uint32_t i = 0; i < 200; i += 2) {
        uint32_t key;
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_delete(&tree, &key), BFS_OK);
    }

    /* Verify odd keys remain, even keys gone */
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t key, result;
        make_key(&key, i);
        if (i % 2 == 0) {
            TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_ERR_NOTFOUND);
        } else {
            TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_OK);
            TEST_ASSERT_EQ(read_key(&result), i * 10);
        }
    }

    /* Scan should return 100 odd keys in order */
    scan_ctx_t sc = { .count = 0 };
    bfs_btree_scan(&tree, NULL, scan_collector, &sc);
    TEST_ASSERT_EQ(sc.count, 100);
    for (uint32_t i = 0; i < 100; i++)
        TEST_ASSERT_EQ(sc.keys[i], i * 2 + 1);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: search_floor empty tree ──────────────────────────── */

static void test_search_floor_empty_tree(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, key_out, val_out;
    make_key(&key, 10);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&tree, &key, &key_out, &val_out), BFS_ERR_NOTFOUND);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: search_floor key smaller than all ───────────────── */

static void test_search_floor_key_smaller_than_all(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val;
    make_key(&key, 10); make_key(&val, 100);
    bfs_btree_insert(&tree, &key, &val);
    make_key(&key, 20); make_key(&val, 200);
    bfs_btree_insert(&tree, &key, &val);

    uint32_t search, key_out, val_out;
    make_key(&search, 5);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&tree, &search, &key_out, &val_out), BFS_ERR_NOTFOUND);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: search_floor key larger than all ────────────────── */

static void test_search_floor_key_larger_than_all(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val;
    make_key(&key, 10); make_key(&val, 100);
    bfs_btree_insert(&tree, &key, &val);
    make_key(&key, 20); make_key(&val, 200);
    bfs_btree_insert(&tree, &key, &val);

    uint32_t search, key_out, val_out;
    make_key(&search, 99);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&tree, &search, &key_out, &val_out), BFS_OK);
    TEST_ASSERT_EQ(read_key(&key_out), 20);
    TEST_ASSERT_EQ(read_key(&val_out), 200);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: search_floor exact match ────────────────────────── */

static void test_search_floor_exact_match(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);

    uint32_t key, val;
    make_key(&key, 10); make_key(&val, 100);
    bfs_btree_insert(&tree, &key, &val);
    make_key(&key, 20); make_key(&val, 200);
    bfs_btree_insert(&tree, &key, &val);
    make_key(&key, 30); make_key(&val, 300);
    bfs_btree_insert(&tree, &key, &val);

    uint32_t search, key_out, val_out;
    make_key(&search, 20);
    TEST_ASSERT_EQ(bfs_btree_search_floor(&tree, &search, &key_out, &val_out), BFS_OK);
    TEST_ASSERT_EQ(read_key(&key_out), 20);
    TEST_ASSERT_EQ(read_key(&val_out), 200);

    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static bool owned_rejects_noncurrent_root(uint64_t root_txn, uint64_t live_txn)
{
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    tracked_allocator_t tracked;
    if (!open_owned_root_fixture(&bio, &ba, &tree, &tracked, root_txn))
        return false;

    const bfs_blk_t old_root = tree.root;
    uint8_t before[BLK_SIZE], after[BLK_SIZE];
    uint64_t actual_txn = UINT64_MAX;
    bool ok = bfs_bio_read(bio, old_root, before) == BFS_OK &&
              bfs_btree_root_leaf_txn_id(&tree, &actual_txn) == BFS_OK &&
              actual_txn == root_txn;
    tree.txn_id_fallback = live_txn;
    tracked_allocator_reset(&tracked);
    fail_write_target = bio;
    fail_write_original_ops = bio->ops;
    observed_write_calls = 0;
    fail_next_write = false;
    bio->ops = &fail_write_ops;
    uint32_t key, val;
    make_key(&key, 20);
    make_key(&val, 200);
    bfs_err_t err = bfs_btree_replace_owned_root_leaf(
        &tree, old_root, &key, &val, 1);
    bio->ops = fail_write_original_ops;

    ok = ok && err == BFS_ERR_UNSUPPORTED && tree.root == old_root &&
         tracked.alloc_count == 0 && observed_write_calls == 0 &&
         bfs_bio_read(bio, old_root, after) == BFS_OK &&
         memcmp(before, after, sizeof(before)) == 0;
    close_owned_root_fixture(bio, ba);
    return ok;
}

static void test_owned_root_leaf_transfer(void)
{
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    tracked_allocator_t tracked;
    TEST_ASSERT(open_owned_root_fixture(&bio, &ba, &tree, &tracked, 31));

    const bfs_blk_t old_root = tree.root;
    uint8_t old_bytes[BLK_SIZE], after_bytes[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, old_bytes), BFS_OK);
    uint64_t root_txn = 0;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&tree, &root_txn), BFS_OK);
    TEST_ASSERT_EQ(root_txn, bfs_btree_txn_id(&tree));

    test_free_sink_t sink = {.available = 4};
    tree.free_sink = (bfs_free_sink_t){
        .ctx = &sink,
        .defer = test_defer_free,
        .headroom = test_free_headroom,
        .capacity = 4,
    };
    tracked_allocator_reset(&tracked);
    uint32_t keys[3], vals[3];
    for (uint32_t i = 0; i < 3; i++) {
        make_key(&keys[i], i + 20);
        make_key(&vals[i], i + 200);
    }
    TEST_ASSERT_EQ(bfs_btree_replace_owned_root_leaf(
                       &tree, old_root, keys, vals, 3), BFS_OK);

    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(tracked.alloc_count, 1);
    TEST_ASSERT_EQ(tracked.dealloc_count, 0);
    TEST_ASSERT(!tracked_has_block(tracked.deallocated, tracked.dealloc_count,
                                   old_root));
    TEST_ASSERT_EQ(sink.defer_count, 0);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, after_bytes), BFS_OK);
    TEST_ASSERT_MEM_EQ(old_bytes, after_bytes, sizeof(old_bytes));
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t got;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &keys[i], &got), BFS_OK);
        TEST_ASSERT_EQ(read_key(&got), i + 200);
    }
    uint32_t old_key, got;
    make_key(&old_key, 1);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &old_key, &got), BFS_ERR_NOTFOUND);
    close_owned_root_fixture(bio, ba);
}

static void test_owned_root_leaf_rejects_committed_root(void)
{
    TEST_ASSERT(owned_rejects_noncurrent_root(31, 32));
}

static void test_owned_root_leaf_rejects_future_root(void)
{
    TEST_ASSERT(owned_rejects_noncurrent_root(33, 32));
}

static void test_owned_root_leaf_rejects_mismatched_root(void)
{
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    tracked_allocator_t tracked;
    TEST_ASSERT(open_owned_root_fixture(&bio, &ba, &tree, &tracked, 32));

    const bfs_blk_t old_root = tree.root;
    uint8_t before[BLK_SIZE], after[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, before), BFS_OK);
    tracked_allocator_reset(&tracked);
    fail_write_target = bio;
    fail_write_original_ops = bio->ops;
    observed_write_calls = 0;
    fail_next_write = false;
    bio->ops = &fail_write_ops;
    uint32_t key, val;
    make_key(&key, 20);
    make_key(&val, 200);
    bfs_err_t err = bfs_btree_replace_owned_root_leaf(
        &tree, old_root + 1, &key, &val, 1);
    bio->ops = fail_write_original_ops;

    TEST_ASSERT_EQ(err, BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(tree.root, old_root);
    TEST_ASSERT_EQ(tracked.alloc_count, 0);
    TEST_ASSERT_EQ(observed_write_calls, 0);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, after), BFS_OK);
    TEST_ASSERT_MEM_EQ(before, after, sizeof(before));
    close_owned_root_fixture(bio, ba);
}

static void test_owned_root_leaf_write_failure_returns_scratch(void)
{
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    tracked_allocator_t tracked;
    TEST_ASSERT(open_owned_root_fixture(&bio, &ba, &tree, &tracked, 32));

    const bfs_blk_t old_root = tree.root;
    uint8_t before[BLK_SIZE], after[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, before), BFS_OK);
    test_free_sink_t sink = {.available = 4};
    tree.free_sink = (bfs_free_sink_t){
        .ctx = &sink,
        .defer = test_defer_free,
        .headroom = test_free_headroom,
        .capacity = 4,
    };
    tracked_allocator_reset(&tracked);
    fail_write_target = bio;
    fail_write_original_ops = bio->ops;
    observed_write_calls = 0;
    fail_next_write = true;
    bio->ops = &fail_write_ops;
    uint32_t key, val;
    make_key(&key, 20);
    make_key(&val, 200);
    bfs_err_t err = bfs_btree_replace_owned_root_leaf(
        &tree, old_root, &key, &val, 1);
    bio->ops = fail_write_original_ops;
    fail_next_write = false;

    TEST_ASSERT_EQ(err, BFS_ERR_IO);
    TEST_ASSERT_EQ(observed_write_calls, 1);
    TEST_ASSERT_EQ(tree.root, old_root);
    TEST_ASSERT_EQ(tracked.alloc_count, 1);
    TEST_ASSERT_EQ(tracked.dealloc_count, 1);
    TEST_ASSERT_EQ(tracked.deallocated[0], tracked.allocated[0]);
    TEST_ASSERT(tracked.allocated[0] != old_root);
    TEST_ASSERT(!tracked_has_block(tracked.deallocated, tracked.dealloc_count,
                                   old_root));
    TEST_ASSERT_EQ(sink.defer_count, 0);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, after), BFS_OK);
    TEST_ASSERT_MEM_EQ(before, after, sizeof(before));
    close_owned_root_fixture(bio, ba);
}

static void test_owned_root_leaf_bypasses_full_pending_headroom(void)
{
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    tracked_allocator_t tracked;
    TEST_ASSERT(open_owned_root_fixture(&bio, &ba, &tree, &tracked, 32));

    const bfs_blk_t old_root = tree.root;
    uint8_t old_bytes[BLK_SIZE], after_bytes[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, old_bytes), BFS_OK);
    test_free_sink_t sink = {.available = 0};
    tree.free_sink = (bfs_free_sink_t){
        .ctx = &sink,
        .defer = test_defer_free,
        .headroom = test_free_headroom,
        .capacity = 1,
    };
    tracked_allocator_reset(&tracked);
    fail_write_target = bio;
    fail_write_original_ops = bio->ops;
    observed_write_calls = 0;
    fail_next_write = false;
    bio->ops = &fail_write_ops;
    uint32_t keys[2], vals[2];
    make_key(&keys[0], 20);
    make_key(&keys[1], 21);
    make_key(&vals[0], 200);
    make_key(&vals[1], 210);

    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, keys, vals, 2),
                   BFS_ERR_AGAIN);
    TEST_ASSERT_EQ(tree.root, old_root);
    TEST_ASSERT_EQ(tracked.alloc_count, 0);
    TEST_ASSERT_EQ(observed_write_calls, 0);
    TEST_ASSERT_EQ(bfs_btree_replace_owned_root_leaf(
                       &tree, old_root, keys, vals, 2), BFS_OK);
    bio->ops = fail_write_original_ops;

    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(tracked.alloc_count, 1);
    TEST_ASSERT_EQ(tracked.dealloc_count, 0);
    TEST_ASSERT_EQ(observed_write_calls, 1);
    TEST_ASSERT_EQ(sink.defer_count, 0);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, after_bytes), BFS_OK);
    TEST_ASSERT_MEM_EQ(old_bytes, after_bytes, sizeof(old_bytes));
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t got;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &keys[i], &got), BFS_OK);
        TEST_ASSERT_EQ(read_key(&got), 200 + i * 10);
    }
    close_owned_root_fixture(bio, ba);
}

static void test_legacy_leaf_padding_cow_canonicalization(void)
{
    const uint32_t block_sizes[] = {1024, 4096, 65536};
    for (uint32_t size_index = 0;
         size_index < sizeof(block_sizes) / sizeof(block_sizes[0]);
         size_index++) {
        const uint32_t block_size = block_sizes[size_index];
        bfs_bio_t *bio = NULL;
        bootstrap_alloc_t *ba = NULL;
        bfs_btree_t tree;
        TEST_ASSERT(open_padding_fixture(block_size, &u32_u64_ops,
                                         &bio, &ba, &tree));

        uint32_t keys[2];
        uint64_t vals[2];
        for (uint32_t i = 0; i < 2; i++) {
            make_key(&keys[i], 10 + i);
            make_u64_value(&vals[i], 1000 + i);
            TEST_ASSERT_EQ(bfs_btree_insert(&tree, &keys[i], &vals[i]), BFS_OK);
        }
        TEST_ASSERT_EQ(tree.height, 1);

        uint8_t *legacy = malloc(block_size);
        uint8_t *before = malloc(block_size);
        uint8_t *canonical = malloc(block_size);
        TEST_ASSERT(legacy != NULL && before != NULL && canonical != NULL);
        const bfs_blk_t old_root = tree.root;
        TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, legacy), BFS_OK);
        uint32_t gap_start, values_start, values_end;
        TEST_ASSERT(node_padding_ranges(&tree, legacy, &gap_start,
                                        &values_start, &values_end));
        TEST_ASSERT(gap_start < values_start);
        TEST_ASSERT(values_end < block_size);
        TEST_ASSERT(seed_legacy_node_padding(&tree, old_root, legacy, 19));
        TEST_ASSERT(range_has_nonzero(legacy, gap_start, values_start));
        TEST_ASSERT(range_has_nonzero(legacy, values_end, block_size));
        TEST_ASSERT_EQ(bfs_be32(((bfs_btnode_hdr_t *)legacy)->crc32),
                       reference_node_crc(legacy, block_size));
        TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, before), BFS_OK);

        uint64_t got = 0;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &keys[0], &got), BFS_OK);
        TEST_ASSERT_EQ(read_u64_value(&got), 1000);

        const uint64_t replacement = bfs_be64(9000);
        TEST_ASSERT_EQ(bfs_btree_update(&tree, &keys[0], &replacement), BFS_OK);
        TEST_ASSERT(tree.root != old_root);
        TEST_ASSERT_EQ(bfs_bio_read(bio, tree.root, canonical), BFS_OK);
        TEST_ASSERT(node_has_canonical_padding(&tree, canonical));
        TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, legacy), BFS_OK);
        TEST_ASSERT_MEM_EQ(before, legacy, block_size);

        for (uint32_t i = 0; i < 2; i++) {
            TEST_ASSERT_EQ(bfs_btree_search(&tree, &keys[i], &got), BFS_OK);
            TEST_ASSERT_EQ(read_u64_value(&got), i == 0 ? 9000 : 1001);
        }
        free(legacy);
        free(before);
        free(canonical);
        close_padding_fixture(bio, ba);
    }
}

static void test_legacy_full_leaf_no_key_gap(void)
{
    const uint32_t block_size = 1024;
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    TEST_ASSERT(open_padding_fixture(block_size, &u32_u40_ops,
                                     &bio, &ba, &tree));

    uint32_t capacity = leaf_max_keys(&tree);
    TEST_ASSERT(capacity > 0);
    for (uint32_t i = 0; i < capacity; i++) {
        uint32_t key;
        uint8_t val[5];
        make_key(&key, i + 1);
        make_u40_value(val, i + 10000);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, val), BFS_OK);
    }
    TEST_ASSERT_EQ(tree.height, 1);

    uint8_t *legacy = malloc(block_size);
    uint8_t *before = malloc(block_size);
    uint8_t *canonical = malloc(block_size);
    TEST_ASSERT(legacy != NULL && before != NULL && canonical != NULL);
    const bfs_blk_t old_root = tree.root;
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, legacy), BFS_OK);
    uint32_t gap_start, values_start, values_end;
    TEST_ASSERT(node_padding_ranges(&tree, legacy, &gap_start,
                                    &values_start, &values_end));
    TEST_ASSERT_EQ(gap_start, values_start);
    TEST_ASSERT(values_end < block_size);
    fill_nonzero_range(legacy, values_end, block_size, 41);
    ((bfs_btnode_hdr_t *)legacy)->crc32 =
        bfs_be32(reference_node_crc(legacy, block_size));
    TEST_ASSERT(range_has_nonzero(legacy, values_end, block_size));
    TEST_ASSERT_EQ(bfs_bio_write(bio, old_root, legacy), BFS_OK);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, before), BFS_OK);

    uint32_t target_key;
    make_key(&target_key, capacity / 2 + 1);
    uint8_t new_value[5], got[5];
    make_u40_value(new_value, 0xABCDEF0123ULL);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &target_key, &got), BFS_OK);
    TEST_ASSERT_EQ(read_u40_value(got), capacity / 2 + 10000);
    TEST_ASSERT_EQ(bfs_btree_update(&tree, &target_key, new_value), BFS_OK);
    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(bfs_bio_read(bio, tree.root, canonical), BFS_OK);
    TEST_ASSERT(node_has_canonical_padding(&tree, canonical));
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, legacy), BFS_OK);
    TEST_ASSERT_MEM_EQ(before, legacy, block_size);

    for (uint32_t i = 0; i < capacity; i++) {
        uint32_t key;
        make_key(&key, i + 1);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &got), BFS_OK);
        uint64_t expected = i == capacity / 2 ? 0xABCDEF0123ULL : i + 10000;
        TEST_ASSERT_EQ(read_u40_value(got), expected);
    }
    free(legacy);
    free(before);
    free(canonical);
    close_padding_fixture(bio, ba);
}

static void test_legacy_internal_padding_cow_canonicalization(void)
{
    const uint32_t block_size = 1024;
    const uint32_t item_count = 100;
    bfs_bio_t *bio = NULL;
    bootstrap_alloc_t *ba = NULL;
    bfs_btree_t tree;
    TEST_ASSERT(open_padding_fixture(block_size, &u32_u64_ops,
                                     &bio, &ba, &tree));
    for (uint32_t i = 0; i < item_count; i++) {
        uint32_t key;
        uint64_t val;
        make_key(&key, i);
        make_u64_value(&val, 10000 + i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    }
    TEST_ASSERT_EQ(tree.height, 2);

    collected_nodes_t collected = {0};
    TEST_ASSERT_EQ(bfs_btree_walk_nodes(&tree, collect_node_block,
                                        &collected), BFS_OK);
    TEST_ASSERT(!collected.overflow);
    TEST_ASSERT(collected.count >= 3);
    uint8_t *scratch = malloc(block_size);
    uint8_t *root_before = malloc(block_size);
    uint8_t *root_after = malloc(block_size);
    uint8_t *leaf_before = malloc(block_size);
    uint8_t *leaf_after = malloc(block_size);
    TEST_ASSERT(scratch != NULL && root_before != NULL && root_after != NULL &&
                leaf_before != NULL && leaf_after != NULL);

    uint32_t leaf_count = 0;
    uint32_t internal_count = 0;
    for (uint32_t i = 0; i < collected.count; i++) {
        TEST_ASSERT(seed_legacy_node_padding(&tree, collected.blocks[i],
                                             scratch, 53 + i * 17));
        TEST_ASSERT_EQ(bfs_bio_read(bio, collected.blocks[i], scratch), BFS_OK);
        uint32_t gap_start, values_start, values_end;
        TEST_ASSERT(node_padding_ranges(&tree, scratch, &gap_start,
                                        &values_start, &values_end));
        TEST_ASSERT(range_has_nonzero(scratch, gap_start, values_start));
        TEST_ASSERT(range_has_nonzero(scratch, values_end, block_size));
        TEST_ASSERT_EQ(bfs_be32(((bfs_btnode_hdr_t *)scratch)->crc32),
                       reference_node_crc(scratch, block_size));
        if (node_level(scratch) == BFS_BTNODE_LEAF) leaf_count++;
        else internal_count++;
    }
    TEST_ASSERT(leaf_count >= 2);
    TEST_ASSERT(internal_count >= 1);

    uint64_t got;
    for (uint32_t i = 0; i < item_count; i++) {
        uint32_t key;
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &got), BFS_OK);
        TEST_ASSERT_EQ(read_u64_value(&got), 10000 + i);
    }

    const bfs_blk_t old_root = tree.root;
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, root_before), BFS_OK);
    const uint32_t old_root_keys = num_keys(root_before);
    TEST_ASSERT(old_root_keys > 0);
    uint32_t target_key;
    make_key(&target_key, 5);
    uint32_t child_index = 0;
    while (child_index < old_root_keys &&
           u32_compare(&target_key, node_key(&tree, root_before, child_index)) >= 0)
        child_index++;
    const bfs_blk_t old_leaf = get_child(&tree, root_before, child_index);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_leaf, leaf_before), BFS_OK);
    TEST_ASSERT_EQ(node_level(leaf_before), BFS_BTNODE_LEAF);

    const uint64_t replacement = bfs_be64(0x1122334455667788ULL);
    TEST_ASSERT_EQ(bfs_btree_update(&tree, &target_key, &replacement), BFS_OK);
    TEST_ASSERT(tree.root != old_root);
    TEST_ASSERT_EQ(tree.height, 2);
    TEST_ASSERT_EQ(bfs_bio_read(bio, tree.root, root_after), BFS_OK);
    TEST_ASSERT(node_has_canonical_padding(&tree, root_after));
    TEST_ASSERT_EQ(num_keys(root_after), old_root_keys);
    for (uint32_t i = 0; i < old_root_keys; i++)
        TEST_ASSERT_MEM_EQ(node_key(&tree, root_before, i),
                           node_key(&tree, root_after, i), u32_ops.key_size);
    for (uint32_t i = 0; i <= old_root_keys; i++) {
        bfs_blk_t old_child = get_child(&tree, root_before, i);
        bfs_blk_t new_child = get_child(&tree, root_after, i);
        if (i == child_index)
            TEST_ASSERT(new_child != old_child);
        else
            TEST_ASSERT_EQ(new_child, old_child);
    }
    const bfs_blk_t new_leaf = get_child(&tree, root_after, child_index);
    TEST_ASSERT_EQ(bfs_bio_read(bio, new_leaf, leaf_after), BFS_OK);
    TEST_ASSERT(node_has_canonical_padding(&tree, leaf_after));
    TEST_ASSERT_EQ(num_keys(leaf_after), num_keys(leaf_before));
    for (uint32_t i = 0; i < num_keys(leaf_before); i++) {
        TEST_ASSERT_MEM_EQ(node_key(&tree, leaf_before, i),
                           node_key(&tree, leaf_after, i), u32_ops.key_size);
        uint64_t expected = read_key(node_key(&tree, leaf_before, i)) == 5
                                ? 0x1122334455667788ULL
                                : read_u64_value(leaf_val(&tree, leaf_before, i));
        TEST_ASSERT_EQ(read_u64_value(leaf_val(&tree, leaf_after, i)), expected);
    }
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_root, scratch), BFS_OK);
    TEST_ASSERT_MEM_EQ(root_before, scratch, block_size);
    TEST_ASSERT_EQ(bfs_bio_read(bio, old_leaf, scratch), BFS_OK);
    TEST_ASSERT_MEM_EQ(leaf_before, scratch, block_size);

    for (uint32_t i = 0; i < item_count; i++) {
        uint32_t key;
        make_key(&key, i);
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &got), BFS_OK);
        uint64_t expected = i == 5 ? 0x1122334455667788ULL : 10000 + i;
        TEST_ASSERT_EQ(read_u64_value(&got), expected);
    }

    free(scratch);
    free(root_before);
    free(root_after);
    free(leaf_before);
    free(leaf_after);
    close_padding_fixture(bio, ba);
}

static void test_replace_root_leaf(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &u32_ops,
                                  BFS_BLK_NULL, 1), BFS_OK);
    uint32_t k, v;
    make_key(&k, 1); make_key(&v, 10);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &k, &v), BFS_OK);
    uint32_t keys[3], vals[3];
    for (uint32_t i = 0; i < 3; i++) { make_key(&keys[i], i + 20); make_key(&vals[i], i + 200); }
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, keys, vals, 3), BFS_OK);
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t got;
        TEST_ASSERT_EQ(bfs_btree_search(&tree, &keys[i], &got), BFS_OK);
        TEST_ASSERT_EQ(read_key(&got), i + 200);
    }
    TEST_ASSERT_EQ(bfs_btree_leaf_capacity(&tree), (BLK_SIZE - 28) / 8);
    free(ba); bfs_bio_close(bio); unlink(TEST_IMG);
}

static void test_replace_root_leaf_rejects_order(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);
    uint32_t k, v, keys[2], vals[2], got;
    make_key(&k, 1); make_key(&v, 2); bfs_btree_insert(&tree, &k, &v);
    make_key(&keys[0], 5); make_key(&keys[1], 5);
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, keys, vals, 2), BFS_ERR_INVAL);
    make_key(&keys[0], 6); make_key(&keys[1], 4);
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, keys, vals, 2), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &k, &got), BFS_OK);
    TEST_ASSERT_EQ(read_key(&got), 2);
    free(ba); bfs_bio_close(bio); unlink(TEST_IMG);
}

static void test_replace_root_leaf_write_failure(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);
    uint32_t k, v, key, val, got;
    make_key(&k, 1); make_key(&v, 2); bfs_btree_insert(&tree, &k, &v);
    make_key(&key, 3); make_key(&val, 4);
    bfs_blk_t old_root = tree.root;
    fail_write_target = bio; fail_write_original_ops = bio->ops;
    bio->ops = &fail_write_ops; fail_next_write = true;
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, &key, &val, 1), BFS_ERR_IO);
    bio->ops = fail_write_original_ops;
    TEST_ASSERT_EQ(tree.root, old_root);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &k, &got), BFS_OK);
    TEST_ASSERT_EQ(read_key(&got), 2);
    free(ba); bfs_bio_close(bio); unlink(TEST_IMG);
}

static void test_replace_root_leaf_deeper_unsupported(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1);
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t k, v; make_key(&k, i); make_key(&v, i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &k, &v), BFS_OK);
    }
    TEST_ASSERT(tree.height > 1);
    uint32_t key, val; make_key(&key, 2000); make_key(&val, 1);
    bfs_blk_t root = tree.root;
    TEST_ASSERT_EQ(bfs_btree_replace_root_leaf(&tree, &key, &val, 1), BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(tree.root, root);
    free(ba); bfs_bio_close(bio); unlink(TEST_IMG);
}

static void test_cached_node_crc_revalidation(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bfs_cache_t cache;
    TEST_ASSERT_EQ(bfs_cache_init(&cache, bio, 1), BFS_OK);
    bfs_cache_set_node_write_retention(&cache, true);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, &cache.bio, &ba->base, &u32_ops,
                                  BFS_BLK_NULL, 1), BFS_OK);
    uint32_t key, val, found;
    make_key(&key, 10); make_key(&val, 20);
    TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, tree.root));
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, tree.root));

    uint8_t good[BLK_SIZE], bad[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, tree.root, good), BFS_OK);
    memcpy(bad, good, BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bfs_btnode_hdr_t *bad_hdr = (bfs_btnode_hdr_t *)bad;
    bad_hdr->num_keys = bfs_be32(leaf_max_keys(&tree) + 1);
    bad_hdr->crc32 = 0;
    bad_hdr->crc32 = bfs_be32(node_compute_crc(&tree, bad));
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, tree.root, bad), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, tree.root));
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(bfs_bio_write_node(&cache.bio, tree.root, good), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, tree.root));

    /* A hot cache does not observe out-of-band media changes. Invalidation
     * forces a fresh read and CRC check. */
    memcpy(bad, good, BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bad[BLK_SIZE - 1] ^= 1;
    TEST_ASSERT_EQ(bfs_bio_write(bio, tree.root, bad), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_OK);
    bfs_cache_invalidate(&cache);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(bfs_bio_write(bio, tree.root, good), BFS_OK);
    bfs_cache_invalidate(&cache);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_OK);

    memcpy(bad, good, BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bad[BLK_SIZE - 1] ^= 1;
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, tree.root, bad), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, tree.root));
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(bfs_bio_write(&cache.bio, tree.root, good), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_OK);
    TEST_ASSERT(bfs_bio_node_crc_valid(&cache.bio, tree.root));

    TEST_ASSERT_EQ(bfs_bio_read(&cache.bio, tree.root + 1, bad), BFS_OK);
    TEST_ASSERT(!bfs_bio_node_crc_valid(&cache.bio, tree.root));
    good[BLK_SIZE - 1] ^= 1;
    TEST_ASSERT_EQ(bfs_bio_write(bio, tree.root, good), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_ERR_CORRUPT);
    bfs_cache_destroy(&cache);
    free(ba); bfs_bio_close(bio); unlink(TEST_IMG);
}

/* The read check hashes only used ranges of canonical nodes. It must still
 * reject a change to any byte, and accept legacy non-zero padding. */
static void flip_and_expect(bfs_btree_t *tree, bfs_blk_t blk, const uint8_t *good,
                            uint32_t offset, bool recompute_crc, bfs_err_t expected)
{
    uint8_t bad[BLK_SIZE];
    uint32_t key, found;
    make_key(&key, 3);
    memcpy(bad, good, BLK_SIZE); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    bad[offset] ^= 0x5a;
    if (recompute_crc)
        ((bfs_btnode_hdr_t *)bad)->crc32 = bfs_be32(reference_node_crc(bad, BLK_SIZE));
    TEST_ASSERT_EQ(bfs_bio_write(tree->bio, blk, bad), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(tree, &key, &found), expected);
    TEST_ASSERT_EQ(bfs_bio_write(tree->bio, blk, good), BFS_OK);
    TEST_ASSERT_EQ(bfs_btree_search(tree, &key, &found), BFS_OK);
}

static void test_read_crc_covers_every_byte(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    for (uint32_t i = 1; i <= 5; i++)
        TEST_ASSERT_EQ(insert_tenfold(&tree, i), BFS_OK);
    TEST_ASSERT_EQ(tree.height, 1);
    uint8_t good[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, tree.root, good), BFS_OK);
    uint32_t gap_start, values_start, values_end;
    TEST_ASSERT(node_padding_ranges(&tree, good, &gap_start, &values_start, &values_end));
    TEST_ASSERT(range_is_zero(good, gap_start, values_start));
    TEST_ASSERT(range_is_zero(good, values_end, BLK_SIZE));

    const uint32_t offsets[] = {
        sizeof(bfs_btnode_hdr_t), gap_start - 1, gap_start, values_start - 1,
        values_start, values_end - 1, values_end, values_end + 17, BLK_SIZE - 1,
    };
    for (uint32_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
        flip_and_expect(&tree, tree.root, good, offsets[i], false, BFS_ERR_CORRUPT);
    /* Non-zero padding with a full-block CRC, as older writers left it. */
    flip_and_expect(&tree, tree.root, good, gap_start, true, BFS_OK);
    flip_and_expect(&tree, tree.root, good, BLK_SIZE - 1, true, BFS_OK);

    uint8_t legacy[BLK_SIZE];
    TEST_ASSERT(seed_legacy_node_padding(&tree, tree.root, legacy, 7));
    uint32_t key, found;
    make_key(&key, 3);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &found), BFS_OK);
    flip_and_expect(&tree, tree.root, legacy, BLK_SIZE - 2, false, BFS_ERR_CORRUPT);
    flip_and_expect(&tree, tree.root, legacy, gap_start + 1, false, BFS_ERR_CORRUPT);
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* lower_bound either names the first key >= the query or, when that key is
 * in a later leaf, a bound no smaller than the query below which no key
 * lies. */
static void test_lower_bound_matches_reference(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    bootstrap_alloc_t *ba = bootstrap_create(2, BLK_COUNT);
    bfs_btree_t tree;
    TEST_ASSERT_EQ(bfs_btree_init(&tree, bio, &ba->base, &u32_ops, BFS_BLK_NULL, 1),
                   BFS_OK);
    uint32_t key, val, out;
    make_key(&key, 5);
    TEST_ASSERT_EQ(bfs_btree_lower_bound(&tree, &key, &out), BFS_ERR_NOTFOUND);
    const uint32_t count = 1500;
    for (uint32_t i = 1; i <= count; i++) {
        make_key(&key, i * 10u); val = bfs_be32(i);
        TEST_ASSERT_EQ(bfs_btree_insert(&tree, &key, &val), BFS_OK);
    }
    TEST_ASSERT(tree.height >= 2);
    unsigned again = 0;
    for (uint32_t q = 0; q <= count * 10u + 1u; q += 3u) {
        make_key(&key, q);
        bfs_err_t err = bfs_btree_lower_bound(&tree, &key, &out);
        uint32_t expected = q <= 10u ? 10u : ((q + 9u) / 10u) * 10u;
        uint32_t got = read_key(&out);
        if (expected > count * 10u) {
            TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);
        } else if (err == BFS_OK) {
            TEST_ASSERT_EQ(got, expected);
        } else {
            TEST_ASSERT_EQ(err, BFS_ERR_AGAIN);
            TEST_ASSERT(got > q && got <= expected);
            again++;
        }
    }
    TEST_ASSERT(again > 0);
    free(ba);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("B+tree")
    TEST_RUN(test_empty_tree_search);
    TEST_RUN(test_single_insert_search);
    TEST_RUN(test_owned_rewrite_requires_registration);
    TEST_RUN(test_owned_rewrite_publishes_after_fallible_steps);
    TEST_RUN(test_owned_rewrite_abort_after_staging_keeps_bytes);
    TEST_RUN(test_duplicate_insert);
    TEST_RUN(test_sequential_inserts);
    TEST_RUN(test_reverse_inserts);
    TEST_RUN(test_scan_all);
    TEST_RUN(test_scan_from_key);
    TEST_RUN(test_scan_three_levels);
    TEST_RUN(test_scan_survives_callback_changes);
    TEST_RUN(test_scan_cursor_resumes_in_leaf);
    TEST_RUN(test_update_key_stays_inside_its_leaf);
    TEST_RUN(test_search_hint_follows_changes);
    TEST_RUN(test_be32_search_matches_comparator);
    TEST_RUN(test_cow_old_root_preserved);
    TEST_RUN(test_single_delete);
    TEST_RUN(test_delete_all);
    TEST_RUN(test_delete_reverse);
    TEST_RUN(test_insert_delete_interleaved);
    TEST_RUN(test_search_floor_empty_tree);
    TEST_RUN(test_search_floor_key_smaller_than_all);
    TEST_RUN(test_search_floor_key_larger_than_all);
    TEST_RUN(test_search_floor_exact_match);
    TEST_RUN(test_owned_root_leaf_transfer);
    TEST_RUN(test_owned_root_leaf_rejects_committed_root);
    TEST_RUN(test_owned_root_leaf_rejects_future_root);
    TEST_RUN(test_owned_root_leaf_rejects_mismatched_root);
    TEST_RUN(test_owned_root_leaf_write_failure_returns_scratch);
    TEST_RUN(test_owned_root_leaf_bypasses_full_pending_headroom);
    TEST_RUN(test_legacy_leaf_padding_cow_canonicalization);
    TEST_RUN(test_legacy_full_leaf_no_key_gap);
    TEST_RUN(test_legacy_internal_padding_cow_canonicalization);
    TEST_RUN(test_replace_root_leaf);
    TEST_RUN(test_replace_root_leaf_rejects_order);
    TEST_RUN(test_replace_root_leaf_write_failure);
    TEST_RUN(test_replace_root_leaf_deeper_unsupported);
    TEST_RUN(test_cached_node_crc_revalidation);
    TEST_RUN(test_read_crc_covers_every_byte);
    TEST_RUN(test_lower_bound_matches_reference);
TEST_SUITE_END()
