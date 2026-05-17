/*
 * BFS — Edge case, stress, crash safety, and fuzz tests
 * Goal: find bugs the existing 85 tests miss.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_dir.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define TEST_IMG "test_edge_cases.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192
#define BLK_COUNT_SMALL 512

static bfs_fs_t g_fs;

static bfs_fs_t *setup(const char *vol, uint32_t blk_count)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, blk_count);
    bfs_fs_format(bio, vol, 0);
    bfs_fs_mount(&g_fs, bio);
    return &g_fs;
}

static void teardown(bfs_fs_t *fs)
{
    bfs_bio_t *bio = fs->bio;
    bfs_fs_unmount(fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* ═══════════════════════════════════════════════════════════════
 * 1. BOUNDARY / EDGE CASES
 * ═══════════════════════════════════════════════════════════════ */

static void test_max_filename_255_bytes(void)
{
    bfs_fs_t *fs = setup("MaxName", BLK_COUNT);
    char name[255];
    memset(name, 'A', 255);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, 255, &ino), BFS_OK);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, 255, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, ino);

    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, name, 255), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, 255, &found_ino, &type), BFS_ERR_NOTFOUND);

    teardown(fs);
}

static void test_zero_length_file(void)
{
    bfs_fs_t *fs = setup("ZeroLen", BLK_COUNT);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "empty", 5, &ino), BFS_OK);

    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, fs, ino), BFS_OK);
    TEST_ASSERT_EQ(f.size, 0);

    char buf[16];
    TEST_ASSERT_EQ(bfs_file_read(&f, buf, 16), 0);
    TEST_ASSERT_EQ(bfs_file_write(&f, buf, 0), 0);
    TEST_ASSERT_EQ(f.size, 0);
    TEST_ASSERT_EQ(bfs_file_truncate(&f, 0), BFS_OK);
    TEST_ASSERT_EQ(f.size, 0);

    teardown(fs);
}

static void test_single_byte_file(void)
{
    bfs_fs_t *fs = setup("OneByte", BLK_COUNT);
    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "one", 3, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    uint8_t w = 0x42;
    TEST_ASSERT_EQ(bfs_file_write(&f, &w, 1), 1);
    TEST_ASSERT_EQ(f.size, 1);

    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    uint8_t r = 0;
    TEST_ASSERT_EQ(bfs_file_read(&f, &r, 1), 1);
    TEST_ASSERT_EQ(r, 0x42);

    teardown(fs);
}

static void test_file_exactly_one_block(void)
{
    bfs_fs_t *fs = setup("OneBlock", BLK_COUNT);
    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "blk", 3, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    uint8_t data[BLK_SIZE];
    memset(data, 0xAB, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_EQ(f.size, (uint64_t)BLK_SIZE);

    /* Block 0 should exist, block 1 should not */
    bfs_blk_t dblk;
    TEST_ASSERT_EQ(bfs_extent_lookup(&f.extents, 0, &dblk), BFS_OK);
    TEST_ASSERT_EQ(bfs_extent_lookup(&f.extents, 1, &dblk), BFS_ERR_NOTFOUND);

    teardown(fs);
}

static void test_file_block_boundary_write(void)
{
    bfs_fs_t *fs = setup("Boundary", BLK_COUNT);
    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "bnd", 3, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    bfs_file_seek(&f, BLK_SIZE - 1, BFS_SEEK_SET);
    uint8_t w[2] = {0xDE, 0xAD};
    TEST_ASSERT_EQ(bfs_file_write(&f, w, 2), 2);
    TEST_ASSERT_EQ(f.size, (uint64_t)(BLK_SIZE + 1));

    bfs_file_seek(&f, BLK_SIZE - 1, BFS_SEEK_SET);
    uint8_t r[2] = {0, 0};
    TEST_ASSERT_EQ(bfs_file_read(&f, r, 2), 2);
    TEST_ASSERT_EQ(r[0], 0xDE);
    TEST_ASSERT_EQ(r[1], 0xAD);

    teardown(fs);
}

static bool empty_scan_cb(const char *name, uint8_t name_len,
                          uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    (void)inode_nr; (void)entry_type;
    int *count = (int *)ctx;
    /* Skip '..' */
    if (name_len == 2 && name[0] == '.' && name[1] == '.') {
        (*count)++; /* count dotdot separately */
        return true;
    }
    (*count) += 100; /* real entry = big number */
    return true;
}

static void test_empty_directory_scan(void)
{
    bfs_fs_t *fs = setup("EmptyDir", BLK_COUNT);
    uint32_t dir_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "empty", 5, &dir_ino), BFS_OK);

    int count = 0;
    bfs_dir_scan(&fs->dir_tree, dir_ino, empty_scan_cb, &count);
    /* Should only find '..' (count=1), no real entries */
    TEST_ASSERT_EQ(count, 1);

    teardown(fs);
}

static void test_delete_nonexistent(void)
{
    bfs_fs_t *fs = setup("DelNone", BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "ghost", 5), BFS_ERR_NOTFOUND);
    teardown(fs);
}

static void test_create_in_nonexistent_dir(void)
{
    bfs_fs_t *fs = setup("BadParent", BLK_COUNT);
    uint32_t ino;
    /* Parent inode 9999 doesn't exist as a directory — but create_file
     * just inserts into the dir tree with that parent_id. The file should
     * be creatable but not findable via normal traversal from root.
     * This tests that the system doesn't crash. */
    bfs_err_t err = bfs_fs_create_file(fs, 9999, "orphan", 6, &ino);
    /* Should succeed (dir tree doesn't validate parent existence) or
     * return an error — either way, no crash */
    (void)err;
    teardown(fs);
}

static void test_rmdir_with_dotdot_only(void)
{
    bfs_fs_t *fs = setup("RmDotDot", BLK_COUNT);
    uint32_t dir_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "sub", 3, &dir_ino), BFS_OK);

    /* Verify '..' exists */
    uint32_t parent, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, dir_ino, "..", 2, &parent, &type), BFS_OK);

    /* rmdir should succeed — '..' is not a real child */
    TEST_ASSERT_EQ(bfs_fs_rmdir(fs, BFS_ROOT_INO, "sub", 3), BFS_OK);

    /* Directory should be gone from parent */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "sub", 3, &parent, &type), BFS_ERR_NOTFOUND);

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 2. STRESS / EXHAUSTION
 * ═══════════════════════════════════════════════════════════════ */

static void test_fill_disk_completely(void)
{
    bfs_fs_t *fs = setup("FillDisk", BLK_COUNT_SMALL);

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "fill", 4, &ino);
    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    uint8_t data[BLK_SIZE];
    memset(data, 0xFF, BLK_SIZE);
    uint32_t written = 0;
    while (bfs_file_write(&f, data, BLK_SIZE) == BLK_SIZE)
        written++;
    TEST_ASSERT(written > 0);

    /* Free some space by truncating */
    uint64_t half = (uint64_t)(written / 2) * BLK_SIZE;
    TEST_ASSERT_EQ(bfs_file_truncate(&f, half), BFS_OK);

    /* Should be able to allocate again */
    bfs_file_seek(&f, (int64_t)half, BFS_SEEK_SET);
    bfs_fs_sync(fs); /* reclaim truncated blocks */
    int32_t n = bfs_file_write(&f, data, BLK_SIZE);
    TEST_ASSERT(n > 0);

    teardown(fs);
}

static void test_many_hardlinks_same_file(void)
{
    bfs_fs_t *fs = setup("ManyLinks", BLK_COUNT);

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "target", 6, &ino);

    /* Write some data */
    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    bfs_file_write(&f, "data", 4);

    /* Create 50 hard links */
    for (int i = 0; i < 50; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "link_%02d", i);
        TEST_ASSERT_EQ(bfs_fs_make_hardlink(fs, BFS_ROOT_INO, name, (uint8_t)len, ino), BFS_OK);
    }

    /* Verify link_count = 51 (1 original + 50 links) */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 51);

    /* Delete links one by one, verify count decreases */
    for (int i = 0; i < 50; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "link_%02d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK); if (i % 50 == 49) bfs_fs_sync(fs);

        TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, ino, &inode), BFS_OK);
        TEST_ASSERT_EQ(bfs_be32(inode.link_count), (uint32_t)(50 - i));
    }

    /* Delete original — inode should be freed */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "target", 6), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, ino, &inode), BFS_ERR_NOTFOUND);

    teardown(fs);
}

static void test_deeply_nested_path(void)
{
    bfs_fs_t *fs = setup("DeepNest", BLK_COUNT);

    uint32_t parent = BFS_ROOT_INO;
    for (int depth = 0; depth < 50; depth++) {
        char name[8];
        int len = snprintf(name, sizeof(name), "d%02d", depth);
        uint32_t dir_ino;
        TEST_ASSERT_EQ(bfs_fs_mkdir(fs, parent, name, (uint8_t)len, &dir_ino), BFS_OK);
        parent = dir_ino;
    }

    /* Create file at depth 50 */
    uint32_t file_ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, parent, "deep.txt", 8, &file_ino), BFS_OK);

    /* Verify it exists */
    uint32_t found, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, parent, "deep.txt", 8, &found, &type), BFS_OK);
    TEST_ASSERT_EQ(found, file_ino);

    teardown(fs);
}

static void test_many_small_files(void)
{
    bfs_fs_t *fs = setup("ManySmall", BLK_COUNT);

    uint32_t free_before = fs->freespace.total_free;
    uint32_t inos[500];

    /* Create 500 1-byte files */
    for (int i = 0; i < 500; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "f%03d", i);
        TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)len, &inos[i]), BFS_OK);

        bfs_file_t f;
        bfs_file_open(&f, fs, inos[i]);
        uint8_t b = (uint8_t)(i & 0xFF);
        TEST_ASSERT_EQ(bfs_file_write(&f, &b, 1), 1);
    }

    /* Verify all */
    for (int i = 0; i < 500; i++) {
        bfs_file_t f;
        bfs_file_open(&f, fs, inos[i]);
        uint8_t b = 0;
        TEST_ASSERT_EQ(bfs_file_read(&f, &b, 1), 1);
        TEST_ASSERT_EQ(b, (uint8_t)(i & 0xFF));
    }

    /* Delete all */
    for (int i = 0; i < 500; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "f%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK); if (i % 50 == 49) bfs_fs_sync(fs);
    }

    /* Sync to reclaim pending frees */
    bfs_fs_sync(fs);

    /* Free space should be approximately recovered */
    uint32_t free_after = fs->freespace.total_free;
    /* Allow some overhead for metadata, but most should be back */
    TEST_ASSERT(free_after >= free_before - 20);

    teardown(fs);
}

static void test_large_file_many_extents(void)
{
    bfs_fs_t *fs = setup("ManyExt", BLK_COUNT);

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "sparse", 6, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    /* Write 200 blocks with gaps: write block 0, skip 1, write 2, skip 3... */
    uint8_t data[BLK_SIZE];
    for (int i = 0; i < 200; i++) {
        memset(data, (uint8_t)(i & 0xFF), BLK_SIZE);
        int64_t off = (int64_t)i * 2 * BLK_SIZE;
        bfs_file_seek(&f, off, BFS_SEEK_SET);
        TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);
    }

    /* Read all back and verify */
    for (int i = 0; i < 200; i++) {
        int64_t off = (int64_t)i * 2 * BLK_SIZE;
        bfs_file_seek(&f, off, BFS_SEEK_SET);
        TEST_ASSERT_EQ(bfs_file_read(&f, data, BLK_SIZE), BLK_SIZE);
        TEST_ASSERT_EQ(data[0], (uint8_t)(i & 0xFF));
        TEST_ASSERT_EQ(data[BLK_SIZE - 1], (uint8_t)(i & 0xFF));
    }

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 3. CRASH SAFETY / CONSISTENCY
 * ═══════════════════════════════════════════════════════════════ */

static void test_delete_frees_exact_blocks(void)
{
    bfs_fs_t *fs = setup("FreeCnt", BLK_COUNT);

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "exact", 5, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    uint8_t data[BLK_SIZE];
    memset(data, 0xCC, BLK_SIZE);
    for (int i = 0; i < 10; i++)
        bfs_file_write(&f, data, BLK_SIZE);

    /* Sync to stabilize COW state */
    bfs_fs_sync(fs);
    uint32_t free_before_delete = fs->freespace.total_free;

    /* Delete the file */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "exact", 5), BFS_OK);

    /* The 10 data blocks should be freed immediately */
    bfs_fs_sync(fs); /* reclaim deferred freed blocks */
    uint32_t free_after_delete = fs->freespace.total_free;
    /* At minimum, 10 data blocks should be freed (COW may add pending frees) */
    TEST_ASSERT(free_after_delete >= free_before_delete + 10);

    teardown(fs);
}

static void test_inode_persists_after_write(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "Persist", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "persist.dat", 11, &ino);

    bfs_file_t f;
    bfs_file_open(&f, &fs, ino);
    const char *msg = "Hello persistent world!";
    TEST_ASSERT_EQ(bfs_file_write(&f, msg, 23), 23);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Verify file exists and has correct size */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    uint64_t size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
    TEST_ASSERT_EQ(size, 23);

    /* Verify data */
    bfs_file_open(&f, &fs, ino);
    char buf[32] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&f, buf, 32), 23);
    TEST_ASSERT_MEM_EQ(buf, msg, 23);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_hardlink_survives_remount(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "HLPersist", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "orig", 4, &ino);
    bfs_file_t f;
    bfs_file_open(&f, &fs, ino);
    bfs_file_write(&f, "linkdata", 8);

    TEST_ASSERT_EQ(bfs_fs_make_hardlink(&fs, BFS_ROOT_INO, "hlink", 5, ino), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    uint32_t ino1, ino2, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "orig", 4, &ino1, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "hlink", 5, &ino2, &type), BFS_OK);
    TEST_ASSERT_EQ(ino1, ino2);

    /* Verify data via link */
    bfs_file_open(&f, &fs, ino2);
    char buf[16] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&f, buf, 16), 8);
    TEST_ASSERT_MEM_EQ(buf, "linkdata", 8);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_comment_survives_remount(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "CmtPersist", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "noted", 5, &ino);
    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "Important file!", 15), BFS_OK);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    char buf[80] = {0};
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, buf, 80), BFS_OK);
    TEST_ASSERT_MEM_EQ(buf, "Important file!", 15);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ═══════════════════════════════════════════════════════════════
 * 4. FUZZ TEST
 * ═══════════════════════════════════════════════════════════════ */

static void test_random_operations_fuzz(void)
{
    bfs_fs_t *fs = setup("Fuzz", BLK_COUNT);
    uint32_t rng = 0xDEADBEEF;

    #define POOL_SIZE 100
    uint8_t exists[POOL_SIZE];
    uint32_t inos[POOL_SIZE];
    memset(exists, 0, sizeof(exists));
    memset(inos, 0, sizeof(inos));

    for (int op = 0; op < 5000; op++) {
        uint32_t idx = xorshift32(&rng) % POOL_SIZE;
        uint32_t action = xorshift32(&rng) % 8;
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fz_%03u", idx);

        switch (action) {
        case 0: /* create file */
            if (!exists[idx]) {
                uint32_t ino;
                if (bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)nlen, &ino) == BFS_OK) {
                    exists[idx] = 1;
                    inos[idx] = ino;
                }
            }
            break;
        case 1: /* delete file */
            if (exists[idx]) {
                if (bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)nlen) == BFS_OK)
                    exists[idx] = 0;
            }
            break;
        case 2: /* write */
            if (exists[idx]) {
                bfs_file_t f;
                if (bfs_file_open(&f, fs, inos[idx]) == BFS_OK) {
                    uint8_t data[64];
                    memset(data, (uint8_t)idx, 64);
                    bfs_file_write(&f, data, 64);
                }
            }
            break;
        case 3: /* read */
            if (exists[idx]) {
                bfs_file_t f;
                if (bfs_file_open(&f, fs, inos[idx]) == BFS_OK) {
                    uint8_t buf[64];
                    bfs_file_read(&f, buf, 64);
                }
            }
            break;
        case 4: /* mkdir */
            if (!exists[idx]) {
                uint32_t ino;
                char dname[16];
                int dlen = snprintf(dname, sizeof(dname), "dz_%03u", idx);
                if (bfs_fs_mkdir(fs, BFS_ROOT_INO, dname, (uint8_t)dlen, &ino) == BFS_OK) {
                    /* don't track dirs in exists[] to keep it simple */
                }
            }
            break;
        case 5: /* rmdir */
        {
            char dname[16];
            int dlen = snprintf(dname, sizeof(dname), "dz_%03u", idx);
            bfs_fs_rmdir(fs, BFS_ROOT_INO, dname, (uint8_t)dlen);
            break;
        }
        case 6: /* rename */
            if (exists[idx]) {
                uint32_t idx2 = xorshift32(&rng) % POOL_SIZE;
                if (!exists[idx2]) {
                    char name2[16];
                    int nlen2 = snprintf(name2, sizeof(name2), "fz_%03u", idx2);
                    if (bfs_fs_rename(fs, BFS_ROOT_INO, name, (uint8_t)nlen,
                                       BFS_ROOT_INO, name2, (uint8_t)nlen2) == BFS_OK) {
                        exists[idx] = 0;
                        exists[idx2] = 1;
                        inos[idx2] = inos[idx];
                    }
                }
            }
            break;
        case 7: /* hardlink */
            if (exists[idx]) {
                uint32_t idx2 = xorshift32(&rng) % POOL_SIZE;
                if (!exists[idx2]) {
                    char name2[16];
                    int nlen2 = snprintf(name2, sizeof(name2), "fz_%03u", idx2);
                    if (bfs_fs_make_hardlink(fs, BFS_ROOT_INO, name2, (uint8_t)nlen2, inos[idx]) == BFS_OK) {
                        exists[idx2] = 1;
                        inos[idx2] = inos[idx];
                    }
                }
            }
            break;
        }

        /* Periodic sync */
        if (op % 50 == 49)
            bfs_fs_sync(fs);
    }

    /* Final sync */
    bfs_fs_sync(fs);

    /* Verify consistency: every file marked as existing should be findable */
    for (int i = 0; i < POOL_SIZE; i++) {
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fz_%03u", i);
        uint32_t found_ino, type;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, name, (uint8_t)nlen, &found_ino, &type);
        if (exists[i]) {
            TEST_ASSERT_EQ(err, BFS_OK);
            /* Should be openable */
            bfs_file_t f;
            bfs_file_open(&f, fs, found_ino);
        } else {
            TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);
        }
    }

    teardown(fs);
    #undef POOL_SIZE
}

/* ═══════════════════════════════════════════════════════════════ */

TEST_SUITE_BEGIN("Edge Cases")
    /* Boundary */
    TEST_RUN(test_max_filename_255_bytes);
    TEST_RUN(test_zero_length_file);
    TEST_RUN(test_single_byte_file);
    TEST_RUN(test_file_exactly_one_block);
    TEST_RUN(test_file_block_boundary_write);
    TEST_RUN(test_empty_directory_scan);
    TEST_RUN(test_delete_nonexistent);
    TEST_RUN(test_create_in_nonexistent_dir);
    TEST_RUN(test_rmdir_with_dotdot_only);
    /* Stress */
    TEST_RUN(test_fill_disk_completely);
    TEST_RUN(test_many_hardlinks_same_file);
    TEST_RUN(test_deeply_nested_path);
    TEST_RUN(test_many_small_files);
    TEST_RUN(test_large_file_many_extents);
    /* Crash safety */
    TEST_RUN(test_delete_frees_exact_blocks);
    TEST_RUN(test_inode_persists_after_write);
    TEST_RUN(test_hardlink_survives_remount);
    TEST_RUN(test_comment_survives_remount);
    /* Fuzz */
    TEST_RUN(test_random_operations_fuzz);
TEST_SUITE_END()
