/* SPDX-License-Identifier: MPL-2.0 */
/* BFS offline checker and bounded free-space repair tool. */

#include <stdio.h>
#include <string.h>

#include "bfs_diagnostics.h"
#include "bfs_fs.h"
#include "bfs_fsck.h"
#include "bfs_host_commands.h"
#include "bfs_host_common.h"
#include "bfs_posix_bio.h"

int bfs_host_fsck_main(int argc, char **argv)
{
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--fix") != 0)) {
        fprintf(stderr, "Usage: bfsfsck <image> [--fix]\n");
        return 2;
    }
    bool repair = argc == 3;
    bfs_err_t open_error;
    char format_error[BFS_FORMAT_ERROR_MAX] = {0};
    bfs_bio_t *bio = bfs_host_open_bfs_image(argv[1], repair, &open_error, format_error);
    if (!bio) {
        if (open_error == BFS_ERR_UNSUPPORTED) fprintf(stderr, "%s\n", format_error);
        else fprintf(stderr, "Cannot open %s (error %d)\n", argv[1], open_error);
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

#ifndef BFS_HOST_COMMAND_LIBRARY
int main(int argc, char **argv)
{
    return bfs_host_fsck_main(argc, argv);
}
#endif
