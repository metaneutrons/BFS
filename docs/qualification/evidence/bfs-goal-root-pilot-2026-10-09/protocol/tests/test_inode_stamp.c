/* SPDX-License-Identifier: MPL-2.0 */
/* Focused persistence and completion tests for inode timestamps. */

#include "test_harness.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "block_device_emu.h"
#include <unistd.h>

#define STAMP_IMAGE "test_inode_stamp.img"
#define STAMP_BLOCK_SIZE 4096
#define STAMP_BLOCK_COUNT 4096

typedef struct {
    bfs_fs_t fs;
    bfs_bio_t *bio;
} inode_stamp_fixture_t;

typedef struct {
    bfs_inode_stamp_t value;
    uint32_t calls;
} stamp_source_t;

static bool fixture_open(inode_stamp_fixture_t *fixture, uint32_t options)
{
    memset(fixture, 0, sizeof(*fixture));
    (void)unlink(STAMP_IMAGE);
    fixture->bio = bio_emu_create(STAMP_IMAGE, STAMP_BLOCK_SIZE,
                                  STAMP_BLOCK_COUNT);
    if (!fixture->bio) return false;
    if (bfs_fs_format(fixture->bio, "InodeStamp", options) != BFS_OK) {
        bfs_bio_close(fixture->bio);
        fixture->bio = NULL;
        (void)unlink(STAMP_IMAGE);
        return false;
    }
    if (bfs_fs_mount(&fixture->fs, fixture->bio) != BFS_OK) {
        bfs_bio_close(fixture->bio);
        fixture->bio = NULL;
        (void)unlink(STAMP_IMAGE);
        return false;
    }
    return true;
}

static void fixture_close(inode_stamp_fixture_t *fixture)
{
    if (fixture->fs.mounted) (void)bfs_fs_unmount(&fixture->fs);
    if (fixture->bio) bfs_bio_close(fixture->bio);
    fixture->bio = NULL;
    (void)unlink(STAMP_IMAGE);
}

static void sample_stamp(void *context, bfs_inode_stamp_t *stamp)
{
    stamp_source_t *source = (stamp_source_t *)context;
    source->calls++;
    *stamp = source->value;
}

static bool inode_matches_modification_stamp(
    const bfs_inode_t *inode, const bfs_inode_stamp_t *stamp)
{
    return bfs_be16(inode->modify_days) == stamp->days &&
           bfs_be16(inode->modify_mins) == stamp->mins &&
           bfs_be16(inode->modify_ticks) == stamp->ticks;
}

static bool inode_matches_creation_stamp(
    const bfs_inode_t *inode, const bfs_inode_stamp_t *stamp)
{
    return bfs_be16(inode->create_days) == stamp->days &&
           bfs_be16(inode->create_mins) == stamp->mins &&
           bfs_be16(inode->create_ticks) == stamp->ticks;
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

static void test_create_stamp_encoding_survives_remount(void)
{
    inode_stamp_fixture_t fixture;
    stamp_source_t source = {
        .value = {.days = UINT16_MAX, .mins = 0x1234, .ticks = 0xABCD},
        .calls = 0,
    };
    uint32_t ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));

    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "boundary", 8,
                       sample_stamp, &source, &ino), BFS_OK);
    TEST_ASSERT(ino > BFS_ROOT_INO);
    TEST_ASSERT_EQ(source.calls, 1);

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(inode.create_days, bfs_be16(source.value.days));
    TEST_ASSERT_EQ(inode.create_mins, bfs_be16(source.value.mins));
    TEST_ASSERT_EQ(inode.create_ticks, bfs_be16(source.value.ticks));
    TEST_ASSERT_EQ(inode.modify_days, bfs_be16(source.value.days));
    TEST_ASSERT_EQ(inode.modify_mins, bfs_be16(source.value.mins));
    TEST_ASSERT_EQ(inode.modify_ticks, bfs_be16(source.value.ticks));
    TEST_ASSERT(inode_matches_creation_stamp(&inode, &source.value));
    TEST_ASSERT(inode_matches_modification_stamp(&inode, &source.value));

    TEST_ASSERT_EQ(bfs_fs_unmount(&fixture.fs), BFS_OK);
    bfs_bio_close(fixture.bio);
    fixture.bio = bio_emu_open(STAMP_IMAGE, STAMP_BLOCK_SIZE);
    TEST_ASSERT(fixture.bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount(&fixture.fs, fixture.bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT(inode_matches_creation_stamp(&inode, &source.value));
    TEST_ASSERT(inode_matches_modification_stamp(&inode, &source.value));
    TEST_ASSERT_EQ(source.calls, 1);

    fixture_close(&fixture);
}

static void test_create_rejections_and_null_wrapper_equivalence(void)
{
    inode_stamp_fixture_t fixture;
    stamp_source_t source = {
        .value = {.days = 10, .mins = 20, .ticks = 30},
        .calls = 0,
    };
    uint32_t ignored_ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));

    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, 9999, "missing", 7,
                       sample_stamp, &source, &ignored_ino), BFS_ERR_NOTFOUND);
    TEST_ASSERT_EQ(source.calls, 0);
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "bad/name", 8,
                       sample_stamp, &source, &ignored_ino), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(source.calls, 0);

    uint32_t legacy_ino = 0, null_stamp_ino = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "legacy", 6, &legacy_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "nullstamp", 9,
                       NULL, NULL, &null_stamp_ino), BFS_OK);
    bfs_inode_t legacy, null_stamp;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, legacy_ino, &legacy), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, null_stamp_ino, &null_stamp), BFS_OK);
    TEST_ASSERT_EQ(legacy.create_days, 0);
    TEST_ASSERT_EQ(legacy.create_mins, 0);
    TEST_ASSERT_EQ(legacy.create_ticks, 0);
    TEST_ASSERT_EQ(legacy.modify_days, 0);
    TEST_ASSERT_EQ(legacy.modify_mins, 0);
    TEST_ASSERT_EQ(legacy.modify_ticks, 0);
    TEST_ASSERT_EQ(null_stamp.create_days, legacy.create_days);
    TEST_ASSERT_EQ(null_stamp.create_mins, legacy.create_mins);
    TEST_ASSERT_EQ(null_stamp.create_ticks, legacy.create_ticks);
    TEST_ASSERT_EQ(null_stamp.modify_days, legacy.modify_days);
    TEST_ASSERT_EQ(null_stamp.modify_mins, legacy.modify_mins);
    TEST_ASSERT_EQ(null_stamp.modify_ticks, legacy.modify_ticks);
    bfs_inode_t normalized_legacy = legacy;
    normalized_legacy.inode_nr = null_stamp.inode_nr;
    TEST_ASSERT_MEM_EQ(&normalized_legacy, &null_stamp, sizeof(null_stamp));

    /* The callback precedes the first inode insertion; a duplicate name can
     * therefore fail after sampling, while invalid preflight calls cannot. */
    source.calls = 0;
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "legacy", 6,
                       sample_stamp, &source, &ignored_ino), BFS_ERR_EXISTS);
    TEST_ASSERT_EQ(source.calls, 1);

    fixture_close(&fixture);
}

static void test_write_stamp_preserves_fresh_inode_metadata(void)
{
    inode_stamp_fixture_t fixture;
    stamp_source_t creation = {
        .value = {.days = 11, .mins = 22, .ticks = 33},
        .calls = 0,
    };
    stamp_source_t write_stamp = {
        .value = {.days = UINT16_MAX, .mins = 0x7654, .ticks = 0x3210},
        .calls = 0,
    };
    uint32_t ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "fresh", 5,
                       sample_stamp, &creation, &ino), BFS_OK);

    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);

    /* Change metadata after opening the handle. The completion update must
     * read this latest inode before applying the requested fields. */
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    const bfs_inode_stamp_t fresh_creation = {
        .days = 0x0102, .mins = 0x0304, .ticks = 0x0506,
    };
    const bfs_inode_stamp_t old_modification = {
        .days = 0x1112, .mins = 0x1314, .ticks = 0x1516,
    };
    set_inode_stamps(&inode, &fresh_creation, &old_modification);
    inode.protection = bfs_be32(0xA5A5000Fu);
    inode.uid = bfs_be16(0x2345);
    inode.gid = bfs_be16(0xABCD);
    TEST_ASSERT_EQ(bfs_inode_write(&fixture.fs.inode_tree, ino, &inode), BFS_OK);

    static const uint8_t payload[] = "payload";
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, payload, sizeof(payload) - 1,
                                             sample_stamp, &write_stamp,
                                             0x00000005u),
                   (int32_t)(sizeof(payload) - 1));
    TEST_ASSERT_EQ(write_stamp.calls, 1);

    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT(inode_matches_creation_stamp(&inode, &fresh_creation));
    TEST_ASSERT(inode_matches_modification_stamp(&inode, &write_stamp.value));
    TEST_ASSERT_EQ(bfs_be32(inode.inode_nr), ino);
    TEST_ASSERT_EQ(bfs_be32(inode.type), BFS_INODE_FILE);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 1);
    TEST_ASSERT_EQ(bfs_be16(inode.uid), 0x2345);
    TEST_ASSERT_EQ(bfs_be16(inode.gid), 0xABCD);
    TEST_ASSERT_EQ(bfs_be32(inode.protection), 0xA5A5000Au);
    TEST_ASSERT_EQ(bfs_be32(inode.size_lo), sizeof(payload) - 1);

    bfs_file_t reader;
    uint8_t actual[sizeof(payload) - 1];
    TEST_ASSERT_EQ(bfs_file_open(&reader, &fixture.fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&reader, actual, sizeof(actual)), sizeof(actual));
    TEST_ASSERT_MEM_EQ(actual, payload, sizeof(actual));

    fixture_close(&fixture);
}

static void test_write_null_stamp_matches_legacy_and_null_mask_clears(void)
{
    inode_stamp_fixture_t fixture;
    uint32_t legacy_ino = 0, explicit_ino = 0, clear_ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "legacy", 6, &legacy_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "explicit", 8, &explicit_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "clear", 5, &clear_ino), BFS_OK);

    const bfs_inode_stamp_t creation = {.days = 7, .mins = 8, .ticks = 9};
    const bfs_inode_stamp_t modification = {.days = 10, .mins = 11, .ticks = 12};
    uint32_t compare_inos[] = {legacy_ino, explicit_ino, clear_ino};
    for (size_t i = 0; i < sizeof(compare_inos) / sizeof(compare_inos[0]); i++) {
        bfs_inode_t inode;
        TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, compare_inos[i],
                                      &inode), BFS_OK);
        set_inode_stamps(&inode, &creation, &modification);
        inode.protection = bfs_be32(0xF00Fu);
        TEST_ASSERT_EQ(bfs_inode_write(&fixture.fs.inode_tree, compare_inos[i],
                                       &inode), BFS_OK);
    }

    bfs_file_t legacy_file, explicit_file;
    TEST_ASSERT_EQ(bfs_file_open(&legacy_file, &fixture.fs, legacy_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_open(&explicit_file, &fixture.fs, explicit_ino), BFS_OK);
    static const uint8_t payload[] = "same bytes";
    TEST_ASSERT_EQ(bfs_file_write(&legacy_file, payload, sizeof(payload) - 1),
                   (int32_t)(sizeof(payload) - 1));
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&explicit_file, payload,
                                             sizeof(payload) - 1, NULL, NULL, 0),
                   (int32_t)(sizeof(payload) - 1));

    bfs_inode_t legacy, explicit;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, legacy_ino, &legacy), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, explicit_ino, &explicit), BFS_OK);
    TEST_ASSERT_EQ(legacy.create_days, explicit.create_days);
    TEST_ASSERT_EQ(legacy.create_mins, explicit.create_mins);
    TEST_ASSERT_EQ(legacy.create_ticks, explicit.create_ticks);
    TEST_ASSERT_EQ(legacy.modify_days, explicit.modify_days);
    TEST_ASSERT_EQ(legacy.modify_mins, explicit.modify_mins);
    TEST_ASSERT_EQ(legacy.modify_ticks, explicit.modify_ticks);
    TEST_ASSERT_EQ(legacy.protection, explicit.protection);
    TEST_ASSERT(inode_matches_creation_stamp(&legacy, &creation));
    TEST_ASSERT(inode_matches_modification_stamp(&legacy, &modification));
    TEST_ASSERT_EQ(bfs_be32(legacy.protection), 0xF00Fu);

    bfs_file_t clear_file;
    TEST_ASSERT_EQ(bfs_file_open(&clear_file, &fixture.fs, clear_ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&clear_file, payload,
                                             sizeof(payload) - 1, NULL, NULL,
                                             0x0000F000u),
                   (int32_t)(sizeof(payload) - 1));
    bfs_inode_t cleared;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, clear_ino, &cleared), BFS_OK);
    TEST_ASSERT(inode_matches_creation_stamp(&cleared, &creation));
    TEST_ASSERT(inode_matches_modification_stamp(&cleared, &modification));
    TEST_ASSERT_EQ(bfs_be32(cleared.protection), 0x0000000Fu);

    fixture_close(&fixture);
}

static void test_zero_error_and_readonly_calls_do_not_sample(void)
{
    inode_stamp_fixture_t fixture;
    stamp_source_t source = {
        .value = {.days = 20, .mins = 21, .ticks = 22},
        .calls = 0,
    };
    uint32_t ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));
    TEST_ASSERT_EQ(bfs_fs_create_file(&fixture.fs, BFS_ROOT_INO,
                                      "readonly", 8, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "bad/name", 8,
                       sample_stamp, &source, NULL), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(source.calls, 0);

    bfs_inode_t original;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &original), BFS_OK);
    original.protection = bfs_be32(0xF0);
    TEST_ASSERT_EQ(bfs_inode_write(&fixture.fs.inode_tree, ino, &original), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    uint8_t byte = 0x71;
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, &byte, 0,
                                             sample_stamp, &source, 0xF0), 0);
    TEST_ASSERT_EQ(source.calls, 0);
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, NULL, 1,
                                             sample_stamp, &source, 0),
                   BFS_ERR_INVAL);
    TEST_ASSERT_EQ(source.calls, 0);
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, &byte, UINT32_MAX,
                                             sample_stamp, &source, 0xF0),
                   BFS_ERR_INVAL);
    TEST_ASSERT_EQ(source.calls, 0);
    bfs_inode_t unchanged;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &unchanged), BFS_OK);
    TEST_ASSERT_MEM_EQ(&original, &unchanged, sizeof(original));

    TEST_ASSERT_EQ(bfs_fs_unmount(&fixture.fs), BFS_OK);
    bfs_bio_close(fixture.bio);
    fixture.bio = bio_emu_open(STAMP_IMAGE, STAMP_BLOCK_SIZE);
    TEST_ASSERT(fixture.bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&fixture.fs, fixture.bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "rocreate", 8,
                       sample_stamp, &source, NULL), BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(source.calls, 0);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, &byte, 1,
                                             sample_stamp, &source, 0xFFu),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(source.calls, 0);

    fixture_close(&fixture);
}

static void test_snapshot_keeps_old_stamp_and_content(void)
{
    inode_stamp_fixture_t fixture;
    stamp_source_t creation = {
        .value = {.days = 101, .mins = 102, .ticks = 103}, .calls = 0,
    };
    stamp_source_t before = {
        .value = {.days = 201, .mins = 202, .ticks = 203}, .calls = 0,
    };
    stamp_source_t after = {
        .value = {.days = 301, .mins = 302, .ticks = 303}, .calls = 0,
    };
    uint32_t ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "snap", 4,
                       sample_stamp, &creation, &ino), BFS_OK);

    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    static const uint8_t old_data[] = "before";
    static const uint8_t new_data[] = "after!";
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, old_data, sizeof(old_data) - 1,
                                             sample_stamp, &before, 0),
                   (int32_t)(sizeof(old_data) - 1));
    TEST_ASSERT_EQ(before.calls, 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_snapshot_create(&fixture.fs, "before-stamp"), BFS_OK);

    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, new_data, sizeof(new_data) - 1,
                                             sample_stamp, &after, 0),
                   (int32_t)(sizeof(new_data) - 1));
    TEST_ASSERT_EQ(after.calls, 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&fixture.fs), BFS_OK);

    bfs_bio_t *bio = fixture.bio;
    TEST_ASSERT_EQ(bfs_fs_unmount(&fixture.fs), BFS_OK);
    bfs_bio_close(bio);
    fixture.bio = bio_emu_open(STAMP_IMAGE, STAMP_BLOCK_SIZE);
    TEST_ASSERT(fixture.bio != NULL);
    TEST_ASSERT_EQ(bfs_fs_mount_readonly(&fixture.fs, fixture.bio), BFS_OK);

    bfs_inode_t live_inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fixture.fs.inode_tree, ino, &live_inode), BFS_OK);
    TEST_ASSERT(inode_matches_creation_stamp(&live_inode, &creation.value));
    TEST_ASSERT(inode_matches_modification_stamp(&live_inode, &after.value));

    bfs_file_t live_file;
    uint8_t actual[sizeof(new_data) - 1];
    TEST_ASSERT_EQ(bfs_file_open(&live_file, &fixture.fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&live_file, actual, sizeof(actual)), sizeof(actual));
    TEST_ASSERT_MEM_EQ(actual, new_data, sizeof(actual));

    bfs_snapshot_record_t record;
    TEST_ASSERT_EQ(bfs_snapshot_find_by_name(&fixture.fs, "before-stamp",
                                             NULL, &record), BFS_OK);
    bfs_dir_tree_t snapshot_dir;
    bfs_btree_t snapshot_inode;
    TEST_ASSERT_EQ(bfs_snapshot_open(&record, fixture.bio,
                                     bfs_freespace_allocator(&fixture.fs.freespace),
                                     &snapshot_dir, &snapshot_inode), BFS_OK);
    bfs_inode_t snapshot_value;
    TEST_ASSERT_EQ(bfs_inode_read(&snapshot_inode, ino, &snapshot_value), BFS_OK);
    TEST_ASSERT(inode_matches_creation_stamp(&snapshot_value, &creation.value));
    TEST_ASSERT(inode_matches_modification_stamp(&snapshot_value, &before.value));

    bfs_file_t snapshot_file;
    TEST_ASSERT_EQ(bfs_file_open_readonly_view(&snapshot_file, &fixture.fs,
                                               &snapshot_inode, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&snapshot_file, actual, sizeof(actual)), sizeof(actual));
    TEST_ASSERT_MEM_EQ(actual, old_data, sizeof(actual));
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&snapshot_file, new_data,
                                             sizeof(new_data) - 1,
                                             sample_stamp, &after, 0xFF),
                   BFS_ERR_UNSUPPORTED);
    TEST_ASSERT_EQ(after.calls, 1);

    fixture_close(&fixture);
}

static void test_open_unlinked_write_updates_inode_stamp(void)
{
    inode_stamp_fixture_t fixture;
    stamp_source_t creation = {
        .value = {.days = 41, .mins = 42, .ticks = 43}, .calls = 0,
    };
    stamp_source_t first_write = {
        .value = {.days = 51, .mins = 52, .ticks = 53}, .calls = 0,
    };
    stamp_source_t orphan_write = {
        .value = {.days = 61, .mins = 62, .ticks = 63}, .calls = 0,
    };
    uint32_t ino = 0;
    TEST_ASSERT(fixture_open(&fixture, 0));
    TEST_ASSERT_EQ(bfs_fs_create_file_with_stamp(
                       &fixture.fs, BFS_ROOT_INO, "orphan", 6,
                       sample_stamp, &creation, &ino), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fixture.fs, ino), BFS_OK);
    static const uint8_t first[] = "old";
    static const uint8_t second[] = "new";
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, first, sizeof(first) - 1,
                                             sample_stamp, &first_write, 0),
                   (int32_t)(sizeof(first) - 1));

    uint32_t orphan_ino = 0;
    TEST_ASSERT_EQ(bfs_fs_unlink_open_file(&fixture.fs, BFS_ROOT_INO,
                                           "orphan", 6, &orphan_ino), BFS_OK);
    TEST_ASSERT_EQ(orphan_ino, ino);
    TEST_ASSERT_EQ(bfs_file_mark_unlinked(&file), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, second, sizeof(second) - 1,
                                             sample_stamp, &orphan_write, 0),
                   (int32_t)(sizeof(second) - 1));
    TEST_ASSERT_EQ(orphan_write.calls, 1);

    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read_unlinked(&fixture.fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.link_count), 0);
    TEST_ASSERT(inode_matches_creation_stamp(&inode, &creation.value));
    TEST_ASSERT(inode_matches_modification_stamp(&inode, &orphan_write.value));

    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    uint8_t actual[(sizeof(first) - 1) + (sizeof(second) - 1)];
    TEST_ASSERT_EQ(bfs_file_read(&file, actual, sizeof(actual)), sizeof(actual));
    TEST_ASSERT_MEM_EQ(actual, "oldnew", sizeof(actual));

    fixture_close(&fixture);
}

TEST_SUITE_BEGIN("Inode Stamps")
    TEST_RUN(test_create_stamp_encoding_survives_remount);
    TEST_RUN(test_create_rejections_and_null_wrapper_equivalence);
    TEST_RUN(test_write_stamp_preserves_fresh_inode_metadata);
    TEST_RUN(test_write_null_stamp_matches_legacy_and_null_mask_clears);
    TEST_RUN(test_zero_error_and_readonly_calls_do_not_sample);
    TEST_RUN(test_snapshot_keeps_old_stamp_and_content);
    TEST_RUN(test_open_unlinked_write_updates_inode_stamp);
TEST_SUITE_END()
