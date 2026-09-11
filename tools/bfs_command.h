/* SPDX-License-Identifier: MPL-2.0 */
/* Shared declarations for the AmigaOS BFS administration command. */

#ifndef BFS_COMMAND_H
#define BFS_COMMAND_H

#include <exec/types.h>

#define BFS_NAME_BSTR_MAX 34
#define BFS_SNAPSHOT_ENTRY_META_OFFSET 260

typedef struct {
    UBYTE storage[BFS_NAME_BSTR_MAX + 3];
} bfs_bstr_t;

void bfs_put(const char *text);
void bfs_putnum(LONG value);
void bfs_putu64(unsigned long long value, int width);
void bfs_putpad(const char *text, int width);
void bfs_copy(void *destination, const void *source, int length);
int bfs_equal_nocase(const char *left, const char *right);
int bfs_build_name_bstr(UBYTE *destination, const char *name);
UBYTE *bfs_bstr_bytes(bfs_bstr_t *bstr);
int bfs_contains_slash(const char *text);

int bfs_format_command(const char *drive, const char *name);
int bfs_snapshot_command(const char *operation, const char *drive,
                         const char *name, const char *option);
int bfs_check_command(const char *drive);
int bfs_info_command(const char *drive);

#endif
