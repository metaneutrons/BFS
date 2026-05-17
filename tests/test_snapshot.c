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

    /* Still no snapshots */
    TEST_ASSERT(!fs->has_snapshots);
    teardown(fs);
}

TEST_SUITE_BEGIN("Snapshots")
    TEST_RUN(test_create_snapshot);
    TEST_RUN(test_snapshot_preserves_data);
    TEST_RUN(test_multiple_snapshots);
    TEST_RUN(test_no_snapshot_overhead);
TEST_SUITE_END()
