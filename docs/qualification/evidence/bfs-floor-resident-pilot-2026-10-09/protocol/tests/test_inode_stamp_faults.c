/* SPDX-License-Identifier: MPL-2.0 */
/* Combined size/stamp publication must retain the established COW recovery. */

#include "test_harness.h"
#include "bfs_file.h"
#include "bfs_fsck.h"
#include "bfs_btree_internal.h"
#include "block_device_emu.h"
#include <unistd.h>

#define STAMP_FAULT_IMAGE "test_inode_stamp_faults.img"
#define STAMP_FAULT_BLOCK_SIZE 4096
#define STAMP_FAULT_BLOCK_COUNT 1024
#define STAMP_FAULT_CLEAR 0x10u

typedef struct {
    bfs_bio_t bio;
    bfs_bio_t *inner;
    bfs_btree_t *inode_tree;
    uint32_t target_ino;
    bool armed;
    bool partial;
    uint32_t matches;
    uint32_t samples;
    bool fail_second_data;
    uint32_t payload_writes;
    bool watch_publications;
    uint32_t publications_without_stamp;
    uint32_t publications_with_stamp;
    bfs_blk_t failed_block;
    uint8_t original[STAMP_FAULT_BLOCK_SIZE];
    uint8_t candidate[STAMP_FAULT_BLOCK_SIZE];
    uint8_t partial_buffer[STAMP_FAULT_BLOCK_SIZE];
} stamp_fault_bio_t;

static void fault_stamp_sample(void *context, bfs_inode_stamp_t *stamp)
{
    uint32_t *samples = context;
    (*samples)++;
    stamp->days = 0xF101;
    stamp->mins = 0x0312;
    stamp->ticks = 0x0423;
}

static bool is_combined_inode(const stamp_fault_bio_t *fault,
                              const void *buffer)
{
    /* Decode through the same layout accessors as the core, not a mirrored
     * key/value-offset formula. The tiny fixture's inode tree is a root leaf. */
    uint8_t *view = (uint8_t *)buffer;
    if (!fault->inode_tree || bfs_be32(hdr_of(view)->magic) != BFS_NODE_MAGIC ||
        !is_leaf(view) || num_keys(view) > leaf_max_keys(fault->inode_tree))
        return false;
    for (uint32_t i = 0; i < num_keys(view); i++) {
        if (bfs_load_be32(node_key(fault->inode_tree, view, i)) != fault->target_ino)
            continue;
        const bfs_inode_t *inode = leaf_val(fault->inode_tree, view, i);
        if (bfs_be32(inode->inode_nr) == fault->target_ino &&
            bfs_be32(inode->type) == BFS_INODE_FILE &&
            bfs_be32(inode->link_count) == 1 &&
            bfs_be32(inode->size_hi) == 0 &&
            bfs_be32(inode->size_lo) == 2 * STAMP_FAULT_BLOCK_SIZE &&
            bfs_be32(inode->protection) == 0x83 &&
            bfs_be16(inode->uid) == 123 && bfs_be16(inode->gid) == 456 &&
            bfs_be16(inode->modify_days) == 0xF101 &&
            bfs_be16(inode->modify_mins) == 0x0312 &&
            bfs_be16(inode->modify_ticks) == 0x0423)
            return true;
    }
    return false;
}

static bfs_err_t fault_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    stamp_fault_bio_t *fault = (stamp_fault_bio_t *)bio;
    return bfs_bio_read(fault->inner, block, buffer);
}

static bfs_err_t fault_write(bfs_bio_t *bio, bfs_blk_t block,
                              const void *buffer)
{
    stamp_fault_bio_t *fault = (stamp_fault_bio_t *)bio;
    if (fault->watch_publications && bfs_load_be32(buffer) == BFS_SB_MAGIC) {
        if (fault->samples == 0) fault->publications_without_stamp++;
        else fault->publications_with_stamp++;
    }
    if (fault->fail_second_data) {
        const uint8_t *bytes = buffer;
        bool payload = true;
        for (uint32_t i = 0; i < bio->block_size; i++) {
            if (bytes[i] != 0x3C) { payload = false; break; }
        }
        if (payload && ++fault->payload_writes == 2) return BFS_ERR_IO;
    }
    if (fault->armed && is_combined_inode(fault, buffer)) {
        fault->armed = false;
        fault->matches++;
        fault->failed_block = block;
        memcpy(fault->candidate, buffer, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        bfs_err_t err = bfs_bio_read(fault->inner, block, fault->original);
        if (err != BFS_OK) return err;
        if (fault->partial) {
            memcpy(fault->partial_buffer, fault->original, bio->block_size); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
            memcpy(fault->partial_buffer, buffer, bio->block_size / 2); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
            err = bfs_bio_write(fault->inner, block, fault->partial_buffer);
            if (err != BFS_OK) return err;
        }
        return BFS_ERR_IO;
    }
    return bfs_bio_write(fault->inner, block, buffer);
}

static bfs_err_t fault_sync(bfs_bio_t *bio)
{
    return bfs_bio_sync(((stamp_fault_bio_t *)bio)->inner);
}

static const bfs_bio_ops_t fault_ops = {
    .read_block = fault_read,
    .write_block = fault_write,
    .sync = fault_sync,
};

static void exercise_combined_publication_failure(bool partial)
{
    unlink(STAMP_FAULT_IMAGE);
    stamp_fault_bio_t fault = {0};
    fault.inner = bio_emu_create(STAMP_FAULT_IMAGE, STAMP_FAULT_BLOCK_SIZE,
                                 STAMP_FAULT_BLOCK_COUNT);
    TEST_ASSERT(fault.inner != NULL);
    fault.bio.ops = &fault_ops;
    fault.bio.block_size = fault.inner->block_size;
    fault.bio.block_count = fault.inner->block_count;
    TEST_ASSERT_EQ(bfs_fs_format(&fault.bio, "StampFault",
                                BFS_OPT_DATA_CHECKSUMS | BFS_OPT_DATA_ORDERED),
                   BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fault.bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    inode.uid = bfs_be16(123);
    inode.gid = bfs_be16(456);
    inode.protection = bfs_be32(0x93);
    inode.create_days = bfs_be16(11);
    inode.create_mins = bfs_be16(12);
    inode.create_ticks = bfs_be16(13);
    inode.modify_days = bfs_be16(21);
    inode.modify_mins = bfs_be16(22);
    inode.modify_ticks = bfs_be16(23);
    TEST_ASSERT_EQ(bfs_inode_write(&fs.inode_tree, ino, &inode), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    uint8_t old_data[STAMP_FAULT_BLOCK_SIZE];
    uint8_t append_data[STAMP_FAULT_BLOCK_SIZE];
    uint8_t result[STAMP_FAULT_BLOCK_SIZE];
    memset(old_data, 0xA5, sizeof(old_data));
    memset(append_data, 0x3C, sizeof(append_data));
    TEST_ASSERT_EQ(bfs_file_write(&file, old_data, sizeof(old_data)),
                   STAMP_FAULT_BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_inode_t committed;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &committed), BFS_OK);
    uint64_t generation = fs.recovery_generation;
    TEST_ASSERT_EQ(fs.inode_tree.height, 1);
    fault.inode_tree = &fs.inode_tree;
    fault.target_ino = ino;
    fault.partial = partial;
    fault.armed = true;

    /* Append beyond committed EOF: do not assume an unshared in-place data
     * overwrite would be rolled back by a metadata failure. */
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, append_data, sizeof(append_data),
                                             fault_stamp_sample, &fault.samples,
                                             STAMP_FAULT_CLEAR), BFS_ERR_IO);
    TEST_ASSERT_EQ(fault.samples, 1);
    TEST_ASSERT_EQ(fault.matches, 1);
    TEST_ASSERT(!fault.armed);
    TEST_ASSERT(fs.recovery_generation > generation);
    TEST_ASSERT_EQ(fs.recovery_error, BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&file, result, 1), BFS_ERR_IO);

    TEST_ASSERT_EQ(bfs_bio_read(fault.inner, fault.failed_block, result), BFS_OK);
    if (partial) {
        TEST_ASSERT_MEM_EQ(result, fault.candidate, STAMP_FAULT_BLOCK_SIZE / 2);
        TEST_ASSERT_MEM_EQ(result + STAMP_FAULT_BLOCK_SIZE / 2,
                            fault.original + STAMP_FAULT_BLOCK_SIZE / 2,
                            STAMP_FAULT_BLOCK_SIZE / 2);
    } else {
        TEST_ASSERT_MEM_EQ(result, fault.original, STAMP_FAULT_BLOCK_SIZE);
    }
    /* A partial write may happen to contain all active inode bytes. Its
     * failure must still prevent publishing the new root. */
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_MEM_EQ(&inode, &committed, sizeof(inode));
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(file.size, STAMP_FAULT_BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_file_read(&file, result, sizeof(result)),
                   STAMP_FAULT_BLOCK_SIZE);
    TEST_ASSERT_MEM_EQ(result, old_data, sizeof(result));
    TEST_ASSERT_EQ(bfs_file_read(&file, result, 1), 0);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    TEST_ASSERT_EQ(report.leaked_blocks, 0);

    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fault.bio), BFS_OK);
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_MEM_EQ(&inode, &committed, sizeof(inode));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(fault.inner);
    unlink(STAMP_FAULT_IMAGE);
}

static void test_combined_inode_write_failure_reloads_committed(void)
{
    exercise_combined_publication_failure(false);
}

static void test_combined_inode_partial_write_reloads_committed(void)
{
    exercise_combined_publication_failure(true);
}

static void test_positive_prefix_gets_one_combined_stamp(void)
{
    unlink(STAMP_FAULT_IMAGE);
    stamp_fault_bio_t fault = {0};
    fault.inner = bio_emu_create(STAMP_FAULT_IMAGE, STAMP_FAULT_BLOCK_SIZE,
                                 STAMP_FAULT_BLOCK_COUNT);
    TEST_ASSERT(fault.inner != NULL);
    fault.bio.ops = &fault_ops;
    fault.bio.block_size = fault.inner->block_size;
    fault.bio.block_count = fault.inner->block_count;
    TEST_ASSERT_EQ(bfs_fs_format(&fault.bio, "StampPrefix", 0), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fault.bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    uint8_t data[2 * STAMP_FAULT_BLOCK_SIZE];
    uint8_t result[STAMP_FAULT_BLOCK_SIZE];
    memset(data, 0x3C, sizeof(data));
    fault.fail_second_data = true;
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, data, sizeof(data),
                                             fault_stamp_sample, &fault.samples,
                                             0), STAMP_FAULT_BLOCK_SIZE);
    TEST_ASSERT_EQ(fault.payload_writes, 2);
    TEST_ASSERT_EQ(fault.samples, 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fault.bio), BFS_OK);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.size_lo), STAMP_FAULT_BLOCK_SIZE);
    TEST_ASSERT_EQ(bfs_be16(inode.modify_days), 0xF101);
    TEST_ASSERT_EQ(bfs_be16(inode.modify_mins), 0x0312);
    TEST_ASSERT_EQ(bfs_be16(inode.modify_ticks), 0x0423);
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_file_read(&file, result, sizeof(result)), sizeof(result));
    TEST_ASSERT_MEM_EQ(result, data, sizeof(result));
    TEST_ASSERT_EQ(bfs_file_read(&file, result, 1), 0);
    bfs_fsck_report_t report;
    TEST_ASSERT_EQ(bfs_fs_check(&fs, false, &report), BFS_OK);
    TEST_ASSERT_EQ(report.errors, 0);
    TEST_ASSERT_EQ(report.leaked_blocks, 0);
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(fault.inner);
    unlink(STAMP_FAULT_IMAGE);
}

static void test_intermediate_commits_do_not_sample_final_stamp(void)
{
    unlink(STAMP_FAULT_IMAGE);
    stamp_fault_bio_t fault = {0};
    fault.inner = bio_emu_create(STAMP_FAULT_IMAGE, STAMP_FAULT_BLOCK_SIZE,
                                 STAMP_FAULT_BLOCK_COUNT);
    TEST_ASSERT(fault.inner != NULL);
    fault.bio.ops = &fault_ops;
    fault.bio.block_size = fault.inner->block_size;
    fault.bio.block_count = fault.inner->block_count;
    TEST_ASSERT_EQ(bfs_fs_format(&fault.bio, "StampMidcommit",
                                BFS_OPT_DATA_CHECKSUMS), BFS_OK);
    bfs_fs_t fs;
    TEST_ASSERT_EQ(bfs_fs_mount(&fs, &fault.bio), BFS_OK);
    uint32_t ino;
    TEST_ASSERT_EQ(bfs_fs_create_file(&fs, BFS_ROOT_INO, "file", 4, &ino), BFS_OK);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    bfs_file_t file;
    TEST_ASSERT_EQ(bfs_file_open(&file, &fs, ino), BFS_OK);
    uint64_t starting_txn = fs.live_txn_id;
    fs.pending_frees_cap = 3u * BFS_BTREE_MAX_OP_FREES;
    uint8_t data[4 * STAMP_FAULT_BLOCK_SIZE];
    uint8_t result[sizeof(data)];
    memset(data, 0x3C, sizeof(data));
    fault.watch_publications = true;
    TEST_ASSERT_EQ(bfs_file_write_with_stamp(&file, data, sizeof(data),
                                             fault_stamp_sample, &fault.samples,
                                             0), sizeof(data));
    TEST_ASSERT(fs.live_txn_id > starting_txn);
    TEST_ASSERT(fault.publications_without_stamp > 0);
    TEST_ASSERT_EQ(fault.publications_with_stamp, 0);
    TEST_ASSERT_EQ(fault.samples, 1);
    TEST_ASSERT_EQ(bfs_fs_sync(&fs), BFS_OK);
    TEST_ASSERT(fault.publications_with_stamp > 0);
    bfs_inode_t inode;
    TEST_ASSERT_EQ(bfs_inode_read(&fs.inode_tree, ino, &inode), BFS_OK);
    TEST_ASSERT_EQ(bfs_be32(inode.size_lo), sizeof(data));
    TEST_ASSERT_EQ(bfs_be16(inode.modify_days), 0xF101);
    TEST_ASSERT_EQ(bfs_file_seek(&file, 0, BFS_SEEK_SET), 0);
    TEST_ASSERT_EQ(bfs_file_read(&file, result, sizeof(result)), sizeof(result));
    TEST_ASSERT_MEM_EQ(result, data, sizeof(result));
    TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
    bfs_bio_close(fault.inner);
    unlink(STAMP_FAULT_IMAGE);
}

TEST_SUITE_BEGIN("Inode Stamp Publication Faults")
    TEST_RUN(test_combined_inode_write_failure_reloads_committed);
    TEST_RUN(test_combined_inode_partial_write_reloads_committed);
    TEST_RUN(test_positive_prefix_gets_one_combined_stamp);
    TEST_RUN(test_intermediate_commits_do_not_sample_final_stamp);
TEST_SUITE_END()
