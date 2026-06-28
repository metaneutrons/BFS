/*
 * BFS — Tests for new features: hard links, soft links, comments,
 *         parent tracking, link_count in delete, fsck integration
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "block_device_emu.h"
#include <unistd.h>
#include <stdio.h>

#define TEST_IMG "test_features.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192

static bfs_fs_t g_fs;

static bfs_fs_t *setup(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "FeatureTest", 0);
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

/* ── Hard links ────────────────────────────────────────────── */

static void test_hardlink_create(void)
{
    bfs_fs_t *fs = setup();

    /* Create a file */
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "original.txt", 12, &ino), BFS_OK);

    /* Write data to it */
    bfs_file_t f;
    TEST_ASSERT_EQ(bfs_file_open(&f, fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&f, "hello", 5), 5);

    /* Create hard link */
    TEST_ASSERT_EQ(bfs_fs_make_hardlink(fs, BFS_ROOT_INO, "link.txt", 8, ino), BFS_OK);

    /* Both names should resolve to the same inode */
    uint32_t ino1, ino2, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "original.txt", 12, &ino1, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "link.txt", 8, &ino2, &type), BFS_OK);
    TEST_ASSERT_EQ(ino1, ino2);
    TEST_ASSERT_EQ(type, BFS_INODE_FILE);

    /* Link count should be 2 */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 2);

    teardown(fs);
}

static void test_hardlink_delete_preserves_data(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "file.txt", 8, &ino);

    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    bfs_file_write(&f, "data!", 5);

    bfs_fs_make_hardlink(fs, BFS_ROOT_INO, "link.txt", 8, ino);

    /* Delete original — data should survive via link */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "file.txt", 8), BFS_OK);

    /* Link should still exist */
    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "link.txt", 8, &found_ino, &type), BFS_OK);

    /* Inode should still exist with link_count=1 */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 1);

    /* Data should still be readable */
    bfs_file_open(&f, fs, ino);
    char buf[8] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&f, buf, 5), 5);
    TEST_ASSERT_MEM_EQ(buf, "data!", 5);

    /* Delete the link — now inode should be freed */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "link.txt", 8), BFS_OK);

    /* Inode should be gone */
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, ino, &inode), BFS_ERR_NOTFOUND);

    teardown(fs);
}

/* ── Soft links ────────────────────────────────────────────── */

static void test_softlink_create_read(void)
{
    bfs_fs_t *fs = setup();

    /* Create a soft link */
    const char *target = "subdir/target.txt";
    TEST_ASSERT_EQ(bfs_fs_make_softlink(fs, BFS_ROOT_INO, "mylink", 6,
                                         target, 17), BFS_OK);

    /* Verify it's in the directory */
    uint32_t ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "mylink", 6, &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(type, BFS_INODE_SOFTLINK);

    /* Read the link target */
    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    char buf[64] = {0};
    int32_t n = bfs_file_read(&f, buf, 64);
    TEST_ASSERT_EQ(n, 17);
    TEST_ASSERT_MEM_EQ(buf, target, 17);

    teardown(fs);
}

/* ── File comments ─────────────────────────────────────────── */

static void test_comment_set_get(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "noted.txt", 9, &ino);

    /* Set comment */
    TEST_ASSERT_EQ(bfs_fs_set_comment(fs, ino, "This is important", 17), BFS_OK);

    /* Get comment */
    char buf[80] = {0};
    TEST_ASSERT_EQ(bfs_fs_get_comment(fs, ino, buf, 80), BFS_OK);
    TEST_ASSERT_MEM_EQ(buf, "This is important", 17);

    /* Update comment */
    TEST_ASSERT_EQ(bfs_fs_set_comment(fs, ino, "Updated", 7), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_get_comment(fs, ino, buf, 80), BFS_OK);
    TEST_ASSERT_MEM_EQ(buf, "Updated", 7);

    teardown(fs);
}

static void test_comment_get_truncates_to_buffer(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "tinybuf", 7, &ino);
    TEST_ASSERT_EQ(bfs_fs_set_comment(fs, ino, "This is important", 17), BFS_OK);

    char buf[5] = {0x55, 0x55, 0x55, 0x55, 0x55};
    TEST_ASSERT_EQ(bfs_fs_get_comment(fs, ino, buf, sizeof(buf)), BFS_OK);
    TEST_ASSERT_MEM_EQ(buf, "This", 4);
    TEST_ASSERT_EQ(buf[4], 0);

    teardown(fs);
}

/* ── Parent directory tracking (..) ────────────────────────── */

static void test_parent_tracking(void)
{
    bfs_fs_t *fs = setup();

    /* Create nested dirs: root/a/b */
    uint32_t a_ino, b_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "a", 1, &a_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, a_ino, "b", 1, &b_ino), BFS_OK);

    /* Verify '..' in 'b' points to 'a' */
    uint32_t parent_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, b_ino, "..", 2, &parent_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(parent_ino, a_ino);

    /* Verify '..' in 'a' points to root */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, a_ino, "..", 2, &parent_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(parent_ino, BFS_ROOT_INO);

    teardown(fs);
}

static void test_rmdir_removes_inode_and_parent_entry(void)
{
    bfs_fs_t *fs = setup();

    uint32_t dir_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "gone", 4, &dir_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_rmdir(fs, BFS_ROOT_INO, "gone", 4), BFS_OK);

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, dir_ino, &inode), BFS_ERR_NOTFOUND);
    uint32_t parent, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, dir_ino, "..", 2, &parent, &type), BFS_ERR_NOTFOUND);

    teardown(fs);
}

static void test_rename_directory_updates_parent(void)
{
    bfs_fs_t *fs = setup();

    uint32_t a_ino, b_ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "a", 1, &a_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "b", 1, &b_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, "b", 1, a_ino, "b", 1), BFS_OK);

    uint32_t parent, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, b_ino, "..", 2, &parent, &type), BFS_OK);
    TEST_ASSERT_EQ(parent, a_ino);

    teardown(fs);
}

/* ── Delete with link_count ────────────────────────────────── */

static void test_delete_frees_blocks(void)
{
    bfs_fs_t *fs = setup();

    uint32_t free_before = fs->freespace.total_free;

    /* Create file and write data */
    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "big.dat", 7, &ino);
    bfs_file_t f;
    bfs_file_open(&f, fs, ino);
    uint8_t block[BLK_SIZE];
    memset(block, 0xCC, BLK_SIZE);
    for (int i = 0; i < 10; i++)
        bfs_file_write(&f, block, BLK_SIZE);

    uint32_t free_after_write = fs->freespace.total_free;
    TEST_ASSERT(free_after_write < free_before); /* blocks consumed */

    /* Delete file — blocks should be freed */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "big.dat", 7), BFS_OK);

    /* Free space should increase (data blocks returned) */
    TEST_ASSERT(fs->freespace.total_free > free_after_write);

    teardown(fs);
}

/* ── Persistence of new features across remount ────────────── */

static void test_features_persist(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "PersistTest", 0);

    bfs_fs_t fs;
    bfs_fs_mount(&fs, bio);

    /* Create file with comment and hard link */
    uint32_t ino;
    bfs_fs_create_file(&fs, BFS_ROOT_INO, "test.txt", 8, &ino);
    bfs_fs_set_comment(&fs, ino, "my comment", 10);
    bfs_fs_make_hardlink(&fs, BFS_ROOT_INO, "test_link", 9, ino);

    /* Create subdir with file */
    uint32_t dir_ino, file_ino;
    bfs_fs_mkdir(&fs, BFS_ROOT_INO, "sub", 3, &dir_ino);
    bfs_fs_create_file(&fs, dir_ino, "inner.txt", 9, &file_ino);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);

    /* Remount and verify */
    bio = bio_emu_open(TEST_IMG, BLK_SIZE);
    bfs_fs_mount(&fs, bio);

    /* Hard link should exist */
    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "test_link", 9, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, ino);

    /* Comment should persist */
    char buf[80] = {0};
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fs, ino, buf, 80), BFS_OK);
    TEST_ASSERT_MEM_EQ(buf, "my comment", 10);

    /* Subdir and inner file should exist */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "sub", 3, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, dir_ino, "inner.txt", 9, &found_ino, &type), BFS_OK);

    /* '..' in sub should point to root */
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs.dir_tree, dir_ino, "..", 2, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, BFS_ROOT_INO);

    bfs_fs_unmount(&fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

TEST_SUITE_BEGIN("New Features")
    TEST_RUN(test_hardlink_create);
    TEST_RUN(test_hardlink_delete_preserves_data);
    TEST_RUN(test_softlink_create_read);
    TEST_RUN(test_comment_set_get);
    TEST_RUN(test_comment_get_truncates_to_buffer);
    TEST_RUN(test_parent_tracking);
    TEST_RUN(test_rmdir_removes_inode_and_parent_entry);
    TEST_RUN(test_rename_directory_updates_parent);
    TEST_RUN(test_delete_frees_blocks);
    TEST_RUN(test_features_persist);
TEST_SUITE_END()
