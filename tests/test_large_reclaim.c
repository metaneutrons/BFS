/* SPDX-License-Identifier: MPL-2.0 */
#include "test_harness.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <unistd.h>

#define IMAGE "test_large_reclaim.img"
#define BS 4096u
#define DATA_BLOCKS (BFS_PENDING_FREES_MAX + 128u)

static bfs_fs_t fs;

static void test_large_reclaim_units(void)
{
    unlink(IMAGE);
    bfs_bio_t *bio = bio_emu_create(IMAGE, BS, 65536u);
    TEST_ASSERT(bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_format(bio, "LargeReclaim", 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    uint32_t free_before = fs.freespace.total_free;
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "large", 5, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    bfs_blk_t first;
    TEST_ASSERT_EQ(bfs_extent_append(&file.extents, 0, DATA_BLOCKS, &first), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    inode.extent_root = bfs_be32(file.extents.tree.root);
    inode.size_lo = bfs_be32(DATA_BLOCKS * BS);
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);

    TEST_ASSERT_EQ(bfs_snapshot_create(&fs, "large-snapshot"), BFS_OK);
    TEST_ASSERT_EQ(bfs_refcount_get(&fs.refcount, first), 2);
    TEST_ASSERT_EQ(bfs_snapshot_delete(&fs, 1), BFS_OK);
    TEST_ASSERT(fs.pending_frees_dynamic != NULL);
    TEST_ASSERT(!fs.has_snapshots);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT(fs.pending_frees_dynamic == NULL);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);

    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_truncate(&file, 0), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(file.size, 0);

    TEST_ASSERT_EQ(bfs_extent_append(&file.extents, 0, DATA_BLOCKS, &first), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    inode.extent_root = bfs_be32(file.extents.tree.root);
    inode.size_lo = bfs_be32(DATA_BLOCKS * BS);
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_delete_file(&fs, BFS_ROOT_INO, "large", 5), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(fs.freespace.total_free + 8u >= free_before);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
    TEST_ASSERT(fs.freespace.total_free + 8u >= free_before);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(bio);
    unlink(IMAGE);
}

TEST_SUITE_BEGIN("Large Reclaim Units")
    TEST_RUN(test_large_reclaim_units);
TEST_SUITE_END()
