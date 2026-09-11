/* SPDX-License-Identifier: MPL-2.0 */
/* Test-only committed-image producer for the independent format oracle. */

#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_posix_bio.h"
#include "bfs_snapshot.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_BLOCK_SIZE 4096u
#define DEFAULT_BLOCK_COUNT 512u

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || !text[0] || !end || *end != '\0' || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    bool directory_scale = false;
    bool hard_link = false;
    uint32_t block_size = DEFAULT_BLOCK_SIZE;
    uint32_t block_count = DEFAULT_BLOCK_COUNT;
    uint32_t format_options = 0;
    if (argc < 2) return 2;
    for (int index = 2; index < argc; index++) {
        if (strcmp(argv[index], "--directory-scale") == 0) directory_scale = true;
        else if (strcmp(argv[index], "--hard-link") == 0) hard_link = true;
        else if (strcmp(argv[index], "--block-size") == 0 && index + 1 < argc &&
                 parse_u32(argv[++index], &block_size)) continue;
        else if (strcmp(argv[index], "--block-count") == 0 && index + 1 < argc &&
                 parse_u32(argv[++index], &block_count)) continue;
        else if (strcmp(argv[index], "--format-options") == 0 && index + 1 < argc &&
                 parse_u32(argv[++index], &format_options)) continue;
        else return 2;
    }
    if (!bfs_block_size_valid(block_size) || block_count < BFS_MIN_VOLUME_BLOCKS)
        return 2;
    if (format_options & ~(BFS_OPT_DATA_CHECKSUMS | BFS_OPT_SNAPSHOTS |
                           BFS_OPT_DATA_ORDERED))
        return 2;
    uint64_t image_size = (uint64_t)block_size * block_count;
    if (image_size > INT64_MAX) return 2;
    int fd = open(argv[1], O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)image_size) != 0 ||
        close(fd) != 0)
        return 1;
    bfs_posix_bio_options_t options = {
        .block_size = block_size,
        .writable = true,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(argv[1], &options);
    if (!bio) return 1;
    bfs_fs_t fs = {0};
    uint32_t inode;
    uint32_t oracle_inode;
    bfs_file_t file;
    const char contents[] = "oracle contents";
    bfs_err_t error = bfs_fs_format(bio, "Oracle", format_options);
    if (error == BFS_OK) error = bfs_fs_mount(&fs, bio);
    if (error == BFS_OK) {
        error = bfs_fs_create_file(&fs, BFS_ROOT_INO, "oracle.txt", 10, &inode);
        if (error == BFS_OK) oracle_inode = inode;
    }
    if (error == BFS_OK) error = bfs_file_open(&file, &fs, inode);
    if (error == BFS_OK && bfs_file_write(&file, contents, sizeof(contents) - 1) !=
        (int32_t)(sizeof(contents) - 1))
        error = BFS_ERR_IO;
    if (error == BFS_OK) error = bfs_fs_set_comment(&fs, inode, "fixture", 7);
    if (error == BFS_OK) error = bfs_fs_mkdir(&fs, BFS_ROOT_INO, "folder", 6, &inode);
    if (error == BFS_OK)
        error = bfs_fs_create_file(&fs, inode, "nested.txt", 10, &inode);
    if (error == BFS_OK)
        error = bfs_fs_make_softlink(&fs, BFS_ROOT_INO, "oracle-link", 11,
                                     "oracle.txt", 10);
    if (error == BFS_OK && hard_link)
        error = bfs_fs_make_hardlink(&fs, BFS_ROOT_INO, "oracle-hardlink", 15, oracle_inode);
    if (error == BFS_OK)
        error = bfs_fs_create_file(&fs, BFS_ROOT_INO, "@bfs-hex-literal", 16, &inode);
    if (error == BFS_OK && directory_scale) {
        for (unsigned index = 0; index < 16 && error == BFS_OK; index++) {
            char name[16];
            int length = snprintf(name, sizeof(name), "entry-%02u", index);
            if (length < 0)
                error = BFS_ERR_IO;
            else
                error = bfs_fs_create_file(&fs, BFS_ROOT_INO, name, (uint8_t)length, &inode);
        }
    }
    if (error == BFS_OK) error = bfs_snapshot_create(&fs, "oracle-snapshot");
    if (error == BFS_OK) error = bfs_file_open(&file, &fs, oracle_inode);
    if (error == BFS_OK) error = bfs_file_truncate(&file, 0);
    if (error == BFS_OK && bfs_file_write(&file, "live contents", 13) != 13)
        error = BFS_ERR_IO;
    if (fs.mounted && bfs_fs_unmount(&fs) != BFS_OK && error == BFS_OK) error = BFS_ERR_IO;
    bfs_bio_close(bio);
    return error == BFS_OK ? 0 : 1;
}
