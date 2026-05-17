/*
 * BFS — Crash injection tests
 *
 * Simulates power loss at every possible write point during an operation,
 * then verifies the filesystem is still mountable and consistent.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG   "test_crash_inject.img"
#define BLK_SIZE   4096
#define BLK_COUNT  256

/* ── Crashable block device ────────────────────────────────── */

typedef struct {
    bfs_bio_t base;
    bfs_bio_t *inner;
    uint32_t writes_before_crash;
    uint32_t write_count;
    bool crashed;
} crashable_bio_t;

static bfs_err_t crash_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf) {
    return bfs_bio_read(((crashable_bio_t *)bio)->inner, blk, buf);
}
static bfs_err_t crash_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf) {
    crashable_bio_t *cb = (crashable_bio_t *)bio;
    if (cb->crashed) return BFS_OK; /* silently lost */
    cb->write_count++;
    if (cb->write_count > cb->writes_before_crash) {
        cb->crashed = true;
        return BFS_OK; /* this write lost */
    }
    return bfs_bio_write(cb->inner, blk, buf);
}
static bfs_err_t crash_sync(bfs_bio_t *bio) {
    crashable_bio_t *cb = (crashable_bio_t *)bio;
    if (cb->crashed) return BFS_OK;
    return bfs_bio_sync(cb->inner);
}
static void crash_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t crash_ops = {
    .read_block = crash_read, .write_block = crash_write,
    .sync = crash_sync, .close = crash_close,
};

static void crashable_init(crashable_bio_t *cb, bfs_bio_t *inner, uint32_t writes) {
    cb->base.ops = &crash_ops;
    cb->base.block_size = inner->block_size;
    cb->base.block_count = inner->block_count;
    cb->inner = inner;
    cb->writes_before_crash = writes;
    cb->write_count = 0;
    cb->crashed = false;
}

/* ── Helpers ───────────────────────────────────────────────── */

/* Create a formatted+synced baseline image */
static void make_baseline(void) {
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Crash", 0);
    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    bfs_fs_sync(&fs);
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
}

/* Create baseline with a file already committed */
static void make_baseline_with_file(void) {
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Crash", 0);
    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "existing", 8, &ino);
    bfs_file_t f;
    bfs_file_open(&f, &fs, ino);
    uint8_t data[BLK_SIZE];
    memset(data, 0xAA, BLK_SIZE);
    bfs_file_write(&f, data, BLK_SIZE);
    bfs_fs_sync(&fs);
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
}

/* Count writes an operation takes (run with no crash limit) */
static uint32_t count_op_writes(void (*op)(bfs_fs_t *fs)) {
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    crashable_bio_t cb;
    crashable_init(&cb, bio, UINT32_MAX);
    bfs_fs_t fs;
    bfs_fs_mount(&fs, &cb.base);
    /* Reset counter AFTER mount (mount does its own writes) */
    cb.write_count = 0;
    op(&fs);
    uint32_t writes = cb.write_count;
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    return writes;
}

/* Verify filesystem is consistent after crash */
static bool verify_after_crash(void) {
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    if (!bio) return false;
    bfs_fs_t fs;
    bfs_err_t err = bfs_fs_mount(&fs, bio);
    if (err == BFS_ERR_CORRUPT) { bfs_bio_close(bio); return false; }
    if (err == BFS_OK) bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    return true;
}

/* ── Tests ─────────────────────────────────────────────────── */

static void op_create_file(bfs_fs_t *fs) {
    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "newfile", 7, &ino);
    bfs_fs_sync(fs);
}

static void test_crash_during_create(void)
{
    make_baseline();
    uint32_t n = count_op_writes(op_create_file);
    if (n > 15) n = 15; /* cap for speed */

    for (uint32_t cp = 1; cp <= n; cp++) {
        make_baseline(); /* fresh start each time */
        bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
        crashable_bio_t cb;
        crashable_init(&cb, bio, UINT32_MAX);
        bfs_fs_t fs;
        bfs_fs_mount(&fs, &cb.base);
        cb.write_count = 0;
        cb.writes_before_crash = cp;
        uint32_t ino;
        bfs_fs_create_file(&fs, BFS_ROOT_INO, "newfile", 7, &ino);
        bfs_fs_sync(&fs);
        bfs_bio_close(bio);
        TEST_ASSERT(verify_after_crash());
    }
    unlink(TEST_IMG);
}

static void op_delete_file(bfs_fs_t *fs) {
    bfs_fs_delete_file(fs, BFS_ROOT_INO, "existing", 8);
    bfs_fs_sync(fs);
}

static void test_crash_during_delete(void)
{
    make_baseline_with_file();
    uint32_t n = count_op_writes(op_delete_file);
    if (n > 15) n = 15;

    for (uint32_t cp = 1; cp <= n; cp++) {
        make_baseline_with_file();
        bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
        crashable_bio_t cb;
        crashable_init(&cb, bio, UINT32_MAX);
        bfs_fs_t fs;
        bfs_fs_mount(&fs, &cb.base);
        cb.write_count = 0;
        cb.writes_before_crash = cp;
        bfs_fs_delete_file(&fs, BFS_ROOT_INO, "existing", 8);
        bfs_fs_sync(&fs);
        bfs_bio_close(bio);
        TEST_ASSERT(verify_after_crash());
    }
    unlink(TEST_IMG);
}

static void op_write_file(bfs_fs_t *fs) {
    bfs_file_t f;
    bfs_file_open(&f, fs, 2); /* inode 2 = first created file */
    uint8_t data[BLK_SIZE];
    memset(data, 0xBB, BLK_SIZE);
    bfs_file_write(&f, data, BLK_SIZE);
    bfs_fs_sync(fs);
}

static void test_crash_during_write(void)
{
    make_baseline_with_file();
    uint32_t n = count_op_writes(op_write_file);
    if (n > 15) n = 15;

    for (uint32_t cp = 1; cp <= n; cp++) {
        make_baseline_with_file();
        bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
        crashable_bio_t cb;
        crashable_init(&cb, bio, UINT32_MAX);
        bfs_fs_t fs;
        bfs_fs_mount(&fs, &cb.base);
        cb.write_count = 0;
        cb.writes_before_crash = cp;
        bfs_file_t f;
        bfs_file_open(&f, &fs, 2);
        uint8_t data[BLK_SIZE];
        memset(data, 0xBB, BLK_SIZE);
        bfs_file_write(&f, data, BLK_SIZE);
        bfs_fs_sync(&fs);
        bfs_bio_close(bio);
        TEST_ASSERT(verify_after_crash());
    }
    unlink(TEST_IMG);
}

static void test_crash_during_sync(void)
{
    make_baseline();
    /* Create file without syncing, then sync with crash */
    bfs_bio_t *bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    crashable_bio_t cb;
    crashable_init(&cb, bio, UINT32_MAX);
    bfs_fs_t fs;
    bfs_fs_mount(&fs, &cb.base);
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "unsaved", 7, &ino);
    cb.write_count = 0;
    uint32_t n = 0;
    /* Count sync writes */
    bfs_fs_sync(&fs);
    n = cb.write_count;
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    if (n > 10) n = 10;

    for (uint32_t cp = 1; cp <= n; cp++) {
        make_baseline();
        bio = bio_emu_open(TEST_IMG, BLK_SIZE);
        crashable_init(&cb, bio, UINT32_MAX);
        bfs_fs_mount(&fs, &cb.base);
        bfs_fs_create_file(&fs, BFS_ROOT_INO, "unsaved", 7, &ino);
        cb.write_count = 0;
        cb.writes_before_crash = cp;
        bfs_fs_sync(&fs);
        bfs_bio_close(bio);
        TEST_ASSERT(verify_after_crash());
    }
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Crash Injection")
    TEST_RUN(test_crash_during_create);
    TEST_RUN(test_crash_during_delete);
    TEST_RUN(test_crash_during_write);
    TEST_RUN(test_crash_during_sync);
TEST_SUITE_END()
