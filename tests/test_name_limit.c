/*
 * BFS — The per-mount limit for new names (BFS_SHORT_NAME_MAX by default in
 * the adapters, BFS_NAME_MAX with their long-name option).
 */

#include "test_harness.h"
#include "bfs_fs.h"
#include "block_device_emu.h"
#include <string.h>
#include <unistd.h>

#define TEST_IMG "test_name_limit.img"
#define BLK_SIZE 4096
#define BLK_COUNT 2048

static bfs_fs_t g_fs;

static bfs_fs_t *setup(void)
{
    unlink(TEST_IMG);
    bfs_bio_t *bio = bio_emu_create(TEST_IMG, BLK_SIZE, BLK_COUNT);
    if (!bio || bfs_fs_format(bio, "Names", 0) != BFS_OK ||
        bfs_fs_mount(&g_fs, bio) != BFS_OK)
        return NULL;
    return &g_fs;
}

static void teardown(bfs_fs_t *fs)
{
    bfs_bio_t *bio = fs->bio;
    bfs_fs_unmount(fs);
    bfs_bio_close(bio);
    unlink(TEST_IMG);
}

static void fill(char *name, uint8_t len, char c)
{
    memset(name, c, len);
}

static void test_core_default_allows_255(void)
{
    bfs_fs_t *fs = setup();
    TEST_ASSERT(fs != NULL);
    if (!fs) return;
    TEST_ASSERT_EQ(fs->name_max, BFS_NAME_MAX);
    char name[BFS_NAME_MAX];
    fill(name, BFS_NAME_MAX, 'a');
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, BFS_NAME_MAX, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_set_name_limit(fs, 0), BFS_ERR_INVAL);
    teardown(fs);
}

static void test_short_limit_refuses_new_long_names(void)
{
    bfs_fs_t *fs = setup();
    TEST_ASSERT(fs != NULL);
    if (!fs) return;
    char longest[BFS_NAME_MAX], name[BFS_NAME_MAX];
    uint32_t file, dir, ino;

    /* Names created while long names were allowed. */
    fill(longest, BFS_NAME_MAX, 'L');
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, longest, BFS_NAME_MAX, &file), BFS_OK);
    fill(name, BFS_SHORT_NAME_MAX + 1, 'D');
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, name, BFS_SHORT_NAME_MAX + 1, &dir), BFS_OK);

    TEST_ASSERT_EQ(bfs_fs_set_name_limit(fs, BFS_SHORT_NAME_MAX), BFS_OK);

    /* 107 bytes is allowed everywhere. */
    fill(name, BFS_SHORT_NAME_MAX, 's');
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, BFS_SHORT_NAME_MAX, &ino), BFS_OK);

    /* 108 bytes is refused for every operation that makes a name. */
    fill(name, BFS_SHORT_NAME_MAX + 1, 'n');
    const uint8_t too_long = BFS_SHORT_NAME_MAX + 1;
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, too_long, &ino),
                   BFS_ERR_NAME_TOO_LONG);
    TEST_ASSERT_EQ(bfs_fs_mkdir(fs, BFS_ROOT_INO, name, too_long, &ino), BFS_ERR_NAME_TOO_LONG);
    TEST_ASSERT_EQ(bfs_fs_make_hardlink(fs, BFS_ROOT_INO, name, too_long, file),
                   BFS_ERR_NAME_TOO_LONG);
    TEST_ASSERT_EQ(bfs_fs_make_softlink(fs, BFS_ROOT_INO, name, too_long, "target", 6),
                   BFS_ERR_NAME_TOO_LONG);
    fill(name, BFS_SHORT_NAME_MAX, 's');
    char target[BFS_NAME_MAX];
    fill(target, too_long, 'r');
    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, name, BFS_SHORT_NAME_MAX,
                                 BFS_ROOT_INO, target, too_long), BFS_ERR_NAME_TOO_LONG);

    /* Existing long names stay usable: lookup, rename to a short name, and
     * delete. A case-only rename makes a new spelling and is refused. */
    uint32_t type;
    TEST_ASSERT_EQ(bfs_dir_lookup(&fs->dir_tree, BFS_ROOT_INO, longest, BFS_NAME_MAX,
                                  &ino, &type), BFS_OK);
    TEST_ASSERT_EQ(ino, file);
    char lower[BFS_NAME_MAX];
    fill(lower, BFS_NAME_MAX, 'l');
    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, longest, BFS_NAME_MAX,
                                 BFS_ROOT_INO, lower, BFS_NAME_MAX), BFS_ERR_NAME_TOO_LONG);
    TEST_ASSERT_EQ(bfs_fs_rename(fs, BFS_ROOT_INO, longest, BFS_NAME_MAX,
                                 BFS_ROOT_INO, "short", 5), BFS_OK);
    fill(name, BFS_SHORT_NAME_MAX + 1, 'D');
    TEST_ASSERT_EQ(bfs_fs_rmdir(fs, BFS_ROOT_INO, name, BFS_SHORT_NAME_MAX + 1), BFS_OK);

    /* The limit is per mount: the long-name option restores 255. */
    TEST_ASSERT_EQ(bfs_fs_set_name_limit(fs, BFS_NAME_MAX), BFS_OK);
    fill(name, BFS_NAME_MAX, 'n');
    TEST_ASSERT_EQ(bfs_fs_create_file(fs, BFS_ROOT_INO, name, BFS_NAME_MAX, &ino), BFS_OK);
    teardown(fs);
}

TEST_SUITE_BEGIN("Name limit")
    TEST_RUN(test_core_default_allows_255);
    TEST_RUN(test_short_limit_refuses_new_long_names);
TEST_SUITE_END()
