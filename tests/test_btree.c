/*
 * BFS — B+tree tests: search, insert, split, scan, COW
 */

#include "test_harness.h"
#include "bfs_btree.h"
#include "bfs_btree_internal.h"
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

static void make_key(uint32_t *k, uint32_t v) { *k = bfs_be32(v); }
static uint32_t read_key(const void *k) { return bfs_load_be32(k); }

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

TEST_SUITE_BEGIN("B+tree")
    TEST_RUN(test_empty_tree_search);
    TEST_RUN(test_single_insert_search);
    TEST_RUN(test_duplicate_insert);
    TEST_RUN(test_sequential_inserts);
    TEST_RUN(test_reverse_inserts);
    TEST_RUN(test_scan_all);
    TEST_RUN(test_scan_from_key);
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
    TEST_RUN(test_replace_root_leaf);
    TEST_RUN(test_replace_root_leaf_rejects_order);
    TEST_RUN(test_replace_root_leaf_write_failure);
    TEST_RUN(test_replace_root_leaf_deeper_unsupported);
    TEST_RUN(test_cached_node_crc_revalidation);
TEST_SUITE_END()
