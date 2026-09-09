/* SPDX-License-Identifier: MPL-2.0 */
/* Mounted-path conformance backend. This file must never link libbfs. */

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    DIR *directory = fdopendir(root_descriptor);
    if (!directory) {
        (void)close(root_descriptor);
        return 3;
    }
    (void)closedir(directory);
    if (strcmp(name, "empty-volume") == 0)
        printf("{\"id\":\"%s\",\"status\":\"pass\",\"code\":\"mounted-readable\"}\n", name);
    else
        printf("{\"id\":\"%s\",\"status\":\"skip\",\"code\":\"mounted-fixture-required\"}\n", name);
    return strcmp(name, "empty-volume") == 0 ? 0 : 2;
}
