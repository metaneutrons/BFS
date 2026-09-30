/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Natural allocation/free convergence across repeated sealed transaction
 * commits. Exercise the mounted filesystem API with real file-backed devices,
 * checksummed data, and strict read-only fsck after each published state.
 */

#include "test_harness.h"
#include "bfs_dir.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_superblock.h"
#include "block_device_emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_IMG "test_sf_convergence.img"
#define BLK_COUNT 8192u
#define CYCLES 12u
#define FILE_MAX_SIZE (2u * 4096u + 97u)

typedef struct {
    bfs_bio_t bio;
    bfs_bio_t *inner;
    bfs_fs_t *observed_fs;
    bfs_blk_t backup_sb_block;
    bool armed;
    uint32_t commit_writes;
    uint32_t commit_sb_writes;
    uint32_t frozen_writes;
    uint32_t non_sb_writes_while_frozen;
    uint32_t frozen_sb_writes;
    uint32_t metadata_fence_syncs;
    uint32_t publication_syncs;
    bool frozen_sb_write_seen;
    bool sb_write_waiting_for_sync;
    bool pending_at_metadata_fence;
    bool pending_at_sb_write;
    bool pending_at_publication_sync;
    bool publication_order_valid;
    uint32_t witnessed_commits;
} seal_witness_bio_t;

static bfs_err_t witness_read(bfs_bio_t *bio, bfs_blk_t block, void *buffer)
{
    seal_witness_bio_t *witness = (seal_witness_bio_t *)bio;
    return bfs_bio_read(witness->inner, block, buffer);
}

static bool witness_is_superblock_block(const seal_witness_bio_t *witness,
                                        bfs_blk_t block)
{
    return block == 0 || block == witness->backup_sb_block;
}

static bfs_err_t witness_write(bfs_bio_t *bio, bfs_blk_t block,
                               const void *buffer)
{
    seal_witness_bio_t *witness = (seal_witness_bio_t *)bio;
    bool is_superblock = witness_is_superblock_block(witness, block);

    if (witness->armed) {
        witness->commit_writes++;
        if (is_superblock) witness->commit_sb_writes++;
        if (witness->observed_fs &&
            witness->observed_fs->freespace.allocation_frozen) {
            witness->frozen_writes++;
            if (is_superblock) {
                witness->frozen_sb_writes++;
                witness->frozen_sb_write_seen = true;
                witness->sb_write_waiting_for_sync = true;
                witness->pending_at_sb_write |=
                    witness->observed_fs->pending_count != 0;
                if (witness->metadata_fence_syncs != 1 ||
                    witness->frozen_sb_writes != 1)
                    witness->publication_order_valid = false;
            } else {
                witness->non_sb_writes_while_frozen++;
                if (witness->frozen_sb_write_seen)
                    witness->publication_order_valid = false;
            }
        }
    }

    return bfs_bio_write(witness->inner, block, buffer);
}

static bfs_err_t witness_sync(bfs_bio_t *bio)
{
    seal_witness_bio_t *witness = (seal_witness_bio_t *)bio;
    if (witness->armed && witness->observed_fs &&
        witness->observed_fs->freespace.allocation_frozen) {
        if (witness->sb_write_waiting_for_sync) {
            witness->publication_syncs++;
            witness->pending_at_publication_sync |=
                witness->observed_fs->pending_count != 0;
            witness->sb_write_waiting_for_sync = false;
        } else {
            witness->metadata_fence_syncs++;
            witness->pending_at_metadata_fence |=
                witness->observed_fs->pending_count != 0;
            if (witness->frozen_sb_write_seen)
                witness->publication_order_valid = false;
        }
    }
    return bfs_bio_sync(witness->inner);
}

static void witness_close(bfs_bio_t *bio)
{
    seal_witness_bio_t *witness = (seal_witness_bio_t *)bio;
    bfs_bio_close(witness->inner);
}

static const bfs_bio_ops_t witness_ops = {
    .read_block = witness_read,
    .write_block = witness_write,
    .sync = witness_sync,
    .close = witness_close,
};

static void witness_reset_commit(seal_witness_bio_t *witness)
{
    witness->commit_writes = 0;
    witness->commit_sb_writes = 0;
    witness->frozen_writes = 0;
    witness->non_sb_writes_while_frozen = 0;
    witness->frozen_sb_writes = 0;
    witness->metadata_fence_syncs = 0;
    witness->publication_syncs = 0;
    witness->frozen_sb_write_seen = false;
    witness->sb_write_waiting_for_sync = false;
    witness->pending_at_metadata_fence = false;
    witness->pending_at_sb_write = false;
    witness->pending_at_publication_sync = false;
    witness->publication_order_valid = true;
    witness->armed = true;
}

static void fill_pattern(uint8_t *buffer, size_t length, uint32_t seed)
{
    for (size_t i = 0; i < length; i++)
        buffer[i] = (uint8_t)(seed + (uint32_t)i * 37u +
                              (uint32_t)(i >> 8) * 11u);
}

static bool commit_settled(const bfs_fs_t *fs)
{
    return fs->pending_count == 0 && fs->freespace.reserve_count == 0 &&
           !fs->freespace.allocation_frozen && fs->recovery_error == BFS_OK;
}

static bool sync_is_sealed(seal_witness_bio_t *witness, bfs_fs_t *fs,
                           const char *label)
{
    /* Reset immediately before every explicit sync so the witness covers only
     * this natural transaction, not its preceding file/namespace mutations. */
    witness_reset_commit(witness);
    bfs_err_t err = bfs_fs_sync(fs);
    witness->armed = false;

    bool witnessed = err == BFS_OK &&
        witness->metadata_fence_syncs == 1 &&
        witness->commit_sb_writes == 1 && witness->frozen_sb_writes == 1 &&
        witness->publication_syncs == 1 &&
        witness->non_sb_writes_while_frozen == 0 &&
        witness->frozen_writes == 1 &&
        !witness->sb_write_waiting_for_sync &&
        witness->pending_at_metadata_fence && witness->pending_at_sb_write &&
        witness->pending_at_publication_sync &&
        witness->publication_order_valid && commit_settled(fs);
    if (!witnessed) {
        fprintf(stderr,
                "  %s seal witness failed: err=%d writes=%u sb=%u frozen=%u "
                "non-sb-frozen=%u fences=%u pub-syncs=%u pending=%d/%d/%d "
                "order=%d settled=%d\n",
                label, err, witness->commit_writes, witness->commit_sb_writes,
                witness->frozen_writes,
                witness->non_sb_writes_while_frozen,
                witness->metadata_fence_syncs, witness->publication_syncs,
                witness->pending_at_metadata_fence,
                witness->pending_at_sb_write,
                witness->pending_at_publication_sync,
                witness->publication_order_valid, commit_settled(fs));
        return false;
    }
    witness->witnessed_commits++;
    return true;
}

static uint64_t free_with_active_pool(const bfs_fs_t *fs)
{
    return (uint64_t)fs->freespace.total_free +
           bfs_be32(fs->freespace.sb->emergency_count);
}

static bool strict_readonly_fsck(bfs_fs_t *fs)
{
    bfs_fsck_report_t report = {0};
    bfs_err_t err = bfs_fs_check(fs, false, &report);
    if (err != BFS_OK || report.errors != 0 || report.warnings != 0 ||
        report.leaked_blocks != 0) {
        fprintf(stderr,
                "  readonly fsck: err=%d errors=%u warnings=%u leaked=%u\n",
                err, report.errors, report.warnings, report.leaked_blocks);
        return false;
    }
    return true;
}

static bool read_file_matches(bfs_fs_t *fs, uint32_t ino,
                              const uint8_t *expected, uint32_t length)
{
    uint8_t actual[FILE_MAX_SIZE];
    if (length > sizeof(actual)) return false;

    bfs_file_t file;
    bfs_err_t err = bfs_file_open(&file, fs, ino);
    if (err != BFS_OK || file.size != length) {
        fprintf(stderr, "  open/size mismatch: err=%d size=%llu expected=%u\n",
                err, (unsigned long long)(err == BFS_OK ? file.size : 0), length);
        return false;
    }

    int32_t count = bfs_file_read(&file, actual, length);
    if (count != (int32_t)length ||
        memcmp(actual, expected, length) != 0 ||
        bfs_file_read(&file, actual, 1) != 0) {
        fprintf(stderr, "  file data mismatch: read=%d expected=%u\n",
                count, length);
        return false;
    }
    return true;
}

/* The writer is always abandoned only after a successful sync. Inspect the
 * durable image read-only, then remount writable for the next natural cycle. */
static bool inspect_readonly_and_remount(bfs_fs_t *fs, bfs_bio_t *bio,
                                         bool *writer_mounted,
                                         const char *name, uint32_t name_len,
                                         bool should_exist, uint32_t ino,
                                         const uint8_t *expected,
                                         uint32_t expected_len)
{
    if (*writer_mounted) {
        bfs_fs_abandon(fs);
        *writer_mounted = false;
    }

    bfs_fs_t readonly_fs;
    bfs_err_t err = bfs_fs_mount_readonly(&readonly_fs, bio);
    if (err != BFS_OK) {
        fprintf(stderr, "  readonly remount failed: %d\n", err);
        return false;
    }

    bool ok = strict_readonly_fsck(&readonly_fs);
    uint32_t found_ino = 0, found_type = 0;
    err = bfs_dir_lookup(&readonly_fs.dir_tree, BFS_ROOT_INO, name,
                         (uint8_t)name_len, &found_ino, &found_type);
    if (should_exist) {
        if (err != BFS_OK || found_ino != ino || found_type != BFS_INODE_FILE) {
            fprintf(stderr,
                    "  readonly lookup mismatch: err=%d ino=%u expected=%u type=%u\n",
                    err, found_ino, ino, found_type);
            ok = false;
        } else if (!read_file_matches(&readonly_fs, ino, expected,
                                      expected_len)) {
            ok = false;
        }
    } else if (err != BFS_ERR_NOTFOUND) {
        fprintf(stderr, "  deleted name remains visible: lookup err=%d\n", err);
        ok = false;
    }

    err = bfs_fs_unmount(&readonly_fs);
    if (err != BFS_OK) {
        fprintf(stderr, "  readonly unmount failed: %d\n", err);
        return false;
    }

    err = bfs_fs_mount(fs, bio);
    if (err != BFS_OK) {
        fprintf(stderr, "  writable remount failed: %d\n", err);
        return false;
    }
    *writer_mounted = true;
    return ok;
}

static bool run_convergence_case(uint32_t block_size)
{
    bool ok = false;
    bool writer_mounted = false;
    bfs_fs_t fs;
    seal_witness_bio_t witness = {0};
    bfs_bio_t *inner_bio = NULL;
    bfs_bio_t *bio = NULL;
    uint8_t *data_a = NULL;
    uint8_t *data_b = NULL;
    uint32_t length = 2u * block_size + 97u;
    const char *name = "sealed-cycle";
    const uint8_t name_len = 12;
    uint32_t options = BFS_OPT_DATA_CHECKSUMS | BFS_OPT_DATA_ORDERED;

    unlink(TEST_IMG);
    inner_bio = bio_emu_create(TEST_IMG, block_size, BLK_COUNT);
    if (!inner_bio) {
        fprintf(stderr, "  failed to create %u-byte-block image\n", block_size);
        goto cleanup;
    }
    witness.bio.ops = &witness_ops;
    witness.bio.block_size = inner_bio->block_size;
    witness.bio.block_count = inner_bio->block_count;
    witness.inner = inner_bio;
    bio = &witness.bio;
    if (bfs_fs_format(bio, "SFConverge", options) != BFS_OK) {
        fprintf(stderr, "  format failed for block size %u\n", block_size);
        goto cleanup;
    }
    if (bfs_fs_mount(&fs, bio) != BFS_OK) {
        fprintf(stderr, "  mount failed for block size %u\n", block_size);
        goto cleanup;
    }
    writer_mounted = true;
    witness.observed_fs = &fs;
    witness.backup_sb_block = (bfs_blk_t)(
        bfs_sb_backup_offset(&fs.txn.sb_new) / bio->block_size);

    data_a = malloc(length);
    data_b = malloc(length);
    if (!data_a || !data_b) {
        fprintf(stderr, "  payload allocation failed\n");
        goto cleanup;
    }

    /* Establish the empty accounting baseline. This is intentionally not a
     * churn-cycle witness; only the three natural mutation commits below are
     * required to take the sealed path. */
    witness_reset_commit(&witness);
    witness.armed = false;
    if (bfs_fs_sync(&fs) != BFS_OK || !commit_settled(&fs)) {
        fprintf(stderr, "  initial empty commit did not settle\n");
        goto cleanup;
    }
    const uint64_t empty_baseline = free_with_active_pool(&fs);

    for (uint32_t cycle = 0; cycle < CYCLES; cycle++) {
        fill_pattern(data_a, length, 0x31u + cycle * 13u);
        fill_pattern(data_b, length, 0xA7u + cycle * 17u);
        if (memcmp(data_a, data_b, length) == 0) {
            fprintf(stderr, "  generated overwrite patterns are identical\n");
            goto cleanup;
        }

        uint32_t ino = 0;
        if (bfs_fs_create_file(&fs, BFS_ROOT_INO, name, name_len, &ino) != BFS_OK ||
            ino == 0) {
            fprintf(stderr, "  cycle %u create failed\n", cycle);
            goto cleanup;
        }
        bfs_file_t fresh;
        if (bfs_file_open(&fresh, &fs, ino) != BFS_OK || fresh.size != 0 ||
            bfs_file_read(&fresh, data_b, 1) != 0) {
            fprintf(stderr, "  cycle %u freshly-created file was not empty\n", cycle);
            goto cleanup;
        }
        if (bfs_file_open(&fresh, &fs, ino) != BFS_OK ||
            bfs_file_write(&fresh, data_a, length) != (int32_t)length ||
            !read_file_matches(&fs, ino, data_a, length)) {
            fprintf(stderr, "  cycle %u initial write/read failed\n", cycle);
            goto cleanup;
        }
        if (!sync_is_sealed(&witness, &fs, "create/write A")) {
            fprintf(stderr, "  cycle %u create/write commit did not settle\n", cycle);
            goto cleanup;
        }
        if (!inspect_readonly_and_remount(&fs, bio, &writer_mounted,
                                          name, name_len, true, ino,
                                          data_a, length)) {
            fprintf(stderr, "  cycle %u create/write readonly check failed\n", cycle);
            goto cleanup;
        }

        if (bfs_file_open(&fresh, &fs, ino) != BFS_OK ||
            fresh.inode_nr != ino || fresh.size != length ||
            bfs_file_write(&fresh, data_b, length) != (int32_t)length ||
            !read_file_matches(&fs, ino, data_b, length)) {
            fprintf(stderr, "  cycle %u same-inode overwrite/read failed\n", cycle);
            goto cleanup;
        }
        if (!sync_is_sealed(&witness, &fs, "overwrite B")) {
            fprintf(stderr, "  cycle %u overwrite commit did not settle\n", cycle);
            goto cleanup;
        }
        if (!inspect_readonly_and_remount(&fs, bio, &writer_mounted,
                                          name, name_len, true, ino,
                                          data_b, length)) {
            fprintf(stderr, "  cycle %u overwrite readonly check failed\n", cycle);
            goto cleanup;
        }

        if (bfs_fs_delete_file(&fs, BFS_ROOT_INO, name, name_len) != BFS_OK) {
            fprintf(stderr, "  cycle %u delete failed\n", cycle);
            goto cleanup;
        }
        uint32_t found_ino = 0, found_type = 0;
        if (bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, name, name_len,
                           &found_ino, &found_type) != BFS_ERR_NOTFOUND) {
            fprintf(stderr, "  cycle %u deleted name remains in writer view\n", cycle);
            goto cleanup;
        }
        if (!sync_is_sealed(&witness, &fs, "delete")) {
            fprintf(stderr, "  cycle %u delete commit did not settle\n", cycle);
            goto cleanup;
        }
        uint64_t after_delete = free_with_active_pool(&fs);
        if (after_delete != empty_baseline) {
            fprintf(stderr,
                    "  cycle %u space did not converge: empty=%llu deleted=%llu\n",
                    cycle, (unsigned long long)empty_baseline,
                    (unsigned long long)after_delete);
            goto cleanup;
        }
        if (!inspect_readonly_and_remount(&fs, bio, &writer_mounted,
                                          name, name_len, false, 0,
                                          NULL, 0)) {
            fprintf(stderr, "  cycle %u delete readonly check failed\n", cycle);
            goto cleanup;
        }
    }

    if (witness.witnessed_commits != CYCLES * 3u) {
        fprintf(stderr, "  only %u/%u natural commits had an SF witness\n",
                witness.witnessed_commits, CYCLES * 3u);
        goto cleanup;
    }
    printf("    %u-byte blocks: %u/%u commits witnessed; each had one frozen "
           "metadata fence, one SB write+sync, pending retained, zero "
           "non-SB frozen writes\n",
           block_size, witness.witnessed_commits, CYCLES * 3u);
    ok = true;

cleanup:
    if (writer_mounted) {
        bfs_err_t err = bfs_fs_unmount(&fs);
        if (err != BFS_OK) {
            fprintf(stderr, "  final writable unmount failed: %d\n", err);
            ok = false;
        }
    }
    free(data_b);
    free(data_a);
    if (inner_bio) bfs_bio_close(inner_bio);
    unlink(TEST_IMG);
    return ok;
}

static void test_convergence_1024(void)
{
    TEST_ASSERT(run_convergence_case(1024));
}

static void test_convergence_4096(void)
{
    TEST_ASSERT(run_convergence_case(4096));
}

TEST_SUITE_BEGIN("Sealed transaction natural convergence")
    TEST_RUN(test_convergence_1024);
    TEST_RUN(test_convergence_4096);
TEST_SUITE_END()
