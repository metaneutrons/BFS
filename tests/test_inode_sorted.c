/*
 * BFS — Inode reads in ascending order (bfs_inode_read_sorted) give the same
 * result as single reads, with and without the cache and its leaf hints.
 */

#include "test_harness.h"
#include "bfs_alloc.h"
#include "bfs_cache.h"
#include "bfs_inode.h"
#include "block_device_emu.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_IMG "test_inode_sorted.img"
#define BLOCK_COUNT 8192u
#define DATA_START 2u
#define MAX_INO 3000u
#define BATCH 200u

typedef struct {
    bfs_bio_t *device;
    bfs_cache_t cache;
    bool cached;
    bfs_freespace_t space;
    bfs_btree_t tree;
    bfs_btree_key_hint_cache_t hints;
} fixture_t;

static bfs_bio_t *fixture_bio(fixture_t *f)
{
    return f->cached ? &f->cache.bio : f->device;
}

static bool fixture_open(fixture_t *f, uint32_t block_size, bool cached, bool hints)
{
    unlink(TEST_IMG);
    memset(f, 0, sizeof(*f));
    f->device = bio_emu_create(TEST_IMG, block_size, BLOCK_COUNT);
    if (!f->device) return false;
    f->cached = cached;
    if (cached && bfs_cache_init(&f->cache, f->device, 64) != BFS_OK) return false;
    bfs_freespace_init(&f->space, fixture_bio(f), BFS_BLK_NULL, 1);
    if (bfs_freespace_add(&f->space, DATA_START, BLOCK_COUNT - DATA_START) != BFS_OK)
        return false;
    bfs_freespace_refill_reserve(&f->space);
    if (bfs_inode_init(&f->tree, fixture_bio(f), bfs_freespace_allocator(&f->space),
                       BFS_BLK_NULL, 1) != BFS_OK)
        return false;
    if (hints) {
        bfs_btree_key_hint_cache_reset(&f->hints);
        f->tree.key_hint_cache = &f->hints;
    }
    /* Every third inode number is missing, so lookups also miss inside leaves
     * and between them. */
    for (uint32_t ino = 1; ino <= MAX_INO; ino++) {
        if (ino % 3 == 0) continue;
        bfs_inode_t inode;
        memset(&inode, 0, sizeof(inode));
        inode.inode_nr = bfs_be32(ino);
        inode.type = bfs_be32(BFS_INODE_FILE);
        inode.link_count = bfs_be32(1);
        inode.size_lo = bfs_be32(ino * 7u);
        if (bfs_inode_write(&f->tree, ino, &inode) != BFS_OK) return false;
    }
    return f->tree.height > 1;
}

static void fixture_close(fixture_t *f)
{
    if (f->cached) bfs_cache_destroy(&f->cache);
    bfs_bio_close(f->device);
    unlink(TEST_IMG);
}

static int compare_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state >> 8;
}

/* Every element agrees with a single read: result code and inode bytes. */
static void check_batch(fixture_t *f, const uint32_t *inos, uint32_t count)
{
    static bfs_inode_t batch[BATCH];
    static bfs_err_t results[BATCH];
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&f->tree, inos, count, batch, results), BFS_OK);
    for (uint32_t i = 0; i < count; i++) {
        bfs_inode_t single;
        bfs_err_t expected = bfs_inode_read(&f->tree, inos[i], &single);
        TEST_ASSERT_EQ(results[i], expected);
        if (expected == BFS_OK && results[i] == BFS_OK)
            TEST_ASSERT_MEM_EQ(&batch[i], &single, sizeof(single));
    }
}

static void sorted_reads_at(uint32_t block_size, bool cached, bool hints)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, block_size, cached, hints));
    uint32_t inos[BATCH];
    uint32_t seed = 12345u + block_size + (cached ? 1u : 0u) + (hints ? 2u : 0u);
    for (unsigned round = 0; round < 40; round++) {
        uint32_t count = 1u + next_random(&seed) % BATCH;
        uint32_t spread = 1u + next_random(&seed) % (MAX_INO + 20u);
        uint32_t base = next_random(&seed) % (MAX_INO + 10u);
        for (uint32_t i = 0; i < count; i++)
            inos[i] = 1u + (base + next_random(&seed) % spread) % (MAX_INO + 10u);
        qsort(inos, count, sizeof(inos[0]), compare_u32);
        check_batch(&f, inos, count);
    }
    /* Every number once, including those past the last inode. */
    for (uint32_t start = 1; start <= MAX_INO + 10u; start += BATCH) {
        uint32_t count = 0;
        for (uint32_t ino = start; ino < start + BATCH && ino <= MAX_INO + 10u; ino++)
            inos[count++] = ino;
        check_batch(&f, inos, count);
    }
    fixture_close(&f);
}

static void test_sorted_reads_1k_raw(void) { sorted_reads_at(1024, false, false); }
static void test_sorted_reads_1k_cached_hints(void) { sorted_reads_at(1024, true, true); }
static void test_sorted_reads_4k_cached(void) { sorted_reads_at(4096, true, false); }
static void test_sorted_reads_4k_cached_hints(void) { sorted_reads_at(4096, true, true); }

static void test_sorted_reads_edges(void)
{
    fixture_t f;
    TEST_ASSERT(fixture_open(&f, 1024, true, true));
    bfs_inode_t out[4];
    bfs_err_t results[4];

    /* Descending numbers are refused before anything is read. */
    uint32_t descending[2] = { 10, 5 };
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&f.tree, descending, 2, out, results), BFS_ERR_INVAL);

    /* Invalid numbers, duplicates (hard links) and a missing number. */
    uint32_t mixed[4] = { 0, 7, 7, 9 };
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&f.tree, mixed, 4, out, results), BFS_OK);
    TEST_ASSERT_EQ(results[0], BFS_ERR_INVAL);
    TEST_ASSERT_EQ(results[1], BFS_OK);
    TEST_ASSERT_EQ(results[2], BFS_OK);
    TEST_ASSERT_EQ(results[3], BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_be32(out[2].inode_nr), 7);
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&f.tree, mixed, 0, NULL, NULL), BFS_OK);

    /* A stored inode whose number does not match its key is corrupt. */
    bfs_inode_t forged;
    memset(&forged, 0, sizeof(forged));
    forged.inode_nr = bfs_be32(99);
    forged.type = bfs_be32(BFS_INODE_FILE);
    forged.link_count = bfs_be32(1);
    uint32_t key = bfs_be32(3001);
    TEST_ASSERT_EQ(bfs_btree_insert(&f.tree, &key, &forged), BFS_OK);
    uint32_t corrupt[2] = { 3000, 3001 };
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&f.tree, corrupt, 2, out, results), BFS_OK);
    TEST_ASSERT_EQ(results[0], BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(results[1], BFS_ERR_CORRUPT);
    fixture_close(&f);

    /* An empty tree finds nothing. */
    unlink(TEST_IMG);
    bfs_bio_t *device = bio_emu_create(TEST_IMG, 1024, 64);
    TEST_ASSERT(device != NULL);
    bfs_freespace_t space;
    bfs_freespace_init(&space, device, BFS_BLK_NULL, 1);
    TEST_ASSERT_EQ(bfs_freespace_add(&space, DATA_START, 62), BFS_OK);
    bfs_btree_t empty;
    TEST_ASSERT_EQ(bfs_inode_init(&empty, device, bfs_freespace_allocator(&space),
                                  BFS_BLK_NULL, 1), BFS_OK);
    uint32_t some[2] = { 1, 2 };
    TEST_ASSERT_EQ(bfs_inode_read_sorted(&empty, some, 2, out, results), BFS_OK);
    TEST_ASSERT_EQ(results[0], BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(results[1], BFS_ERR_NOTFOUND);
    bfs_bio_close(device);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Sorted inode reads")
    TEST_RUN(test_sorted_reads_1k_raw);
    TEST_RUN(test_sorted_reads_1k_cached_hints);
    TEST_RUN(test_sorted_reads_4k_cached);
    TEST_RUN(test_sorted_reads_4k_cached_hints);
    TEST_RUN(test_sorted_reads_edges);
TEST_SUITE_END()
