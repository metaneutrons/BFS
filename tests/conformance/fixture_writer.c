/* SPDX-License-Identifier: MPL-2.0 */
/* Test-only committed-image producer for the independent format oracle. */

#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_posix_bio.h"
#include "bfs_snapshot.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FIXTURE_BLOCK_SIZE 4096u
#define FIXTURE_BLOCK_COUNT 512u

int main(int argc, char **argv)
{
    if (argc != 2 && (argc != 3 || strcmp(argv[2], "--directory-scale") != 0)) return 2;
    int fd = open(argv[1], O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)FIXTURE_BLOCK_SIZE * FIXTURE_BLOCK_COUNT) != 0 ||
        close(fd) != 0)
        return 1;
    bfs_posix_bio_options_t options = {
        .block_size = FIXTURE_BLOCK_SIZE,
        .writable = true,
        .lock = true,
    };
    bfs_bio_t *bio = bfs_posix_bio_open(argv[1], &options);
    if (!bio) return 1;
    bfs_fs_t fs = {0};
    uint32_t inode;
    bfs_file_t file;
    const char contents[] = "oracle contents";
    bfs_err_t error = bfs_fs_format(bio, "Oracle", 0);
    if (error == BFS_OK) error = bfs_fs_mount(&fs, bio);
    if (error == BFS_OK) error = bfs_fs_create_file(&fs, BFS_ROOT_INO, "oracle.txt", 10, &inode);
    if (error == BFS_OK) error = bfs_file_open(&file, &fs, inode);
    if (error == BFS_OK && bfs_file_write(&file, contents, sizeof(contents) - 1) !=
        (int32_t)(sizeof(contents) - 1))
        error = BFS_ERR_IO;
    if (error == BFS_OK) error = bfs_fs_set_comment(&fs, inode, "fixture", 7);
    if (error == BFS_OK && argc == 3) {
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
    if (fs.mounted && bfs_fs_unmount(&fs) != BFS_OK && error == BFS_OK) error = BFS_ERR_IO;
    bfs_bio_close(bio);
    return error == BFS_OK ? 0 : 1;
}
