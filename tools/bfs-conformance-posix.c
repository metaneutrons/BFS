/* SPDX-License-Identifier: MPL-2.0 */
/* Mounted-path conformance backend. This file must never link libbfs. */

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static int open_root(const char *requested)
{
    struct stat path_st;
    struct stat st;
    struct stat filesystem_root;
    if (!requested || strnlen(requested, PATH_MAX) >= PATH_MAX ||
        strcmp(requested, "/") == 0)
        return -1;
    if (lstat(requested, &path_st) != 0 || S_ISLNK(path_st.st_mode)) return -1;
    int descriptor = open(requested, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    if (fstat(descriptor, &st) != 0 || stat("/", &filesystem_root) != 0 ||
        !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
        st.st_dev != path_st.st_dev || st.st_ino != path_st.st_ino ||
        (st.st_dev == filesystem_root.st_dev && st.st_ino == filesystem_root.st_ino)) {
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static int test_regular_file(int root)
{
    int descriptor = openat(root, "oracle.txt", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;

    FILE *file = fdopen(descriptor, "rb");
    if (file == NULL) {
        (void)close(descriptor);
        return -1;
    }

    const char expected[] = "oracle contents";
    char actual[sizeof(expected) - 1u];
    size_t got = fread(actual, 1u, sizeof(actual), file);
    int result = got == sizeof(actual) && ferror(file) == 0
                     && memcmp(actual, expected, sizeof(actual)) == 0
                     ? 0 : -1;
    if (fclose(file) != 0) result = -1;
    return result;
}

static int test_readonly_refusal(int root)
{
    errno = 0;
    return mkdirat(root, "mutation-attempt", 0700) == -1 && errno == EROFS ? 0 : -1;
}

static int test_soft_link(int root)
{
    char target[32];
    ssize_t length = readlinkat(root, "oracle-link", target, sizeof(target));
    return length == 10 && memcmp(target, "oracle.txt", 10) == 0 ? 0 : -1;
}

static int test_directory_scale(int root)
{
    int duplicate = dup(root);
    if (duplicate < 0) return -1;
    DIR *root_stream = fdopendir(duplicate);
    if (!root_stream) {
        (void)close(duplicate);
        return -1;
    }
    unsigned entries = 0;
    struct dirent *entry;
    while ((entry = readdir(root_stream)) != NULL)
        entries += strncmp(entry->d_name, "entry-", 6) == 0;
    if (closedir(root_stream) != 0 || entries != 16) return -1;
    int directory = openat(root, "folder", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return -1;
    DIR *stream = fdopendir(directory);
    if (!stream) {
        (void)close(directory);
        return -1;
    }
    bool found = false;
    entry = NULL;
    while ((entry = readdir(stream)) != NULL)
        found = found || strcmp(entry->d_name, "nested.txt") == 0;
    return closedir(stream) == 0 && found ? 0 : -1;
}

static int test_comment_metadata(int root)
{
    char value[16];
    int file = openat(root, "oracle.txt", O_RDONLY | O_CLOEXEC);
    if (file < 0) return -1;
#if defined(__APPLE__)
    ssize_t length = fgetxattr(file, "user.bfs.comment", value, sizeof(value), 0, 0);
#else
    ssize_t length = fgetxattr(file, "user.bfs.comment", value, sizeof(value));
#endif
    int result = length == 7 && memcmp(value, "fixture", 7) == 0 ? 0 : -1;
    if (close(file) != 0) result = -1;
    return result;
}

static int test_name_encoding(int root)
{
    const char *escaped = "@bfs-hex-406266732D6865782D6C69746572616C";
    int file = openat(root, escaped, O_RDONLY | O_CLOEXEC);
    if (file < 0) return -1;
    return close(file) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    const char *root = NULL;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--case") == 0 && index + 1 < argc) name = argv[++index];
        else if (strcmp(argv[index], "--root") == 0 && index + 1 < argc) root = argv[++index];
        else if (strcmp(argv[index], "--seed") == 0 && index + 1 < argc) index++;
        else return 3;
    }
    int root_descriptor = name ? open_root(root) : -1;
    if (root_descriptor < 0) return 3;
    int result = -1;
    if (strcmp(name, "empty-volume") == 0) {
        DIR *directory = fdopendir(root_descriptor);
        if (directory) result = closedir(directory) == 0 ? 0 : -1;
        else (void)close(root_descriptor);
    } else if (strcmp(name, "regular-file") == 0 || strcmp(name, "snapshot") == 0)
        result = test_regular_file(root_descriptor);
    else if (strcmp(name, "read-only-refusal") == 0)
        result = test_readonly_refusal(root_descriptor);
    else if (strcmp(name, "soft-link") == 0)
        result = test_soft_link(root_descriptor);
    else if (strcmp(name, "directory-scale") == 0)
        result = test_directory_scale(root_descriptor);
    else if (strcmp(name, "comment-metadata") == 0)
        result = test_comment_metadata(root_descriptor);
    else if (strcmp(name, "name-encoding") == 0)
        result = test_name_encoding(root_descriptor);
    else
        result = 1;
    if (result == 0)
        printf("{\"id\":\"%s\",\"status\":\"pass\",\"code\":\"mounted-readable\"}\n", name);
    else if (result == 1)
        printf("{\"id\":\"%s\",\"status\":\"skip\",\"code\":\"not-qualified\"}\n", name);
    else
        printf("{\"id\":\"%s\",\"status\":\"fail\",\"code\":\"mounted-observation-failed\"}\n", name);
    return result == 0 ? 0 : result == 1 ? 2 : 1;
}
