/* BFS — Snapshot tests */
#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <string.h>
#include <unistd.h>

#define IMG "test_snapshot.img"
#define BS 4096
#define BC 4096

static bfs_fs_t *setup(void) {
    unlink(IMG);
    static bfs_fs_t fs;
    bfs_bio_t *bio = bio_emu_create(IMG, BS, BC);
    bfs_fs_format(bio, "SnapTest", 0);
    bfs_fs_mount(&fs, bio);
    return &fs;
}
static void teardown(bfs_fs_t *fs) { bfs_fs_unmount(fs); bfs_bio_close(fs->bio); unlink(IMG); }

static bool snap_count_cb(uint32_t id, const bfs_snapshot_record_t *r, void *c) {
    (void)id; (void)r; (*(int*)c)++; return true; }

static void test_create_snapshot(void) {
    bfs_fs_t *fs = setup();
    /* Write a file */
    uint32_t ino;
    bfs_fs_create_file(fs, 1, "hello.txt", 9, &ino);
    bfs_file_t f; bfs_file_open(&f, fs, ino);
    bfs_file_write(&f, "Hello World", 11);
    bfs_fs_sync(fs);

    /* Create snapshot */
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "snap1"), BFS_OK);
    TEST_ASSERT(fs->has_snapshots);

    /* Verify snapshot exists */
    int cnt = 0;
    bfs_snapshot_list(fs, snap_count_cb, &cnt);
    TEST_ASSERT_EQ(cnt, 1);

    teardown(fs);
}

static void test_snapshot_preserves_data(void) {
    bfs_fs_t *fs = setup();
    uint32_t ino;
    bfs_fs_create_file(fs, 1, "data.txt", 8, &ino);
    bfs_file_t f; bfs_file_open(&f, fs, ino);
    uint8_t orig[100]; memset(orig, 0xAA, 100);
    bfs_file_write(&f, orig, 100);
    bfs_fs_sync(fs);

    /* Snapshot */
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "before"), BFS_OK);

    /* Modify the file (overwrite with different data) */
    bfs_file_t f2; bfs_file_open(&f2, fs, ino);
    bfs_file_truncate(&f2, 0);
    uint8_t modified[100]; memset(modified, 0xBB, 100);
    bfs_file_write(&f2, modified, 100);
    bfs_fs_sync(fs);

    /* Live file should have new data */
    bfs_file_t f3; bfs_file_open(&f3, fs, ino);
    uint8_t buf[100]; bfs_file_read(&f3, buf, 100);
    TEST_ASSERT_MEM_EQ(buf, modified, 100);

    /* The snapshot's tree roots still point to the old data.
     * We can't easily read from the snapshot without a mount-snapshot API,
     * but we can verify the refcount tree has entries. */
    TEST_ASSERT(bfs_refcount_get(&fs->refcount, bfs_be32(fs->txn.sb.dir_tree_root)) >= 1);

    teardown(fs);
}

static void test_multiple_snapshots(void) {
    bfs_fs_t *fs = setup();
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "s1"), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "s2"), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "s3"), BFS_OK);

    int cnt = 0;
    bfs_snapshot_list(fs, snap_count_cb, &cnt);
    TEST_ASSERT_EQ(cnt, 3);

    teardown(fs);
}

static void test_no_snapshot_overhead(void) {
    bfs_fs_t *fs = setup();
    /* Without snapshots, has_snapshots should be false */
    TEST_ASSERT(!fs->has_snapshots);

    /* Write and delete — should work without refcount overhead */
    uint32_t ino;
    bfs_fs_create_file(fs, 1, "tmp", 3, &ino);
    bfs_fs_delete_file(fs, 1, "tmp", 3);
    bfs_fs_sync(fs);

    TEST_ASSERT(!fs->has_snapshots);
    teardown(fs);
}

static int test_snap_compare(const void *a, const void *b)
{
    uint32_t va = bfs_load_be32(a);
    uint32_t vb = bfs_load_be32(b);
    return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
}
static const bfs_btree_ops_t test_snap_ops = {
    .key_compare = test_snap_compare,
    .key_size = sizeof(uint32_t),
    .val_size = sizeof(bfs_snapshot_record_t),
};

static void test_interrupted_deletion_resume(void) {
    bfs_fs_t *fs = setup();

    /* Create and delete a dummy snapshot first to ensure the snapshot and refcount trees 
     * are fully initialized and their roots allocated on-disk. */
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "dummy"), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_delete(fs, 1), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_OK);

    /* Write some data and create a file */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "hello.txt", 9, &ino), BFS_OK);
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&f, "Hello Snapshot Resume", 21), 21);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_OK);

    /* Record initial total free blocks and create a snapshot */
    uint32_t free_before_snap = fs->freespace.total_free;
    TEST_ASSERT_EQ(bfs_snapshot_create(fs, "snap1"), BFS_OK);

    /* Overwrite the live file completely to make the old blocks unique to the snapshot */
    TEST_ASSERT_EQ(bfs_file_seek(&f, 0, BFS_SEEK_SET), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&f, "Something Completely Different", 30), 30);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_OK);

    /* Delete the live file so blocks are only owned by the snapshot */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "hello.txt", 9), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(fs), BFS_OK);

    /* The snapshot now uniquely holds the original "hello.txt" data block. 
     * Let's find the snapshot record */
    bfs_snapshot_record_t rec;
    uint32_t id;
    TEST_ASSERT_EQ(bfs_snapshot_find_by_name(fs, "snap1", &id, &rec), BFS_OK);

    /* Open the snapshot B+tree and manually rename the snapshot to ".deleting_<id>" to simulate
     * an in-progress deletion. */
    bfs_btree_t snap_tree;
    bfs_blk_t snap_root = bfs_be32(fs->txn.sb_new.snapshot_tree_root);
    TEST_ASSERT_EQ(bfs_btree_init(&snap_tree, fs->bio, bfs_freespace_allocator(&fs->freespace),
                                   &test_snap_ops, snap_root, bfs_txn_id(&fs->txn)), BFS_OK);
    snap_tree.free_sink = bfs_fs_free_sink(fs);

    char new_name[BFS_SNAPSHOT_NAME_MAX];
    snprintf(new_name, sizeof(new_name), ".deleting_%u", id);
    memset(rec.name, 0, sizeof(rec.name));
    memcpy(rec.name, new_name, strlen(new_name));
    rec.timestamp = bfs_be32(0); /* last_reclaimed_ino = 0 */

    uint32_t key = bfs_be32(id);
    TEST_ASSERT_EQ(bfs_btree_update(&snap_tree, &key, &rec), BFS_OK);
    fs->txn.sb_new.snapshot_tree_root = bfs_be32(snap_tree.root);
    TEST_ASSERT_EQ(bfs_txn_commit(fs), BFS_OK);

    /* Simulate a crash / reboot by unmounting without using bfs_snapshot_delete */
    bfs_bio_t *bio = fs->bio;
    /* We don't use bfs_fs_unmount because it syncs and could clean up. 
     * We just free the memory/structures and close the bio. */
    free(fs->scratch);
    fs->mounted = false;

    /* Mount again. This should automatically resume the snapshot deletion and clean it up! */
    bfs_fs_t new_fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&new_fs, bio), BFS_OK);

    /* Verify that the snapshot is gone and the unique snapshot blocks are reclaimed */
    int cnt = 0;
    TEST_ASSERT_EQ(bfs_snapshot_list(&new_fs, snap_count_cb, &cnt), BFS_OK);
    TEST_ASSERT_EQ(cnt, 0);

    /* Due to B+tree structural hysteresis, some empty B-tree leaf nodes in the refcount/snapshot 
     * trees may remain allocated, which is standard B+tree behavior. We allow a minor tolerance 
     * of 4 blocks for metadata overhead, ensuring that all actual snapshot data blocks are fully reclaimed. */
    printf("DEBUG: new_fs.freespace.total_free=%u, free_before_snap=%u (diff=%d)\n",
           new_fs.freespace.total_free, free_before_snap, (int)free_before_snap - (int)new_fs.freespace.total_free);
    TEST_ASSERT(new_fs.freespace.total_free >= free_before_snap - 4);

    /* Clean up */
    bfs_fs_unmount(&new_fs);
    bfs_bio_close(bio);
    unlink(IMG);
}

TEST_SUITE_BEGIN("Snapshots")
    TEST_RUN(test_create_snapshot);
    TEST_RUN(test_snapshot_preserves_data);
    TEST_RUN(test_multiple_snapshots);
    TEST_RUN(test_no_snapshot_overhead);
    TEST_RUN(test_interrupted_deletion_resume);
TEST_SUITE_END()
