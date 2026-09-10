/*
 * BFS — Directory operations tests
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "bfs_file.h"
#include "bfs_inode.h"
#include "block_device_emu.h"
#include <string.h>
#include <unistd.h>

#define TEST_IMG "test_dirops.img"
#define BLK_SIZE 4096
#define BLK_COUNT 8192

static bfs_fs_t g_fs;

static bfs_fs_t *setup(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    bfs_fs_format(bio, "DirOps", 0);
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

/* ── Test: create file ─────────────────────────────────────── */

static void test_create_file(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "test.txt", 8, &ino), BFS_OK);
    TEST_ASSERT(ino > BFS_ROOT_INO);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "test.txt", 8, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, ino);
    TEST_ASSERT_EQ(type, BFS_INODE_FILE);

    /* Duplicate should fail */
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "test.txt", 8, &ino), BFS_ERR_EXISTS);

    teardown(fs);
}

/* ── Test: mkdir ───────────────────────────────────────────── */

static void test_mkdir(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "subdir", 6, &ino), BFS_OK);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "subdir", 6, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(type, BFS_INODE_DIR);

    /* Create file inside subdir */
    uint32_t file_ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, ino, "inner.txt", 9, &file_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, ino, "inner.txt", 9, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, file_ino);

    teardown(fs);
}

/* ── Test: rmdir ───────────────────────────────────────────── */

static void test_rmdir(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_mkdir(fs, BFS_ROOT_INO, "empty_dir", 9, &ino);

    /* Remove empty dir */
    TEST_ASSERT_EQ(bfs_fs_rmdir(fs, BFS_ROOT_INO, "empty_dir", 9), BFS_OK);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "empty_dir", 9, &found_ino, &type), BFS_ERR_NOTFOUND);

    /* Non-empty dir should fail */
    bfs_fs_mkdir(fs, BFS_ROOT_INO, "full_dir", 8, &ino);
    uint32_t file_ino;
    bfs_fs_create_file(fs, ino, "file.txt", 8, &file_ino);
    TEST_ASSERT_EQ(bfs_fs_rmdir(fs, BFS_ROOT_INO, "full_dir", 8), BFS_ERR_NOTEMPTY);

    teardown(fs);
}

/* ── Test: delete file ─────────────────────────────────────── */

static void test_delete_file(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "doomed.txt", 10, &ino);
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "doomed.txt", 10), BFS_OK);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "doomed.txt", 10, &found_ino, &type), BFS_ERR_NOTFOUND);

    /* Delete non-existent should fail */
    TEST_ASSERT_EQ(bfs_fs_delete_file(fs, BFS_ROOT_INO, "nope", 4), BFS_ERR_NOTFOUND);

    teardown(fs);
}

/* ── Test: rename within same directory ────────────────────── */

static void test_rename_same_dir(void)
{
    bfs_fs_t *fs = setup();

    uint32_t ino;
    bfs_fs_create_file(fs, BFS_ROOT_INO, "old.txt", 7, &ino);

    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, "old.txt", 7,
                                      BFS_ROOT_INO, "new.txt", 7), BFS_OK);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "old.txt", 7, &found_ino, &type), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "new.txt", 7, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, ino);

    teardown(fs);
}

/* ── Test: rename across directories ───────────────────────── */

static void test_rename_cross_dir(void)
{
    bfs_fs_t *fs = setup();

    uint32_t dir_ino, file_ino;
    bfs_fs_mkdir(fs, BFS_ROOT_INO, "dest", 4, &dir_ino);
    bfs_fs_create_file(fs, BFS_ROOT_INO, "moveme.txt", 10, &file_ino);

    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, "moveme.txt", 10,
                                      dir_ino, "moved.txt", 9), BFS_OK);

    uint32_t found_ino, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "moveme.txt", 10, &found_ino, &type), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, dir_ino, "moved.txt", 9, &found_ino, &type), BFS_OK);
    TEST_ASSERT_EQ(found_ino, file_ino);

    teardown(fs);
}

typedef struct {
    const char *name;
    uint8_t length;
    bool found;
} name_scan_t;

static bool find_name(const char *name, uint8_t length, uint32_t inode,
                      uint32_t type, void *opaque)
{
    (void)inode;
    (void)type;
    name_scan_t *scan = opaque;
    scan->found = length == scan->length && memcmp(name, scan->name, length) == 0;
    return !scan->found;
}

static void test_rename_replaces_file_and_preserves_open_target(void)
{
    bfs_fs_t *fs = setup();
    uint32_t source, target, orphan;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "source", 6, &source), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "target", 6, &target), BFS_OK);
    bfs_file_t target_handle;
    TEST_ASSERT_EQ(bfs_file_open(&target_handle, fs, target), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&target_handle, "old", 3), 3);

    bfs_rename_options_t options = {
        .preserve_replaced = true,
        .orphan_ino_out = &orphan,
    };
    TEST_ASSERT_EQ(bfs_fs_rename_replace(fs, BFS_ROOT_INO, "source", 6,
                                         BFS_ROOT_INO, "target", 6, &options), BFS_OK);
    TEST_ASSERT_EQ(orphan, target);
    uint32_t found, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "target", 6,
                                  &found, &type), BFS_OK);
    TEST_ASSERT_EQ(found, source);
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "source", 6,
                                  NULL, NULL), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(bfs_file_mark_unlinked(&target_handle), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_seek(&target_handle, 0, BFS_SEEK_SET), 0);
    char data[4] = {0};
    TEST_ASSERT_EQ(bfs_file_read(&target_handle, data, sizeof(data)), 3);
    TEST_ASSERT_MEM_EQ(data, "old", 3);
    TEST_ASSERT_EQ(bfs_fs_reap_unlinked_file(fs, orphan), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, target, &inode), BFS_ERR_NOTFOUND);
    teardown(fs);
}

static void test_rename_replaces_empty_directory_and_rekeys_case(void)
{
    bfs_fs_t *fs = setup();
    uint32_t source, target;
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "source", 6, &source), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, "target", 6, &target), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, "source", 6,
                                 BFS_ROOT_INO, "target", 6), BFS_OK);
    uint32_t found, type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, "target", 6,
                                  &found, &type), BFS_OK);
    TEST_ASSERT_EQ(found, source);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs->inode_tree, target, &inode), BFS_ERR_NOTFOUND);

    uint32_t file;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, "mixed", 5, &file), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, "mixed", 5,
                                 BFS_ROOT_INO, "MIXED", 5), BFS_OK);
    name_scan_t scan = { .name = "MIXED", .length = 5, .found = false };
    TEST_ASSERT_EQ(bfs_dir_scan(&fs->dir_tree, BFS_ROOT_INO, find_name, &scan), BFS_OK);
    TEST_ASSERT(scan.found);
    teardown(fs);
}

TEST_SUITE_BEGIN("Directory Operations")
    TEST_RUN(test_create_file);
    TEST_RUN(test_mkdir);
    TEST_RUN(test_rmdir);
    TEST_RUN(test_delete_file);
    TEST_RUN(test_rename_same_dir);
    TEST_RUN(test_rename_cross_dir);
    TEST_RUN(test_rename_replaces_file_and_preserves_open_target);
    TEST_RUN(test_rename_replaces_empty_directory_and_rekeys_case);
TEST_SUITE_END()
