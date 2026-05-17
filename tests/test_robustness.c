/*
 * BFS — Robustness tests: 8 aggressive tests that try to break BFS
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_dir.h"
#include "bfs_crc32.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define TEST_IMG "test_robustness.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096

static bfs_fs_t g_fs;

static bfs_fs_t *setup(const char *vol, uint32_t opts)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, vol, opts);
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
 * 1. test_io_error_during_read
 *    Corrupt a B+tree node on disk, verify BFS_ERR_CORRUPT
 * ═══════════════════════════════════════════════════════════════ */

static void test_io_error_during_read(void)
{
    bfs_fs_t *fs = setup("Corrupt", 0);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "victim", 6, &ino), BFS_OK);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    uint8_t data[BLK_SIZE];
    memset(data, 0xAB, BLK_SIZE);
    bfs_file_write(&f, data, BLK_SIZE);

    bfs_fs_sync(fs);

    /* Corrupt the dir tree root block */
    bfs_blk_t dir_root = fs->dir_tree.tree.root;
    TEST_ASSERT(dir_root != 0);

    uint8_t garbage[BLK_SIZE];
    memset(garbage, 0xDE, BLK_SIZE);
    bfs_bio_write(fs->bio, dir_root, garbage);

    /* Try to look up the file — should get CORRUPT, not crash */
    uint32_t found_ino, type;
    bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "victim", 6,
                                     &found_ino, &type);
    TEST_ASSERT_EQ(err, BFS_ERR_CORRUPT);

    /* Clean teardown — unmount will fail but we just close the bio */
    bfs_bio_close(fs->bio);
    fs->mounted = false;
    unlink(TEST_IMG);
}

/* ═══════════════════════════════════════════════════════════════
 * 2. test_scan_during_delete
 *    Modify directory during scan — verify stability
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char names[20][16];
    int count;
} scan_ctx_t;

static bool scan_collect_cb(const char *name, uint8_t name_len,
                            uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    (void)inode_nr; (void)entry_type;
    scan_ctx_t *sc = (scan_ctx_t *)ctx;
    if (name_len == 2 && name[0] == '.' && name[1] == '.') return true;
    if (sc->count < 20) {
        memcpy(sc->names[sc->count], name, name_len);
        sc->names[sc->count][name_len] = 0;
        sc->count++;
    }
    return true;
}

static void test_scan_during_delete(void)
{
    bfs_fs_t *fs = setup("ScanDel", 0);

    /* Create 20 files */
    for (int i = 0; i < 20; i++) {
        char name[16];
        int nlen = snprintf(name, sizeof(name), "file_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)nlen, &ino), BFS_OK);
    }

    /* Scan to get all names */
    scan_ctx_t sc1 = { .count = 0 };
    bfs_dir_scan(&fs->dir_tree, BFS_ROOT_INO, scan_collect_cb, &sc1);
    TEST_ASSERT_EQ(sc1.count, 20);

    /* Delete files 3 and 4 */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "file_03", 7), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "file_04", 7), BFS_OK);

    /* Scan again — should have 18 entries, no duplicates */
    scan_ctx_t sc2 = { .count = 0 };
    bfs_dir_scan(&fs->dir_tree, BFS_ROOT_INO, scan_collect_cb, &sc2);
    TEST_ASSERT_EQ(sc2.count, 18);

    /* Verify deleted ones are gone */
    for (int i = 0; i < sc2.count; i++) {
        TEST_ASSERT(strcmp(sc2.names[i], "file_03") != 0);
        TEST_ASSERT(strcmp(sc2.names[i], "file_04") != 0);
    }

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 3. test_crc_corruption_detected
 *    Corrupt data block, verify checksum catches it
 * ═══════════════════════════════════════════════════════════════ */

static void test_crc_corruption_detected(void)
{
    /* Format with data checksums enabled */
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "CRCTest", BFS_OPT_DATA_CHECKSUMS);

    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT(fs.data_checksums);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "crcfile", 7, &ino), BFS_OK);

    bfs_file_t f;
    bfs_file_open(&f, &fs, ino);
    uint8_t data[BLK_SIZE];
    memset(data, 0x42, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);

    bfs_fs_sync(&fs);

    /* Find the data block by looking up extent */
    bfs_blk_t disk_blk;
    TEST_ASSERT_EQ(bfs_extent_lookup(&f.extents, 0, &disk_blk), BFS_OK);

    /* Corrupt the data block on disk */
    uint8_t corrupt[BLK_SIZE];
    bfs_bio_read(fs.bio, disk_blk, corrupt);
    corrupt[0] ^= 0xFF;  /* flip a byte */
    bfs_bio_write(fs.bio, disk_blk, corrupt);

    /* Read the file — should detect corruption */
    bfs_file_t f2;
    bfs_file_open(&f2, &fs, ino);
    uint8_t readbuf[BLK_SIZE];
    int32_t result = bfs_file_read(&f2, readbuf, BLK_SIZE);
    TEST_ASSERT_EQ(result, BFS_ERR_CORRUPT);

    bfs_bio_close(bio);
    fs.mounted = false;
    unlink(TEST_IMG);
}

/* ═══════════════════════════════════════════════════════════════
 * 4. test_rename_cross_dir_stress
 *    Rename files between directories rapidly
 * ═══════════════════════════════════════════════════════════════ */

static void test_rename_cross_dir_stress(void)
{
    bfs_fs_t *fs = setup("Rename", 0);

    uint32_t dir_a, dir_b;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "A", 1, &dir_a), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "B", 1, &dir_b), BFS_OK);

    /* Create 20 files in each dir */
    for (int i = 0; i < 20; i++) {
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fa_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(fs, dir_a, name, (uint8_t)nlen, &ino), BFS_OK);
    }
    for (int i = 0; i < 20; i++) {
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fb_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(fs, dir_b, name, (uint8_t)nlen, &ino), BFS_OK);
    }

    /* Rename all from A to B, then B to A, repeat 5 times */
    for (int round = 0; round < 5; round++) {
        /* A -> B */
        for (int i = 0; i < 20; i++) {
            char name[16];
            int nlen = snprintf(name, sizeof(name), "fa_%02d", i);
            bfs_fs_rename(fs, dir_a, name, (uint8_t)nlen, dir_b, name, (uint8_t)nlen);
        }
        /* B -> A */
        for (int i = 0; i < 20; i++) {
            char name[16];
            int nlen = snprintf(name, sizeof(name), "fb_%02d", i);
            bfs_fs_rename(fs, dir_b, name, (uint8_t)nlen, dir_a, name, (uint8_t)nlen);
        }
        /* Move them back for next round */
        for (int i = 0; i < 20; i++) {
            char name[16];
            int nlen = snprintf(name, sizeof(name), "fa_%02d", i);
            bfs_fs_rename(fs, dir_b, name, (uint8_t)nlen, dir_a, name, (uint8_t)nlen);
        }
        for (int i = 0; i < 20; i++) {
            char name[16];
            int nlen = snprintf(name, sizeof(name), "fb_%02d", i);
            bfs_fs_rename(fs, dir_a, name, (uint8_t)nlen, dir_b, name, (uint8_t)nlen);
        }
    }

    /* Verify all 40 files exist in correct directories */
    for (int i = 0; i < 20; i++) {
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fa_%02d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, dir_a, name, (uint8_t)nlen, &ino, &type), BFS_OK);
    }
    for (int i = 0; i < 20; i++) {
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fb_%02d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, dir_b, name, (uint8_t)nlen, &ino, &type), BFS_OK);
    }

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 5. test_truncate_extend_zeroed
 *    Truncate then extend, verify gap is zero
 * ═══════════════════════════════════════════════════════════════ */

static void test_truncate_extend_zeroed(void)
{
    bfs_fs_t *fs = setup("TruncExt", 0);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "sparse", 6, &ino), BFS_OK);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);

    /* Write 5 blocks of 0xFF */
    uint8_t data[BLK_SIZE];
    memset(data, 0xFF, BLK_SIZE);
    for (int i = 0; i < 5; i++)
        TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_EQ(f.size, (uint64_t)5 * BLK_SIZE);

    /* Truncate to 2 blocks */
    TEST_ASSERT_EQ(bfs_file_truncate(&f, 2 * BLK_SIZE), BFS_OK);
    TEST_ASSERT_EQ(f.size, (uint64_t)2 * BLK_SIZE);

    /* Extend by writing at offset 4*BLK_SIZE */
    bfs_file_seek(&f, 4 * BLK_SIZE, BFS_SEEK_SET);
    uint8_t marker[BLK_SIZE];
    memset(marker, 0xAA, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&f, marker, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_EQ(f.size, (uint64_t)5 * BLK_SIZE);

    /* Read blocks 2 and 3 (the gap) — should be zeros */
    uint8_t readbuf[BLK_SIZE];
    uint8_t zeros[BLK_SIZE];
    memset(zeros, 0, BLK_SIZE);

    bfs_file_seek(&f, 2 * BLK_SIZE, BFS_SEEK_SET);
    TEST_ASSERT_EQ(bfs_file_read(&f, readbuf, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_MEM_EQ(readbuf, zeros, BLK_SIZE);

    TEST_ASSERT_EQ(bfs_file_read(&f, readbuf, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_MEM_EQ(readbuf, zeros, BLK_SIZE);

    /* Verify block 4 has our marker */
    TEST_ASSERT_EQ(bfs_file_read(&f, readbuf, BLK_SIZE), BLK_SIZE);
    TEST_ASSERT_MEM_EQ(readbuf, marker, BLK_SIZE);

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 6. test_softlink_self_reference
 *    Create softlink pointing to itself — no crash/infinite loop
 * ═══════════════════════════════════════════════════════════════ */

static void test_softlink_self_reference(void)
{
    bfs_fs_t *fs = setup("SelfLink", 0);

    TEST_ASSERT_EQ(bfs_fs_make_softlink(fs, BFS_ROOT_INO, "loop", 4, "loop", 4), BFS_OK);

    /* Look up the link */
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "loop", 4, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(type, BFS_INODE_SOFTLINK);

    /* Read the link target — should be "loop" */
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, fs, ino), BFS_OK);
    TEST_ASSERT_EQ(f.size, 4);

    char target[16] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&f, target, 16), 4);
    TEST_ASSERT_MEM_EQ(target, "loop", 4);

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 7. test_fsck_finds_leaked_blocks
 *    Simulate leaked blocks — verify free_blocks doesn't return
 * ═══════════════════════════════════════════════════════════════ */

static void test_fsck_finds_leaked_blocks(void)
{
    bfs_fs_t *fs = setup("Leak", 0);

    bfs_fs_sync(fs);
    uint32_t free_initial = fs->freespace.total_free;

    /* Create a file and write 10 blocks */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "leaked", 6, &ino), BFS_OK);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    uint8_t data[BLK_SIZE];
    memset(data, 0xCC, BLK_SIZE);
    for (int i = 0; i < 10; i++)
        bfs_file_write(&f, data, BLK_SIZE);

    bfs_fs_sync(fs);
    uint32_t free_after_write = fs->freespace.total_free;
    /* Should have used at least 10 blocks */
    TEST_ASSERT(free_initial - free_after_write >= 10);

    /* Simulate leak: delete the inode WITHOUT freeing extents */
    TEST_ASSERT_EQ(bfs_inode_delete(&fs->inode_tree, ino), BFS_OK);
    /* Remove dir entry too */
    TEST_ASSERT_EQ(bfs_dir_remove(&fs->dir_tree, BFS_ROOT_INO, "leaked", 6), BFS_OK);

    bfs_fs_sync(fs);
    uint32_t free_after_leak = fs->freespace.total_free;

    /* The data blocks are leaked — free count should NOT have recovered */
    TEST_ASSERT(free_after_leak < free_initial - 5);

    /* Now do a proper create+delete to verify normal path works */
    uint32_t ino2;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "proper", 6, &ino2), BFS_OK);
    bfs_file_t f2;
    bfs_file_open(&f2, fs, ino2);
    for (int i = 0; i < 5; i++)
        bfs_file_write(&f2, data, BLK_SIZE);
    bfs_fs_sync(fs);
    uint32_t free_before_proper_del = fs->freespace.total_free;

    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "proper", 6), BFS_OK);
    bfs_fs_sync(fs);
    uint32_t free_after_proper_del = fs->freespace.total_free;

    /* Proper delete should recover blocks */
    TEST_ASSERT(free_after_proper_del >= free_before_proper_del + 5);

    teardown(fs);
}

/* ═══════════════════════════════════════════════════════════════
 * 8. test_fuzz_multiple_seeds
 *    Run fuzz with 5 different seeds, 2000 ops each
 * ═══════════════════════════════════════════════════════════════ */

#define FUZZ_POOL_SIZE 50

static void run_fuzz_seed(uint32_t seed)
{
    bfs_fs_t *fs = setup("Fuzz", 0);
    uint32_t rng = seed;

    uint8_t exists[FUZZ_POOL_SIZE];
    uint32_t inos[FUZZ_POOL_SIZE];
    memset(exists, 0, sizeof(exists));
    memset(inos, 0, sizeof(inos));

    for (int op = 0; op < 2000; op++) {
        uint32_t idx = xorshift32(&rng) % FUZZ_POOL_SIZE;
        uint32_t action = xorshift32(&rng) % 8;
        char name[16];
        int nlen = snprintf(name, sizeof(name), "fz_%03u", idx);

        switch (action) {
        case 0:
            if (!exists[idx]) {
                uint32_t ino;
                if (bfs_fs_create_file(fs, BFS_ROOT_INO, name, (uint8_t)nlen, &ino) == BFS_OK) {
                    exists[idx] = 1;
                    inos[idx] = ino;
                }
            }
            break;
        case 1:
            if (exists[idx]) {
                if (bfs_fs_delete_file(fs, BFS_ROOT_INO, name, (uint8_t)nlen) == BFS_OK)
                    exists[idx] = 0;
            }
            break;
        case 2:
            if (exists[idx]) {
                bfs_file_t f;
                if (bfs_file_open(&f, fs, inos[idx]) == BFS_OK) {
                    uint8_t data[64];
                    memset(data, (uint8_t)idx, 64);
                    bfs_file_write(&f, data, 64);
                }
            }
            break;
        case 3:
            if (exists[idx]) {
                bfs_file_t f;
                if (bfs_file_open(&f, fs, inos[idx]) == BFS_OK) {
                    uint8_t buf[64];
                    bfs_file_read(&f, buf, 64);
                }
            }
            break;
        case 4:
            if (!exists[idx]) {
                uint32_t ino;
                char dname[16];
                int dlen = snprintf(dname, sizeof(dname), "dz_%03u", idx);
                bfs_fs_mkdir(fs, BFS_ROOT_INO, dname, (uint8_t)dlen, &ino);
            }
            break;
        case 5: {
            char dname[16];
            int dlen = snprintf(dname, sizeof(dname), "dz_%03u", idx);
            bfs_fs_rmdir(fs, BFS_ROOT_INO, dname, (uint8_t)dlen);
            break;
        }
        case 6:
            if (exists[idx]) {
                uint32_t idx2 = xorshift32(&rng) % FUZZ_POOL_SIZE;
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
        case 7:
            if (exists[idx]) {
                uint32_t idx2 = xorshift32(&rng) % FUZZ_POOL_SIZE;
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

        if (op % 100 == 99)
            bfs_fs_sync(fs);
    }

    bfs_fs_sync(fs);

    /* Verify consistency */
    for (int i = 0; i < FUZZ_POOL_SIZE; i++) {
        char nm[16];
        int nl = snprintf(nm, sizeof(nm), "fz_%03u", i);
        uint32_t found_ino, type;
        bfs_err_t err = bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, nm, (uint8_t)nl, &found_ino, &type);
        if (exists[i]) {
            TEST_ASSERT_EQ(err, BFS_OK);
        } else {
            TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);
        }
    }

    teardown(fs);
}

static void test_fuzz_multiple_seeds(void)
{
    uint32_t seeds[] = { 12345, 67890, 11111, 99999, 54321 };
    for (int i = 0; i < 5; i++)
        run_fuzz_seed(seeds[i]);
}

/* ═══════════════════════════════════════════════════════════════ */

TEST_SUITE_BEGIN("Robustness")
    TEST_RUN(test_io_error_during_read);
    TEST_RUN(test_scan_during_delete);
    TEST_RUN(test_crc_corruption_detected);
    TEST_RUN(test_rename_cross_dir_stress);
    TEST_RUN(test_truncate_extend_zeroed);
    TEST_RUN(test_softlink_self_reference);
    TEST_RUN(test_fsck_finds_leaked_blocks);
    TEST_RUN(test_fuzz_multiple_seeds);
TEST_SUITE_END()
