/* SPDX-License-Identifier: MPL-2.0 */
/* mkbfs — Format a raw HDF image with BFS filesystem */

#include "bfs_fs.h"
#include "bfs_posix_bio.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    uint32_t block_size = 4096;
    const char *volname = "BFSTest";

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: mkbfs <image> [block_size] [volname]\n");
        return 2;
    }
    if (argc >= 3) {
        char *end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(argv[2], &end, 10);
        if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX) {
            fprintf(stderr, "Invalid block size: %s\n", argv[2]);
            return 2;
        }
        block_size = (uint32_t)parsed;
    }
    if (argc >= 4) volname = argv[3];

    bfs_posix_bio_options_t options = {
        .block_size = block_size,
        .writable = true,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(argv[1], &options);
    if (!bio) {
        fprintf(stderr, "Cannot open writable regular image %s\n", argv[1]);
        return 1;
    }

    bfs_err_t err = bfs_fs_format(bio, volname, 0);
    if (err != BFS_OK) {
        fprintf(stderr, "Format failed: %d\n", err);
        bfs_bio_close(bio);
        return 1;
    }

    printf("Formatted %s: %u blocks of %u bytes\n", argv[1], bio->block_count, block_size);
    bfs_bio_close(bio);
    return 0;
}
