/*
 * BFS — Hardware failure simulation tests
 *
 * Tests that simulate real-world hardware failures:
 * torn writes, read errors, allocation exhaustion, double-free,
 * concurrent modification, tree depth limits, superblock corruption,
 * and format on dirty partition.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_dir.h"
#include "bfs_snapshot.h"
#include "bfs_internal.h"
#include "bfs_crc32.h"
#include "bfs_fsck.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_hwfail.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096

/* ── Scan callback (file scope — C99 doesn't allow nested functions) ── */

typedef struct { uint32_t count; } scan_count_t;
static bool scan_count_cb(const char *n, uint8_t l, uint32_t i, uint32_t t, void *c) {
    (void)n;(void)l;(void)i;(void)t;
    ((scan_count_t*)c)->count++; return true;
}

/* ── 1. Torn Write: half-written block ─────────────────────── */

static void test_torn_write_superblock(void)
{
    /* Format a filesystem, then simulate a torn superblock write:
     * write only the first half of the block (rest is garbage). */
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "TornTest", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "safe.txt", 8, &ino);
    bfs_fs_sync(&fs);
    bfs_fs_unmount(&fs);

    /* Now corrupt one superblock copy with a torn write:
     * first 64 bytes valid, rest is 0xFF (simulating partial sector write) */
    uint8_t buf[BLK_SIZE];
    bfs_bio_read(bio, 0, buf); /* read primary superblock */
    memset(buf + 64, 0xFF, BLK_SIZE - 64); /* corrupt second half */
    bfs_bio_write(bio, 0, buf); /* write torn block */
    bfs_bio_sync(bio);

    /* Mount should succeed using the backup superblock */
    bfs_err_t err = bfs_fs_mount(&fs, bio);
    TEST_ASSERT_EQ(err, BFS_OK);

    /* File should still be there */
    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "safe.txt", 8, &found_ino, &type), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_torn_write_btree_node(void)
{
    /* Write a valid B+tree, then corrupt a leaf node (simulating torn write).
     * The CRC should detect it. */
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "TornNode", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    for (int i = 0; i < 5; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "file%d", i);
        uint32_t ino;
        bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
    }
    bfs_fs_sync(&fs);

    /* Corrupt the dir tree root (simulate torn write: valid header, garbage data) */
    bfs_blk_t root = fs.dir_tree.tree.root;
    uint8_t buf[BLK_SIZE];
    bfs_bio_read(bio, root, buf);
    /* Keep first 28 bytes (header) but corrupt the key area */
    memset(buf + 28, 0xDE, 100);
    /* DON'T fix CRC — this simulates a torn write where CRC is stale */
    bfs_bio_write(bio, root, buf);
    bfs_bio_sync(bio);

    /* Lookup should detect corruption via CRC mismatch */
    uint32_t found_ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "file0", 5, &found_ino, &type);
    TEST_ASSERT_EQ(err, BFS_ERR_CORRUPT);

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 2. Read Error after successful write ──────────────────── */

typedef struct {
    bfs_bio_t base;
    bfs_bio_t *inner;
    bfs_blk_t fail_block; /* this block returns IO error on read */
    bfs_blk_t fail_write_block;
    bool fail_all_reads;
    bool fail_all_writes;
    bool fail_sync_now;
    bool persist_write_failure;
    uint32_t failed_writes;
    uint32_t writes_until_failure;
    const uint8_t *watched_data;
    uint32_t watched_data_blocks;
    uint32_t data_writes_until_failure;
    uint32_t watched_data_write_attempts;
} failing_bio_t;

static bfs_err_t fail_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf) {
    failing_bio_t *fb = (failing_bio_t *)bio;
    if (fb->fail_all_reads || blk == fb->fail_block) return BFS_ERR_IO;
    return bfs_bio_read(fb->inner, blk, buf);
}
static bfs_err_t fail_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf) {
    failing_bio_t *fb = (failing_bio_t *)bio;
    if (fb->watched_data && fb->watched_data_blocks > 0) {
        bool watched = false;
        for (uint32_t i = 0; i < fb->watched_data_blocks; i++) {
            const uint8_t *block = fb->watched_data + (size_t)i * bio->block_size;
            if (memcmp(buf, block, bio->block_size) == 0) {
                watched = true;
                break;
            }
        }
        if (watched) {
            fb->watched_data_write_attempts++;
            if (fb->data_writes_until_failure &&
                --fb->data_writes_until_failure == 0) {
                fb->failed_writes++;
                return BFS_ERR_IO;
            }
        }
    }
    if (fb->writes_until_failure && --fb->writes_until_failure == 0) {
        fb->fail_all_writes = fb->persist_write_failure;
        fb->failed_writes++;
        return BFS_ERR_IO;
    }
    if (fb->fail_all_writes || blk == fb->fail_write_block) {
        fb->failed_writes++;
        return BFS_ERR_IO;
    }
    return bfs_bio_write(fb->inner, blk, buf);
}
static bfs_err_t fail_sync(bfs_bio_t *bio) {
    failing_bio_t *fb = (failing_bio_t *)bio;
    if (fb->fail_sync_now) return BFS_ERR_IO;
    return bfs_bio_sync(fb->inner);
}
static void fail_close(bfs_bio_t *bio) { (void)bio; }
static const bfs_bio_ops_t fail_ops = {
    .read_block = fail_read, .write_block = fail_write,
    .sync = fail_sync, .close = fail_close,
};

static void test_read_error_during_lookup(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "ReadFail", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "test.txt", 8, &ino);
    bfs_fs_sync(&fs);

    /* Wrap bio to fail reads on the dir tree root */
    failing_bio_t fb = {0};
    fb.base.ops = &fail_ops;
    fb.base.block_size = bio->block_size;
    fb.base.block_count = bio->block_count;
    fb.inner = bio;
    fb.fail_block = fs.dir_tree.tree.root;

    /* Reinit dir tree with failing bio */
    bfs_dir_tree_t bad_tree;
    bfs_dir_init(&bad_tree, &fb.base, bfs_freespace_allocator(&fs.freespace),
                  fs.dir_tree.tree.root, 1);

    /* Lookup should return IO error, not crash */
    uint32_t found_ino, type;
    bfs_err_t err = bfs_dir_lookup(&bad_tree, BFS_ROOT_INO, "test.txt", 8, &found_ino, &type);
    TEST_ASSERT(err == BFS_ERR_IO || err == BFS_ERR_CORRUPT);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_read_error_during_scan(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "ScanFail", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);
    for (int i = 0; i < 20; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "f%02d", i);
        uint32_t ino;
        bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
    }
    bfs_fs_sync(&fs);

    /* Make a non-root block fail (to test scan error propagation) */
    failing_bio_t fb = {0};
    fb.base.ops = &fail_ops;
    fb.base.block_size = bio->block_size;
    fb.base.block_count = bio->block_count;
    fb.inner = bio;
    fb.fail_block = fs.dir_tree.tree.root;

    /* Scan should either complete or return an error, never crash */
    bfs_dir_tree_t test_tree;
    TEST_ASSERT_EQ(bfs_dir_init(&test_tree, bio,
                               bfs_freespace_allocator(&fs.freespace),
                               fs.dir_tree.tree.root, 1), BFS_OK);
    test_tree.tree.bio = &fb.base;

    typedef struct { uint32_t count; } scan_count_t;
    scan_count_t ctx = {0};
    /* scan — just count entries, don't crash */
    TEST_ASSERT_EQ(bfs_dir_scan(&test_tree, BFS_ROOT_INO, scan_count_cb, &ctx),
                   BFS_ERR_IO);
    TEST_ASSERT_EQ(ctx.count, 0);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 3. Allocation exhaustion during B+tree split ──────────── */

static void test_alloc_failure_during_split(void)
{
    /* Use a tiny disk where the free space runs out during a split */
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, 512); /* 2MB */
    bfs_fs_format(bio, "TinyDisk", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Insert files until we get NOSPC */
    int created = 0;
    for (int i = 0; i < 100; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "f%03d", i);
        uint32_t ino;
        bfs_err_t err = bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
        if (err == BFS_ERR_NOSPC) break;
        if (err != BFS_OK) break;
        created++;
    }
    TEST_ASSERT(created > 0);

    /* Verify all created files are still findable (no corruption from failed split) */
    for (int i = 0; i < created; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "f%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    /* Filesystem should still be syncable */
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 4. Double Free ────────────────────────────────────────── */

static void test_double_free(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "DblFree", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);


    /* Allocate a block */
    bfs_blk_t blk = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(blk != BFS_BLK_NULL);

    /* Free it once — should succeed */
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, blk, 1), BFS_OK);

    /* Free it again — the allocator must reject the overlap. */
    bfs_err_t err = bfs_freespace_free(&fs.freespace, blk, 1);
    TEST_ASSERT_EQ(err, BFS_ERR_EXISTS);

    /* The filesystem should still be usable */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "after_dbl", 9, &ino), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 5. Scan stability during modification ─────────────────── */

static void test_scan_after_insert_before_cursor(void)
{
    /* Create files, scan to get all names, insert a new file that sorts
     * BEFORE existing ones, scan again — verify new file appears */
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "ScanMod", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Create files with names that hash to high values */
    for (int i = 0; i < 10; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "zzz_%02d", i);
        uint32_t ino;
        bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
    }

    /* Insert a file that hashes to a LOW value (sorts before existing) */
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "aaa_first", 9, &ino);

    /* Scan should find all 11 entries */
    scan_count_t ctx = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, scan_count_cb, &ctx);
    TEST_ASSERT_EQ(ctx.count, 11);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 6. Maximum tree depth ─────────────────────────────────── */

static void test_max_tree_depth(void)
{
    /* With 4K blocks and 4-byte keys, leaf holds 508 entries.
     * Internal holds 509 keys. Tree depth 2 holds 508*510 = ~259K entries.
     * We can't realistically hit depth 32, but we can verify the tree
     * handles depth 3+ correctly with many entries. */
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Deep", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Create enough files to force a 3-level dir tree.
     * With 264-byte keys: leaf=14, internal=15. Level 2 = 14*16 = 224 entries.
     * Level 3 needs > 224 entries. */
    for (int i = 0; i < 250; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "deep_file_%04d", i);
        uint32_t ino;
        bfs_err_t err = bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
        if (err != BFS_OK) break; /* disk full is OK */
    }

    /* Verify scan returns all entries */
    scan_count_t ctx = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, scan_count_cb, &ctx);
    TEST_ASSERT(ctx.count >= 200); /* at least 200 should fit */

    /* Verify random lookups */
    for (int i = 0; i < 250; i += 17) {
        char name[32];
        int len = snprintf(name, sizeof(name), "deep_file_%04d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 7. Superblock torn write — both copies ────────────────── */

static void test_both_superblocks_torn(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "BothTorn", 0);
    bfs_bio_close(bio);

    /* Corrupt BOTH superblock copies (A at byte 0, B at midpoint) */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    uint8_t garbage[BLK_SIZE];
    memset(garbage, 0xDE, BLK_SIZE);
    bfs_bio_write(bio, 0, garbage);
    bfs_blk_t backup_blk = BLK_COUNT / 2;
    bfs_bio_write(bio, backup_blk, garbage);
    bfs_bio_sync(bio);

    /* Mount should fail with CORRUPT */
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_ERR_CORRUPT);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ── 8. Format on non-zero partition ───────────────────────── */

static void test_format_dirty_partition(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);

    /* Fill entire disk with garbage (simulating old data) */
    uint8_t garbage[BLK_SIZE];
    memset(garbage, 0xAB, BLK_SIZE);
    for (uint32_t i = 0; i < 20; i++) /* just first 20 blocks */
        bfs_bio_write(bio, i, garbage);
    bfs_bio_sync(bio);

    /* Format should succeed on dirty partition */
    TEST_ASSERT_EQ(bfs_fs_format(bio, "CleanSlate", 0), BFS_OK);

    /* Mount and verify it's a clean filesystem */
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Root dir should exist and be empty (except '..' maybe) */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "new.txt", 7, &ino), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void init_failing_bio(failing_bio_t *fb, bfs_bio_t *inner)
{
    memset(fb, 0, sizeof(*fb));
    fb->base.ops = &fail_ops;
    fb->base.block_size = inner->block_size;
    fb->base.block_count = inner->block_count;
    fb->inner = inner;
    fb->fail_block = UINT32_MAX;
    fb->fail_write_block = UINT32_MAX;
}

static void test_extent_read_error_is_not_a_sparse_hole(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "ExtentIO", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "data", 4, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    /* A leading hole keeps the mapping in an extent tree. */
    TEST_ASSERT_EQ(bfs_file_seek(&file, BLK_SIZE, BFS_SEEK_SET), BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&file, "payload", 7), 7);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(file.extents.tree.root != BFS_BLK_NULL);

    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    fb.fail_block = file.extents.tree.root;
    file.extents.tree.bio = &fb.base;
    file.offset = 0;
    char data[8] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&file, data, 7), BFS_ERR_IO);
    file.extents.tree.bio = bio;

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_delete_inode_read_error_preserves_entry(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "DeleteIO", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "keep", 4, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    fb.fail_block = fs.inode_tree.root;
    fs.inode_tree.bio = &fb.base;
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "keep", 4), BFS_ERR_IO);
    fs.inode_tree.bio = bio;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "keep", 4,
                                  NULL, NULL), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_delete_extent_read_error_preserves_entry(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "DeleteExtentIO", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "keepdata", 8,
                                      &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    /* A leading hole keeps the mapping in an extent tree. */
    TEST_ASSERT_EQ(bfs_file_seek(&file, BLK_SIZE, BFS_SEEK_SET), BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&file, "payload", 7), 7);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(file.extents.tree.root != BFS_BLK_NULL);

    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    fb.fail_block = file.extents.tree.root;
    fs.bio = &fb.base;
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "keepdata", 8),
                   BFS_ERR_IO);
    fs.bio = bio;

    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "keepdata", 8,
                                  NULL, NULL), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT(bfs_be32(inode.extent_root) != BFS_BLK_NULL);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_snapshot_refcount_read_error_blocks_overwrite(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "SnapshotIO", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "shared", 6, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&file, "original", 8), 8);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "point"), BFS_OK);

    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    TEST_ASSERT(fs.refcount.tree.root != BFS_BLK_NULL);
    fb.fail_block = fs.refcount.tree.root;
    fs.refcount.tree.bio = &fb.base;
    file.offset = 0;
    TEST_ASSERT_EQ(bfs_file_write(&file, "replaced", 8), BFS_ERR_IO);
    /* The write calls through this BIO; restore it before the readback. */
    // cppcheck-suppress redundantAssignment
    fs.refcount.tree.bio = bio;

    char data[9] = {0};
    file.offset = 0;
    TEST_ASSERT_EQ(bfs_file_read(&file, data, 8), 8);
    TEST_ASSERT_MEM_EQ(data, "original", 8);
    TEST_ASSERT_EQ(bfs_snapshot_delete(&fs, 1), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_ordered_sync_error_propagates(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "OrderedIO", BFS_OPT_DATA_ORDERED), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    fb.fail_sync_now = true;
    fs.bio = &fb.base;
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
    fs.bio = bio;
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_failed_cow_write_preserves_tree(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "CowIO", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "old", 3,
                                  100, BFS_INODE_FILE), BFS_OK);
    bfs_blk_t old_root = fs.dir_tree.tree.root;

    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    fb.fail_all_writes = true;
    fs.dir_tree.tree.bio = &fb.base;
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, BFS_ROOT_INO, "new", 3,
                                  101, BFS_INODE_FILE), BFS_ERR_IO);
    TEST_ASSERT_EQ(fs.dir_tree.tree.root, old_root);
    fs.dir_tree.tree.bio = bio;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "old", 3,
                                  NULL, NULL), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "new", 3,
                                  NULL, NULL), BFS_ERR_NOTFOUND);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_mount_propagates_superblock_io_error(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "MountIO", 0), BFS_OK);
    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    fb.fail_all_reads = true;
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_ERR_IO);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_recovery_invalidates_open_handles(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "Reload", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&file, "data", 4), 4);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_reload_committed_unlocked(&fs), BFS_OK);
    char data[4];
    TEST_ASSERT_EQ(bfs_file_read(&file, data, 4), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_file_write(&file, "x", 1), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_file_truncate(&file, 0), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&file, data, 4), 4);
    TEST_ASSERT_MEM_EQ(data, "data", 4);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_failed_recovery_blocks_operations(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "RecoveryIO", 0), BFS_OK);
    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
    fb.fail_all_reads = true;
    TEST_ASSERT_EQ(bfs_fs_reload_committed_unlocked(&fs), BFS_ERR_IO);
    /* Recovery invokes fail_read through the installed callback. */
    // cppcheck-suppress redundantAssignment
    fb.fail_all_reads = false;
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_fs_reserve(&fs, 1), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "blocked", 7, NULL), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "blocked"), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_snapshot_resume_deletions(&fs), BFS_ERR_IO);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_ERR_IO);
    TEST_ASSERT(!fs.mounted);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_persistent_delete_failure_requires_remount(void)
{
    unsigned rollback_failures = 0;
    for (uint32_t fail_at = 1; fail_at <= 16; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "Persistent", 0), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "keep", 4), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fs.freespace), BFS_OK);
        fb.persist_write_failure = true;
        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_fs_delete_file(&fs, BFS_ROOT_INO, "file", 4);
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        if (fb.failed_writes > 1) {
            rollback_failures++;
            TEST_ASSERT_EQ(fs.recovery_error, BFS_ERR_IO);
            /* Removing the injected failure must not make partial state committable. */
            fb.fail_all_writes = false;
            TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
            TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "bad", 3, NULL), BFS_ERR_IO);
        }
        bfs_fs_abandon(&fs);
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
        char comment[80];
        TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_OK);
        TEST_ASSERT_MEM_EQ(comment, "keep", 5);
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        bfs_bio_close(bio);
    }
    TEST_ASSERT(rollback_failures > 0);
    unlink(TEST_IMG);
}

static void test_failed_extent_rollback_marks_ownership_uncertain(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "ExtentRollback", 0), BFS_OK);
    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    /* A leading hole keeps the mapping in an extent tree. */
    TEST_ASSERT_EQ(bfs_file_seek(&file, BLK_SIZE, BFS_SEEK_SET), BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&file, "original", 8), 8);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fs.freespace), BFS_OK);
    bfs_blk_t replacement = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(replacement != BFS_BLK_NULL);
    /* Removing the sole leaf key needs no write. Both inserts then fail. */
    fb.fail_all_writes = true;
    TEST_ASSERT_EQ(bfs_extent_remap_block(&file.extents, 1, replacement, NULL), BFS_ERR_IO);
    TEST_ASSERT_EQ(file.extents.tree.free_sink_err, BFS_ERR_IO);
    TEST_ASSERT(fb.failed_writes >= 2);
    bfs_fs_abandon(&fs);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_seek(&file, BLK_SIZE, BFS_SEEK_SET), BLK_SIZE);
    char data[8];
    TEST_ASSERT_EQ(bfs_file_read(&file, data, sizeof(data)), sizeof(data));
    TEST_ASSERT_MEM_EQ(data, "original", sizeof(data));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static bool hwfail_fsck_clean(bfs_bio_t *bio, const char *operation,
                              uint32_t format_options, uint32_t fail_at)
{
    bfs_fs_t check_fs;
    bfs_err_t mount_err = bfs_fs_mount_readonly(&check_fs, bio);
    bfs_fsck_report_t report = {0};
    bfs_err_t check_err = mount_err;
    bfs_err_t unmount_err = BFS_OK;
    if (mount_err == BFS_OK) {
        check_err = bfs_fs_check(&check_fs, false, &report);
        unmount_err = bfs_fs_unmount(&check_fs);
    }
    if (mount_err != BFS_OK || check_err != BFS_OK || report.errors != 0 ||
        report.warnings != 0 || report.leaked_blocks != 0 || unmount_err != BFS_OK) {
        fprintf(stderr,
                "  fsck defect: operation=%s options=0x%08x cut=%u "
                "mount=%d check=%d errors=%u warnings=%u leaks=%u unmount=%d\n",
                operation, format_options, fail_at, mount_err, check_err,
                report.errors, report.warnings, report.leaked_blocks, unmount_err);
        return false;
    }
    return true;
}

static void run_delete_write_failures(uint32_t format_options,
                                      unsigned *post_mutation_failures,
                                      unsigned *injected_io_failures,
                                      unsigned *successes,
                                      unsigned *fsck_failures)
{
    for (uint32_t fail_at = 1; fail_at <= 16; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "DeleteFault", format_options), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "keep", 4), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_fs_delete_file(&fs, BFS_ROOT_INO, "file", 4);
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        TEST_ASSERT((err == BFS_OK) == (fb.failed_writes == 0));
        if (err == BFS_OK) TEST_ASSERT(fb.writes_until_failure > 0);
        /* The operation consumes the countdown through fail_write. */
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        if (err == BFS_ERR_IO) {
            TEST_ASSERT(fb.failed_writes > 0);
            (*injected_io_failures)++;
        } else {
            (*successes)++;
        }
        if (fs.recovery_error != BFS_OK) {
            /* COW retirement can fail after the working directory root moves.
             * Never commit that uncertain root; recover the old committed view. */
            (*post_mutation_failures)++;
            TEST_ASSERT_EQ(fs.recovery_error, BFS_ERR_IO);
            TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
            bfs_fs_abandon(&fs);
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO,
                                          "file", 4, NULL, NULL), BFS_OK);
        } else if (err != BFS_OK) {
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO,
                                          "file", 4, NULL, NULL), BFS_OK);
        }
        if (err != BFS_OK) {
            char comment[80];
            TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_OK);
            TEST_ASSERT_MEM_EQ(comment, "keep", 5);
        }
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        if (!hwfail_fsck_clean(bio, "delete", format_options, fail_at))
            (*fsck_failures)++;
        bfs_bio_close(bio);
    }
}

/* ── Live-transaction owned nodes ──────────────────────────── */

static void test_owned_nodes_rewrite_in_place_until_publication(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "Owned", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "base", 4, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_blk_t committed_root = fs.dir_tree.tree.root;

    /* The first change of the transaction copies the committed root. */
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "a", 1, &ino), BFS_OK);
    bfs_blk_t owned_root = fs.dir_tree.tree.root;
    TEST_ASSERT(owned_root != committed_root);
    TEST_ASSERT_EQ(fs.owned_nodes.txn_id, fs.live_txn_id);
    /* Later changes in the same transaction rewrite the owned root. */
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "b", 1, &ino), BFS_OK);
    TEST_ASSERT_EQ(fs.dir_tree.tree.root, owned_root);

    /* After publication the root is committed and copied again. */
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    uint8_t published[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, owned_root, published), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "c", 1, &ino), BFS_OK);
    TEST_ASSERT(fs.dir_tree.tree.root != owned_root);
    uint8_t after[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, owned_root, after), BFS_OK);
    TEST_ASSERT_MEM_EQ(after, published, BLK_SIZE);

    /* Reloading the committed state forgets the discarded transaction. */
    TEST_ASSERT(fs.owned_nodes.used > 0);
    TEST_ASSERT_EQ(bfs_fs_reload_committed_unlocked(&fs), BFS_OK);
    TEST_ASSERT_EQ(fs.owned_nodes.used, 0);
    uint32_t type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "b", 1, &ino, &type),
                   BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "c", 1, &ino, &type),
                   BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT(hwfail_fsck_clean(bio, "owned-rewrite", 0, 0));
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* Only committed state survives a remount; the uncommitted transaction is
 * either fully published by a successful sync or entirely absent. */
static void check_owned_survivors(bfs_bio_t *bio, bool delete_operation,
                                  bool committed_new)
{
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "kept", 4, &ino, &type),
                   BFS_OK);
    bfs_err_t first = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "first", 5, &ino, &type);
    bfs_err_t second = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "second", 6, &ino, &type);
    if (!committed_new) {
        TEST_ASSERT_EQ(first, BFS_ERR_NOTFOUND);
        TEST_ASSERT_EQ(second, BFS_ERR_NOTFOUND);
    } else if (delete_operation) {
        TEST_ASSERT_EQ(first, BFS_ERR_NOTFOUND);
    } else {
        TEST_ASSERT_EQ(first, BFS_OK);
        TEST_ASSERT_EQ(second, BFS_OK);
    }
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
}

static void run_owned_write_failures(bool delete_operation, unsigned *injected,
                                     unsigned *successes, unsigned *recoveries,
                                     unsigned *fsck_failures)
{
    for (uint32_t fail_at = 1; fail_at <= 24; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "OwnedFault", 0), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "kept", 4, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        /* Give the live transaction owned dir, inode and free-tree nodes. */
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "first", 5, &ino), BFS_OK);

        fb.writes_until_failure = fail_at;
        bfs_err_t err = delete_operation
            ? bfs_fs_delete_file(&fs, BFS_ROOT_INO, "first", 5)
            : bfs_fs_create_file(&fs, BFS_ROOT_INO, "second", 6, &ino);
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        TEST_ASSERT((err == BFS_OK) == (fb.failed_writes == 0));
        bool committed_new = false;
        if (err == BFS_OK) {
            (*successes)++;
            committed_new = bfs_fs_sync(&fs) == BFS_OK;
            TEST_ASSERT(committed_new);
        } else {
            (*injected)++;
            if (fs.recovery_error != BFS_OK) (*recoveries)++;
        }
        bfs_fs_abandon(&fs);
        check_owned_survivors(bio, delete_operation, committed_new);
        if (!hwfail_fsck_clean(bio, delete_operation ? "owned-delete" : "owned-create",
                               0, fail_at))
            (*fsck_failures)++;
        bfs_bio_close(bio);
    }
}

static void test_owned_write_failures_discard_only_uncommitted_state(void)
{
    for (uint32_t op = 0; op < 2; op++) {
        unsigned injected = 0, successes = 0, recoveries = 0, fsck_failures = 0;
        run_owned_write_failures(op == 1, &injected, &successes, &recoveries,
                                 &fsck_failures);
        TEST_ASSERT(injected > 0);
        TEST_ASSERT(successes > 0);
        TEST_ASSERT_EQ(fsck_failures, 0);
    }
    unlink(TEST_IMG);
}

static void test_delete_write_failures_preserve_comment(void)
{
    const uint32_t format_options[] = { 0, BFS_OPT_SNAPSHOTS };
    unsigned fsck_failures = 0;
    for (uint32_t mode = 0; mode < sizeof(format_options) / sizeof(format_options[0]);
         mode++) {
        unsigned post_mutation_failures = 0;
        unsigned injected_io_failures = 0;
        unsigned successes = 0;
        int failures_before = test_fail_count;
        run_delete_write_failures(format_options[mode], &post_mutation_failures,
                                  &injected_io_failures, &successes, &fsck_failures);
        if (test_fail_count != failures_before) {
            unlink(TEST_IMG);
            return;
        }
        TEST_ASSERT(injected_io_failures > 0);
        TEST_ASSERT(successes > 0);
        if (format_options[mode] == BFS_OPT_SNAPSHOTS)
            TEST_ASSERT(post_mutation_failures > 0);
    }
    unlink(TEST_IMG);
    TEST_ASSERT(fsck_failures == 0);
}

static void run_rmdir_write_failures(uint32_t format_options,
                                     unsigned *recovery_failures,
                                     unsigned *injected_io_failures,
                                     unsigned *successes,
                                     unsigned *fsck_failures)
{
    for (uint32_t fail_at = 1; fail_at <= 32; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "RmdirFault", format_options), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t dir_ino;
        TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "dir", 3, &dir_ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, dir_ino, "keep", 4), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_fs_rmdir(&fs, BFS_ROOT_INO, "dir", 3);
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        TEST_ASSERT((err == BFS_OK) == (fb.failed_writes == 0));
        if (err == BFS_OK) TEST_ASSERT(fb.writes_until_failure > 0);
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        if (err == BFS_ERR_IO) {
            TEST_ASSERT(fb.failed_writes > 0);
            (*injected_io_failures)++;
        } else {
            (*successes)++;
        }
        if (err == BFS_ERR_IO) {
            if (fs.recovery_error != BFS_OK) {
                (*recovery_failures)++;
                TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
                bfs_fs_abandon(&fs);
            } else {
                TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
            }
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO,
                                          "dir", 3, NULL, NULL), BFS_OK);
            char comment[80];
            TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, dir_ino, comment,
                                              sizeof(comment)), BFS_OK);
            TEST_ASSERT_MEM_EQ(comment, "keep", 5);
        }
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        if (!hwfail_fsck_clean(bio, "rmdir", format_options, fail_at))
            (*fsck_failures)++;
        bfs_bio_close(bio);
    }
}

static void test_rmdir_write_failures_preserve_committed_directory(void)
{
    const uint32_t format_options[] = { 0, BFS_OPT_SNAPSHOTS };
    unsigned fsck_failures = 0;
    for (uint32_t mode = 0; mode < sizeof(format_options) / sizeof(format_options[0]);
         mode++) {
        unsigned recovery_failures = 0;
        unsigned injected_io_failures = 0;
        unsigned successes = 0;
        int failures_before = test_fail_count;
        run_rmdir_write_failures(format_options[mode], &recovery_failures,
                                 &injected_io_failures, &successes, &fsck_failures);
        if (test_fail_count != failures_before) {
            unlink(TEST_IMG);
            return;
        }
        TEST_ASSERT(injected_io_failures > 0);
        TEST_ASSERT(successes > 0);
        if (format_options[mode] == BFS_OPT_SNAPSHOTS)
            TEST_ASSERT(recovery_failures > 0);
    }
    unlink(TEST_IMG);
    TEST_ASSERT(fsck_failures == 0);
}

static void run_rename_write_failures(uint32_t format_options,
                                      unsigned *recovery_failures,
                                      unsigned *injected_io_failures,
                                      unsigned *successes,
                                      unsigned *fsck_failures)
{
    for (uint32_t fail_at = 1; fail_at <= 32; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "RenameFault", format_options), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t src, dst, child;
        TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "src", 3, &src), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "dst", 3, &dst), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, src, "child", 5, &child), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_fs_rename(&fs, src, "child", 5, dst, "moved", 5);
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        TEST_ASSERT((err == BFS_OK) == (fb.failed_writes == 0));
        if (err == BFS_OK) TEST_ASSERT(fb.writes_until_failure > 0);
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        if (err == BFS_ERR_IO) {
            TEST_ASSERT(fb.failed_writes > 0);
            (*injected_io_failures)++;
        } else {
            (*successes)++;
        }
        if (err == BFS_ERR_IO) {
            if (fs.recovery_error != BFS_OK) {
                (*recovery_failures)++;
                TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
                bfs_fs_abandon(&fs);
            } else {
                TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
            }
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, src,
                                          "child", 5, NULL, NULL), BFS_OK);
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, dst,
                                          "moved", 5, NULL, NULL), BFS_ERR_NOTFOUND);
            uint32_t parent = 0;
            TEST_ASSERT_EQ(bfs_dir_parent_get(&fs.dir_tree, child,
                                          &parent), BFS_OK);
            TEST_ASSERT_EQ(parent, src);
        }
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        if (!hwfail_fsck_clean(bio, "rename", format_options, fail_at))
            (*fsck_failures)++;
        bfs_bio_close(bio);
    }
}

static void test_rename_write_failures_preserve_committed_paths(void)
{
    const uint32_t format_options[] = { 0, BFS_OPT_SNAPSHOTS };
    unsigned fsck_failures = 0;
    for (uint32_t mode = 0; mode < sizeof(format_options) / sizeof(format_options[0]);
         mode++) {
        unsigned recovery_failures = 0;
        unsigned injected_io_failures = 0;
        unsigned successes = 0;
        int failures_before = test_fail_count;
        run_rename_write_failures(format_options[mode], &recovery_failures,
                                  &injected_io_failures, &successes, &fsck_failures);
        if (test_fail_count != failures_before) {
            unlink(TEST_IMG);
            return;
        }
        TEST_ASSERT(injected_io_failures > 0);
        TEST_ASSERT(successes > 0);
        if (format_options[mode] == BFS_OPT_SNAPSHOTS)
            TEST_ASSERT(recovery_failures > 0);
    }
    unlink(TEST_IMG);
    TEST_ASSERT(fsck_failures == 0);
}

static void run_mkdir_write_failures(uint32_t format_options,
                                     unsigned *recovery_failures,
                                     unsigned *injected_io_failures,
                                     unsigned *successes,
                                     unsigned *fsck_failures)
{
    for (uint32_t fail_at = 1; fail_at <= 24; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "MkdirFault", format_options), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        fb.writes_until_failure = fail_at;
        uint32_t ino;
        bfs_err_t err = bfs_fs_mkdir(&fs, BFS_ROOT_INO, "newdir", 6, &ino);
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        TEST_ASSERT((err == BFS_OK) == (fb.failed_writes == 0));
        if (err == BFS_OK) TEST_ASSERT(fb.writes_until_failure > 0);
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        if (err == BFS_ERR_IO) {
            TEST_ASSERT(fb.failed_writes > 0);
            (*injected_io_failures)++;
        } else {
            (*successes)++;
        }
        if (err == BFS_ERR_IO) {
            if (fs.recovery_error != BFS_OK) {
                (*recovery_failures)++;
                TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
                bfs_fs_abandon(&fs);
            } else {
                TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
            }
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO,
                                          "newdir", 6, NULL, NULL), BFS_ERR_NOTFOUND);
        }
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        if (!hwfail_fsck_clean(bio, "mkdir", format_options, fail_at))
            (*fsck_failures)++;
        bfs_bio_close(bio);
    }
}

static void test_mkdir_write_failures_preserve_committed_tree(void)
{
    const uint32_t format_options[] = { 0, BFS_OPT_SNAPSHOTS };
    unsigned fsck_failures = 0;
    for (uint32_t mode = 0; mode < sizeof(format_options) / sizeof(format_options[0]);
         mode++) {
        unsigned recovery_failures = 0;
        unsigned injected_io_failures = 0;
        unsigned successes = 0;
        int failures_before = test_fail_count;
        run_mkdir_write_failures(format_options[mode], &recovery_failures,
                                 &injected_io_failures, &successes, &fsck_failures);
        if (test_fail_count != failures_before) {
            unlink(TEST_IMG);
            return;
        }
        TEST_ASSERT(injected_io_failures > 0);
        TEST_ASSERT(successes > 0);
        if (format_options[mode] == BFS_OPT_SNAPSHOTS)
            TEST_ASSERT(recovery_failures > 0);
    }
    unlink(TEST_IMG);
    TEST_ASSERT(fsck_failures == 0);
}

static void run_comment_write_failures(uint32_t format_options,
                                       unsigned *recovery_failures,
                                       unsigned *injected_io_failures,
                                       unsigned *successes,
                                       unsigned *fsck_failures)
{
    for (uint32_t fail_at = 1; fail_at <= 24; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "CommentFault", format_options), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "old", 3), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_fs_set_comment(&fs, ino, "new", 3);
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        TEST_ASSERT((err == BFS_OK) == (fb.failed_writes == 0));
        if (err == BFS_OK) TEST_ASSERT(fb.writes_until_failure > 0);
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        if (err == BFS_ERR_IO) {
            TEST_ASSERT(fb.failed_writes > 0);
            (*injected_io_failures)++;
        } else {
            (*successes)++;
        }
        if (err == BFS_ERR_IO) {
            if (fs.recovery_error != BFS_OK) {
                (*recovery_failures)++;
                TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_ERR_IO);
                bfs_fs_abandon(&fs);
            } else {
                TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
            }
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
            char comment[80];
            TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment,
                                              sizeof(comment)), BFS_OK);
            TEST_ASSERT_MEM_EQ(comment, "old", 4);
        }
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        if (!hwfail_fsck_clean(bio, "comment", format_options, fail_at))
            (*fsck_failures)++;
        bfs_bio_close(bio);
    }
}

static void test_comment_write_failures_preserve_committed_value(void)
{
    const uint32_t format_options[] = { 0, BFS_OPT_SNAPSHOTS };
    unsigned fsck_failures = 0;
    for (uint32_t mode = 0; mode < sizeof(format_options) / sizeof(format_options[0]);
         mode++) {
        unsigned recovery_failures = 0;
        unsigned injected_io_failures = 0;
        unsigned successes = 0;
        int failures_before = test_fail_count;
        run_comment_write_failures(format_options[mode], &recovery_failures,
                                   &injected_io_failures, &successes, &fsck_failures);
        if (test_fail_count != failures_before) {
            unlink(TEST_IMG);
            return;
        }
        TEST_ASSERT(injected_io_failures > 0);
        TEST_ASSERT(successes > 0);
        if (format_options[mode] == BFS_OPT_SNAPSHOTS)
            TEST_ASSERT(recovery_failures > 0);
    }
    unlink(TEST_IMG);
    TEST_ASSERT(fsck_failures == 0);
}

static void test_snapshot_delete_write_failure_recovery(void)
{
    uint32_t failures = 0;
    for (uint32_t fail_at = 1; fail_at <= 48; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "SnapshotFault", 0), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
        bfs_file_t file;
        TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_file_write(&file, "original", 8), 8);
        TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "point"), BFS_OK);
        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_snapshot_delete(&fs, 1);
        /* Disable injection for the subsequent recovery/readback phase. */
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        if (err == BFS_ERR_IO) failures++;
        TEST_ASSERT_EQ(bfs_snapshot_resume_deletions(&fs), BFS_OK);
        err = bfs_snapshot_find_by_name(&fs, "point", NULL, NULL);
        if (err == BFS_OK)
            TEST_ASSERT_EQ(bfs_snapshot_delete(&fs, 1), BFS_OK);
        else
            TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
        char data[8];
        TEST_ASSERT_EQ(bfs_file_read(&file, data, sizeof(data)), sizeof(data));
        TEST_ASSERT_MEM_EQ(data, "original", sizeof(data));
        TEST_ASSERT(!fs.has_snapshots);
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        bfs_bio_close(bio);
    }
    TEST_ASSERT(failures > 1);
    unlink(TEST_IMG);
}

static void test_allocator_refill_failure_can_retry_free(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "AllocatorFault", 0), BFS_OK);
    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
    bfs_blk_t block = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(block != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    fb.writes_until_failure = 1;
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, block, 1), BFS_ERR_IO);
    /* The allocator consumes the countdown through fail_write. */
    // cppcheck-suppress redundantAssignment
    fb.writes_until_failure = 0;
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, block, 1), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, block, 1), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

typedef struct {
    bfs_fs_t *fs;
    bfs_blk_t previous_end;
    uint32_t extent_count;
    bool have_previous;
    bool invalid;
} reserve_return_scan_t;

static bool reserve_return_is_emergency_slot(const bfs_fs_t *fs, bfs_blk_t blk)
{
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++)
        if (bfs_be32(fs->txn.sb_new.emergency_pool[i]) == blk) return true;
    return false;
}

static bool reserve_return_is_active_emergency_slot(const bfs_fs_t *fs,
                                                    bfs_blk_t blk)
{
    uint32_t count = bfs_be32(fs->txn.sb_new.emergency_count);
    for (uint32_t i = 0; i < count; i++)
        if (bfs_be32(fs->txn.sb_new.emergency_pool[i]) == blk) return true;
    return false;
}

static bool reserve_return_scan_extent(const void *key, const void *value,
                                       void *context)
{
    reserve_return_scan_t *scan = (reserve_return_scan_t *)context;
    bfs_freespace_t *freespace = &scan->fs->freespace;
    bfs_blk_t start = bfs_load_be32(key);
    uint32_t length = bfs_load_be32(value);
    if (length == 0 || start == BFS_BLK_NULL ||
        start >= freespace->tree.bio->block_count ||
        length > freespace->tree.bio->block_count - start ||
        (scan->have_previous && start < scan->previous_end)) {
        scan->invalid = true;
        return false;
    }

    bfs_blk_t end = start + length;
    for (uint32_t i = 0; i < freespace->reserve_count; i++) {
        bfs_blk_t reserved = freespace->reserve[i];
        if (reserved >= start && reserved < end) {
            scan->invalid = true;
            return false;
        }
    }
    for (uint32_t i = 0; i < BFS_EMERGENCY_POOL_SIZE; i++) {
        bfs_blk_t emergency = bfs_be32(scan->fs->txn.sb_new.emergency_pool[i]);
        if (emergency >= start && emergency < end) {
            scan->invalid = true;
            return false;
        }
    }

    scan->previous_end = end;
    scan->have_previous = true;
    scan->extent_count++;
    return true;
}

static bool reserve_return_layout_valid(bfs_fs_t *fs, uint32_t *extent_count_out)
{
    bfs_freespace_t *freespace = &fs->freespace;
    uint32_t emergency_count = bfs_be32(fs->txn.sb_new.emergency_count);
    if (freespace->reserve_count > BFS_ALLOC_RESERVE_SIZE ||
        emergency_count > BFS_EMERGENCY_POOL_SIZE)
        return false;

    for (uint32_t i = 0; i < freespace->reserve_count; i++) {
        bfs_blk_t blk = freespace->reserve[i];
        if (blk == BFS_BLK_NULL || blk >= freespace->tree.bio->block_count)
            return false;
        for (uint32_t j = 0; j < i; j++)
            if (freespace->reserve[j] == blk) return false;
    }
    for (uint32_t i = 0; i < emergency_count; i++) {
        bfs_blk_t blk = bfs_be32(fs->txn.sb_new.emergency_pool[i]);
        if (blk == BFS_BLK_NULL || blk >= freespace->tree.bio->block_count)
            return false;
        for (uint32_t j = 0; j < i; j++)
            if (bfs_be32(fs->txn.sb_new.emergency_pool[j]) == blk) return false;
        for (uint32_t j = 0; j < freespace->reserve_count; j++)
            if (freespace->reserve[j] == blk) return false;
    }

    reserve_return_scan_t scan = {
        .fs = fs,
        .previous_end = 0,
        .extent_count = 0,
        .have_previous = false,
        .invalid = false,
    };
    if (bfs_btree_scan(&freespace->tree, NULL, reserve_return_scan_extent,
                       &scan) != BFS_OK || scan.invalid)
        return false;
    if (extent_count_out) *extent_count_out = scan.extent_count;
    return true;
}

static uint64_t reserve_return_accounting(const bfs_fs_t *fs)
{
    return (uint64_t)fs->freespace.total_free + fs->freespace.reserve_count +
           bfs_be32(fs->txn.sb_new.emergency_count) + fs->pending_count;
}

static uint32_t reserve_return_run_count(const bfs_freespace_t *fs)
{
    bfs_blk_t sorted[BFS_ALLOC_RESERVE_SIZE];
    if (fs->reserve_count > BFS_ALLOC_RESERVE_SIZE) return 0;
    memcpy(sorted, fs->reserve, fs->reserve_count * sizeof(*sorted));
    for (uint32_t i = 1; i < fs->reserve_count; i++) {
        bfs_blk_t value = sorted[i];
        uint32_t j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }
    uint32_t runs = fs->reserve_count > 0 ? 1 : 0;
    for (uint32_t i = 1; i < fs->reserve_count; i++)
        if (sorted[i] != sorted[i - 1] + 1) runs++;
    return runs;
}

/* Emergency-pool node blocks can temporarily be retired into the allocator
 * reserve during COW. Return those blocks to their pool slots so the fixture
 * exercises the ordinary, non-emergency reserve batch. */
static bool reserve_return_restore_emergency_blocks(bfs_fs_t *fs)
{
    bfs_freespace_t *freespace = &fs->freespace;
    uint32_t saved_global_reserve = freespace->global_reserve;
    freespace->global_reserve = UINT32_MAX;
    for (uint32_t i = 0; i < freespace->reserve_count;) {
        bfs_blk_t blk = freespace->reserve[i];
        if (!reserve_return_is_emergency_slot(fs, blk)) {
            i++;
            continue;
        }
        for (uint32_t j = i + 1; j < freespace->reserve_count; j++)
            freespace->reserve[j - 1] = freespace->reserve[j];
        freespace->reserve_count--;
        if (bfs_freespace_free(freespace, blk, 1) != BFS_OK) {
            freespace->global_reserve = saved_global_reserve;
            return false;
        }
    }
    freespace->global_reserve = saved_global_reserve;
    return true;
}

static bool setup_fragmented_reserve_batch(failing_bio_t *fb, bfs_bio_t **bio_out,
                                           bfs_fs_t *fs)
{
    *bio_out = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    if (!*bio_out || bfs_fs_format(*bio_out, "ReserveBatch", 0) != BFS_OK)
        return false;
    init_failing_bio(fb, *bio_out);
    if (bfs_fs_mount(fs, &fb->base) != BFS_OK || bfs_fs_sync(fs) != BFS_OK)
        return false;
    /* These fixtures build reserve layouts with direct allocator calls and
     * fake committed roots to qualify the copy-on-write settlement path. */
    fs->owned_nodes.disabled = true;

    bfs_freespace_t *freespace = &fs->freespace;
    if (freespace->tree.height != 1 || freespace->reserve_count != 0)
        return false;

    /* Create four isolated free extents at the high end. With refill disabled,
     * the next reserve fill must draw blocks from more than one free extent. */
    bfs_blk_t allocated[8];
    uint32_t saved_global_reserve = freespace->global_reserve;
    freespace->global_reserve = UINT32_MAX;
    for (uint32_t i = 0; i < 8; i++) {
        allocated[i] = bfs_freespace_alloc(freespace, 1);
        if (allocated[i] == BFS_BLK_NULL) {
            freespace->global_reserve = saved_global_reserve;
            return false;
        }
    }
    for (uint32_t i = 0; i < 8; i += 2) {
        if (bfs_freespace_free(freespace, allocated[i], 1) != BFS_OK) {
            freespace->global_reserve = saved_global_reserve;
            return false;
        }
    }
    freespace->global_reserve = saved_global_reserve;

    if (!reserve_return_restore_emergency_blocks(fs) ||
        freespace->tree.height != 1 || freespace->reserve_count != 0 ||
        bfs_freespace_refill_reserve(freespace) != BFS_OK ||
        !reserve_return_restore_emergency_blocks(fs))
        return false;

    uint32_t extent_count = 0;
    bool fixture_valid =
        freespace->tree.height == 1 && freespace->reserve_count >= 3 &&
        reserve_return_run_count(freespace) > 1 &&
        reserve_return_layout_valid(fs, &extent_count) &&
        extent_count + freespace->reserve_count <
            bfs_btree_leaf_capacity(&freespace->tree);
    /* Keep the existing batch tests on the unsupported-fast-path fallback. */
    freespace->committed_sb = NULL;
    return fixture_valid;
}

static bool setup_reserve_root_fold_fixture(failing_bio_t *fb,
                                             bfs_bio_t **bio_out,
                                             bfs_fs_t *fs)
{
    if (!setup_fragmented_reserve_batch(fb, bio_out, fs)) return false;
    fs->freespace.committed_sb = &fs->txn.sb;
    return fs->freespace.tree.height == 1 &&
           fs->freespace.tree.root !=
               bfs_be32(fs->txn.sb.free_tree_root);
}

static bool setup_emergency_root_fold_fixture(failing_bio_t *fb,
                                               bfs_bio_t **bio_out,
                                               bfs_fs_t *fs)
{
    if (!setup_reserve_root_fold_fixture(fb, bio_out, fs)) return false;
    bfs_blk_t root = fs->freespace.tree.root;
    if (reserve_return_is_active_emergency_slot(fs, root)) return false;
    if (!reserve_return_is_emergency_slot(fs, root)) {
        uint32_t emergency_count = bfs_be32(fs->txn.sb_new.emergency_count);
        if (emergency_count == 0 ||
            fs->freespace.reserve_count >= BFS_ALLOC_RESERVE_SIZE)
            return false;
        bfs_blk_t emergency_root = bfs_be32(
            fs->txn.sb_new.emergency_pool[emergency_count - 1]);
        /* Copy the valid current leaf into an active emergency slot, then
         * transfer the former root block to the ordinary reserve. This gives
         * the fold a deterministic emergency-origin root fixture. */
        uint8_t root_image[BLK_SIZE];
        if (bfs_bio_read(*bio_out, root, root_image) != BFS_OK ||
            bfs_bio_write(*bio_out, emergency_root, root_image) != BFS_OK)
            return false;
        fs->txn.sb_new.emergency_count = bfs_be32(emergency_count - 1);
        fs->freespace.tree.root = emergency_root;
        fs->freespace.reserve[fs->freespace.reserve_count++] = root;
        root = emergency_root;
    }

    uint64_t root_txn;
    return fs->freespace.tree.height == 1 &&
        reserve_return_is_emergency_slot(fs, root) &&
        !reserve_return_is_active_emergency_slot(fs, root) &&
        bfs_btree_root_leaf_txn_id(&fs->freespace.tree, &root_txn) == BFS_OK &&
        root_txn == bfs_btree_txn_id(&fs->freespace.tree) &&
        fs->freespace.reserve_count > 0;
}

static bool setup_mixed_fragmented_reserve_batch(failing_bio_t *fb,
                                                  bfs_bio_t **bio_out,
                                                  bfs_fs_t *fs,
                                                  bfs_blk_t *retired_out,
                                                  uint32_t *ordinary_count_out,
                                                  uint32_t *emergency_count_out)
{
    if (!setup_fragmented_reserve_batch(fb, bio_out, fs)) return false;
    bfs_freespace_t *freespace = &fs->freespace;
    uint32_t emergency_count = bfs_be32(fs->txn.sb_new.emergency_count);
    if (emergency_count == 0 || emergency_count > BFS_EMERGENCY_POOL_SIZE ||
        freespace->reserve_count >= BFS_ALLOC_RESERVE_SIZE)
        return false;
    bfs_blk_t retired = bfs_be32(
        fs->txn.sb_new.emergency_pool[emergency_count - 1]);
    fs->txn.sb_new.emergency_count = bfs_be32(emergency_count - 1);
    freespace->reserve[freespace->reserve_count++] = retired;
    if (!reserve_return_layout_valid(fs, NULL)) return false;
    *retired_out = retired;
    *ordinary_count_out = freespace->reserve_count - 1;
    *emergency_count_out = emergency_count;
    return true;
}

typedef struct {
    bfs_blk_t root;
    uint32_t total_free;
    uint32_t reserve_count;
    bfs_blk_t reserve[BFS_ALLOC_RESERVE_SIZE];
    uint32_t emergency_count;
    uint32_t emergency_pool[BFS_EMERGENCY_POOL_SIZE];
    uint32_t pending_count;
    bfs_blk_t pending_first;
    uint32_t global_reserve;
} reserve_return_snapshot_t;

static reserve_return_snapshot_t reserve_return_capture(bfs_fs_t *fs)
{
    reserve_return_snapshot_t snapshot = {0};
    snapshot.root = fs->freespace.tree.root;
    snapshot.total_free = fs->freespace.total_free;
    snapshot.reserve_count = fs->freespace.reserve_count;
    memcpy(snapshot.reserve, fs->freespace.reserve,
           snapshot.reserve_count * sizeof(*snapshot.reserve));
    snapshot.emergency_count = bfs_be32(fs->txn.sb_new.emergency_count);
    memcpy(snapshot.emergency_pool, fs->txn.sb_new.emergency_pool,
           sizeof(snapshot.emergency_pool));
    snapshot.pending_count = fs->pending_count;
    snapshot.pending_first = snapshot.pending_count
        ? bfs_fs_pending_items(fs)[0] : BFS_BLK_NULL;
    snapshot.global_reserve = fs->freespace.global_reserve;
    return snapshot;
}

static bool reserve_return_matches_snapshot(
    bfs_fs_t *fs, const reserve_return_snapshot_t *snapshot)
{
    return fs->freespace.tree.root == snapshot->root &&
           fs->freespace.total_free == snapshot->total_free &&
           fs->freespace.reserve_count == snapshot->reserve_count &&
           memcmp(fs->freespace.reserve, snapshot->reserve,
                  snapshot->reserve_count * sizeof(*snapshot->reserve)) == 0 &&
           bfs_be32(fs->txn.sb_new.emergency_count) ==
               snapshot->emergency_count &&
           memcmp(fs->txn.sb_new.emergency_pool, snapshot->emergency_pool,
                  sizeof(snapshot->emergency_pool)) == 0 &&
           fs->pending_count == snapshot->pending_count &&
           (!snapshot->pending_count ||
            bfs_fs_pending_items(fs)[0] == snapshot->pending_first) &&
           fs->freespace.global_reserve == snapshot->global_reserve;
}

typedef struct {
    bfs_blk_t block;
    uint32_t occurrences;
} reserve_return_free_count_t;

static bool reserve_return_count_free_block(const void *key, const void *value,
                                             void *context)
{
    reserve_return_free_count_t *count =
        (reserve_return_free_count_t *)context;
    bfs_blk_t start = bfs_load_be32(key);
    uint32_t length = bfs_load_be32(value);
    if (count->block >= start && count->block - start < length)
        count->occurrences++;
    return true;
}

static uint32_t reserve_return_free_occurrences(bfs_fs_t *fs, bfs_blk_t block)
{
    reserve_return_free_count_t count = { .block = block, .occurrences = 0 };
    if (bfs_btree_scan(&fs->freespace.tree, NULL,
                       reserve_return_count_free_block, &count) != BFS_OK)
        return UINT32_MAX;
    return count.occurrences;
}

typedef struct {
    bfs_blk_t block;
    bool found;
} reserve_return_first_free_t;

static bool reserve_return_get_first_free(const void *key, const void *value,
                                          void *context)
{
    (void)value;
    reserve_return_first_free_t *first =
        (reserve_return_first_free_t *)context;
    first->block = bfs_load_be32(key);
    first->found = true;
    return false;
}

static bfs_blk_t reserve_return_first_free_block(bfs_fs_t *fs)
{
    reserve_return_first_free_t first = {
        .block = BFS_BLK_NULL,
        .found = false,
    };
    if (bfs_btree_scan(&fs->freespace.tree, NULL,
                       reserve_return_get_first_free, &first) != BFS_OK ||
        !first.found)
        return BFS_BLK_NULL;
    return first.block;
}

static bool reserve_return_is_alias_candidate(bfs_fs_t *fs, bfs_blk_t block)
{
    if (block == BFS_BLK_NULL || block == fs->freespace.tree.root ||
        reserve_return_free_occurrences(fs, block) != 1 ||
        reserve_return_is_active_emergency_slot(fs, block))
        return false;
    for (uint32_t i = 0; i < fs->freespace.reserve_count; i++)
        if (fs->freespace.reserve[i] == block) return false;
    for (uint32_t i = 0; i < fs->pending_count; i++)
        if (bfs_fs_pending_items(fs)[i] == block) return false;
    return true;
}

static uint32_t reserve_return_ordinary_count(const bfs_fs_t *fs)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < fs->freespace.reserve_count; i++)
        if (!reserve_return_is_emergency_slot(fs,
                fs->freespace.reserve[i]))
            count++;
    return count;
}

static void test_reserve_return_root_fold_reactivates_emergency_root(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_emergency_root_fold_fixture(&fb, &bio, &fs));

    bfs_blk_t old_root = fs.freespace.tree.root;
    uint32_t ordinary_count = reserve_return_ordinary_count(&fs);
    uint64_t available_before = reserve_return_accounting(&fs);
    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    TEST_ASSERT(ordinary_count > 0);
    TEST_ASSERT_EQ(ordinary_count, before.reserve_count);
    fb.writes_until_failure = UINT32_MAX;

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT_EQ(writes_observed, 1);
    TEST_ASSERT_EQ(fb.failed_writes, 0);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT(reserve_return_is_active_emergency_slot(&fs, old_root));
    TEST_ASSERT(reserve_return_is_emergency_slot(&fs, fs.freespace.tree.root));
    TEST_ASSERT(!reserve_return_is_active_emergency_slot(
        &fs, fs.freespace.tree.root));
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT_EQ(fs.pending_count, before.pending_count);
    TEST_ASSERT_EQ(bfs_fs_pending_items(&fs)[0], before.pending_first);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_root_fold_returns_ordinary_root(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_reserve_root_fold_fixture(&fb, &bio, &fs));

    bfs_blk_t prior_root = fs.freespace.tree.root;
    bfs_blk_t allocated = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(allocated != BFS_BLK_NULL);
    bfs_blk_t old_root = fs.freespace.tree.root;
    TEST_ASSERT(old_root != prior_root);
    TEST_ASSERT(!reserve_return_is_emergency_slot(&fs, old_root));
    uint32_t ordinary_count = reserve_return_ordinary_count(&fs);
    TEST_ASSERT(ordinary_count > 0);
    uint32_t old_free = fs.freespace.total_free;
    uint64_t available_before = reserve_return_accounting(&fs);
    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    fb.writes_until_failure = UINT32_MAX;

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT_EQ(writes_observed, 1);
    TEST_ASSERT_EQ(fb.failed_writes, 0);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, old_free + ordinary_count + 1);
    TEST_ASSERT_EQ(reserve_return_free_occurrences(&fs, old_root), 1);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT_EQ(fs.pending_count, before.pending_count);
    TEST_ASSERT_EQ(bfs_fs_pending_items(&fs)[0], before.pending_first);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_root_fold_first_write_failure_restores(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_reserve_root_fold_fixture(&fb, &bio, &fs));

    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = 1;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_IO);
    TEST_ASSERT_EQ(fb.failed_writes, 1);
    TEST_ASSERT_EQ(fb.writes_until_failure, 0);
    TEST_ASSERT(reserve_return_matches_snapshot(&fs, &before));
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    fb.writes_until_failure = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT_EQ(writes_observed, 1);
    TEST_ASSERT(fs.freespace.tree.root != before.root);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_count),
                   before.emergency_count - 1);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_root_fold_without_emergency_scratch_falls_back(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_emergency_root_fold_fixture(&fb, &bio, &fs));
    TEST_ASSERT(reserve_return_ordinary_count(&fs) ==
                fs.freespace.reserve_count);
    fs.txn.sb_new.emergency_count = bfs_be32(0);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT_EQ(writes_observed, 1);
    TEST_ASSERT(fs.freespace.tree.root != before.root);
    TEST_ASSERT(!reserve_return_is_emergency_slot(&fs,
                                                  fs.freespace.tree.root));
    TEST_ASSERT(reserve_return_is_active_emergency_slot(&fs, before.root));
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT_EQ(fs.pending_count, before.pending_count);
    TEST_ASSERT_EQ(bfs_fs_pending_items(&fs)[0], before.pending_first);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_root_fold_rejects_future_root(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_reserve_root_fold_fixture(&fb, &bio, &fs));

    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    uint64_t available_before = reserve_return_accounting(&fs);
    uint64_t root_txn = 0;
    TEST_ASSERT_EQ(bfs_btree_root_leaf_txn_id(&fs.freespace.tree, &root_txn),
                   BFS_OK);
    TEST_ASSERT(root_txn > 0);
    /* This published root has a future transaction tag. The tag check must
     * run before the published-root eligibility check. */
    fs.txn.sb.free_tree_root = bfs_be32(fs.freespace.tree.root);
    uint64_t saved_live_txn = fs.live_txn_id;
    fs.live_txn_id = root_txn - 1;
    TEST_ASSERT(root_txn > bfs_btree_txn_id(&fs.freespace.tree));

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(fb.failed_writes, 0);
    TEST_ASSERT(reserve_return_matches_snapshot(&fs, &before));
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));
    fs.live_txn_id = saved_live_txn;

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_root_fold_rejects_committed_root(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_reserve_root_fold_fixture(&fb, &bio, &fs));

    bfs_blk_t old_root = fs.freespace.tree.root;
    bfs_superblock_t claimed_committed = fs.txn.sb;
    claimed_committed.free_tree_root = bfs_be32(old_root);
    fs.freespace.committed_sb = &claimed_committed;
    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = UINT32_MAX;

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT(writes_observed > 1);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(reserve_return_free_occurrences(&fs, old_root), 1);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT_EQ(fs.pending_count, before.pending_count);
    TEST_ASSERT_EQ(bfs_fs_pending_items(&fs)[0], before.pending_first);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_fold_falls_back_for_inactive_pool_alias(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_reserve_root_fold_fixture(&fb, &bio, &fs));
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_blk_t alias = reserve_return_first_free_block(&fs);
    TEST_ASSERT(reserve_return_is_alias_candidate(&fs, alias));
    uint32_t emergency_count = bfs_be32(fs.txn.sb_new.emergency_count);
    TEST_ASSERT(emergency_count > 0);
    uint32_t inactive_slot;
    if (emergency_count == BFS_EMERGENCY_POOL_SIZE) {
        inactive_slot = emergency_count - 1;
        fs.txn.sb_new.emergency_count = bfs_be32(emergency_count - 1);
    } else {
        inactive_slot = emergency_count;
    }
    fs.txn.sb_new.emergency_pool[inactive_slot] = bfs_be32(alias);
    TEST_ASSERT(!reserve_return_is_active_emergency_slot(&fs, alias));
    TEST_ASSERT_EQ(reserve_return_free_occurrences(&fs, alias), 1);

    uint64_t available_before = reserve_return_accounting(&fs);
    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    fb.writes_until_failure = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT(writes_observed > 0);
    TEST_ASSERT(fs.freespace.tree.root != before.root);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT_EQ(reserve_return_free_occurrences(&fs, alias), 1);
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_pool[inactive_slot]), alias);
    TEST_ASSERT(!reserve_return_is_active_emergency_slot(&fs, alias));
    TEST_ASSERT_EQ(fs.pending_count, before.pending_count);
    TEST_ASSERT_EQ(bfs_fs_pending_items(&fs)[0], before.pending_first);

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_fold_rejects_active_pool_alias(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_reserve_root_fold_fixture(&fb, &bio, &fs));
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_blk_t alias = reserve_return_first_free_block(&fs);
    TEST_ASSERT(reserve_return_is_alias_candidate(&fs, alias));
    uint32_t emergency_count = bfs_be32(fs.txn.sb_new.emergency_count);
    TEST_ASSERT(emergency_count > 0);
    /* The fold takes its scratch block from the last active pool slot. */
    uint32_t selected_scratch_slot = emergency_count - 1;
    fs.txn.sb_new.emergency_pool[selected_scratch_slot] = bfs_be32(alias);
    TEST_ASSERT(reserve_return_is_active_emergency_slot(&fs, alias));
    TEST_ASSERT_EQ(reserve_return_free_occurrences(&fs, alias), 1);

    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(UINT32_MAX - fb.writes_until_failure, 0);
    TEST_ASSERT_EQ(fb.failed_writes, 0);
    TEST_ASSERT(reserve_return_matches_snapshot(&fs, &before));
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT_EQ(reserve_return_free_occurrences(&fs, alias), 1);
    TEST_ASSERT(reserve_return_is_active_emergency_slot(&fs, alias));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_mixed_batch_reactivates_emergency_slot(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    bfs_blk_t retired;
    uint32_t ordinary_count, emergency_count;
    TEST_ASSERT(setup_mixed_fragmented_reserve_batch(
        &fb, &bio, &fs, &retired, &ordinary_count, &emergency_count));
    bfs_blk_t second_retired = bfs_be32(
        fs.txn.sb_new.emergency_pool[emergency_count - 2]);
    fs.txn.sb_new.emergency_count = bfs_be32(emergency_count - 2);
    fs.freespace.reserve[fs.freespace.reserve_count++] = second_retired;
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));
    bfs_blk_t old_root = fs.freespace.tree.root;
    uint32_t reserve_count_before = fs.freespace.reserve_count;
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = UINT32_MAX;

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    TEST_ASSERT(writes_observed > 0 && writes_observed < ordinary_count);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_count_before, ordinary_count + 2);
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_count), emergency_count - 1);
    TEST_ASSERT(reserve_return_is_active_emergency_slot(&fs, retired));
    TEST_ASSERT(!reserve_return_is_active_emergency_slot(&fs, second_retired));
    TEST_ASSERT_EQ(fs.freespace.tree.height, 1);
    TEST_ASSERT_EQ(fs.freespace.tree.root, second_retired);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_mixed_batch_first_write_failure_restores(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    bfs_blk_t retired;
    uint32_t ordinary_count, emergency_count;
    TEST_ASSERT(setup_mixed_fragmented_reserve_batch(
        &fb, &bio, &fs, &retired, &ordinary_count, &emergency_count));
    reserve_return_snapshot_t before = reserve_return_capture(&fs);
    TEST_ASSERT_EQ(before.pending_count, 1);
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = 1;

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_IO);
    TEST_ASSERT_EQ(fb.failed_writes, 1);
    TEST_ASSERT(reserve_return_matches_snapshot(&fs, &before));
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_count), emergency_count - 1);
    TEST_ASSERT(!reserve_return_is_active_emergency_slot(&fs, retired));
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_mixed_batch_second_write_failure_keeps_ownership(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    bfs_blk_t retired;
    uint32_t ordinary_count, emergency_count;
    TEST_ASSERT(setup_mixed_fragmented_reserve_batch(
        &fb, &bio, &fs, &retired, &ordinary_count, &emergency_count));
    bfs_blk_t old_root = fs.freespace.tree.root;
    uint32_t old_free = fs.freespace.total_free;
    uint32_t old_global_reserve = fs.freespace.global_reserve;
    uint64_t available_before = reserve_return_accounting(&fs);
    fb.writes_until_failure = 2;

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_IO);
    TEST_ASSERT_EQ(fb.failed_writes, 1);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, old_free + ordinary_count - 1);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 2);
    TEST_ASSERT_EQ(fs.freespace.reserve[0], retired);
    TEST_ASSERT_EQ(fs.freespace.reserve[1], old_root);
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_count), emergency_count - 1);
    TEST_ASSERT_EQ(fs.freespace.global_reserve, old_global_reserve);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_count), emergency_count - 1);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_batch_fragmented_accounting(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_fragmented_reserve_batch(&fb, &bio, &fs));

    uint64_t available_before = reserve_return_accounting(&fs);
    uint32_t global_reserve_before = fs.freespace.global_reserve;
    uint32_t reserve_count_before = fs.freespace.reserve_count;
    bfs_blk_t old_root = fs.freespace.tree.root;
    fb.writes_until_failure = UINT32_MAX;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    uint32_t writes_observed = UINT32_MAX - fb.writes_until_failure;
    /* Retiring the old root can require additional writes after the batch. */
    TEST_ASSERT(writes_observed > 0 && writes_observed < reserve_count_before);
    TEST_ASSERT_EQ(fb.failed_writes, 0);
    fb.writes_until_failure = 0;
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(fs.freespace.global_reserve, global_reserve_before);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_batch_first_write_failure_is_retryable(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_fragmented_reserve_batch(&fb, &bio, &fs));

    bfs_blk_t old_root = fs.freespace.tree.root;
    uint32_t old_free = fs.freespace.total_free;
    uint32_t old_count = fs.freespace.reserve_count;
    bfs_blk_t old_reserve[BFS_ALLOC_RESERVE_SIZE];
    memcpy(old_reserve, fs.freespace.reserve, old_count * sizeof(*old_reserve));
    uint32_t old_emergency_count = bfs_be32(fs.txn.sb_new.emergency_count);
    uint32_t old_pending_count = fs.pending_count;
    uint32_t old_global_reserve = fs.freespace.global_reserve;
    uint64_t available_before = reserve_return_accounting(&fs);

    fb.writes_until_failure = 1;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_IO);
    TEST_ASSERT_EQ(fb.failed_writes, 1);
    TEST_ASSERT_EQ(fb.writes_until_failure, 0);
    TEST_ASSERT_EQ(fs.freespace.tree.root, old_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, old_free);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, old_count);
    TEST_ASSERT_MEM_EQ(fs.freespace.reserve, old_reserve,
                      old_count * sizeof(*old_reserve));
    TEST_ASSERT_EQ(bfs_be32(fs.txn.sb_new.emergency_count), old_emergency_count);
    TEST_ASSERT_EQ(fs.pending_count, old_pending_count);
    TEST_ASSERT_EQ(fs.freespace.global_reserve, old_global_reserve);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(fs.freespace.global_reserve, old_global_reserve);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_batch_later_write_failure_keeps_ownership(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    TEST_ASSERT(setup_fragmented_reserve_batch(&fb, &bio, &fs));

    bfs_blk_t old_root = fs.freespace.tree.root;
    uint32_t old_free = fs.freespace.total_free;
    uint32_t old_count = fs.freespace.reserve_count;
    uint32_t old_global_reserve = fs.freespace.global_reserve;
    uint64_t available_before = reserve_return_accounting(&fs);

    fb.writes_until_failure = 2;
    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_ERR_IO);
    TEST_ASSERT_EQ(fb.failed_writes, 1);
    TEST_ASSERT(fs.freespace.tree.root != old_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, old_free + old_count - 1);
    TEST_ASSERT_EQ(fs.freespace.global_reserve, old_global_reserve);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    TEST_ASSERT_EQ(bfs_freespace_return_reserve(&fs.freespace), BFS_OK);
    TEST_ASSERT_EQ(fs.freespace.reserve_count, 0);
    TEST_ASSERT_EQ(reserve_return_accounting(&fs), available_before);
    TEST_ASSERT(reserve_return_layout_valid(&fs, NULL));

    bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_reserve_return_write_failures_preserve_committed_tree(void)
{
    unsigned failures = 0;
    for (uint32_t fail_at = 1; fail_at <= 16; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "ReserveFault", 0), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fs.freespace), BFS_OK);
        uint32_t count = fs.freespace.reserve_count;
        uint32_t scratch = 4u * fs.freespace.tree.height + 8u;
        TEST_ASSERT(count > scratch + 1);
        bfs_blk_t start = fs.freespace.reserve[count - 1];
        TEST_ASSERT_EQ(fs.freespace.reserve[count - 2], start + 1);

        fb.writes_until_failure = fail_at;
        bfs_err_t err = bfs_freespace_return_reserve(&fs.freespace);
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        if (err == BFS_ERR_IO) failures++;

        /* The failed working tree must never replace the committed root. */
        bfs_fs_abandon(&fs);
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
        bfs_fsck_report_t report;
        TEST_ASSERT_EQ(bfs_fs_check(&fs, false, &report), BFS_OK);
        TEST_ASSERT_EQ(report.errors, 0);
        TEST_ASSERT_EQ(report.leaked_blocks, 0);
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        bfs_bio_close(bio);
    }
    TEST_ASSERT(failures > 0);
    unlink(TEST_IMG);
}

static void test_batch_leaf_write_failure_preserves_root(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "BatchFault", 0), BFS_OK);
    failing_bio_t fb;
    init_failing_bio(&fb, bio);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
    bfs_blk_t start = bfs_freespace_alloc(&fs.freespace, 8);
    TEST_ASSERT(start != BFS_BLK_NULL);
    TEST_ASSERT_EQ(fs.freespace.tree.height, 1);
    bfs_blk_t blocks[] = {start, start + 2};
    bfs_blk_t old_root = fs.freespace.tree.root;
    uint32_t old_free = fs.freespace.total_free;

    fb.writes_until_failure = 1;
    TEST_ASSERT_EQ(bfs_freespace_free_sorted_blocks(&fs.freespace, blocks, 2),
                   BFS_ERR_IO);
    TEST_ASSERT_EQ(fb.writes_until_failure, 0);
    TEST_ASSERT_EQ(fs.freespace.tree.root, old_root);
    TEST_ASSERT_EQ(fs.freespace.total_free, old_free);
    TEST_ASSERT_EQ(bfs_freespace_free_sorted_blocks(&fs.freespace, blocks, 2),
                   BFS_OK);

    bfs_fs_abandon(&fs);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    TEST_ASSERT_EQ(report.leaked_blocks, 0);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static bool append_file_state_matches(bfs_fs_t *fs, uint32_t ino,
                                      uint64_t expected_size,
                                      bool check_extent_root,
                                      bfs_blk_t expected_extent_root,
                                      bool check_block_one_hole,
                                      const uint8_t *expected_data,
                                      uint32_t expected_data_size)
{
    bfs_file_t file;
    bfs_blk_t mapped;
    uint8_t actual[4u * BLK_SIZE];
    if (expected_data_size > sizeof(actual) ||
        bfs_file_open(&file, fs, ino) != BFS_OK || file.size != expected_size)
        return false;
    if (check_extent_root && file.extents.tree.root != expected_extent_root)
        return false;
    if (check_block_one_hole &&
        bfs_extent_lookup(&file.extents, 1, &mapped) != BFS_ERR_NOTFOUND)
        return false;
    if (bfs_file_read(&file, actual, sizeof(actual)) != (int32_t)expected_data_size)
        return false;
    return expected_data_size == 0 ||
           (expected_data && memcmp(actual, expected_data, expected_data_size) == 0);
}

static bool append_fsck_has_no_leaks(bfs_fs_t *fs)
{
    bfs_fsck_report_t report;
    return fs->pending_count == 0 &&
           bfs_fs_check(fs, false, &report) == BFS_OK &&
           report.errors == 0 && report.leaked_blocks == 0;
}

static bool append_sync_remount_matches_file(failing_bio_t *fb, bfs_fs_t *fs,
                                              uint32_t ino, uint64_t expected_size,
                                              bool check_extent_root,
                                              bfs_blk_t expected_root,
                                              bool check_block_one_hole,
                                              const uint8_t *expected_data)
{
    if (bfs_fs_sync(fs) != BFS_OK || !append_fsck_has_no_leaks(fs) ||
        bfs_fs_unmount(fs) != BFS_OK)
        return false;
    if (bfs_fs_mount(fs, &fb->base) != BFS_OK ||
        !append_file_state_matches(fs, ino, expected_size, check_extent_root,
                                   expected_root, check_block_one_hole,
                                   expected_data, (uint32_t)expected_size) ||
        !append_fsck_has_no_leaks(fs))
        return false;
    return bfs_fs_unmount(fs) == BFS_OK;
}

static bool setup_append_file(const char *volume_name,
                              const uint8_t *initial_data,
                              uint32_t initial_size, bfs_bio_t **bio_out,
                              failing_bio_t *fb, bfs_fs_t *fs,
                              uint32_t *ino_out, bfs_file_t *file)
{
    *bio_out = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    if (!*bio_out || bfs_fs_format(*bio_out, volume_name, 0) != BFS_OK)
        return false;
    init_failing_bio(fb, *bio_out);
    if (bfs_fs_mount(fs, &fb->base) != BFS_OK ||
        bfs_fs_create_file(fs, BFS_ROOT_INO, "file", 4, ino_out) != BFS_OK)
        return false;
    if (initial_size > 0 &&
        (bfs_file_open(file, fs, *ino_out) != BFS_OK ||
         bfs_file_write(file, initial_data, initial_size) != (int32_t)initial_size))
        return false;
    return bfs_fs_sync(fs) == BFS_OK &&
           bfs_file_open(file, fs, *ino_out) == BFS_OK;
}

static bool append_fails_extent_write_after_payloads(failing_bio_t *fb,
                                                      bfs_file_t *file,
                                                      const uint8_t *data,
                                                      uint32_t block_count,
                                                      failing_bio_t *extent_fault)
{
    init_failing_bio(extent_fault, &fb->base);
    extent_fault->fail_all_writes = true;
    file->extents.tree.bio = &extent_fault->base;
    fb->watched_data = data;
    fb->watched_data_blocks = block_count;
    int32_t written = bfs_file_append(file, data, block_count * BLK_SIZE);
    return written == BFS_ERR_IO &&
           fb->watched_data_write_attempts == block_count &&
           fb->failed_writes == 0 && extent_fault->failed_writes == 1;
}

static void test_short_write_publishes_inode(void)
{
    uint8_t data[3u * BLK_SIZE], actual[sizeof(data)];
    memset(data, 0x6d, sizeof(data));
    unsigned short_writes = 0;
    for (uint32_t fail_at = 1; fail_at <= 48; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "ShortWrite", 0), BFS_OK);
        failing_bio_t fb;
        init_failing_bio(&fb, bio);
        bfs_fs_t fs;
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fb.base), BFS_OK);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
        bfs_file_t file;
        TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fs.freespace), BFS_OK);
        fb.writes_until_failure = fail_at;
        int32_t written = bfs_file_write(&file, data, sizeof(data));
        /* Disable injection before validating the committed result. */
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        TEST_ASSERT(written == BFS_ERR_IO ||
                    (written > 0 && (uint32_t)written <= sizeof(data)));
        if (written > 0 && (uint32_t)written < sizeof(data)) short_writes++;
        uint32_t expected = written > 0 ? (uint32_t)written : 0;
        TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
        TEST_ASSERT_EQ(file.size, expected);
        TEST_ASSERT_EQ(bfs_file_read(&file, actual, sizeof(actual)), expected);
        TEST_ASSERT_MEM_EQ(actual, data, expected);
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
        TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
        TEST_ASSERT_EQ(file.size, expected);
        TEST_ASSERT_EQ(bfs_file_read(&file, actual, sizeof(actual)), expected);
        TEST_ASSERT_MEM_EQ(actual, data, expected);
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        bfs_bio_close(bio);
    }
    TEST_ASSERT(short_writes > 0);
    unlink(TEST_IMG);
}

static void test_append_data_write_failure_publishes_prefix(void)
{
    uint8_t data[4u * BLK_SIZE];
    for (uint32_t block = 0; block < 4; block++)
        memset(data + (size_t)block * BLK_SIZE, (int)(0x41u + block), BLK_SIZE);

    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(setup_append_file("AppendShortWrite", NULL, 0, &bio, &fb,
                                  &fs, &ino, &file));
    TEST_ASSERT_EQ(file.size, 0);

    /* Fail the third matching full-block data write, after two completed blocks. */
    fb.watched_data = data;
    fb.watched_data_blocks = 4;
    fb.data_writes_until_failure = 3;
    int32_t written = bfs_file_append(&file, data, sizeof(data));
    TEST_ASSERT_EQ(written, (int32_t)(2u * BLK_SIZE));
    TEST_ASSERT_EQ(fb.watched_data_write_attempts, 3);
    TEST_ASSERT_EQ(fb.failed_writes, 1);

    TEST_ASSERT(fb.watched_data == data);
    TEST_ASSERT_EQ(fb.watched_data_blocks, 4);
    TEST_ASSERT_EQ(fb.data_writes_until_failure, 0);
    /* Turn off payload matching before checking or syncing the filesystem. */
    fb.watched_data = NULL;
    TEST_ASSERT_EQ(fs.recovery_error, BFS_OK);
    TEST_ASSERT_EQ(file.extents.tree.free_sink_err, BFS_OK);
    TEST_ASSERT(append_file_state_matches(&fs, ino, 2u * BLK_SIZE, false,
                                          BFS_BLK_NULL, false, data,
                                          2u * BLK_SIZE));

    TEST_ASSERT(append_sync_remount_matches_file(
        &fb, &fs, ino, 2u * BLK_SIZE, false, BFS_BLK_NULL, false, data));
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_append_extent_write_failure_preserves_old_file(void)
{
    uint8_t original[BLK_SIZE], data[4u * BLK_SIZE];
    memset(original, 0x29, sizeof(original));
    for (uint32_t block = 0; block < 4; block++)
        memset(data + (size_t)block * BLK_SIZE, (int)(0x51u + block), BLK_SIZE);

    unlink(TEST_IMG);
    bfs_bio_t *bio;
    failing_bio_t fb;
    bfs_fs_t fs;
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(setup_append_file("AppendMetaFail", original, sizeof(original),
                                  &bio, &fb, &fs, &ino, &file));
    uint64_t original_size = file.size;
    bfs_blk_t original_extent_root = file.extents.tree.root;
    TEST_ASSERT_EQ(original_size, (uint64_t)BLK_SIZE);
    /* The one-block file lives in the inode. */
    TEST_ASSERT_EQ(original_extent_root, BFS_BLK_NULL);
    TEST_ASSERT_EQ(file.extents.inline_length, 1);
    bfs_blk_t original_start = file.extents.inline_start;

    /* A block of another file follows the inline extent, so the append cannot
     * extend it and must convert the mapping into a tree. */
    uint32_t gap_ino;
    bfs_file_t gap;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "gap", 3, &gap_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&gap, &fs, gap_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&gap, original, sizeof(original)), (int32_t)sizeof(original));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    /* Route only extent-tree I/O through a failing wrapper. Data and inode I/O
     * still use the filesystem BIO, whose payload matcher proves the batch data
     * writes completed before the conversion failed. */
    failing_bio_t extent_fault;
    TEST_ASSERT(append_fails_extent_write_after_payloads(&fb, &file, data, 4,
                                                         &extent_fault));
    TEST_ASSERT_EQ(file.extents.tree.root, original_extent_root);
    TEST_ASSERT_EQ(file.extents.inline_length, 1);
    TEST_ASSERT_EQ(file.extents.inline_start, original_start);
    TEST_ASSERT_EQ(file.size, original_size);
    TEST_ASSERT_EQ(file.extents.tree.free_sink_err, BFS_OK);
    TEST_ASSERT_EQ(fs.recovery_error, BFS_OK);

    TEST_ASSERT(fb.watched_data == data);
    TEST_ASSERT_EQ(fb.watched_data_blocks, 4);
    fb.watched_data = NULL;
    TEST_ASSERT(append_file_state_matches(&fs, ino, original_size, true,
                                          original_extent_root, true, original,
                                          BLK_SIZE));

    TEST_ASSERT(append_sync_remount_matches_file(
        &fb, &fs, ino, original_size, true, original_extent_root, true, original));
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("Hardware Failure Simulation")
    TEST_RUN(test_torn_write_superblock);
    TEST_RUN(test_torn_write_btree_node);
    TEST_RUN(test_read_error_during_lookup);
    TEST_RUN(test_read_error_during_scan);
    TEST_RUN(test_alloc_failure_during_split);
    TEST_RUN(test_double_free);
    TEST_RUN(test_scan_after_insert_before_cursor);
    TEST_RUN(test_max_tree_depth);
    TEST_RUN(test_both_superblocks_torn);
    TEST_RUN(test_format_dirty_partition);
    TEST_RUN(test_extent_read_error_is_not_a_sparse_hole);
    TEST_RUN(test_delete_inode_read_error_preserves_entry);
    TEST_RUN(test_delete_extent_read_error_preserves_entry);
    TEST_RUN(test_snapshot_refcount_read_error_blocks_overwrite);
    TEST_RUN(test_ordered_sync_error_propagates);
    TEST_RUN(test_failed_cow_write_preserves_tree);
    TEST_RUN(test_mount_propagates_superblock_io_error);
    TEST_RUN(test_recovery_invalidates_open_handles);
    TEST_RUN(test_failed_recovery_blocks_operations);
    TEST_RUN(test_persistent_delete_failure_requires_remount);
    TEST_RUN(test_failed_extent_rollback_marks_ownership_uncertain);
    TEST_RUN(test_owned_nodes_rewrite_in_place_until_publication);
    TEST_RUN(test_owned_write_failures_discard_only_uncommitted_state);
    TEST_RUN(test_delete_write_failures_preserve_comment);
    TEST_RUN(test_rmdir_write_failures_preserve_committed_directory);
    TEST_RUN(test_rename_write_failures_preserve_committed_paths);
    TEST_RUN(test_mkdir_write_failures_preserve_committed_tree);
    TEST_RUN(test_comment_write_failures_preserve_committed_value);
    TEST_RUN(test_snapshot_delete_write_failure_recovery);
    TEST_RUN(test_allocator_refill_failure_can_retry_free);
    TEST_RUN(test_reserve_return_root_fold_reactivates_emergency_root);
    TEST_RUN(test_reserve_return_root_fold_returns_ordinary_root);
    TEST_RUN(test_reserve_return_root_fold_first_write_failure_restores);
    TEST_RUN(test_reserve_return_root_fold_without_emergency_scratch_falls_back);
    TEST_RUN(test_reserve_return_root_fold_rejects_future_root);
    TEST_RUN(test_reserve_return_root_fold_rejects_committed_root);
    TEST_RUN(test_reserve_return_fold_falls_back_for_inactive_pool_alias);
    TEST_RUN(test_reserve_return_fold_rejects_active_pool_alias);
    TEST_RUN(test_reserve_return_batch_fragmented_accounting);
    TEST_RUN(test_reserve_return_batch_first_write_failure_is_retryable);
    TEST_RUN(test_reserve_return_batch_later_write_failure_keeps_ownership);
    TEST_RUN(test_reserve_return_mixed_batch_reactivates_emergency_slot);
    TEST_RUN(test_reserve_return_mixed_batch_first_write_failure_restores);
    TEST_RUN(test_reserve_return_mixed_batch_second_write_failure_keeps_ownership);
    TEST_RUN(test_reserve_return_write_failures_preserve_committed_tree);
    TEST_RUN(test_batch_leaf_write_failure_preserves_root);
    TEST_RUN(test_short_write_publishes_inode);
    TEST_RUN(test_append_data_write_failure_publishes_prefix);
    TEST_RUN(test_append_extent_write_failure_preserves_old_file);
TEST_SUITE_END()
