/*
 * BFS — On-disk corruption / untrusted-media robustness tests
 *
 * These reproduce the two memory-safety criticals C2 and C4: a block whose
 * CRC is valid but whose structural fields are corrupt must be rejected with
 * BFS_ERR_CORRUPT, never used to index past a fixed-size buffer. Without the
 * fixes these tests fault (verified separately under AddressSanitizer); the
 * harness build catches a regression as a crash or a non-CORRUPT return.
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_dir.h"
#include "bfs_ondisk.h"
#include "bfs_crc32.h"
#include "block_device_emu.h"
#include <unistd.h>

#define BS 4096

/* Recompute a node's CRC so a hand-corrupted block still passes node_read's
 * CRC check — i.e. simulate a deliberately-consistent corrupt node, not bit-rot. */
static void refresh_node_crc(uint8_t *buf)
{
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->crc32 = 0;
    hdr->crc32 = bfs_be32(bfs_crc32(0, buf, BS));
}

/* C2: a B+tree node whose num_keys is absurd (but CRC valid) must be rejected
 * before any accessor multiplies num_keys into a block offset. */
static void test_corrupt_btree_numkeys(void)
{
    const char *img = "test_corruption_c2.img";
    unlink(img);
    bfs_bio_t *bio = bio_emu_create(img, BS, 4096);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_format(bio, "C2", 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "victim", 6, &ino), BFS_OK);
    bfs_fs_sync(&fs);

    bfs_blk_t root = fs.dir_tree.tree.root;
    TEST_ASSERT(root != BFS_BLK_NULL);

    uint8_t buf[BS];
    TEST_ASSERT_EQ(bfs_bio_read(fs.bio, root, buf), BFS_OK);
    bfs_btnode_hdr_t *hdr = (bfs_btnode_hdr_t *)buf;
    hdr->num_keys = bfs_be32(100000);   /* far beyond any node's capacity */
    refresh_node_crc(buf);
    TEST_ASSERT_EQ(bfs_bio_write(fs.bio, root, buf), BFS_OK);
    bfs_bio_sync(fs.bio);

    /* Reading the corrupt node via a lookup must fail cleanly, not fault. */
    uint32_t found, type;
    bfs_err_t err = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "victim", 6, &found, &type);
    TEST_ASSERT_EQ(err, BFS_ERR_CORRUPT);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(img);
}

/* C4: a corrupt on-disk extent length must not drive an unbounded write into
 * the fixed pending_frees[] array during truncate. */
static void test_corrupt_extent_length(void)
{
    const char *img = "test_corruption_c4.img";
    unlink(img);
    bfs_bio_t *bio = bio_emu_create(img, BS, 4096);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_format(bio, "C4", 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "v", 1, &ino), BFS_OK);
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, &fs, ino), BFS_OK);
    uint8_t blk[BS];
    memset(blk, 'X', sizeof blk);
    TEST_ASSERT(bfs_file_write(&f, blk, sizeof blk) == (int32_t)sizeof blk);
    bfs_fs_sync(&fs);

    /* Corrupt the single extent value's length in the leaf, keeping CRC valid. */
    bfs_blk_t root = f.extents.tree.root;
    TEST_ASSERT(root != BFS_BLK_NULL);
    uint8_t buf[BS];
    TEST_ASSERT_EQ(bfs_bio_read(fs.bio, root, buf), BFS_OK);
    uint32_t keys_end = sizeof(bfs_btnode_hdr_t)
                      + ((BS - sizeof(bfs_btnode_hdr_t)) / (4 + 12)) * 4;
    uint8_t *val0 = buf + keys_end;            /* bfs_extent_val_t[0] */
    TEST_ASSERT_EQ(bfs_load_be32(val0 + 4), 1);/* sanity: real length is 1 */
    bfs_store_be32(val0 + 4, 0xFFFFFFFFu);     /* corrupt length */
    refresh_node_crc(buf);
    TEST_ASSERT_EQ(bfs_bio_write(fs.bio, root, buf), BFS_OK);
    bfs_bio_sync(fs.bio);

    /* Realistic mid-operation state: a prior pending free makes the old
     * pending_count + len guard wrap. Truncate must reject, not overrun. */
    fs.pending_count = 1;
    bfs_err_t err = bfs_file_truncate(&f, 0);
    TEST_ASSERT_EQ(err, BFS_ERR_CORRUPT);

    fs.pending_count = 0;
    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(img);
}

TEST_SUITE_BEGIN("On-disk Corruption Robustness")
    TEST_RUN(test_corrupt_btree_numkeys);
    TEST_RUN(test_corrupt_extent_length);
TEST_SUITE_END()
