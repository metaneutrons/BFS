/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — format v3: inline extents and inode flags
 *
 * A file whose content is one contiguous run from block 0 keeps it in the
 * inode. These tests cover growth, every conversion into a tree, truncation,
 * checksummed volumes, handle refresh, snapshots, validation, the checker and
 * the HAS_COMMENT flag.
 */

#include "test_harness.h"
#include "bfs_crc32.h"
#include "bfs_dir.h"
#include "bfs_extent.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <unistd.h>

#define IMG    "test_inline_extent.img"
#define BS     4096u
#define BLOCKS 1024u

static bfs_bio_t *bio;
static bfs_fs_t fs;
static uint8_t block_buf[4u * BS];

static bool setup(uint32_t options)
{
    unlink(IMG);
    bio = bio_emu_create(IMG, BS, BLOCKS);
    return bio && bfs_fs_format(bio, "Inline", options) == BFS_OK &&
           bfs_fs_mount(&fs, bio) == BFS_OK;
}

static void teardown(bool commit)
{
    if (commit) (void)bfs_fs_unmount(&fs);
    else bfs_fs_abandon(&fs);
    bfs_bio_close(bio);
    unlink(IMG);
}

static bool check_clean(void)
{
    bfs_fsck_report_t report;
    return bfs_fs_check(&fs, false, &report) == BFS_OK && report.errors == 0 &&
           report.leaked_blocks == 0;
}

/* Mid-transaction checks: COW blocks awaiting reclamation count as leaks. */
static bool check_no_errors(void)
{
    bfs_fsck_report_t report;
    return bfs_fs_check(&fs, false, &report) == BFS_OK && report.errors == 0;
}

static bool check_has_errors(void)
{
    bfs_fsck_report_t report;
    return bfs_fs_check(&fs, false, &report) == BFS_ERR_CORRUPT && report.errors > 0;
}

static bool create_open(const char *name, uint32_t *ino, bfs_file_t *file)
{
    return bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)strlen(name), ino) == BFS_OK &&
           bfs_file_open(file, &fs, *ino) == BFS_OK;
}

/* Publish a handle's mapping the way the file layer does, with a new size. */
static bool publish(bfs_file_t *file, uint64_t size)
{
    bfs_inode_t inode;
    if (bfs_inode_read(&fs.inode_tree, file->inode_nr, &inode) != BFS_OK) return false;
    bfs_extent_store(&file->extents, &inode);
    inode.size_hi = bfs_be32((uint32_t)(size >> 32));
    inode.size_lo = bfs_be32((uint32_t)size);
    file->size = size;
    return bfs_inode_write(&fs.inode_tree, file->inode_nr, &inode) == BFS_OK;
}

static bool fill_blocks(bfs_blk_t start, uint32_t count, uint8_t first)
{
    for (uint32_t i = 0; i < count; i++) {
        memset(block_buf, first + (int)i, BS);
        if (bfs_bio_write(bio, start + i, block_buf) != BFS_OK) return false;
    }
    return true;
}

static void test_contiguous_runs_stay_inline(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("run", &ino, &file));
    bfs_blk_t run = bfs_freespace_alloc(&fs.freespace, 4);
    TEST_ASSERT(run != BFS_BLK_NULL);
    TEST_ASSERT(fill_blocks(run, 4, 0x10));

    TEST_ASSERT_EQ(bfs_extent_map_run(&file.extents, 0, run, 2), BFS_OK);
    TEST_ASSERT(bfs_extent_is_inline(&file.extents));
    TEST_ASSERT_EQ(file.extents.inline_length, 2);
    TEST_ASSERT_EQ(bfs_extent_map_run(&file.extents, 2, run + 2, 2), BFS_OK);
    TEST_ASSERT_EQ(file.extents.inline_length, 4);
    TEST_ASSERT_EQ(file.extents.tree.root, BFS_BLK_NULL);
    /* A mapping inside the inline extent is not a new run. */
    TEST_ASSERT_EQ(bfs_extent_map_block(&file.extents, 3, run, 0), BFS_ERR_EXISTS);

    bfs_blk_t disk;
    uint32_t blocks;
    TEST_ASSERT_EQ(bfs_extent_lookup_run(&file.extents, 1, &disk, &blocks), BFS_OK);
    TEST_ASSERT_EQ(disk, run + 1);
    TEST_ASSERT_EQ(blocks, 3);
    TEST_ASSERT_EQ(bfs_extent_lookup(&file.extents, 4, &disk), BFS_ERR_NOTFOUND);

    TEST_ASSERT(publish(&file, 4u * BS));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(file.extents.inline_start, run);
    TEST_ASSERT_EQ(file.extents.inline_length, 4);
    TEST_ASSERT_EQ(bfs_file_read(&file, block_buf, sizeof(block_buf)), (int32_t)sizeof(block_buf));
    for (uint32_t i = 0; i < 4; i++) TEST_ASSERT_EQ(block_buf[i * BS], 0x10 + i);
    teardown(true);
}

static void test_gap_and_discontiguity_convert_to_tree(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("gap", &ino, &file));
    bfs_blk_t run = bfs_freespace_alloc(&fs.freespace, 3);
    TEST_ASSERT(run != BFS_BLK_NULL);
    TEST_ASSERT(fill_blocks(run, 3, 0x20));

    /* A hole after the inline extent needs a second record. */
    TEST_ASSERT_EQ(bfs_extent_map_block(&file.extents, 0, run, 0), BFS_OK);
    TEST_ASSERT(bfs_extent_is_inline(&file.extents));
    TEST_ASSERT_EQ(bfs_extent_map_block(&file.extents, 5, run + 1, 0), BFS_OK);
    TEST_ASSERT(!bfs_extent_is_inline(&file.extents));
    TEST_ASSERT(file.extents.tree.root != BFS_BLK_NULL);
    bfs_blk_t disk;
    TEST_ASSERT_EQ(bfs_extent_lookup(&file.extents, 0, &disk), BFS_OK);
    TEST_ASSERT_EQ(disk, run);
    TEST_ASSERT_EQ(bfs_extent_lookup(&file.extents, 5, &disk), BFS_OK);
    TEST_ASSERT_EQ(disk, run + 1);
    TEST_ASSERT_EQ(bfs_extent_lookup(&file.extents, 2, &disk), BFS_ERR_NOTFOUND);
    TEST_ASSERT(publish(&file, 6u * BS));

    /* The next logical block on a different disk block does as well. */
    uint32_t other_ino;
    bfs_file_t other;
    TEST_ASSERT(create_open("skip", &other_ino, &other));
    TEST_ASSERT_EQ(bfs_extent_map_block(&other.extents, 0, run + 2, 0), BFS_OK);
    bfs_blk_t far = bfs_freespace_alloc(&fs.freespace, 2);
    TEST_ASSERT(far != BFS_BLK_NULL && far + 1 != run + 3);
    TEST_ASSERT(fill_blocks(far + 1, 1, 0x30));
    TEST_ASSERT_EQ(bfs_freespace_free(&fs.freespace, far, 1), BFS_OK);
    TEST_ASSERT_EQ(bfs_extent_map_block(&other.extents, 1, far + 1, 0), BFS_OK);
    TEST_ASSERT(other.extents.tree.root != BFS_BLK_NULL);
    TEST_ASSERT(publish(&other, 2u * BS));

    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    TEST_ASSERT_EQ(bfs_file_open(&other, &fs, other_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&other, block_buf, 2u * BS), (int32_t)(2u * BS));
    TEST_ASSERT_EQ(block_buf[0], 0x22);
    TEST_ASSERT_EQ(block_buf[BS], 0x30);
    teardown(true);
}

static void test_remap_splits_or_replaces_inline_extent(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("remap", &ino, &file));
    bfs_blk_t run = bfs_freespace_alloc(&fs.freespace, 4);
    bfs_blk_t replacement = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(run != BFS_BLK_NULL && replacement != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_extent_map_run(&file.extents, 0, run, 4), BFS_OK);

    bfs_blk_t old = BFS_BLK_NULL;
    TEST_ASSERT_EQ(bfs_extent_remap_block(&file.extents, 1, replacement, &old), BFS_OK);
    TEST_ASSERT_EQ(old, run + 1);
    TEST_ASSERT(!bfs_extent_is_inline(&file.extents));
    bfs_blk_t disk;
    uint32_t blocks;
    TEST_ASSERT_EQ(bfs_extent_lookup_run(&file.extents, 0, &disk, &blocks), BFS_OK);
    TEST_ASSERT_EQ(disk, run);
    TEST_ASSERT_EQ(blocks, 1);
    TEST_ASSERT_EQ(bfs_extent_lookup_run(&file.extents, 1, &disk, &blocks), BFS_OK);
    TEST_ASSERT_EQ(disk, replacement);
    TEST_ASSERT_EQ(blocks, 1);
    TEST_ASSERT_EQ(bfs_extent_lookup_run(&file.extents, 2, &disk, &blocks), BFS_OK);
    TEST_ASSERT_EQ(disk, run + 2);
    TEST_ASSERT_EQ(blocks, 2);
    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(&fs, old), BFS_OK);
    TEST_ASSERT(publish(&file, 4u * BS));

    /* A one-block inline extent only moves. */
    uint32_t single_ino;
    bfs_file_t single;
    TEST_ASSERT(create_open("single", &single_ino, &single));
    bfs_blk_t first = bfs_freespace_alloc(&fs.freespace, 1);
    bfs_blk_t second = bfs_freespace_alloc(&fs.freespace, 1);
    TEST_ASSERT(first != BFS_BLK_NULL && second != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_extent_map_block(&single.extents, 0, first, 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_extent_remap_block(&single.extents, 0, second, &old), BFS_OK);
    TEST_ASSERT_EQ(old, first);
    TEST_ASSERT(bfs_extent_is_inline(&single.extents));
    TEST_ASSERT_EQ(single.extents.inline_start, second);
    TEST_ASSERT_EQ(bfs_extent_remap_block(&single.extents, 1, first, &old), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_fs_queue_pending_free(&fs, first), BFS_OK);
    TEST_ASSERT(publish(&single, BS));

    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    teardown(true);
}

static void test_truncate_shortens_and_clears_inline_extent(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("trunc", &ino, &file));
    memset(block_buf, 0x55, 3u * BS);
    TEST_ASSERT_EQ(bfs_file_write(&file, block_buf, 3u * BS), (int32_t)(3u * BS));
    TEST_ASSERT_EQ(file.extents.inline_length, 3);
    bfs_blk_t start = file.extents.inline_start;
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    TEST_ASSERT_EQ(bfs_file_truncate(&file, BS + 10), BFS_OK);
    TEST_ASSERT_EQ(file.extents.inline_length, 2);
    TEST_ASSERT_EQ(file.extents.inline_start, start);
    TEST_ASSERT_EQ(bfs_file_truncate(&file, 2u * BS), BFS_OK); /* beyond: no change */
    TEST_ASSERT_EQ(file.extents.inline_length, 2);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_read(&file, block_buf, 2u * BS), (int32_t)(2u * BS));
    TEST_ASSERT_EQ(block_buf[BS + 9], 0x55);
    TEST_ASSERT_EQ(block_buf[BS + 10], 0);

    TEST_ASSERT_EQ(bfs_file_truncate(&file, 0), BFS_OK);
    TEST_ASSERT(!bfs_extent_is_inline(&file.extents));
    TEST_ASSERT_EQ(file.extents.tree.root, BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    teardown(true);
}

static void test_checksummed_volume_inlines_one_block(void)
{
    TEST_ASSERT(setup(BFS_OPT_DATA_CHECKSUMS));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("crc", &ino, &file));
    memset(block_buf, 0x61, BS);
    memset(block_buf + BS, 0x62, BS);
    TEST_ASSERT_EQ(bfs_file_write(&file, block_buf, BS), (int32_t)BS);
    TEST_ASSERT_EQ(file.extents.inline_length, 1);
    TEST_ASSERT_EQ(file.extents.inline_crc, bfs_crc32(0, block_buf, BS));
    /* A second checksummed block needs its own record. */
    TEST_ASSERT_EQ(bfs_file_write(&file, block_buf + BS, BS), (int32_t)BS);
    TEST_ASSERT(!bfs_extent_is_inline(&file.extents));
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());

    uint32_t small_ino;
    bfs_file_t small;
    TEST_ASSERT(create_open("small", &small_ino, &small));
    TEST_ASSERT_EQ(bfs_file_write(&small, block_buf, 100), 100);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_blk_t data = small.extents.inline_start;
    TEST_ASSERT_EQ(bfs_bio_read(bio, data, block_buf), BFS_OK);
    block_buf[7] ^= 0x01;
    TEST_ASSERT_EQ(bfs_bio_write(bio, data, block_buf), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_seek(&small, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_read(&small, block_buf, 100), BFS_ERR_CORRUPT);
    TEST_ASSERT(check_has_errors());
    teardown(false);
}

static void test_second_handle_sees_inline_growth(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t writer, reader;
    TEST_ASSERT(create_open("shared", &ino, &writer));
    TEST_ASSERT_EQ(bfs_file_open(&reader, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_truncate(&writer, 3u * BS), BFS_OK);

    bfs_blk_t run = bfs_freespace_alloc(&fs.freespace, 2);
    TEST_ASSERT(run != BFS_BLK_NULL);
    TEST_ASSERT(fill_blocks(run, 2, 0x71));
    TEST_ASSERT_EQ(bfs_extent_map_block(&writer.extents, 0, run, 0), BFS_OK);
    TEST_ASSERT(publish(&writer, 3u * BS));
    TEST_ASSERT_EQ(bfs_file_read(&reader, block_buf, BS), (int32_t)BS);
    TEST_ASSERT_EQ(block_buf[0], 0x71);

    /* Growth keeps extent_root and size: only the inline length changes. */
    TEST_ASSERT_EQ(bfs_extent_map_block(&writer.extents, 1, run + 1, 0), BFS_OK);
    TEST_ASSERT_EQ(writer.extents.inline_length, 2);
    TEST_ASSERT(publish(&writer, 3u * BS));
    TEST_ASSERT_EQ(bfs_file_read(&reader, block_buf, BS), (int32_t)BS);
    TEST_ASSERT_EQ(block_buf[0], 0x72);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    teardown(true);
}

static void test_delete_reclaims_inline_blocks(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("victim", &ino, &file));
    memset(block_buf, 0x44, sizeof(block_buf));
    TEST_ASSERT_EQ(bfs_file_write(&file, block_buf, sizeof(block_buf)), (int32_t)sizeof(block_buf));
    TEST_ASSERT_EQ(file.extents.inline_length, 4);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "victim", 6), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    /* Unreclaimed data blocks would be reported as leaks. */
    TEST_ASSERT(check_clean());
    teardown(true);
}

static bool read_snapshot_block(const char *name, uint32_t ino, uint32_t file_block,
                                uint8_t *out, bool *is_inline)
{
    bfs_snapshot_record_t record;
    if (bfs_snapshot_find_by_name(&fs, name, NULL, &record) != BFS_OK) return false;
    uint64_t txn = ((uint64_t)bfs_be32(record.txn_id_hi) << 32) | bfs_be32(record.txn_id_lo);
    bfs_btree_t inodes;
    bfs_inode_t inode;
    bfs_extent_tree_t extents;
    bfs_blk_t disk;
    if (bfs_inode_init(&inodes, fs.bio, bfs_freespace_allocator(&fs.freespace),
                       bfs_be32(record.inode_tree_root), txn) != BFS_OK ||
        bfs_inode_read(&inodes, ino, &inode) != BFS_OK ||
        bfs_extent_open(&extents, fs.bio, &fs.freespace, &inode, txn) != BFS_OK ||
        bfs_extent_lookup(&extents, file_block, &disk) != BFS_OK)
        return false;
    *is_inline = bfs_extent_is_inline(&extents);
    return bfs_bio_read(fs.bio, disk, out) == BFS_OK;
}

static void test_snapshot_shares_and_copies_inline_blocks(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("snap", &ino, &file));
    for (uint32_t i = 0; i < 4; i++) memset(block_buf + i * BS, 0x41 + (int)i, BS);
    TEST_ASSERT_EQ(bfs_file_write(&file, block_buf, sizeof(block_buf)), (int32_t)sizeof(block_buf));
    TEST_ASSERT_EQ(file.extents.inline_length, 4);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "before"), BFS_OK);
    TEST_ASSERT(check_clean());

    /* Copy-on-write of an inner block splits the shared inline extent. */
    uint8_t replacement[BS];
    memset(replacement, 0x5A, sizeof(replacement));
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_seek(&file, 2u * BS, BFS_SEEK_SET), (int64_t)(2u * BS));
    TEST_ASSERT_EQ(bfs_file_write(&file, replacement, BS), (int32_t)BS);
    TEST_ASSERT(file.extents.tree.root != BFS_BLK_NULL);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());

    bool snapshot_inline = false;
    uint8_t old_block[BS];
    TEST_ASSERT(read_snapshot_block("before", ino, 2, old_block, &snapshot_inline));
    TEST_ASSERT(snapshot_inline);
    TEST_ASSERT_EQ(old_block[0], 0x43);
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_read(&file, block_buf, sizeof(block_buf)), (int32_t)sizeof(block_buf));
    TEST_ASSERT_EQ(block_buf[1u * BS], 0x42);
    TEST_ASSERT_EQ(block_buf[2u * BS], 0x5A);
    TEST_ASSERT_EQ(block_buf[3u * BS], 0x44);

    uint32_t id;
    TEST_ASSERT_EQ(bfs_snapshot_find_by_name(&fs, "before", &id, NULL), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_delete(&fs, id), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "snap", 4), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    teardown(true);
}

static bfs_inode_t valid_file_inode(uint32_t ino)
{
    bfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_nr = bfs_be32(ino);
    inode.type = bfs_be32(BFS_INODE_FILE);
    inode.link_count = bfs_be32(1);
    return inode;
}

static void test_inode_validation_rules(void)
{
    TEST_ASSERT(setup(0));
    const bfs_btree_t *tree = &fs.inode_tree;
    bfs_inode_t inode = valid_file_inode(7);
    TEST_ASSERT(bfs_inode_valid(tree, 7, &inode));
    inode.flags = bfs_be32(1u << 5);
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));

    inode = valid_file_inode(7);
    inode.flags = bfs_be32(BFS_INODE_FLAG_INLINE_EXTENT | BFS_INODE_FLAG_HAS_COMMENT);
    inode.extent_root = bfs_be32(100);
    inode.inline_length = bfs_be32(2);
    TEST_ASSERT(bfs_inode_valid(tree, 7, &inode));
    inode.inline_length = 0;
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));
    inode.inline_length = bfs_be32(BLOCKS - 99);
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));
    inode.inline_length = bfs_be32(BLOCKS - 100);
    TEST_ASSERT(bfs_inode_valid(tree, 7, &inode));
    inode.extent_root = 0;
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));

    /* Without the flag the inline fields stay zero. */
    inode = valid_file_inode(7);
    inode.inline_length = bfs_be32(1);
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));
    inode.inline_length = 0;
    inode.inline_crc32 = bfs_be32(1);
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));

    /* Directories have no extents in either form. */
    inode = valid_file_inode(7);
    inode.type = bfs_be32(BFS_INODE_DIR);
    TEST_ASSERT(bfs_inode_valid(tree, 7, &inode));
    inode.extent_root = bfs_be32(100);
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));
    inode.flags = bfs_be32(BFS_INODE_FLAG_INLINE_EXTENT);
    inode.inline_length = bfs_be32(1);
    TEST_ASSERT(!bfs_inode_valid(tree, 7, &inode));
    teardown(true);
}

static void test_inline_range_avoids_reserved_blocks(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("reserved", &ino, &file));
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    /* The block holding backup slot B sits at the device midpoint. */
    inode.flags = bfs_be32(BFS_INODE_FLAG_INLINE_EXTENT);
    inode.extent_root = bfs_be32(BLOCKS / 2 - 1);
    inode.inline_length = bfs_be32(2);
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_ERR_CORRUPT);
    bfs_extent_tree_t extents;
    TEST_ASSERT_EQ(bfs_extent_open(&extents, bio, &fs.freespace, &inode, 1), BFS_ERR_CORRUPT);
    TEST_ASSERT(check_has_errors());
    teardown(false);
}

static void test_checker_rejects_shared_inline_blocks(void)
{
    TEST_ASSERT(setup(0));
    uint32_t first_ino, second_ino;
    bfs_file_t first, second;
    TEST_ASSERT(create_open("first", &first_ino, &first));
    TEST_ASSERT(create_open("second", &second_ino, &second));
    memset(block_buf, 0x12, BS);
    TEST_ASSERT_EQ(bfs_file_write(&first, block_buf, BS), (int32_t)BS);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());

    /* Without snapshots no block may be referenced twice. */
    second.extents.inline_start = first.extents.inline_start;
    second.extents.inline_length = 1;
    TEST_ASSERT(publish(&second, BS));
    TEST_ASSERT(check_has_errors());
    teardown(false);
}

typedef struct {
    bfs_bio_t base;
    bfs_bio_t *inner;
    uint32_t reads;
} counting_bio_t;

static bfs_err_t counting_read(bfs_bio_t *b, bfs_blk_t blk, void *buf)
{
    counting_bio_t *c = (counting_bio_t *)b;
    c->reads++;
    return bfs_bio_read(c->inner, blk, buf);
}

static bfs_err_t counting_write(bfs_bio_t *b, bfs_blk_t blk, const void *buf)
{
    return bfs_bio_write(((counting_bio_t *)b)->inner, blk, buf);
}

static bfs_err_t counting_sync(bfs_bio_t *b)
{
    return bfs_bio_sync(((counting_bio_t *)b)->inner);
}

static void counting_close(bfs_bio_t *b) { (void)b; }

static const bfs_bio_ops_t counting_ops = {
    .read_block = counting_read, .write_block = counting_write,
    .sync = counting_sync, .close = counting_close,
};

static void test_comment_flag_follows_entry(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("noted", &ino, &file));
    char comment[80];
    bfs_inode_t inode;

    /* Without the flag a lookup does not touch the directory tree. */
    counting_bio_t counting = { .base = *bio, .inner = bio, .reads = 0 };
    counting.base.ops = &counting_ops;
    bfs_bio_t *dir_bio = fs.dir_tree.tree.bio;
    fs.dir_tree.tree.bio = &counting.base;
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(counting.reads, 0);
    fs.dir_tree.tree.bio = dir_bio;

    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "first", 5), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT(bfs_be32(inode.flags) & BFS_INODE_FLAG_HAS_COMMENT);
    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "second", 6), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_OK);
    TEST_ASSERT_MEM_EQ(comment, "second", 7);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_OK);
    TEST_ASSERT_MEM_EQ(comment, "second", 7);

    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, NULL, 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT(!(bfs_be32(inode.flags) & BFS_INODE_FLAG_HAS_COMMENT));
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_ERR_NOTFOUND);

    /* Deleting a commented file and directory leaves no hidden entry. */
    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "gone", 4), BFS_OK);
    uint32_t dir_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(&fs, BFS_ROOT_INO, "dir", 3, &dir_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, dir_ino, "dir note", 8), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "noted", 5), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_rmdir(&fs, BFS_ROOT_INO, "dir", 3), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(check_clean());
    bool any = true;
    TEST_ASSERT_EQ(bfs_dir_may_have_entries(&fs.dir_tree, ino | 0x80000000u, &any), BFS_OK);
    TEST_ASSERT(!any);
    teardown(true);
}

static void test_checker_verifies_comment_flag(void)
{
    TEST_ASSERT(setup(0));
    uint32_t ino;
    bfs_file_t file;
    TEST_ASSERT(create_open("flagged", &ino, &file));
    TEST_ASSERT_EQ(bfs_fs_set_comment(&fs, ino, "note", 4), BFS_OK);
    TEST_ASSERT(check_no_errors());
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);

    /* Entry without flag. */
    bfs_inode_t cleared = inode;
    cleared.flags = 0;
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &cleared), BFS_OK);
    TEST_ASSERT(check_has_errors());

    /* Flag without entry: also refused by readers. */
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT(check_no_errors());
    TEST_ASSERT_EQ(bfs_dir_remove(&fs.dir_tree, ino | 0x80000000u, "note", 4), BFS_OK);
    TEST_ASSERT(check_has_errors());
    char comment[80];
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, comment, sizeof(comment)), BFS_ERR_CORRUPT);

    /* An entry whose inode does not exist. */
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, ino | 0x80000000u, "note", 4, ino, 0), BFS_OK);
    TEST_ASSERT(check_no_errors());
    TEST_ASSERT_EQ(bfs_dir_insert(&fs.dir_tree, 999u | 0x80000000u, "stray", 5, 999, 0), BFS_OK);
    TEST_ASSERT(check_has_errors());
    teardown(false);
}

TEST_SUITE_BEGIN("Format v3 inline extents and inode flags")
    TEST_RUN(test_contiguous_runs_stay_inline);
    TEST_RUN(test_gap_and_discontiguity_convert_to_tree);
    TEST_RUN(test_remap_splits_or_replaces_inline_extent);
    TEST_RUN(test_truncate_shortens_and_clears_inline_extent);
    TEST_RUN(test_checksummed_volume_inlines_one_block);
    TEST_RUN(test_second_handle_sees_inline_growth);
    TEST_RUN(test_delete_reclaims_inline_blocks);
    TEST_RUN(test_snapshot_shares_and_copies_inline_blocks);
    TEST_RUN(test_inode_validation_rules);
    TEST_RUN(test_inline_range_avoids_reserved_blocks);
    TEST_RUN(test_checker_rejects_shared_inline_blocks);
    TEST_RUN(test_comment_flag_follows_entry);
    TEST_RUN(test_checker_verifies_comment_flag);
TEST_SUITE_END()
