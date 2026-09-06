/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_DOS_PACKETS_H
#define BFS_AMIGA_DOS_PACKETS_H

#include <stddef.h>
#include "stdint.h"
#include <dos/dosextens.h>

/* Classic SDKs omit these wire definitions. See docs/amiga-packets.md. */
enum {
    BFS_ACTION_CHANGE_FILE_POSITION64 = 8001,
    BFS_ACTION_GET_FILE_POSITION64 = 8002,
    BFS_ACTION_CHANGE_FILE_SIZE64 = 8003,
    BFS_ACTION_GET_FILE_SIZE64 = 8004,
    BFS_ACTION_SEEK64 = 26400,
    BFS_ACTION_SET_FILE_SIZE64 = 26401,
    BFS_ACTION_QUERY_ATTR = 26407,
    BFS_ACTION_EXAMINE_OBJECT64 = 26408,
    BFS_ACTION_EXAMINE_NEXT64 = 26409,
    BFS_ACTION_EXAMINE_FH64 = 26410
};

typedef struct {
    struct Message *link;
    struct MsgPort *port;
    LONG type;
    LONG marker;
    LONG error;
    LONG legacy_handle;
    int64_t result;
    LONG handle;
    LONG pad;
    int64_t offset;
    LONG mode;
    struct FileHandle *file_handle;
    int64_t reserved;
} bfs_dos_packet64_t;

#define BFS_DP64_INIT (-3)

_Static_assert(offsetof(bfs_dos_packet64_t, result) == 24, "64-bit result ABI");
_Static_assert(offsetof(bfs_dos_packet64_t, handle) == 32, "64-bit handle ABI");
_Static_assert(offsetof(bfs_dos_packet64_t, offset) == 40, "64-bit offset ABI");
_Static_assert(offsetof(bfs_dos_packet64_t, mode) == 48, "64-bit mode ABI");
_Static_assert(sizeof(bfs_dos_packet64_t) == 64, "64-bit packet ABI");
_Static_assert(offsetof(struct FileInfoBlock, fib_Reserved) == 228, "MorphOS FIB ABI");

#endif
