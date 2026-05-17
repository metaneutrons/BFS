/* SPDX-License-Identifier: MPL-2.0 */
/* mkbfs — Format a raw HDF image with BFS filesystem */

#include "bfs_fs.h"
#include "bfs_bio.h"
#include "block_device_emu.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    uint32_t block_size = 4096;
    const char *volname = "BFSTest";

    if (argc < 2) {
        fprintf(stderr, "Usage: mkbfs <image> [block_size] [volname]\n");
        return 1;
    }
    if (argc >= 3) block_size = atoi(argv[2]);
    if (argc >= 4) volname = argv[3];

    bfs_bio_t *bio = bio_emu_open(argv[1], block_size);
    if (!bio) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
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
