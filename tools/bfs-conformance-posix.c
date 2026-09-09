/* SPDX-License-Identifier: MPL-2.0 */
/* Mounted-path conformance backend. This file must never link libbfs. */

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool valid_root(const char *requested, char resolved[PATH_MAX])
{
    struct stat st;
    return requested && strnlen(requested, PATH_MAX) < PATH_MAX &&
           realpath(requested, resolved) && strcmp(resolved, "/") != 0 &&
           lstat(resolved, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == getuid();
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
    char resolved[PATH_MAX];
    if (!name || !valid_root(root, resolved)) return 3;
    DIR *directory = opendir(resolved);
    if (!directory) return 3;
    (void)closedir(directory);
    if (strcmp(name, "empty-volume") == 0)
        printf("{\"id\":\"%s\",\"status\":\"pass\",\"code\":\"mounted-readable\"}\n", name);
    else
        printf("{\"id\":\"%s\",\"status\":\"skip\",\"code\":\"mounted-fixture-required\"}\n", name);
    return strcmp(name, "empty-volume") == 0 ? 0 : 2;
}
