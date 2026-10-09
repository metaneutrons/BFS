/* SPDX-License-Identifier: MPL-2.0 */
/* Regression coverage for inode reuse within one file-write operation. */

#include "test_harness.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_inode.h"
#include "bfs_btree.h"
#include "block_device_emu.h"
#include <unistd.h>

#define REUSE_IMAGE "test_file_inode_reuse.img"
#define REUSE_BLOCK_SIZE 4096
#define REUSE_BLOCK_COUNT 2048

typedef struct {
    bfs_fs_t fs;
    bfs_bio_t *bio;
} inode_reuse_fixture_t;

typedef struct {
    bfs_inode_stamp_t value;
    uint32_t calls;
} reuse_stamp_source_t;

static uint32_t inode_compare_calls;

static bool fixture_open(inode_reuse_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    (void)unlink(REUSE_IMAGE);
    fixture->bio = bio_emu_create(REUSE_IMAGE, REUSE_BLOCK_SIZE,
                                  REUSE_BLOCK_COUNT);
    if (!fixture->bio) return false;
    if (bfs_fs_format(fixture->bio, "InodeReuse", 0) != BFS_OK) {
        bfs_bio_close(fixture->bio);
        fixture->bio = NULL;
        (void)unlink(REUSE_IMAGE);
        return false;
    }
    if (bfs_fs_mount(&fixture->fs, fixture->bio) != BFS_OK) {
        bfs_bio_close(fixture->bio);
        fixture->bio = NULL;
        (void)unlink(REUSE_IMAGE);
        return false;
    }
    return true;
}

static void fixture_close(inode_reuse_fixture_t *fixture)
{
    if (fixture->fs.mounted) (void)bfs_fs_unmount(&fixture->fs);
    if (fixture->bio) bfs_bio_close(fixture->bio);
    fixture->bio = NULL;
    (void)unlink(REUSE_IMAGE);
}

static void sample_reuse_stamp(void *context, bfs_inode_stamp_t *stamp)
{
    reuse_stamp_source_t *source = (reuse_stamp_source_t *)context;
    source->calls++;
    *stamp = source->value;
}

static int compare_inode_key_counted(const void *a, const void *b)
{
    inode_compare_calls++;
    return bfs_btree_key_compare_be32(a, b);
}

static void reset_inode_compare_probe(bfs_btree_t *tree)
{
    inode_compare_calls = 0;
    tree->hint_leaf = BFS_BLK_NULL;
    tree->hint_generation = 0;
}

static void test_repeated_appends_from_two_stale_handles(void)
{
    enum { ITERATIONS = 6, APPEND_CAPACITY = 3 * (1024 + REUSE_BLOCK_SIZE) };
    inode_reuse_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));

    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "append", 6, &ino), BFS_OK);
    bfs_file_t first, second;
    TEST_ASSERT_EQ(bfs_file_open(&first, &fixture.fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&second, &fixture.fs, ino), BFS_OK);

    uint8_t expected[APPEND_CAPACITY];
    uint8_t actual[APPEND_CAPACITY];
    uint8_t chunk[REUSE_BLOCK_SIZE];
    const uint32_t append_sizes[ITERATIONS] = {
        1024, REUSE_BLOCK_SIZE, 1024, REUSE_BLOCK_SIZE, 1024, REUSE_BLOCK_SIZE,
    };
    uint32_t expected_size = 0;
    for (uint32_t iteration = 0; iteration < ITERATIONS; iteration++) {
        uint32_t length = append_sizes[iteration];
        for (uint32_t i = 0; i < length; i++) {
            uint8_t byte = (uint8_t)((expected_size + i) * 31u ^
                                     ((expected_size + i) >> 4) ^
                                     (iteration * 53u));
            chunk[i] = byte;
            expected[expected_size + i] = byte;
        }
        bfs_file_t *writer = (iteration & 1u) ? &second : &first;
        TEST_ASSERT_EQ(bfs_file_append(writer, chunk, length), (int32_t)length);
        expected_size += length;
    }

    TEST_ASSERT_EQ(expected_size, sizeof(expected));
    TEST_ASSERT_EQ(bfs_file_seek(&first, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_read(&first, actual, expected_size),
                   (int32_t)expected_size);
    TEST_ASSERT_MEM_EQ(actual, expected, expected_size);
    fixture_close(&fixture);
}

static void set_inode_stamps(bfs_inode_t *inode,
                             const bfs_inode_stamp_t *creation,
                             const bfs_inode_stamp_t *modification)
{
    inode->create_days = bfs_be16(creation->days);
    inode->create_mins = bfs_be16(creation->mins);
    inode->create_ticks = bfs_be16(creation->ticks);
    inode->modify_days = bfs_be16(modification->days);
    inode->modify_mins = bfs_be16(modification->mins);
    inode->modify_ticks = bfs_be16(modification->ticks);
}

static void test_stamped_write_preserves_current_metadata_and_comment(void)
{
    inode_reuse_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "metadata", 8, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);

    /* Establish the comment after opening the handle, then set other inode
     * metadata to values that the write path must preserve. */
    TEST_ASSERT_EQ(bfs_fs_set_comment(&fixture.fs, ino, "keep comment", 12), BFS_OK);
    bfs_inode_t before;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &before), BFS_OK);
    const bfs_inode_stamp_t creation = {.days = 0x0102, .mins = 0x0304, .ticks = 0x0506};
    const bfs_inode_stamp_t modification = {.days = 0x1112, .mins = 0x1314, .ticks = 0x1516};
    set_inode_stamps(&before, &creation, &modification);
    before.uid = bfs_be16(0x2345);
    before.gid = bfs_be16(0xABCD);
    before.protection = bfs_be32(0xA5A5000Fu);
    TEST_ASSERT_EQ(bfs_be32(before.flags) & BFS_INODE_FLAG_HAS_COMMENT,
                   BFS_INODE_FLAG_HAS_COMMENT);
    TEST_ASSERT_EQ(bfs_inode_write(&fixture.fs.inode_tree, ino, &before), BFS_OK);

    const uint8_t payload[] = "fresh inode fields";
    reuse_stamp_source_t source = {
        .value = {.days = 0x7654, .mins = 0x3210, .ticks = 0xABCD},
        .calls = 0,
    };
    const uint32_t protection_clear = 0x00000005u;
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, payload, sizeof(payload) - 1,
                                             sample_reuse_stamp, &source,
                                             protection_clear),
                   (int32_t)(sizeof(payload) - 1));
    TEST_ASSERT_EQ(source.calls, 1);

    bfs_inode_t after;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &after), BFS_OK);
    TEST_ASSERT_EQ(after.inode_nr, before.inode_nr);
    TEST_ASSERT_EQ(after.type, before.type);
    TEST_ASSERT_EQ(after.link_count, before.link_count);
    TEST_ASSERT_EQ(after.uid, before.uid);
    TEST_ASSERT_EQ(after.gid, before.gid);
    TEST_ASSERT_EQ(after.create_days, before.create_days);
    TEST_ASSERT_EQ(after.create_mins, before.create_mins);
    TEST_ASSERT_EQ(after.create_ticks, before.create_ticks);
    TEST_ASSERT_EQ(bfs_be32(after.protection),
                   bfs_be32(before.protection) & ~protection_clear);
    TEST_ASSERT_EQ(bfs_be16(after.modify_days), source.value.days);
    TEST_ASSERT_EQ(bfs_be16(after.modify_mins), source.value.mins);
    TEST_ASSERT_EQ(bfs_be16(after.modify_ticks), source.value.ticks);
    TEST_ASSERT_EQ(bfs_be32(after.flags) & BFS_INODE_FLAG_HAS_COMMENT,
                   BFS_INODE_FLAG_HAS_COMMENT);
    TEST_ASSERT_EQ(bfs_be32(after.size_lo), sizeof(payload) - 1);

    char comment[80] = {0};
    TEST_ASSERT_EQ(bfs_fs_get_comment(&fixture.fs, ino, comment, sizeof(comment)), BFS_OK);
    TEST_ASSERT_MEM_EQ(comment, "keep comment", sizeof("keep comment"));
    fixture_close(&fixture);
}

static void test_stamped_write_to_open_unlinked_inode(void)
{
    inode_reuse_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "orphan", 6, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write(&file, "old", 3), 3);

    uint32_t orphan_ino = 0;
    TEST_ASSERT_EQ(bfs_fs_unlink_open_file(&fixture.fs, BFS_ROOT_INO,
                                           "orphan", 6, &orphan_ino), BFS_OK);
    TEST_ASSERT_EQ(orphan_ino, ino);
    TEST_ASSERT_EQ(bfs_file_mark_unlinked(&file), BFS_OK);

    reuse_stamp_source_t source = {
        .value = {.days = 61, .mins = 62, .ticks = 63},
        .calls = 0,
    };
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, "new", 3,
                                             sample_reuse_stamp, &source, 0), 3);
    TEST_ASSERT_EQ(source.calls, 1);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read_unlinked(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 0);
    TEST_ASSERT_EQ(bfs_be32(inode.size_lo), 6);
    TEST_ASSERT_EQ(bfs_be16(inode.modify_days), source.value.days);
    TEST_ASSERT_EQ(bfs_be16(inode.modify_mins), source.value.mins);
    TEST_ASSERT_EQ(bfs_be16(inode.modify_ticks), source.value.ticks);

    char content[6] = {0};
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_read(&file, content, sizeof(content)), sizeof(content));
    TEST_ASSERT_MEM_EQ(content, "oldnew", sizeof(content));
    fixture_close(&fixture);
}

static void test_write_reuses_the_refresh_inode_lookup(void)
{
    inode_reuse_fixture_t fixture;
    TEST_ASSERT(fixture_open(&fixture));
    uint32_t ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "probe", 5, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_OK);

    /* Count B+tree key comparisons through a test-local comparator wrapper.
     * A one-byte write has one refresh lookup and one inode update. Reuse of
     * the refreshed value removes only the second lookup; BIO read counts are
     * intentionally not used as the oracle. */
    bfs_btree_t *tree = &fixture.fs.inode_tree;
    const bfs_btree_ops_t *saved_ops = tree->ops;
    bfs_btree_ops_t counting_ops = *saved_ops;
    counting_ops.key_compare = compare_inode_key_counted;
    tree->ops = &counting_ops;

    reset_inode_compare_probe(tree);
    bfs_err_t lookup_err = bfs_inode_read(tree, ino, &inode);
    uint32_t lookup_comparisons = inode_compare_calls;

    reset_inode_compare_probe(tree);
    bfs_err_t update_err = bfs_inode_write(tree, ino, &inode);
    uint32_t update_comparisons = inode_compare_calls;
    bfs_err_t sync_err = bfs_fs_sync(&fixture.fs);

    reuse_stamp_source_t source = {
        .value = {.days = 7, .mins = 8, .ticks = 9},
        .calls = 0,
    };
    reset_inode_compare_probe(tree);
    int32_t write_result = bfs_file_write_with_stamp(&file, "x", 1,
                                                     sample_reuse_stamp, &source, 0);
    uint32_t operation_comparisons = inode_compare_calls;
    tree->ops = saved_ops;

    fixture_close(&fixture);
    TEST_ASSERT_EQ(lookup_err, BFS_OK);
    TEST_ASSERT_EQ(update_err, BFS_OK);
    TEST_ASSERT_EQ(sync_err, BFS_OK);
    TEST_ASSERT_EQ(write_result, 1);
    TEST_ASSERT_EQ(source.calls, 1);
    TEST_ASSERT(lookup_comparisons > 0);
    TEST_ASSERT(update_comparisons > 0);
    TEST_ASSERT_EQ(operation_comparisons, lookup_comparisons + update_comparisons);
}

TEST_SUITE_BEGIN("File Inode Reuse")
    TEST_RUN(test_repeated_appends_from_two_stale_handles);
    TEST_RUN(test_stamped_write_preserves_current_metadata_and_comment);
    TEST_RUN(test_stamped_write_to_open_unlinked_inode);
    TEST_RUN(test_write_reuses_the_refresh_inode_lookup);
TEST_SUITE_END()
