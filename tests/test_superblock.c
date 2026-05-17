/*
 * BFS — Superblock tests: roundtrip, corruption, alternating recovery
 */

#include "test_harness.h"
#include "bfs_superblock.h"
#include "block_device_emu.h"
#include <unistd.h>

#define TEST_IMG "test_sb.img"
#define BLK_SIZE 4096
#define BLK_COUNT 64

/* Helper: create a valid superblock in host byte order */
static void make_test_sb(bfs_superblock_t *sb, uint64_t txn_id)
{
    memset(sb, 0, sizeof(*sb));
    sb->magic       = bfs_be32(BFS_SB_MAGIC);
    sb->version     = bfs_be32(BFS_SB_VERSION);
    sb->block_size  = bfs_be32(BLK_SIZE);
    sb->block_count = bfs_be32(BLK_COUNT);
    sb->txn_id      = bfs_be64(txn_id);
    sb->free_blocks = bfs_be32(BLK_COUNT - 2);
    /* Backup at partition midpoint */
    uint64_t backup_off = (uint64_t)BLK_COUNT * BLK_SIZE / 2;
    sb->sb_backup_offset_lo = bfs_be32((uint32_t)backup_off);
    sb->sb_backup_offset_hi = bfs_be32((uint32_t)(backup_off >> 32));
    memcpy(sb->volname, "TestVol", 7);
    /* crc32 will be set by sb_write */
}

static void test_sb_roundtrip(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_superblock_t sb, sb_read;
    make_test_sb(&sb, 1);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_read), BFS_OK);

    TEST_ASSERT_EQ(bfs_be32(sb_read.magic), BFS_SB_MAGIC);
    TEST_ASSERT_EQ(bfs_be64(sb_read.txn_id), 1);
    TEST_ASSERT_EQ(bfs_be32(sb_read.block_size), BLK_SIZE);
    TEST_ASSERT_EQ(bfs_be32(sb_read.block_count), BLK_COUNT);
    TEST_ASSERT_MEM_EQ(sb_read.volname, "TestVol", 7);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_sb_corruption_detected(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_superblock_t sb;
    /* Write txn 1 and txn 2 so both slots are filled */
    make_test_sb(&sb, 1);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);
    make_test_sb(&sb, 2);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);

    /* Corrupt a byte in the primary superblock */
    uint8_t buf[BLK_SIZE];
    TEST_ASSERT_EQ(bfs_bio_read(bio, 0, buf), BFS_OK);
    buf[10] ^= 0xFF;
    TEST_ASSERT_EQ(bfs_bio_write(bio, 0, buf), BFS_OK);

    /* The corrupted copy should be detected, surviving copy should work */
    bfs_superblock_t sb_read;
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_read), BFS_OK);
    /* Should get whichever txn_id survived */
    TEST_ASSERT(bfs_be64(sb_read.txn_id) >= 1);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_sb_both_corrupt(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    /* Don't write any valid superblock — both slots are zeros */
    bfs_superblock_t sb_read;
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_read), BFS_ERR_CORRUPT);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_sb_alternating_writes(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_superblock_t sb, sb_read;

    /* Write txn 1 */
    make_test_sb(&sb, 1);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);

    /* Write txn 2 — should go to the other slot */
    make_test_sb(&sb, 2);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);

    /* Read should return txn 2 */
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_read), BFS_OK);
    TEST_ASSERT_EQ(bfs_be64(sb_read.txn_id), 2);

    /* Write txn 3 — should overwrite the slot with txn 1 */
    make_test_sb(&sb, 3);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);

    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_read), BFS_OK);
    TEST_ASSERT_EQ(bfs_be64(sb_read.txn_id), 3);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_sb_recovery_after_failed_write(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    TEST_ASSERT(bio != NULL);

    bfs_superblock_t sb, sb_read;

    /* Write txn 1 and txn 2 to fill both slots */
    make_test_sb(&sb, 1);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);
    make_test_sb(&sb, 2);
    TEST_ASSERT_EQ(bfs_sb_write(bio, &sb), BFS_OK);

    /* Simulate failed write of txn 3: corrupt the slot that would be written */
    /* Slot with txn 1 is the older one — that's where txn 3 would go */
    /* Read both to find which has txn 1 */
    bfs_superblock_t sb_a, sb_b;
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_a), BFS_OK); /* just to confirm both valid */

    /* Read A from byte 0 */
    uint8_t buf[BLK_SIZE];
    bfs_bio_read(bio, 0, buf);
    memcpy(&sb_a, buf, sizeof(sb_a));

    /* Read B from midpoint */
    uint64_t backup_off = (uint64_t)BLK_COUNT * BLK_SIZE / 2;
    bfs_blk_t backup_blk = (bfs_blk_t)(backup_off / BLK_SIZE);
    bfs_bio_read(bio, backup_blk, buf);
    memcpy(&sb_b, buf, sizeof(sb_b));

    /* Zero out the older slot (simulating interrupted write) */
    bfs_blk_t older_blk = (bfs_be64(sb_a.txn_id) <= bfs_be64(sb_b.txn_id))
                            ? 0 : backup_blk;
    memset(buf, 0, BLK_SIZE);
    TEST_ASSERT_EQ(bfs_bio_write(bio, older_blk, buf), BFS_OK);

    /* Recovery: should still read txn 2 from the surviving slot */
    TEST_ASSERT_EQ(bfs_sb_read(bio, &sb_read), BFS_OK);
    TEST_ASSERT_EQ(bfs_be64(sb_read.txn_id), 2);

    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void test_sb_validate_bad_magic(void)
{
    bfs_superblock_t sb;
    make_test_sb(&sb, 1);
    sb.crc32 = bfs_be32(bfs_sb_compute_crc(&sb));

    /* Corrupt magic */
    sb.magic = bfs_be32(0xDEADBEEF);
    TEST_ASSERT_EQ(bfs_sb_validate(&sb), BFS_ERR_CORRUPT);
}

static void test_sb_validate_bad_block_size(void)
{
    bfs_superblock_t sb;
    make_test_sb(&sb, 1);
    sb.block_size = bfs_be32(300);  /* not power of 2 */
    sb.crc32 = bfs_be32(bfs_sb_compute_crc(&sb));
    TEST_ASSERT_EQ(bfs_sb_validate(&sb), BFS_ERR_CORRUPT);
}

TEST_SUITE_BEGIN("Superblock")
    TEST_RUN(test_sb_roundtrip);
    TEST_RUN(test_sb_corruption_detected);
    TEST_RUN(test_sb_both_corrupt);
    TEST_RUN(test_sb_alternating_writes);
    TEST_RUN(test_sb_recovery_after_failed_write);
    TEST_RUN(test_sb_validate_bad_magic);
    TEST_RUN(test_sb_validate_bad_block_size);
TEST_SUITE_END()
