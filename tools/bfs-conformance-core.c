/* SPDX-License-Identifier: MPL-2.0 */
/* Direct conformance backend. It owns and removes every formatted image. */

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_posix_bio.h"
#include "bfs_snapshot.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLOCK_SIZE 4096u
#define BLOCK_COUNT 512u

typedef enum { RESULT_PASS, RESULT_FAIL, RESULT_SKIP, RESULT_ERROR } result_t;

static const char *result_name(result_t result)
{
    static const char *const names[] = { "pass", "fail", "skip", "error" };
    return names[result];
}

static bool supported_case(const char *name)
{
    return strcmp(name, "empty-volume") == 0 || strcmp(name, "regular-file") == 0 ||
           strcmp(name, "multi-block-file") == 0 || strcmp(name, "large-file") == 0 ||
           strcmp(name, "read-only-refusal") == 0 || strcmp(name, "snapshot") == 0 ||
           strcmp(name, "disk-full") == 0 || strcmp(name, "remount") == 0 ||
           strcmp(name, "directory-scale") == 0 ||
           strcmp(name, "hard-link") == 0 || strcmp(name, "soft-link") == 0 ||
           strcmp(name, "comment-metadata") == 0 || strcmp(name, "name-encoding") == 0 ||
           strcmp(name, "sparse-range") == 0 || strcmp(name, "invalid-name") == 0;
}

static bool create_owned_image(char *directory, size_t directory_size,
                               char *image, size_t image_size)
{
    if (directory_size < sizeof("/tmp/bfs-conformance.XXXXXX")) return false;
    snprintf(directory, directory_size, "/tmp/bfs-conformance.XXXXXX");
    if (!mkdtemp(directory))
        return false;
    if (snprintf(image, image_size, "%s/image.bfs", directory) >= (int)image_size) {
        (void)rmdir(directory);
        return false;
    }
    int fd = open(image, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        (void)rmdir(directory);
        return false;
    }
    bool ok = ftruncate(fd, (off_t)BLOCK_SIZE * BLOCK_COUNT) == 0 && close(fd) == 0;
    if (!ok) {
        (void)close(fd);
        (void)unlink(image);
        (void)rmdir(directory);
    }
    return ok;
}

static bool remove_owned_image(const char *directory, const char *image)
{
    return unlink(image) == 0 && rmdir(directory) == 0;
}

static bfs_bio_t *open_image(const char *path, bool writable)
{
    bfs_posix_bio_options_t options = {
        .block_size = BLOCK_SIZE,
        .writable = writable,
        .lock = true,
    };
    return bfs_posix_bio_open(path, &options);
}

static bfs_err_t create_and_write(bfs_fs_t *fs, const uint8_t *contents, uint32_t length,
                                  uint32_t *inode_out)
{
    bfs_err_t error = bfs_fs_create_file(fs, BFS_ROOT_INO, "case.bin", 8, inode_out);
    if (error != BFS_OK) return error;
    bfs_file_t file;
    error = bfs_file_open(&file, fs, *inode_out);
    if (error != BFS_OK) return error;
    return bfs_file_write(&file, contents, length) == (int32_t)length ? BFS_OK : BFS_ERR_IO;
}

static bfs_err_t exercise_disk_full(bfs_fs_t *fs)
{
    uint8_t pattern[BLOCK_SIZE], readback[BLOCK_SIZE];
    bfs_file_t file;
    uint32_t inode, written = 0;
    int32_t result;
    memset(pattern, 0xa5, sizeof(pattern));
    if (bfs_fs_create_file(fs, BFS_ROOT_INO, "fill.bin", 8, &inode) != BFS_OK ||
        bfs_file_open(&file, fs, inode) != BFS_OK)
        return BFS_ERR_IO;
    while (written < BLOCK_COUNT &&
           (result = bfs_file_write(&file, pattern, sizeof(pattern))) == (int32_t)sizeof(pattern))
        written++;
    if (written == 0 || written == BLOCK_COUNT || result != BFS_ERR_NOSPC ||
        bfs_fs_sync(fs) != BFS_OK)
        return BFS_ERR_IO;
    if (bfs_file_seek(&file, 0, BFS_SEEK_SET) < 0 ||
        bfs_file_read(&file, readback, sizeof(readback)) != (int32_t)sizeof(readback) ||
        memcmp(pattern, readback, sizeof(pattern)) != 0)
        return BFS_ERR_IO;
    uint64_t half = (uint64_t)(written / 2) * BLOCK_SIZE;
    if (half == 0 || bfs_file_truncate(&file, half) != BFS_OK || bfs_fs_sync(fs) != BFS_OK ||
        bfs_file_seek(&file, (int64_t)half, BFS_SEEK_SET) < 0 ||
        bfs_file_write(&file, pattern, sizeof(pattern)) != (int32_t)sizeof(pattern))
        return BFS_ERR_IO;
    return BFS_OK;
}

static bfs_err_t exercise_extra_case(bfs_fs_t *fs, const char *name, uint32_t inode)
{
    if (strcmp(name, "disk-full") == 0) {
        return exercise_disk_full(fs);
    } else if (strcmp(name, "directory-scale") == 0) {
        for (unsigned index = 0; index < 48; index++) {
            char entry[16];
            int length = snprintf(entry, sizeof(entry), "entry-%02u", index);
            if (length < 0 || bfs_fs_create_file(fs, BFS_ROOT_INO, entry,
                                                 (uint8_t)length, &inode) != BFS_OK)
                return BFS_ERR_IO;
        }
    } else if (strcmp(name, "hard-link") == 0) {
        if (bfs_fs_make_hardlink(fs, BFS_ROOT_INO, "case-link", 9, inode) != BFS_OK)
            return BFS_ERR_IO;
    } else if (strcmp(name, "soft-link") == 0) {
        if (bfs_fs_make_softlink(fs, BFS_ROOT_INO, "case-soft", 9, "case.bin", 8) != BFS_OK)
            return BFS_ERR_IO;
    } else if (strcmp(name, "comment-metadata") == 0) {
        char comment[16] = {0};
        if (bfs_fs_set_comment(fs, inode, "comment", 7) != BFS_OK ||
            bfs_fs_get_comment(fs, inode, comment, sizeof(comment)) != BFS_OK ||
            memcmp(comment, "comment", 7) != 0)
            return BFS_ERR_IO;
    } else if (strcmp(name, "name-encoding") == 0) {
        const char original[] = { 'G', 'r', (char)0xe4 };
        const char alias[] = { 'G', 'R', (char)0xc4 };
        if (bfs_fs_create_file(fs, BFS_ROOT_INO, original, sizeof(original), &inode) != BFS_OK ||
            bfs_fs_create_file(fs, BFS_ROOT_INO, alias, sizeof(alias), &inode) != BFS_ERR_EXISTS)
            return BFS_ERR_IO;
    } else if (strcmp(name, "sparse-range") == 0) {
        bfs_file_t file;
        if (bfs_file_open(&file, fs, inode) != BFS_OK ||
            bfs_file_seek(&file, 8 * BLOCK_SIZE, BFS_SEEK_SET) < 0 ||
            bfs_file_write(&file, "end", 3) != 3)
            return BFS_ERR_IO;
    } else if (strcmp(name, "invalid-name") == 0) {
        if (bfs_fs_create_file(fs, BFS_ROOT_INO, "bad/name", 8, &inode) != BFS_ERR_INVAL)
            return BFS_ERR_IO;
    }
    return BFS_OK;
}

static bfs_err_t verify_readonly(const char *path, const uint8_t *contents, uint32_t length,
                                 bool reject_mutation)
{
    bfs_bio_t *bio = open_image(path, false);
    if (!bio) return BFS_ERR_IO;
    bfs_fs_t fs = {0};
    bfs_err_t error = bfs_fs_mount_readonly(&fs, bio);
    if (error == BFS_OK && reject_mutation) {
        uint32_t ignored;
        error = bfs_fs_create_file(&fs, BFS_ROOT_INO, "denied", 6, &ignored);
        if (error == BFS_ERR_UNSUPPORTED) error = BFS_OK;
    }
    if (error == BFS_OK && contents) {
        uint32_t inode, type;
        error = bfs_dir_lookup(&fs.dir_tree, BFS_ROOT_INO, "case.bin", 8, &inode, &type);
        if (error == BFS_OK) {
            uint8_t *actual = malloc(length);
            bfs_file_t file;
            if (!actual || bfs_file_open(&file, &fs, inode) != BFS_OK ||
                bfs_file_read(&file, actual, length) != (int32_t)length ||
                memcmp(actual, contents, length) != 0)
                error = BFS_ERR_IO;
            free(actual);
        }
    }
    if (fs.mounted && bfs_fs_unmount(&fs) != BFS_OK && error == BFS_OK) error = BFS_ERR_IO;
    bfs_bio_close(bio);
    return error;
}

static bool snapshot_seen(uint32_t id, const bfs_snapshot_record_t *record, void *context)
{
    (void)id;
    (void)record;
    *(bool *)context = true;
    return false;
}

static uint32_t case_length(const char *name, bool has_file)
{
    if (!has_file) return 0;
    if (strcmp(name, "regular-file") == 0 || strcmp(name, "remount") == 0 ||
        strcmp(name, "directory-scale") == 0 || strcmp(name, "hard-link") == 0 ||
        strcmp(name, "soft-link") == 0 || strcmp(name, "comment-metadata") == 0 ||
        strcmp(name, "name-encoding") == 0 || strcmp(name, "sparse-range") == 0)
        return 19;
    return strcmp(name, "multi-block-file") == 0 ? 2 * BLOCK_SIZE + 17 :
        64 * BLOCK_SIZE + 31;
}

static bfs_err_t mutate_case(bfs_fs_t *fs, const char *name, const uint8_t *contents,
                             uint32_t length, bool has_file)
{
    uint32_t inode = 0;
    if (has_file && create_and_write(fs, contents, length, &inode) != BFS_OK)
        return BFS_ERR_IO;
    if (exercise_extra_case(fs, name, inode) != BFS_OK) return BFS_ERR_IO;
    if (strcmp(name, "snapshot") != 0) return BFS_OK;
    bool seen = false;
    return bfs_snapshot_create(fs, "check") == BFS_OK &&
           bfs_snapshot_list(fs, snapshot_seen, &seen) == BFS_OK && seen ? BFS_OK : BFS_ERR_IO;
}

static result_t run_case(const char *name)
{
    if (!supported_case(name)) return RESULT_SKIP;
    char directory[64], image[96];
    result_t result = RESULT_ERROR;
    uint8_t *contents = NULL;
    bfs_bio_t *bio = NULL;
    bfs_fs_t fs;
    bool mounted = false;
    if (!create_owned_image(directory, sizeof(directory), image, sizeof(image))) return RESULT_ERROR;
    bool has_file = strcmp(name, "empty-volume") != 0 && strcmp(name, "invalid-name") != 0 &&
                    strcmp(name, "disk-full") != 0;
    uint32_t length = case_length(name, has_file);
    if (length) {
        contents = malloc(length);
        if (!contents) goto done;
        for (uint32_t index = 0; index < length; index++) contents[index] = (uint8_t)(index * 31u);
    }
    bio = open_image(image, true);
    if (!bio || bfs_fs_format(bio, "Conformance", 0) != BFS_OK || bfs_fs_mount(&fs, bio) != BFS_OK)
        goto done;
    mounted = true;
    if (mutate_case(&fs, name, contents, length, has_file) != BFS_OK) goto done;
    if (bfs_fs_unmount(&fs) != BFS_OK) goto done;
    mounted = false;
    bfs_bio_close(bio);
    bio = NULL;
    if (verify_readonly(image, has_file ? contents : NULL, length,
                        strcmp(name, "read-only-refusal") == 0) != BFS_OK)
        goto done;
    result = RESULT_PASS;
done:
    if (mounted) (void)bfs_fs_unmount(&fs);
    if (bio) bfs_bio_close(bio);
    free(contents);
    if (!remove_owned_image(directory, image)) result = RESULT_ERROR;
    return result;
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--case") == 0 && index + 1 < argc) name = argv[++index];
        else if (strcmp(argv[index], "--seed") == 0 && index + 1 < argc) index++;
        else return 3;
    }
    if (!name) return 3;
    result_t result = run_case(name);
    printf("{\"id\":\"%s\",\"status\":\"%s\",\"code\":\"direct-v1\"}\n",
           name, result_name(result));
    return result == RESULT_PASS ? 0 : result == RESULT_FAIL ? 1 : result == RESULT_SKIP ? 2 : 3;
}
