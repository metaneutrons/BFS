/* SPDX-License-Identifier: MPL-2.0 */

#include "bfs_host_common.h"

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
