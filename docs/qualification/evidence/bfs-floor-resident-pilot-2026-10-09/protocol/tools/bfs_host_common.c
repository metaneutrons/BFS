/* SPDX-License-Identifier: MPL-2.0 */

#include "bfs_host_common.h"

#include <stdio.h>

#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_posix_bio.h"
#include "bfs_superblock.h"

bfs_bio_t *bfs_host_open_bfs_image(const char *path, bool writable,
                                    bfs_err_t *error,
                                    char diagnostic[BFS_FORMAT_ERROR_MAX])
{
    bfs_posix_bio_options_t options = {
        .block_size = BFS_MIN_BLOCK_SIZE,
        .writable = writable,
        .lock = true,
    };
    bfs_superblock_t superblock = {0};
    uint64_t byte_offset, byte_length;
    bfs_bio_t *bio;

    if (!path || !error || !diagnostic) return NULL;
    *error = BFS_ERR_IO;
    diagnostic[0] = 0;
    bio = bfs_posix_bio_open(path, &options);
    if (!bio) return NULL;
    bfs_err_t result = bfs_posix_bio_get_range(bio, &byte_offset, &byte_length);
    if (result == BFS_OK && byte_offset == 0)
        result = bfs_sb_probe(bio, byte_length, &superblock);
    if (result != BFS_OK) {
        if (result == BFS_ERR_UNSUPPORTED)
            bfs_sb_describe_unsupported(&superblock, diagnostic);
        bfs_bio_close(bio);
        *error = result;
        return NULL;
    }
    *error = BFS_OK;
    return bio;
}

int bfs_host_format_image(const char *path, uint32_t block_size, const char *label)
{
    bfs_posix_bio_options_t options = {
        .block_size = block_size,
        .writable = true,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(path, &options);
    if (!bio) {
        fprintf(stderr, "Cannot open writable regular image %s\n", path);
        return 1;
    }
    bfs_err_t error = bfs_fs_format(bio, label, 0);
    if (error != BFS_OK) {
        fprintf(stderr, "Format failed: %d\n", error);
        bfs_bio_close(bio);
        return 1;
    }
    printf("Formatted %s: %u blocks of %u bytes\n", path, bio->block_count, block_size);
    bfs_bio_close(bio);
    return 0;
}

int bfs_host_check_image(const char *path, bool repair)
{
    bfs_err_t open_error;
    char diagnostic[BFS_FORMAT_ERROR_MAX] = {0};
    bfs_bio_t *bio = bfs_host_open_bfs_image(path, repair, &open_error, diagnostic);
    if (!bio) {
        if (open_error == BFS_ERR_UNSUPPORTED) fprintf(stderr, "%s\n", diagnostic);
        else fprintf(stderr, "Cannot open %s (error %d)\n", path, open_error);
        return 1;
    }

    bfs_fs_t fs;
    bfs_err_t result = repair ? bfs_fs_mount(&fs, bio) : bfs_fs_mount_readonly(&fs, bio);
    if (result != BFS_OK) {
        fprintf(stderr, "Mount failed (error %d)\n", result);
        bfs_bio_close(bio);
        return 1;
    }

    printf("=== BFS Filesystem Check ===\n");
    printf("  Volume: %s  Blocks: %u  Free: %u\n", fs.txn.sb.volname,
           bio->block_count, fs.freespace.total_free);
    bfs_fsck_report_t report = {0};
    result = bfs_fs_check(&fs, repair, &report);
    if (result != BFS_OK && result != BFS_ERR_CORRUPT) {
        fprintf(stderr, "ERROR: checker failed (error %d)\n", result);
        report.errors++;
    }

    if (!repair) {
        if (bfs_fs_unmount(&fs) != BFS_OK) {
            fprintf(stderr, "ERROR: cannot close the filesystem cleanly\n");
            report.errors++;
        }
    } else {
        /* bfs_fs_check() commits exactly once only when it repaired leaked
         * blocks. A normal writable unmount would publish an otherwise empty
         * transaction after a clean repair check. */
        bfs_fs_abandon(&fs);
    }
    bfs_bio_close(bio);

    if (report.leaked_blocks)
        printf("  Leaked blocks: %u%s\n", report.leaked_blocks,
               report.repaired_blocks ? " (repaired)" : "");
    printf("\n  Errors: %u  Warnings: %u\n", report.errors, report.warnings);
    printf("  %s\n", report.errors ? "ERRORS FOUND" :
           (report.warnings ? "Minor issues" : "CLEAN"));
    return report.errors ? 2 : (report.warnings ? 1 : 0);
}
