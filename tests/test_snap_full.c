/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Tests for snapshots, refcounts, inode metadata, and edge cases
 */
#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_snapshot.h"
#include "bfs_refcount.h"
#include "block_device_emu.h"
#include <string.h>
#include <unistd.h>

#define IMG "test_snap_full.img"
#define BS 4096
#define BC 8192

static bfs_bio_t *bio;
static bfs_fs_t fs;

static void setup(void) {
    unlink(IMG);
    bio = bio_emu_create(IMG, BS, BC);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "SnapTest", 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
}
static void teardown(void) { bfs_fs_unmount(&fs); bfs_bio_close(bio); unlink(IMG); }

/* ── Refcount tests ────────────────────────────────────────── */

static void test_refcount_basic(void) {
    setup();
    /* No snapshots yet — refcount tree not initialized */
    TEST_ASSERT(!fs.has_snapshots);

    /* Create snapshot to initialize refcount tree */
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "s1"), BFS_OK);
    TEST_ASSERT(fs.has_snapshots);

    /* Refcount of a random block should be >= 1 */
    uint32_t rc = bfs_refcount_get(&fs.refcount, fs.dir_tree.tree.root);
    TEST_ASSERT(rc >= 2); /* shared between live + snapshot */

    teardown();
}

static void test_refcount_inc_dec(void) {
    setup();
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "s1"), BFS_OK);

    /* Manually inc a block */
    bfs_blk_t test_blk = 100;
    TEST_ASSERT_EQ(bfs_refcount_inc(&fs.refcount, test_blk), BFS_OK);
    TEST_ASSERT_EQ(bfs_refcount_get(&fs.refcount, test_blk), (uint32_t)2);

    TEST_ASSERT_EQ(bfs_refcount_inc(&fs.refcount, test_blk), BFS_OK);
    TEST_ASSERT_EQ(bfs_refcount_get(&fs.refcount, test_blk), (uint32_t)3);

    /* Dec back */
    bool freed = false;
    TEST_ASSERT_EQ(bfs_refcount_dec(&fs.refcount, test_blk, &freed), BFS_OK);
    TEST_ASSERT(!freed);
    TEST_ASSERT_EQ(bfs_refcount_get(&fs.refcount, test_blk), (uint32_t)2);

    TEST_ASSERT_EQ(bfs_refcount_dec(&fs.refcount, test_blk, &freed), BFS_OK);
    TEST_ASSERT(!freed); /* back to implicit 1, removed from tree */

    TEST_ASSERT_EQ(bfs_refcount_dec(&fs.refcount, test_blk, &freed), BFS_OK);
    TEST_ASSERT(freed); /* now 0 = free */

    teardown();
}

/* ── Snapshot edge cases ───────────────────────────────────── */

static void test_snapshot_empty_fs(void) {
    setup();
    /* Snapshot of empty filesystem (only root dir) */
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "empty"), BFS_OK);
    teardown();
}

static void test_snapshot_after_writes(void) {
    setup();
    /* Write files, snapshot, write more, verify live has new data */
    uint32_t ino;
    bfs_fs_create_file(&fs, 1, "a.txt", 5, &ino);
    bfs_file_t f; bfs_file_open(&f, &fs, ino);
    uint8_t data[100]; memset(data, 0xAA, 100);
    bfs_file_write(&f, data, 100);
    bfs_fs_sync(&fs);

    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "v1"), BFS_OK);

    /* Modify file */
    bfs_file_t f2; bfs_file_open(&f2, &fs, ino);
    bfs_file_truncate(&f2, 0);
    memset(data, 0xBB, 100);
    bfs_file_write(&f2, data, 100);
    bfs_fs_sync(&fs);

    /* Verify live has 0xBB */
    bfs_file_t f3; bfs_file_open(&f3, &fs, ino);
    uint8_t buf[100]; bfs_file_read(&f3, buf, 100);
    TEST_ASSERT_EQ(buf[0], 0xBB);
    TEST_ASSERT_EQ(buf[99], 0xBB);

    teardown();
}

static void test_snapshot_cow_preserves_shared(void) {
    setup();
    /* After snapshot, COW should not free shared blocks */
    (void)fs.freespace.total_free;

    uint32_t ino;
    bfs_fs_create_file(&fs, 1, "cow.txt", 7, &ino);
    bfs_file_t f; bfs_file_open(&f, &fs, ino);
    uint8_t data[4096]; memset(data, 0xCC, 4096);
    bfs_file_write(&f, data, 4096);
    bfs_fs_sync(&fs);

    uint32_t after_write = fs.freespace.total_free;
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "cow_snap"), BFS_OK);

    /* Overwrite — COW should allocate new block, not free old (shared) */
    bfs_file_t f2; bfs_file_open(&f2, &fs, ino);
    bfs_file_truncate(&f2, 0);
    memset(data, 0xDD, 4096);
    bfs_file_write(&f2, data, 4096);
    bfs_fs_sync(&fs);

    /* Free space should decrease (new block allocated, old not freed) */
    TEST_ASSERT(fs.freespace.total_free < after_write);

    teardown();
}

static void test_multiple_snapshots_refcount(void) {
    setup();
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "s1"), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "s2"), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "s3"), BFS_OK);

    /* Dir tree root should have refcount >= 4 (live + 3 snapshots) */
    uint32_t rc = bfs_refcount_get(&fs.refcount, fs.dir_tree.tree.root);
    TEST_ASSERT(rc >= 4);

    teardown();
}

static bool list_cb(uint32_t id, const bfs_snapshot_record_t *r, void *ctx) {
    (void)r; int *ids = (int *)ctx; ids[id-1] = 1; return true; }

static void test_snapshot_list_order(void) {
    setup();
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "first"), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "second"), BFS_OK);

    int ids[4] = {0};
    bfs_snapshot_list(&fs, list_cb, ids);
    TEST_ASSERT(ids[0]); /* id 1 exists */
    TEST_ASSERT(ids[1]); /* id 2 exists */

    teardown();
}

/* ── Inode metadata tests ──────────────────────────────────── */

static void test_inode_timestamps_on_create(void) {
    setup();
    /* Create file — inode should have non-zero timestamps
     * (on host, DateStamp isn't available, so timestamps stay 0.
     *  This test verifies the field exists and is writable.) */
    uint32_t ino;
    bfs_fs_create_file(&fs, 1, "ts.txt", 6, &ino);
    bfs_fs_sync(&fs);

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    /* On host, timestamps are 0 (no DateStamp). Verify fields exist. */
    /* Write a timestamp manually and read back */
    inode.modify_days = bfs_be16(17290);
    inode.modify_mins = bfs_be16(720);
    inode.modify_ticks = bfs_be16(0);
    bfs_inode_write(&fs.inode_tree, ino, &inode);
    bfs_fs_sync(&fs);

    bfs_inode_t inode2;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode2), BFS_OK);
    TEST_ASSERT_EQ(bfs_be16(inode2.modify_days), (uint16_t)17290);
    TEST_ASSERT_EQ(bfs_be16(inode2.modify_mins), (uint16_t)720);

    teardown();
}

static void test_inode_uid_gid(void) {
    setup();
    uint32_t ino;
    bfs_fs_create_file(&fs, 1, "owned.txt", 9, &ino);

    bfs_inode_t inode;
    bfs_inode_read(&fs.inode_tree, ino, &inode);
    inode.uid = bfs_be16(1000);
    inode.gid = bfs_be16(100);
    bfs_inode_write(&fs.inode_tree, ino, &inode);
    bfs_fs_sync(&fs);

    bfs_inode_t inode2;
    bfs_inode_read(&fs.inode_tree, ino, &inode2);
    TEST_ASSERT_EQ(bfs_be16(inode2.uid), (uint16_t)1000);
    TEST_ASSERT_EQ(bfs_be16(inode2.gid), (uint16_t)100);

    teardown();
}

static void test_inode_protection_bits(void) {
    setup();
    uint32_t ino;
    bfs_fs_create_file(&fs, 1, "prot.txt", 8, &ino);

    bfs_inode_t inode;
    bfs_inode_read(&fs.inode_tree, ino, &inode);
    inode.protection = bfs_be32(0xF); /* RWED denied */
    bfs_inode_write(&fs.inode_tree, ino, &inode);
    bfs_fs_sync(&fs);

    bfs_inode_t inode2;
    bfs_inode_read(&fs.inode_tree, ino, &inode2);
    TEST_ASSERT_EQ(bfs_be32(inode2.protection), (uint32_t)0xF);

    teardown();
}

/* ── Snapshot + write stress ───────────────────────────────── */

static void test_snapshot_stress(void) {
    setup();
    /* Create files, snapshot, modify, snapshot, modify — verify no corruption */
    uint32_t ino;
    bfs_fs_create_file(&fs, 1, "stress.dat", 10, &ino);
    bfs_file_t f; bfs_file_open(&f, &fs, ino);
    uint8_t data[1024]; memset(data, 0x11, 1024);
    bfs_file_write(&f, data, 1024);
    bfs_fs_sync(&fs);

    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "snap_a"), BFS_OK);

    /* Overwrite */
    bfs_file_t f2; bfs_file_open(&f2, &fs, ino);
    bfs_file_truncate(&f2, 0);
    memset(data, 0x22, 1024);
    bfs_file_write(&f2, data, 1024);
    bfs_fs_sync(&fs);

    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "snap_b"), BFS_OK);

    /* Overwrite again */
    bfs_file_t f3; bfs_file_open(&f3, &fs, ino);
    bfs_file_truncate(&f3, 0);
    memset(data, 0x33, 1024);
    bfs_file_write(&f3, data, 1024);
    bfs_fs_sync(&fs);

    /* Verify live has 0x33 */
    bfs_file_t f4; bfs_file_open(&f4, &fs, ino);
    uint8_t buf[1024]; bfs_file_read(&f4, buf, 1024);
    TEST_ASSERT_EQ(buf[0], 0x33);
    TEST_ASSERT_EQ(buf[1023], 0x33);

    /* Should have 2 snapshots */
    TEST_ASSERT_EQ(bfs_snapshot_next_id(&fs), (uint32_t)3);

    teardown();
}

TEST_SUITE_BEGIN("Snapshots & Metadata")
    TEST_RUN(test_refcount_basic);
    TEST_RUN(test_refcount_inc_dec);
    TEST_RUN(test_snapshot_empty_fs);
    TEST_RUN(test_snapshot_after_writes);
    TEST_RUN(test_snapshot_cow_preserves_shared);
    TEST_RUN(test_multiple_snapshots_refcount);
    TEST_RUN(test_snapshot_list_order);
    TEST_RUN(test_inode_timestamps_on_create);
    TEST_RUN(test_inode_uid_gid);
    TEST_RUN(test_inode_protection_bits);
    TEST_RUN(test_snapshot_stress);
TEST_SUITE_END()
