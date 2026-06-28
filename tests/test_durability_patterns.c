/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Durability and Transaction Pattern Regression Tests
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_IMG "test_durability.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192 /* 32MB */

static bfs_fs_t g_fs;
static bfs_bio_t *g_bio;

static void setup(void)
{
    g_bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(g_bio, "Durability", 0);
    bfs_fs_mount(&g_fs, g_bio);
}

static void teardown(void)
{
    bfs_fs_unmount(&g_fs);
    bio_emu_create(TEST_IMG, BLK_SIZE, 0); /* delete */
}

/* ── 1. Transactional Consistency (The "Stale Handle" Pattern) ── */

static void test_txn_stale_handle_consistency(void)
{
    setup();
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&g_fs, BFS_ROOT_INO, "file1", 4, &ino), BFS_OK);
    
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &g_fs, ino), BFS_OK);

    /* Perform multiple syncs while file is open. 
     * This tests if the handle's internal tree correctly tracks live_txn_id. */
    for (int i = 0; i < 5; i++) {
        char buf[64];
        sprintf(buf, "data %d", i);
        TEST_ASSERT(bfs_file_write(&f, buf, 64) == 64);
        bfs_inode_t inode; bfs_inode_read(&g_fs.inode_tree, ino, &inode); inode.extent_root = bfs_be32(f.extents.tree.root); bfs_inode_write(&g_fs.inode_tree, ino, &inode); TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);
    }

    /* If the handle didn't track txn_id_ptr, it would now likely fail or corrupt
     * on the next write due to COW logic using a stale txn_id. */
    TEST_ASSERT(bfs_file_write(&f, "final", 5) == 5);
    bfs_inode_t inode; bfs_inode_read(&g_fs.inode_tree, ino, &inode); inode.extent_root = bfs_be32(f.extents.tree.root); bfs_inode_write(&g_fs.inode_tree, ino, &inode); TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    teardown();
}

/* ── 2. Backup Superblock Integrity (The "50% Fill" Pattern) ── */

static void test_backup_sb_protection(void)
{
    setup();
    bfs_blk_t backup_blk = (bfs_blk_t)((uint64_t)BLK_COUNT * BLK_SIZE / 2 / BLK_SIZE);
    
    /* Fill disk to ~60% to ensure we pass the midpoint (backup SB) */
    /* Actually we need to fill enough to ensure backup SB isn't overwritten.
     * We'll write one large file that would normally overlap it if not reserved. */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&g_fs, BFS_ROOT_INO, "bigfile", 7, &ino), BFS_OK);
    
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &g_fs, ino), BFS_OK);
    
    /* Write enough blocks to pass the midpoint */
    uint8_t *zeros = calloc(1, BLK_SIZE);
    for (uint32_t i = 0; i < BLK_COUNT / 2 + 10; i++) {
        if (bfs_file_write(&f, zeros, BLK_SIZE) != BLK_SIZE) break;
    }
    free(zeros);
    bfs_inode_t inode; bfs_inode_read(&g_fs.inode_tree, ino, &inode); inode.extent_root = bfs_be32(f.extents.tree.root); bfs_inode_write(&g_fs.inode_tree, ino, &inode); TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    /* Read backup SB directly and verify it's still a valid SB and matches primary magic */
    uint8_t *sb_buf = malloc(BLK_SIZE);
    TEST_ASSERT_EQ(bfs_bio_read(g_bio, backup_blk, sb_buf), BFS_OK);
    bfs_superblock_t *sb = (bfs_superblock_t *)sb_buf;
    TEST_ASSERT_EQ(bfs_be32(sb->magic), BFS_SB_MAGIC);
    free(sb_buf);

    teardown();
}

/* ── 3. Fragmented Truncation (The "Batch/Sync" Pattern) ── */

static void test_fragmented_truncate_reclamation(void)
{
    setup();
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&g_fs, BFS_ROOT_INO, "frag", 4, &ino), BFS_OK);
    
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &g_fs, ino), BFS_OK);

    /* Force fragmentation by interleaving writes with other allocations
     * Actually, we can just use bfs_extent_append with 1 block many times. */
    for (uint32_t i = 0; i < 2000; i++) {
        bfs_blk_t db;
        TEST_ASSERT_EQ(bfs_extent_append(&f.extents, i, 1, &db), BFS_OK);
    }
    bfs_inode_t inode; bfs_inode_read(&g_fs.inode_tree, ino, &inode); inode.extent_root = bfs_be32(f.extents.tree.root); bfs_inode_write(&g_fs.inode_tree, ino, &inode); TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    uint32_t free_before = g_fs.freespace.total_free;
    
    /* Truncate to 0. This should trigger the ERR_AGAIN batching loop. */
    TEST_ASSERT_EQ(bfs_fs_delete_file(&g_fs, BFS_ROOT_INO, "frag", 4), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    /* Verify all 2000 blocks + extent tree metadata were reclaimed */
    TEST_ASSERT(g_fs.freespace.total_free >= free_before + 2000);

    teardown();
}

/* ── 4. Linear Directory Complexity (The "O(N)" Pattern) ── */

static uint32_t g_read_count = 0;
static bfs_err_t (*orig_read)(bfs_bio_t *bio, bfs_blk_t blk, void *buf);

static bfs_err_t tracking_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    g_read_count++;
    return orig_read(bio, blk, buf);
}

static bool dummy_cb(const char *n, uint8_t nl, uint32_t i, uint32_t t, void *ctx) { 
    (void)n; (void)nl; (void)i; (void)t; (void)ctx; return true; 
}

static void test_exall_linear_complexity(void)
{
    setup();
    /* Mock bio to track reads - copy ops to mutable struct */
    static bfs_bio_ops_t mock_ops;
    mock_ops = *g_bio->ops;
    orig_read = mock_ops.read_block;
    mock_ops.read_block = tracking_read;
    g_bio->ops = &mock_ops;

    /* Create 100 files */
    for (int i = 0; i < 100; i++) {
        char name[16]; sprintf(name, "f%03d", i);
        bfs_fs_create_file(&g_fs, BFS_ROOT_INO, name, strlen(name), NULL);
    }
    bfs_fs_sync(&g_fs);

    /* Measure reads for 100 entries */
    g_read_count = 0;
    /* In a real Amiga environment we'd use ACTION_EXAMINE_ALL, 
     * here we test the underlying bfs_dir_scan which it uses. */
    bfs_dir_scan(&g_fs.dir_tree, BFS_ROOT_INO, dummy_cb, NULL);
    uint32_t reads_100 = g_read_count;

    /* Create 900 more (total 1000) */
    for (int i = 100; i < 1000; i++) {
        char name[16]; sprintf(name, "f%03d", i);
        bfs_fs_create_file(&g_fs, BFS_ROOT_INO, name, strlen(name), NULL);
    }
    bfs_fs_sync(&g_fs);

    g_read_count = 0;
    bfs_dir_scan(&g_fs.dir_tree, BFS_ROOT_INO, dummy_cb, NULL);
    uint32_t reads_1000 = g_read_count;

    /* For O(N), reads_1000 should be roughly 10x reads_100.
     * If it were O(N^2) it would be 100x.
     * B+tree scan is actually O(N/B) where B is branching factor. */
    TEST_ASSERT(reads_1000 < reads_100 * 20); 

    teardown();
}

/* ── 5. Read-Modify-Write Safety (The "Garbage Write" Pattern) ── */

static bool g_inject_error = false;
static bfs_err_t error_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    if (g_inject_error) return BFS_ERR_IO;
    return orig_read(bio, blk, buf);
}

static void test_rmw_garbage_write_prevention(void)
{
    setup();
    static bfs_bio_ops_t mock_ops;
    mock_ops = *g_bio->ops;
    orig_read = mock_ops.read_block;
    mock_ops.read_block = error_read;
    g_bio->ops = &mock_ops;

    uint32_t ino;
    bfs_fs_create_file(&g_fs, BFS_ROOT_INO, "safe", 4, &ino);
    bfs_fs_sync(&g_fs);

    bfs_file_t f;
    bfs_file_open(&f, &g_fs, ino);

    /* Write 1 block normally */
    char data[BLK_SIZE]; memset(data, 'A', BLK_SIZE);
    bfs_file_write(&f, data, BLK_SIZE);
    bfs_fs_sync(&g_fs);

    /* Attempt partial write with injected read error */
    g_inject_error = true;
    f.offset = 10;
    /* This should fail because it can't read the block to modify it */
    TEST_ASSERT(bfs_file_write(&f, "B", 1) < 0);
    
    g_inject_error = false;
    /* Verify the block was NOT corrupted with garbage */
    char check[BLK_SIZE];
    f.offset = 0;
    bfs_file_read(&f, check, BLK_SIZE);
    for (int i = 0; i < BLK_SIZE; i++) TEST_ASSERT_EQ(check[i], 'A');

    teardown();
}

/* ── 6. Ordered Data Mode (The "Write-Before-Commit" Pattern) ── */

static void test_data_ordered_consistency(void)
{
    /* Format with ordered data flag */
    g_bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(g_bio, "Ordered", BFS_OPT_DATA_ORDERED);
    bfs_fs_mount(&g_fs, g_bio);

    uint32_t ino;
    bfs_fs_create_file(&g_fs, BFS_ROOT_INO, "ordered", 7, &ino);
    
    bfs_file_t f;
    bfs_file_open(&f, &g_fs, ino);

    /* Perform write and sync */
    char data[BLK_SIZE]; memset(data, 'O', BLK_SIZE);
    TEST_ASSERT(bfs_file_write(&f, data, BLK_SIZE) == (int32_t)BLK_SIZE);
    
    /* This sync will now trigger an extra bio_sync() for data before SB commit */
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    /* Verify content persists */
    char check[BLK_SIZE];
    f.offset = 0;
    bfs_file_read(&f, check, BLK_SIZE);
    for (int i = 0; i < BLK_SIZE; i++) TEST_ASSERT_EQ(check[i], 'O');

    teardown();
}

/* ── 7. Online Compaction (The "Zero-Downtime Re-packing" Pattern) ── */

static void test_online_compaction(void)
{
    setup();
    /* Create a fragmented directory tree with 500 entries */
    for (int i = 0; i < 500; i++) {
        char name[16]; sprintf(name, "file_%d", i);
        bfs_fs_create_file(&g_fs, BFS_ROOT_INO, name, strlen(name), NULL);
    }
    bfs_fs_sync(&g_fs);

    uint32_t free_before = g_fs.freespace.total_free;
    
    /* Compact the directory tree (build new, swap root, commit, free old) */
    TEST_ASSERT_EQ(bfs_fs_compact_tree(&g_fs, &g_fs.dir_tree.tree), BFS_OK);
    
    /* Sync to reclaim the old fragmented blocks */
    TEST_ASSERT_EQ(bfs_fs_sync(&g_fs), BFS_OK);

    /* Verify all files still exist and are accessible after compaction */
    for (int i = 0; i < 500; i++) {
        char name[16]; sprintf(name, "file_%d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&g_fs.dir_tree, BFS_ROOT_INO, name, strlen(name), &ino, &type), BFS_OK);
    }

    /* Free space should be stable (within metadata overhead bounds) */
    TEST_ASSERT(g_fs.freespace.total_free >= free_before - 64);

    teardown();
}

TEST_SUITE_BEGIN("Durability Patterns")
    TEST_RUN(test_txn_stale_handle_consistency);
    TEST_RUN(test_backup_sb_protection);
    TEST_RUN(test_fragmented_truncate_reclamation);
    TEST_RUN(test_exall_linear_complexity);
    TEST_RUN(test_rmw_garbage_write_prevention);
    TEST_RUN(test_data_ordered_consistency);
    TEST_RUN(test_online_compaction);
TEST_SUITE_END()
