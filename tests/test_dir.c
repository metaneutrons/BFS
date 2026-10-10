/*
 * BFS — Directory B+tree tests
 */

#include "test_harness.h"
#include "bfs_dir.h"
#include "bfs_alloc.h"
#include "bfs_btree_internal.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define TEST_IMG "test_dir.img"
#define BLK_SIZE 4096
#define BLK_COUNT 16384  /* 64MB — large dirs need space */
#define DATA_START 2
#define ROOT_DIR_INO 5

static bfs_freespace_t g_fs;

static bfs_freespace_t *make_fs(bfs_bio_t *bio)
{
    bfs_freespace_init(&g_fs, bio, BFS_BLK_NULL, 1);
    bfs_freespace_add(&g_fs, DATA_START, BLK_COUNT - DATA_START);
    bfs_freespace_refill_reserve(&g_fs);
    return &g_fs;
}

/* ── Test: basic insert and lookup ─────────────────────────── */

static void test_dir_basic(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, "hello.txt", 9, 100, BFS_INODE_FILE), BFS_OK);

    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, "hello.txt", 9, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 100);
    TEST_ASSERT_EQ(type, BFS_INODE_FILE);

    /* Not found */
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, "nope", 4, &ino, &type), BFS_ERR_NOTFOUND);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: a leaf with an entry whose hash does not match its name ── */

static bool count_entry(const char *name, uint8_t name_len, uint32_t inode_nr,
                        uint32_t type, void *ctx)
{
    (void)name;
    (void)name_len;
    (void)inode_nr;
    (void)type;
    (*(unsigned *)ctx)++;
    return true;
}

/* Leaf validation checks every entry, so a CRC-valid leaf with a forged hash
 * is rejected by lookups as well as scans, before any entry is used. */
static void test_dir_forged_hash_rejects_leaf(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);
    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);
    TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, "alpha", 5, 100, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, "beta", 4, 101, BFS_INODE_FILE), BFS_OK);
    TEST_ASSERT_EQ(dt.tree.height, 1);
    unsigned count = 0;
    TEST_ASSERT_EQ(bfs_dir_scan(&dt, ROOT_DIR_INO, count_entry, &count), BFS_OK);
    TEST_ASSERT_EQ(count, 2);

    uint8_t block[BLK_SIZE];
    bfs_blk_t root = dt.tree.root;
    TEST_ASSERT_EQ(bfs_bio_read(bio, root, block), BFS_OK);
    uint32_t n = num_keys(block), forged = n;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *val = leaf_val(&dt.tree, block, i);
        if (val[BFS_DIR_HEAD_NAME_LEN] == 4 &&
            memcmp(val + BFS_DIR_HEAD_NAME, "beta", 4) == 0) forged = i;
    }
    TEST_ASSERT(forged < n);
    ((uint8_t *)node_key(&dt.tree, block, forged))[BFS_DIR_KEY_HASH + 3] ^= 1;
    hdr_of(block)->crc32 = bfs_be32(node_compute_crc(&dt.tree, block));
    TEST_ASSERT_EQ(bfs_bio_write(bio, root, block), BFS_OK);

    bfs_dir_tree_t reopened;
    bfs_dir_init(&reopened, bio, bfs_freespace_allocator(fs), root, 1);
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&reopened, ROOT_DIR_INO, "alpha", 5, &ino, &type),
                   BFS_ERR_CORRUPT);
    count = 0;
    TEST_ASSERT_EQ(bfs_dir_scan(&reopened, ROOT_DIR_INO, count_entry, &count),
                   BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(count, 0);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: case-insensitive lookup ─────────────────────────── */

static void test_dir_case_insensitive(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, "MyFile.txt", 10, 42, BFS_INODE_FILE), BFS_OK);

    uint32_t ino, type;
    /* Lookup with different case */
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, "myfile.txt", 10, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 42);

    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, "MYFILE.TXT", 10, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 42);

    /* Duplicate with different case should fail */
    TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, "myfile.txt", 10, 99, BFS_INODE_FILE), BFS_ERR_EXISTS);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: international characters ────────────────────────── */

static void test_dir_intl_chars(void)
{
    /* Amiga intl: ä (0xE4) folds to Ä (0xC4) */
    TEST_ASSERT_EQ(bfs_intl_toupper(0xE4), 0xC4);
    TEST_ASSERT_EQ(bfs_intl_toupper(0xF6), 0xD6); /* ö → Ö */
    TEST_ASSERT_EQ(bfs_intl_toupper(0xFC), 0xDC); /* ü → Ü */
    TEST_ASSERT_EQ(bfs_intl_toupper(0xF7), 0xF7); /* ÷ stays ÷ (exception) */
}

/* ── Test: remove ──────────────────────────────────────────── */

static void test_dir_remove(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    bfs_dir_insert(&dt, ROOT_DIR_INO, "file1", 5, 10, BFS_INODE_FILE);
    bfs_dir_insert(&dt, ROOT_DIR_INO, "file2", 5, 20, BFS_INODE_FILE);

    TEST_ASSERT_EQ(bfs_dir_remove(&dt, ROOT_DIR_INO, "file1", 5), BFS_OK);

    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, "file1", 5, &ino, &type), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, "file2", 5, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 20);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: scan directory ──────────────────────────────────── */

typedef struct { uint32_t count; uint32_t inodes[200]; } scan_result_t;

static bool dir_scan_collector(const char *name, uint8_t name_len,
                                uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    (void)name; (void)name_len; (void)entry_type;
    scan_result_t *sr = (scan_result_t *)ctx;
    if (sr->count < 200)
        sr->inodes[sr->count++] = inode_nr;
    return true;
}

static void test_dir_scan(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    /* Insert entries in two different directories */
    for (uint32_t i = 0; i < 10; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "file%u", i);
        bfs_dir_insert(&dt, ROOT_DIR_INO, name, (uint8_t)len, 100 + i, BFS_INODE_FILE);
    }
    for (uint32_t i = 0; i < 5; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "sub%u", i);
        bfs_dir_insert(&dt, 200, name, (uint8_t)len, 300 + i, BFS_INODE_FILE);
    }

    /* Scan root dir — should get exactly 10 entries */
    scan_result_t sr = { .count = 0 };
    TEST_ASSERT_EQ(bfs_dir_scan(&dt, ROOT_DIR_INO, dir_scan_collector, &sr), BFS_OK);
    TEST_ASSERT_EQ(sr.count, 10);

    /* Scan dir 200 — should get exactly 5 entries */
    sr.count = 0;
    TEST_ASSERT_EQ(bfs_dir_scan(&dt, 200, dir_scan_collector, &sr), BFS_OK);
    TEST_ASSERT_EQ(sr.count, 5);

    /* Scan empty dir — should get 0 */
    sr.count = 0;
    TEST_ASSERT_EQ(bfs_dir_scan(&dt, 999, dir_scan_collector, &sr), BFS_OK);
    TEST_ASSERT_EQ(sr.count, 0);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: resume a scan after a name ─────────────────────── */

static uint8_t entry_name(char name[8], uint32_t inode_nr)
{
    return (uint8_t)snprintf(name, 8, "e%02u", inode_nr - 100);
}

static void test_dir_scan_after(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    char name[8];
    for (uint32_t i = 0; i < 40; i++) {
        uint8_t len = entry_name(name, 100 + i);
        TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, name, len, 100 + i, BFS_INODE_FILE), BFS_OK);
        /* The neighbouring directory must not leak into the scan. */
        TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO + 1, name, len, 200 + i, BFS_INODE_FILE), BFS_OK);
    }
    scan_result_t all = { .count = 0 };
    TEST_ASSERT_EQ(bfs_dir_scan(&dt, ROOT_DIR_INO, dir_scan_collector, &all), BFS_OK);
    TEST_ASSERT_EQ(all.count, 40);

    /* Resuming after the k-th entry yields exactly the entries after it. */
    for (uint32_t k = 0; k < all.count; k++) {
        scan_result_t rest = { .count = 0 };
        uint8_t len = entry_name(name, all.inodes[k]);
        TEST_ASSERT_EQ(bfs_dir_scan_after(&dt, ROOT_DIR_INO, name, len, dir_scan_collector, &rest), BFS_OK);
        TEST_ASSERT_EQ(rest.count, all.count - k - 1);
        for (uint32_t i = 0; i < rest.count; i++)
            TEST_ASSERT_EQ(rest.inodes[i], all.inodes[k + 1 + i]);
    }

    /* The resume name compares case-insensitively and need not exist. */
    uint8_t len = entry_name(name, all.inodes[5]);
    name[0] = 'E';
    scan_result_t rest = { .count = 0 };
    TEST_ASSERT_EQ(bfs_dir_scan_after(&dt, ROOT_DIR_INO, name, len, dir_scan_collector, &rest), BFS_OK);
    TEST_ASSERT_EQ(rest.count, all.count - 6);
    TEST_ASSERT_EQ(rest.inodes[0], all.inodes[6]);
    len = entry_name(name, all.inodes[10]);
    TEST_ASSERT_EQ(bfs_dir_remove(&dt, ROOT_DIR_INO, name, len), BFS_OK);
    rest.count = 0;
    TEST_ASSERT_EQ(bfs_dir_scan_after(&dt, ROOT_DIR_INO, name, len, dir_scan_collector, &rest), BFS_OK);
    TEST_ASSERT_EQ(rest.count, all.count - 11);
    TEST_ASSERT_EQ(rest.inodes[0], all.inodes[11]);

    TEST_ASSERT_EQ(bfs_dir_scan_after(&dt, ROOT_DIR_INO, name, 0, dir_scan_collector, &rest),
                   BFS_ERR_INVAL);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: large directory (1000 entries) ──────────────────── */

static void test_dir_large(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    for (uint32_t i = 0; i < 1000; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "document_%04u.txt", i);
        TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, name, (uint8_t)len, 1000 + i, BFS_INODE_FILE), BFS_OK);
    }

    /* Spot-check lookups */
    for (uint32_t i = 0; i < 1000; i += 37) {
        char name[32];
        int len = snprintf(name, sizeof(name), "document_%04u.txt", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
        TEST_ASSERT_EQ(ino, 1000 + i);
    }

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── Test: max filename length (255 bytes) ─────────────────── */

static void test_max_filename_length(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_freespace_t *fs = make_fs(bio);

    bfs_dir_tree_t dt;
    bfs_dir_init(&dt, bio, bfs_freespace_allocator(fs), BFS_BLK_NULL, 1);

    char name[BFS_NAME_MAX];
    memset(name, 'A', BFS_NAME_MAX);

    TEST_ASSERT_EQ(bfs_dir_insert(&dt, ROOT_DIR_INO, name, BFS_NAME_MAX, 42, BFS_INODE_FILE), BFS_OK);

    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, name, BFS_NAME_MAX, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 42);

    /* Case-insensitive: lowercase should also find it */
    char lower[BFS_NAME_MAX];
    memset(lower, 'a', BFS_NAME_MAX);
    TEST_ASSERT_EQ(bfs_dir_lookup(&dt, ROOT_DIR_INO, lower, BFS_NAME_MAX, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, 42);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Directory B+tree")
    TEST_RUN(test_dir_basic);
    TEST_RUN(test_dir_forged_hash_rejects_leaf);
    TEST_RUN(test_dir_case_insensitive);
    TEST_RUN(test_dir_intl_chars);
    TEST_RUN(test_dir_remove);
    TEST_RUN(test_dir_scan);
    TEST_RUN(test_dir_scan_after);
    TEST_RUN(test_dir_large);
    TEST_RUN(test_max_filename_length);
TEST_SUITE_END()
