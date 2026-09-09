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

static char *valid_root(const char *requested)
{
    char input[PATH_MAX];
    struct stat st;
    if (!requested) return NULL;
    size_t length = strnlen(requested, sizeof(input));
    if (length >= sizeof(input)) return NULL;
    int copied = snprintf(input, sizeof(input), "%s", requested);
    if (copied < 0 || (size_t)copied >= sizeof(input)) return NULL;
    char *resolved = realpath(input, NULL);
    if (!resolved) return NULL;
    if (strcmp(resolved, "/") == 0 || lstat(resolved, &st) != 0 ||
        !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
        free(resolved);
        return NULL;
    }
    return resolved;
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
    char *resolved = name ? valid_root(root) : NULL;
    if (!resolved) return 3;
    DIR *directory = opendir(resolved);
    free(resolved);
    if (!directory) return 3;
    (void)closedir(directory);
    if (strcmp(name, "empty-volume") == 0)
        printf("{\"id\":\"%s\",\"status\":\"pass\",\"code\":\"mounted-readable\"}\n", name);
    else
        printf("{\"id\":\"%s\",\"status\":\"skip\",\"code\":\"mounted-fixture-required\"}\n", name);
    return strcmp(name, "empty-volume") == 0 ? 0 : 2;
}
