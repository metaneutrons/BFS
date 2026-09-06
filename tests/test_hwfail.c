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
} failing_bio_t;

static bfs_err_t fail_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf) {
    failing_bio_t *fb = (failing_bio_t *)bio;
    if (fb->fail_all_reads || blk == fb->fail_block) return BFS_ERR_IO;
    return bfs_bio_read(fb->inner, blk, buf);
}
static bfs_err_t fail_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf) {
    failing_bio_t *fb = (failing_bio_t *)bio;
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
    TEST_ASSERT_EQ(bfs_file_write(&file, "payload", 7), 7);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

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
    TEST_ASSERT_EQ(bfs_file_write(&file, "payload", 7), 7);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

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
    TEST_ASSERT_EQ(bfs_file_write(&file, "original", 8), 8);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_freespace_refill_reserve(&fs.freespace), BFS_OK);
    bfs_blk_t replacement = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(replacement != BFS_BLK_NULL);
    /* Removing the sole leaf key needs no write. Both inserts then fail. */
    fb.fail_all_writes = true;
    TEST_ASSERT_EQ(bfs_extent_remap_block(&file.extents, 0, replacement, NULL), BFS_ERR_IO);
    TEST_ASSERT_EQ(file.extents.tree.free_sink_err, BFS_ERR_IO);
    TEST_ASSERT(fb.failed_writes >= 2);
    bfs_fs_abandon(&fs);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    char data[8];
    TEST_ASSERT_EQ(bfs_file_read(&file, data, sizeof(data)), sizeof(data));
    TEST_ASSERT_MEM_EQ(data, "original", sizeof(data));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_delete_write_failures_preserve_comment(void)
{
    for (uint32_t fail_at = 1; fail_at <= 16; fail_at++) {
        unlink(TEST_IMG);
        bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
        TEST_ASSERT(bio != NULL);
        TEST_ASSERT_EQ(bfs_fs_format(bio, "DeleteFault", 0), BFS_OK);
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
        /* The operation consumes the countdown through fail_write. */
        // cppcheck-suppress redundantAssignment
        fb.writes_until_failure = 0;
        TEST_ASSERT(err == BFS_OK || err == BFS_ERR_IO);
        if (err != BFS_OK) {
            TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "file", 4, NULL, NULL), BFS_OK);
            char comment[80];
            TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_OK);
            TEST_ASSERT_MEM_EQ(comment, "keep", 5);
        }
        TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
        bfs_bio_close(bio);
    }
    unlink(TEST_IMG);
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
    TEST_RUN(test_delete_write_failures_preserve_comment);
    TEST_RUN(test_snapshot_delete_write_failure_recovery);
    TEST_RUN(test_allocator_refill_failure_can_retry_free);
    TEST_RUN(test_short_write_publishes_inode);
TEST_SUITE_END()
