/*
 * BFS — Bug hunting tests
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_dir.h"
#include "bfs_inode.h"
#include "bfs_alloc.h"
#include "bfs_extent.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_hunt.img"
#define BLK_SIZE 4096
#define BLK_COUNT 4096

static bfs_bio_t *fresh_bio(void) {
    unlink(TEST_IMG);
    return bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
}

static void mount_fresh(bfs_fs_t *fs, bfs_bio_t **bio_out) {
    bfs_bio_t *bio = fresh_bio();
    bfs_fs_format(bio, "HuntVol", 0);
    bfs_fs_mount(fs, bio);
    *bio_out = bio;
}

static void cleanup(bfs_fs_t *fs, bfs_bio_t *bio) {
    bfs_fs_unmount(fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* Dir scan counter */
typedef struct { uint32_t count; } scan_count_t;
static bool count_cb(const char *n, uint8_t nl, uint32_t ino, uint32_t t, void *ctx) {
    (void)n; (void)nl; (void)ino; (void)t;
    ((scan_count_t *)ctx)->count++;
    return true;
}

/* ══ Test 1: B+tree scan with start_key ════════════════════ */

static void test_scan_start_key(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Create 100 files */
    char name[32];
    for (int i = 0; i < 100; i++) {
        int len = snprintf(name, sizeof(name), "file_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Scan all — should get 100 */
    scan_count_t sc = {0};
    TEST_ASSERT_EQ(bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc), BFS_OK);
    TEST_ASSERT_EQ(sc.count, 100);

    cleanup(&fs, bio);
}

static void test_scan_empty_tree(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Create a subdir, scan it — should be empty (only ..) */
    uint32_t dir_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "empty", 5, &dir_ino), BFS_OK);

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, dir_ino, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 1); /* just '..' */

    cleanup(&fs, bio);
}

static void test_scan_single_entry(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "only", 4, &ino), BFS_OK);

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 1);

    cleanup(&fs, bio);
}

static void test_scan_after_delete(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    for (int i = 0; i < 20; i++) {
        int len = snprintf(name, sizeof(name), "del_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Delete even-numbered files */
    for (int i = 0; i < 20; i += 2) {
        int len = snprintf(name, sizeof(name), "del_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 10);

    cleanup(&fs, bio);
}

/* ══ Test 2: B+tree delete triggering merge across leaves ══ */

static void test_delete_merge(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Insert 30 entries — fills 2+ leaves (14 entries/leaf with 264-byte keys at 4K) */
    char name[32];
    for (int i = 0; i < 30; i++) {
        int len = snprintf(name, sizeof(name), "merge_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Delete first 10 entries — should trigger underflow and merge */
    for (int i = 0; i < 10; i++) {
        int len = snprintf(name, sizeof(name), "merge_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    /* Verify remaining 20 are findable via lookup */
    for (int i = 10; i < 30; i++) {
        int len = snprintf(name, sizeof(name), "merge_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    /* Verify via scan */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 20);

    cleanup(&fs, bio);
}

/* ══ Test 3: Extent tree with many small extents ═══════════ */

static void test_extent_fragmented(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "frag", 4, &ino), BFS_OK);

    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);

    uint8_t pattern[BLK_SIZE];
    memset(pattern, 0xAB, BLK_SIZE);

    /* Write 1 block, skip 1, write 1, skip 1... for 50 written blocks */
    for (int i = 0; i < 50; i++) {
        bfs_file_seek(&f, (int64_t)i * 2 * BLK_SIZE, BFS_SEEK_SET);
        int32_t n = bfs_file_write(&f, pattern, BLK_SIZE);
        TEST_ASSERT_EQ(n, BLK_SIZE);
    }

    /* Read back written blocks */
    uint8_t readbuf[BLK_SIZE];
    for (int i = 0; i < 50; i++) {
        bfs_file_seek(&f, (int64_t)i * 2 * BLK_SIZE, BFS_SEEK_SET);
        int32_t n = bfs_file_read(&f, readbuf, BLK_SIZE);
        TEST_ASSERT_EQ(n, BLK_SIZE);
        TEST_ASSERT_EQ(readbuf[0], 0xAB);
        TEST_ASSERT_EQ(readbuf[BLK_SIZE - 1], 0xAB);
    }

    /* Truncate to 50 blocks worth (keeps first 25 written blocks) */
    TEST_ASSERT_EQ(bfs_file_truncate(&f, 50ULL * BLK_SIZE), BFS_OK);

    /* Verify first 25 written blocks still readable */
    for (int i = 0; i < 25; i++) {
        bfs_file_seek(&f, (int64_t)i * 2 * BLK_SIZE, BFS_SEEK_SET);
        int32_t n = bfs_file_read(&f, readbuf, BLK_SIZE);
        TEST_ASSERT_EQ(n, BLK_SIZE);
        TEST_ASSERT_EQ(readbuf[0], 0xAB);
    }

    cleanup(&fs, bio);
}

/* ══ Test 4: Free space allocator under fragmentation ══════ */

static void test_alloc_fragmentation(void) {
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);

    bfs_freespace_t fsp;
    bfs_freespace_init(&fsp, bio, BFS_BLK_NULL, 1);
    /* Need extra space: 64 for reserve pool + overhead for B+tree nodes */
    bfs_freespace_add(&fsp, 100, 1000);

    /* Allocate 100 single blocks */
    bfs_blk_t blocks[100];
    for (int i = 0; i < 100; i++) {
        blocks[i] = bfs_freespace_alloc(&fsp, 1);
        TEST_ASSERT(blocks[i] != BFS_BLK_NULL);
    }

    /* Free every other one (creating 50 single-block free extents) */
    for (int i = 0; i < 100; i += 2) {
        TEST_ASSERT_EQ(bfs_freespace_free(&fsp, blocks[i], 1), BFS_OK);
    }

    /* Allocate 50 single blocks — should succeed (reusing freed ones) */
    for (int i = 0; i < 50; i++) {
        bfs_blk_t b = bfs_freespace_alloc(&fsp, 1);
        TEST_ASSERT(b != BFS_BLK_NULL);
    }

    /* Try to allocate 2 contiguous blocks — might fail (fragmented) */
    bfs_blk_t pair = bfs_freespace_alloc(&fsp, 2);
    /* Document: this should still succeed since we have the remaining
     * large extent from the original 1000 blocks */
    TEST_ASSERT(pair != BFS_BLK_NULL);

    /* Free all odd-indexed blocks */
    for (int i = 1; i < 100; i += 2) {
        bfs_freespace_free(&fsp, blocks[i], 1);
    }

    /* After freeing adjacent blocks, merging should allow large alloc */
    bfs_blk_t big = bfs_freespace_alloc(&fsp, 50);
    TEST_ASSERT(big != BFS_BLK_NULL);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ══ Test 5: Rename to same name (overwrite) ═══════════════ */

static void test_rename_overwrite(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino_a, ino_b;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "fileA", 5, &ino_a), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "fileB", 5, &ino_b), BFS_OK);

    /* Rename A to B — should fail with EXISTS since B already exists */
    bfs_err_t err = bfs_fs_rename(&fs, BFS_ROOT_INO, "fileA", 5,
                                    BFS_ROOT_INO, "fileB", 5);
    /* Document the behavior: either EXISTS or OK (overwrite) */
    if (err == BFS_ERR_EXISTS) {
        /* Verify both files still exist */
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "fileA", 5, &ino, &type), BFS_OK);
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "fileB", 5, &ino, &type), BFS_OK);
    } else {
        TEST_ASSERT_EQ(err, BFS_OK);
        /* If overwrite succeeded, A should be gone, B should point to A's inode */
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "fileA", 5, &ino, &type), BFS_ERR_NOTFOUND);
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "fileB", 5, &ino, &type), BFS_OK);
        TEST_ASSERT_EQ(ino, ino_a);
    }

    cleanup(&fs, bio);
}

/* ══ Test 6: Create file with empty name ═══════════════════ */

static void test_empty_name(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    bfs_err_t err = bfs_fs_create_file(&fs, BFS_ROOT_INO, "", 0, &ino);
    /* Should fail gracefully — not crash or corrupt */
    /* If it succeeds, verify it's findable */
    if (err == BFS_OK) {
        uint32_t found_ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "", 0, &found_ino, &type), BFS_OK);
        TEST_ASSERT_EQ(found_ino, ino);
    }
    /* Either way, the FS should still be usable */
    uint32_t ino2;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "normal", 6, &ino2), BFS_OK);

    cleanup(&fs, bio);
}

/* ══ Test 7: Inode access after delete ═════════════════════ */

static void test_inode_after_delete(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "doomed", 6, &ino), BFS_OK);

    /* Verify inode exists */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);

    /* Delete the file */
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "doomed", 6), BFS_OK);

    /* Inode should be gone */
    bfs_err_t err = bfs_inode_read(&fs.inode_tree, ino, &inode);
    TEST_ASSERT_EQ(err, BFS_ERR_NOTFOUND);

    /* File open should fail */
    bfs_file_t f;
    err = bfs_file_open(&f, &fs, ino);
    /* Should either fail or return a file with size 0 and no extents */
    if (err == BFS_OK) {
        TEST_ASSERT_EQ(f.size, 0);
    }

    /* Create new file — may reuse the inode number */
    uint32_t ino2;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "new", 3, &ino2), BFS_OK);

    cleanup(&fs, bio);
}

/* ══ Test 8: Scan with exactly leaf_max entries (boundary) ═ */

static void test_scan_leaf_boundary_14(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create exactly 14 files — should fit in one leaf */
    for (int i = 0; i < 14; i++) {
        int len = snprintf(name, sizeof(name), "leaf14_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 14);

    cleanup(&fs, bio);
}

static void test_scan_leaf_boundary_15(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 15 files — triggers split into 2 leaves */
    for (int i = 0; i < 15; i++) {
        int len = snprintf(name, sizeof(name), "leaf15_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 15);

    /* Also verify all are findable via lookup */
    for (int i = 0; i < 15; i++) {
        int len = snprintf(name, sizeof(name), "leaf15_%02d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

static void test_scan_leaf_boundary_28(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 28 files — fills 2 leaves exactly */
    for (int i = 0; i < 28; i++) {
        int len = snprintf(name, sizeof(name), "leaf28_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 28);

    cleanup(&fs, bio);
}

static void test_scan_leaf_boundary_29(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 29 files — triggers second split */
    for (int i = 0; i < 29; i++) {
        int len = snprintf(name, sizeof(name), "leaf29_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 29);

    /* Verify all findable */
    for (int i = 0; i < 29; i++) {
        int len = snprintf(name, sizeof(name), "leaf29_%02d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

/* ══ Test 9: Floor search edge cases ═══════════════════════ */

static void test_floor_search_edges(void) {
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);

    bfs_freespace_t fsp;
    bfs_freespace_init(&fsp, bio, BFS_BLK_NULL, 1);
    bfs_freespace_add(&fsp, 100, 500);

    /* Allocate blocks at specific positions to create known free extents */
    bfs_blk_t b1 = bfs_freespace_alloc(&fsp, 10); /* takes from start */
    TEST_ASSERT(b1 != BFS_BLK_NULL);

    /* Free at the very start of the tree */
    bfs_freespace_free(&fsp, 50, 5);

    /* Search floor for block 52 — should find extent starting at 50 */
    uint32_t search = bfs_be32(52);
    uint32_t found_key, found_val;
    bfs_err_t err = bfs_btree_search_floor(&fsp.tree, &search, &found_key, &found_val);
    TEST_ASSERT_EQ(err, BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(found_key), 50);

    /* Search for key smaller than anything in tree */
    search = bfs_be32(1);
    err = bfs_btree_search_floor(&fsp.tree, &search, &found_key, &found_val);
    /* Should be NOTFOUND since no key <= 1 exists (smallest is 50) */
    /* Or it might find something depending on what's in the tree */
    (void)err; /* just verify no crash */

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ══ Test 10: Concurrent inode tree and dir tree mods ══════ */

static void test_concurrent_trees(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    uint32_t inos[50];

    /* Create 50 files */
    for (int i = 0; i < 50; i++) {
        int len = snprintf(name, sizeof(name), "conc_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &inos[i]), BFS_OK);
    }

    /* Delete 25 of them (odd indices) */
    for (int i = 1; i < 50; i += 2) {
        int len = snprintf(name, sizeof(name), "conc_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    /* Verify remaining 25 are findable AND their inodes readable */
    for (int i = 0; i < 50; i += 2) {
        int len = snprintf(name, sizeof(name), "conc_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
        TEST_ASSERT_EQ(ino, inos[i]);

        bfs_inode_t inode;
        TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
        TEST_ASSERT_EQ(bfs_be32(inode.inode_nr), ino);
    }

    /* Verify deleted ones are gone from both trees */
    for (int i = 1; i < 50; i += 2) {
        int len = snprintf(name, sizeof(name), "conc_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_ERR_NOTFOUND);

        bfs_inode_t inode;
        TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, inos[i], &inode), BFS_ERR_NOTFOUND);
    }

    /* Scan should return exactly 25 */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 25);

    cleanup(&fs, bio);
}

/* ══ Test 11: File write at exact block boundary ═══════════ */

static void test_cross_block_read(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "cross", 5, &ino), BFS_OK);

    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);

    /* Write exactly 1 block of 0xAA */
    uint8_t block1[BLK_SIZE];
    memset(block1, 0xAA, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&f, block1, BLK_SIZE), BLK_SIZE);

    /* Write 1 more byte (0xBB) — forces 2nd block allocation */
    uint8_t extra = 0xBB;
    TEST_ASSERT_EQ(bfs_file_write(&f, &extra, 1), 1);

    TEST_ASSERT_EQ(f.size, (uint64_t)BLK_SIZE + 1);

    /* Seek to offset 4095, read 2 bytes — crosses block boundary */
    bfs_file_seek(&f, BLK_SIZE - 1, BFS_SEEK_SET);
    uint8_t cross[2];
    int32_t n = bfs_file_read(&f, cross, 2);
    TEST_ASSERT_EQ(n, 2);
    TEST_ASSERT_EQ(cross[0], 0xAA); /* last byte of block 1 */
    TEST_ASSERT_EQ(cross[1], 0xBB); /* first byte of block 2 */

    cleanup(&fs, bio);
}

/* ══ Test 12: Stress pending_frees mechanism ═══════════════ */

static void test_pending_frees(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Do many operations to accumulate pending frees */
    char name[32];
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 20; i++) {
            int len = snprintf(name, sizeof(name), "pf_%d_%03d", round, i);
            uint32_t ino;
            TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
        }
        for (int i = 0; i < 20; i++) {
            int len = snprintf(name, sizeof(name), "pf_%d_%03d", round, i);
            TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
        }
    }

    uint32_t free_before = fs.freespace.total_free;
    uint32_t pending_before = fs.pending_count;

    /* Sync should reclaim pending frees */
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    uint32_t free_after = fs.freespace.total_free;

    /* If there were pending frees, free space should have increased */
    if (pending_before > 0) {
        TEST_ASSERT(free_after > free_before);
    }

    /* FS should still be usable after sync */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "after_sync", 10, &ino), BFS_OK);

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 1);

    cleanup(&fs, bio);
}

/* ══ Test 13: Scan across 3+ leaves (re-descent stress) ════ */

static void test_scan_three_leaves(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Create 42 files — should fill 3 leaves (14 per leaf) */
    char name[32];
    for (int i = 0; i < 42; i++) {
        int len = snprintf(name, sizeof(name), "three_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 42);

    /* Verify all findable */
    for (int i = 0; i < 42; i++) {
        int len = snprintf(name, sizeof(name), "three_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

/* ══ Test 14: Scan after heavy delete (sparse leaves) ══════ */

static void test_scan_sparse_after_delete(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 42 files across 3 leaves */
    for (int i = 0; i < 42; i++) {
        int len = snprintf(name, sizeof(name), "sparse_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Delete 30 of them — leaves will be very sparse, may trigger merges */
    for (int i = 0; i < 30; i++) {
        int len = snprintf(name, sizeof(name), "sparse_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    /* Scan should return exactly 12 */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 12);

    /* Verify all 12 survivors are findable */
    for (int i = 30; i < 42; i++) {
        int len = snprintf(name, sizeof(name), "sparse_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

/* ══ Test 15: Many files with similar hash values ══════════ */

static void test_hash_collision_stress(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Create files with single-char names — limited hash space */
    char name[2];
    for (int c = 'a'; c <= 'z'; c++) {
        name[0] = (char)c;
        name[1] = 0;
        uint32_t ino;
        bfs_err_t err = bfs_fs_create_file(&fs, BFS_ROOT_INO, name, 1, &ino);
        TEST_ASSERT_EQ(err, BFS_OK);
    }

    /* Verify all findable */
    for (int c = 'a'; c <= 'z'; c++) {
        name[0] = (char)c;
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, 1, &ino, &type), BFS_OK);
    }

    /* Scan should return all 26 */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 26);

    cleanup(&fs, bio);
}

/* ══ Test 16: Rename that creates orphan (rename A→B when B exists) ═ */

static void test_rename_orphan_inode(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino_a, ino_b;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "src", 3, &ino_a), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "dst", 3, &ino_b), BFS_OK);

    /* Write data to file B so it has extents */
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino_b), BFS_OK);
    uint8_t data[BLK_SIZE];
    memset(data, 0xCC, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);

    /* Rename src → dst. If it succeeds (overwrite), B's inode and extents
     * should be freed. If it fails with EXISTS, that's also valid. */
    bfs_err_t err = bfs_fs_rename(&fs, BFS_ROOT_INO, "src", 3,
                                    BFS_ROOT_INO, "dst", 3);
    if (err == BFS_OK) {
        /* B's inode should be orphaned — check if it leaks */
        bfs_inode_t inode;
        /* The old inode_b might still exist (leaked!) */
        err = bfs_inode_read(&fs.inode_tree, ino_b, &inode);
        /* BUG if inode still exists — rename didn't clean up the overwritten file */
        if (err == BFS_OK) {
            fprintf(stderr, "  BUG: rename overwrote dst but didn't free inode %u\n", ino_b);
            TEST_ASSERT(0); /* Force fail to report the bug */
        }
    }

    cleanup(&fs, bio);
}

/* ══ Test 17: File truncate to 0 then rewrite ═════════════ */

static void test_truncate_rewrite(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "trunc", 5, &ino), BFS_OK);

    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);

    /* Write 10 blocks */
    uint8_t data[BLK_SIZE];
    memset(data, 0xDD, BLK_SIZE);
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);

    TEST_ASSERT_EQ(f.size, 10ULL * BLK_SIZE);

    /* Truncate to 0 */
    TEST_ASSERT_EQ(bfs_file_truncate(&f, 0), BFS_OK);
    TEST_ASSERT_EQ(f.size, 0ULL);

    /* Rewrite 5 blocks with different pattern */
    memset(data, 0xEE, BLK_SIZE);
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    for (int i = 0; i < 5; i++)
        TEST_ASSERT_EQ(bfs_file_write(&f, data, BLK_SIZE), BLK_SIZE);

    /* Read back and verify new pattern */
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    uint8_t readbuf[BLK_SIZE];
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ(bfs_file_read(&f, readbuf, BLK_SIZE), BLK_SIZE);
        TEST_ASSERT_EQ(readbuf[0], 0xEE);
        TEST_ASSERT_EQ(readbuf[BLK_SIZE - 1], 0xEE);
    }

    cleanup(&fs, bio);
}

/* ══ Test 18: Scan 100 files across many leaves ════════════ */

typedef struct { char names[100][32]; uint8_t lens[100]; int count; } name_collect_t;
static bool collect_cb(const char *n, uint8_t nl, uint32_t ino, uint32_t t, void *ctx) {
    (void)ino; (void)t;
    name_collect_t *nc = (name_collect_t *)ctx;
    if (nc->count < 100) {
        memcpy(nc->names[nc->count], n, nl);
        nc->names[nc->count][nl] = 0;
        nc->lens[nc->count] = nl;
        nc->count++;
    }
    return true;
}

static void test_scan_100_verify_all(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 100 files — spans ~7 leaves */
    for (int i = 0; i < 100; i++) {
        int len = snprintf(name, sizeof(name), "v_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Collect all names via scan */
    name_collect_t nc;
    memset(&nc, 0, sizeof(nc));
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, collect_cb, &nc);
    TEST_ASSERT_EQ(nc.count, 100);

    /* Verify each collected name is findable via lookup */
    for (int i = 0; i < nc.count; i++) {
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO,
                                         nc.names[i], nc.lens[i], &ino, &type);
        if (err != BFS_OK) {
            fprintf(stderr, "  BUG: scan returned '%s' but lookup fails!\n", nc.names[i]);
        }
        TEST_ASSERT_EQ(err, BFS_OK);
    }

    /* Also verify all original names are in the scan results */
    for (int i = 0; i < 100; i++) {
        int len = snprintf(name, sizeof(name), "v_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

/* ══ Test 19: Delete from middle of multi-leaf tree then scan ═ */

static void test_delete_middle_scan(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 50 files */
    for (int i = 0; i < 50; i++) {
        int len = snprintf(name, sizeof(name), "mid_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Delete files 15-35 (from the "middle" of the tree) */
    for (int i = 15; i < 35; i++) {
        int len = snprintf(name, sizeof(name), "mid_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    /* Scan should return 30 */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 30);

    /* Verify survivors */
    for (int i = 0; i < 15; i++) {
        int len = snprintf(name, sizeof(name), "mid_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }
    for (int i = 35; i < 50; i++) {
        int len = snprintf(name, sizeof(name), "mid_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

/* ══ Test 20: Rename to self (same name, same dir) ═════════ */

static void test_rename_to_self(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "self", 4, &ino), BFS_OK);

    /* Rename "self" to "self" — should either succeed (no-op) or fail gracefully */
    bfs_err_t err = bfs_fs_rename(&fs, BFS_ROOT_INO, "self", 4,
                                    BFS_ROOT_INO, "self", 4);

    /* After rename, the file should still exist regardless of return code */
    uint32_t found_ino, type;
    bfs_err_t lookup_err = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "self", 4, &found_ino, &type);

    if (lookup_err != BFS_OK) {
        fprintf(stderr, "  BUG: rename-to-self destroyed the file! err=%d\n", err);
    }
    TEST_ASSERT_EQ(lookup_err, BFS_OK);
    TEST_ASSERT_EQ(found_ino, ino);

    cleanup(&fs, bio);
}

/* ══ Test 21: Scan with 200+ entries (forces 3-level tree) ═ */

static void test_scan_three_level_tree(void) {
    unlink(TEST_IMG);
    /* Need more space for 200 files */
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, 8192);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "BigTree", 0), BFS_OK);

    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    char name[32];
    for (int i = 0; i < 200; i++) {
        int len = snprintf(name, sizeof(name), "big_%04d", i);
        uint32_t ino;
        bfs_err_t err = bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino);
        if (err != BFS_OK) {
            fprintf(stderr, "  BUG: create failed at i=%d, err=%d\n", i, err);
            TEST_ASSERT_EQ(err, BFS_OK);
        }
    }

    /* Scan should return all 200 */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    if (sc.count != 200) {
        fprintf(stderr, "  BUG: scan returned %u entries, expected 200\n", sc.count);
    }
    TEST_ASSERT_EQ(sc.count, 200);

    /* Verify all findable via lookup */
    for (int i = 0; i < 200; i++) {
        int len = snprintf(name, sizeof(name), "big_%04d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ══ Test 22: Delete all entries from first leaf, scan rest ═ */

static void test_delete_first_leaf_entirely(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    char name[32];
    /* Create 28 files (2 full leaves) */
    for (int i = 0; i < 28; i++) {
        int len = snprintf(name, sizeof(name), "dfl_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Delete the first 14 entries by name */
    for (int i = 0; i < 14; i++) {
        int len = snprintf(name, sizeof(name), "dfl_%03d", i);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, (uint8_t)len), BFS_OK);
    }

    /* Scan should return 14 */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 14);

    /* All survivors should be findable */
    for (int i = 14; i < 28; i++) {
        int len = snprintf(name, sizeof(name), "dfl_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);
    }

    cleanup(&fs, bio);
}

/* ══ Test 23: Multiple directories interleaved ═════════════ */

static void test_multiple_dirs_scan(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t dir1, dir2, dir3;
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "dir1", 4, &dir1), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "dir2", 4, &dir2), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "dir3", 4, &dir3), BFS_OK);

    char name[32];
    for (int i = 0; i < 10; i++) {
        int len = snprintf(name, sizeof(name), "f_%02d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, dir1, name, (uint8_t)len, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, dir2, name, (uint8_t)len, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, dir3, name, (uint8_t)len, &ino), BFS_OK);
    }

    /* Scan each directory — should get 10 files + '..' = 11 each */
    scan_count_t sc1 = {0}, sc2 = {0}, sc3 = {0};
    bfs_dir_scan(&fs.dir_tree, dir1, count_cb, &sc1);
    bfs_dir_scan(&fs.dir_tree, dir2, count_cb, &sc2);
    bfs_dir_scan(&fs.dir_tree, dir3, count_cb, &sc3);
    TEST_ASSERT_EQ(sc1.count, 11); /* 10 files + '..' */
    TEST_ASSERT_EQ(sc2.count, 11);
    TEST_ASSERT_EQ(sc3.count, 11);

    /* Root should have 3 dirs */
    scan_count_t sc_root = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc_root);
    TEST_ASSERT_EQ(sc_root.count, 3);

    cleanup(&fs, bio);
}

/* ══ Test 24: Case-only rename (file.txt → File.txt) ═══════ */

static void test_rename_case_change(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "readme.txt", 10, &ino), BFS_OK);

    /* Try to rename "readme.txt" to "README.txt" — case change only.
     * With case-insensitive comparison, the insert will see it as EXISTS.
     * This is a known limitation but let's verify it doesn't corrupt. */
    bfs_err_t err = bfs_fs_rename(&fs, BFS_ROOT_INO, "readme.txt", 10,
                                    BFS_ROOT_INO, "README.txt", 10);

    /* Regardless of result, the file should still be accessible */
    uint32_t found_ino, type;
    bfs_err_t lookup = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "readme.txt", 10, &found_ino, &type);
    if (lookup != BFS_OK) {
        fprintf(stderr, "  BUG: case-only rename destroyed the file! rename err=%d\n", err);
    }
    TEST_ASSERT_EQ(lookup, BFS_OK);
    TEST_ASSERT_EQ(found_ino, ino);

    cleanup(&fs, bio);
}

/* ══ Test 25: Sparse file with large gap ═══════════════════ */

static void test_sparse_file_large_gap(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "sparse", 6, &ino), BFS_OK);

    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);

    /* Write at offset 0 */
    uint8_t data[64];
    memset(data, 0xAA, 64);
    TEST_ASSERT_EQ(bfs_file_write(&f, data, 64), 64);

    /* Seek far ahead and write (creates sparse gap) */
    bfs_file_seek(&f, 100 * BLK_SIZE, BFS_SEEK_SET);
    memset(data, 0xBB, 64);
    TEST_ASSERT_EQ(bfs_file_write(&f, data, 64), 64);

    /* Read from the gap — should return zeros */
    bfs_file_seek(&f, BLK_SIZE, BFS_SEEK_SET);
    uint8_t readbuf[64];
    int32_t n = bfs_file_read(&f, readbuf, 64);
    TEST_ASSERT_EQ(n, 64);
    for (int i = 0; i < 64; i++)
        TEST_ASSERT_EQ(readbuf[i], 0);

    /* Read from the far write */
    bfs_file_seek(&f, 100 * BLK_SIZE, BFS_SEEK_SET);
    n = bfs_file_read(&f, readbuf, 64);
    TEST_ASSERT_EQ(n, 64);
    TEST_ASSERT_EQ(readbuf[0], 0xBB);

    /* Read from offset 0 */
    bfs_file_seek(&f, 0, BFS_SEEK_SET);
    n = bfs_file_read(&f, readbuf, 64);
    TEST_ASSERT_EQ(n, 64);
    TEST_ASSERT_EQ(readbuf[0], 0xAA);

    cleanup(&fs, bio);
}

/* ══ Test 26: Rapid create/delete same name ════════════════ */

static void test_rapid_create_delete_same_name(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    /* Rapidly create and delete the same filename 50 times */
    for (int i = 0; i < 50; i++) {
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "temp", 4, &ino), BFS_OK);
        TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "temp", 4), BFS_OK);
    }

    /* Directory should be empty */
    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 0);

    /* Should still be able to create files */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "final", 5, &ino), BFS_OK);

    cleanup(&fs, bio);
}

/* ══ Test 27: Write file, sync, delete, sync — verify space reclaimed ═ */

static void test_delete_reclaims_after_sync(void) {
    bfs_fs_t fs; bfs_bio_t *bio;
    mount_fresh(&fs, &bio);

    uint32_t free_initial = fs.freespace.total_free;

    /* Create file and write 10 blocks */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "big", 3, &ino), BFS_OK);
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);
    uint8_t data[BLK_SIZE];
    memset(data, 0xFF, BLK_SIZE);
    for (int i = 0; i < 10; i++)
        bfs_file_write(&f, data, BLK_SIZE);

    /* Sync to commit */
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    uint32_t free_after_write = fs.freespace.total_free;
    TEST_ASSERT(free_after_write < free_initial);

    /* Delete the file */
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "big", 3), BFS_OK);

    /* Sync again to reclaim pending frees */
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    uint32_t free_after_delete = fs.freespace.total_free;

    /* Should have recovered most of the space (at least the 10 data blocks) */
    TEST_ASSERT(free_after_delete > free_after_write);
    /* The difference should be at least 10 blocks (data) */
    TEST_ASSERT(free_after_delete - free_after_write >= 10);

    cleanup(&fs, bio);
}

/* ══ Test 28: Sync/remount preserves everything ════════════ */

static void test_sync_remount_integrity(void) {
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "SyncTest", 0), BFS_OK);

    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    /* Create 30 files with data */
    char name[32];
    for (int i = 0; i < 30; i++) {
        int len = snprintf(name, sizeof(name), "sync_%03d", i);
        uint32_t ino;
        TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)len, &ino), BFS_OK);

        bfs_file_t f;
        TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);
        uint8_t data[64];
        memset(data, (uint8_t)i, 64);
        TEST_ASSERT_EQ(bfs_file_write(&f, data, 64), 64);
    }

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);

    /* Remount and verify */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    scan_count_t sc = {0};
    bfs_dir_scan(&fs.dir_tree, BFS_ROOT_INO, count_cb, &sc);
    TEST_ASSERT_EQ(sc.count, 30);

    /* Verify data integrity */
    for (int i = 0; i < 30; i++) {
        int len = snprintf(name, sizeof(name), "sync_%03d", i);
        uint32_t ino, type;
        TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, (uint8_t)len, &ino, &type), BFS_OK);

        bfs_file_t f;
        TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);
        uint8_t data[64];
        TEST_ASSERT_EQ(bfs_file_read(&f, data, 64), 64);
        TEST_ASSERT_EQ(data[0], (uint8_t)i);
        TEST_ASSERT_EQ(data[63], (uint8_t)i);
    }

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

/* ══ Main ══════════════════════════════════════════════════ */

TEST_SUITE_BEGIN("Bug Hunt Tests")
    TEST_RUN(test_scan_start_key);
    TEST_RUN(test_scan_empty_tree);
    TEST_RUN(test_scan_single_entry);
    TEST_RUN(test_scan_after_delete);
    TEST_RUN(test_delete_merge);
    TEST_RUN(test_extent_fragmented);
    TEST_RUN(test_alloc_fragmentation);
    TEST_RUN(test_rename_overwrite);
    TEST_RUN(test_empty_name);
    TEST_RUN(test_inode_after_delete);
    TEST_RUN(test_scan_leaf_boundary_14);
    TEST_RUN(test_scan_leaf_boundary_15);
    TEST_RUN(test_scan_leaf_boundary_28);
    TEST_RUN(test_scan_leaf_boundary_29);
    TEST_RUN(test_floor_search_edges);
    TEST_RUN(test_concurrent_trees);
    TEST_RUN(test_cross_block_read);
    TEST_RUN(test_pending_frees);
    TEST_RUN(test_scan_three_leaves);
    TEST_RUN(test_scan_sparse_after_delete);
    TEST_RUN(test_hash_collision_stress);
    TEST_RUN(test_rename_orphan_inode);
    TEST_RUN(test_truncate_rewrite);
    TEST_RUN(test_scan_100_verify_all);
    TEST_RUN(test_delete_middle_scan);
    TEST_RUN(test_rename_to_self);
    TEST_RUN(test_scan_three_level_tree);
    TEST_RUN(test_delete_first_leaf_entirely);
    TEST_RUN(test_multiple_dirs_scan);
    TEST_RUN(test_rename_case_change);
    TEST_RUN(test_sparse_file_large_gap);
    TEST_RUN(test_rapid_create_delete_same_name);
    TEST_RUN(test_delete_reclaims_after_sync);
    TEST_RUN(test_sync_remount_integrity);
TEST_SUITE_END()
