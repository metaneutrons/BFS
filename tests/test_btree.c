/*
 * BFS — B+tree tests: search, insert, split, scan, COW
 */

#include "test_harness.h"
#include "bfs_btree.h"
#include "block_device_emu.h"
#include <unistd.h>

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
    uint32_t va = bfs_be32(*(const uint32_t *)a);
    uint32_t vb = bfs_be32(*(const uint32_t *)b);
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
static uint32_t read_key(const uint32_t *k) { return bfs_be32(*k); }

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

    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_OK);
    TEST_ASSERT_EQ(read_key(&result), 999);

    /* Not found */
    make_key(&key, 101);
    TEST_ASSERT_EQ(bfs_btree_search(&tree, &key, &result), BFS_ERR_NOTFOUND);

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
        sc->keys[sc->count++] = read_key((const uint32_t *)key);
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

    bfs_bio_close(bio);
    unlink(TEST_IMG);
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
TEST_SUITE_END()
