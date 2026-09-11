/* SPDX-License-Identifier: MPL-2.0 */
/* Shared DOS-packet contract for BFS snapshot administration. */

#ifndef BFS_AMIGA_SNAPSHOT_PROTOCOL_H
#define BFS_AMIGA_SNAPSHOT_PROTOCOL_H

#include <exec/types.h>

/* Keep the existing administration packet range contiguous. */
#define BFS_ACTION_SNAPSHOT_CREATE      3000
#define BFS_ACTION_SNAPSHOT_DELETE      3001
#define BFS_ACTION_SNAPSHOT_LIST        3002
#define BFS_ACTION_SNAPSHOT_SHOW        3003
#define BFS_ACTION_SNAPSHOT_CAPABILITY  3006
#define BFS_ACTION_SNAPSHOT_MOUNT       3007
#define BFS_ACTION_SNAPSHOT_RELEASE     3008

#define BFS_SNAPSHOT_CAP_MOUNT_SOURCE   0x00000001u
#define BFS_SNAPSHOT_CAP_MOUNTED_VIEW   0x00000002u

#define BFS_SNAPSHOT_STARTUP_VERSION    1u

typedef struct {
    ULONG version;
    ULONG flags;
} bfs_snapshot_capability_t;

#endif /* BFS_AMIGA_SNAPSHOT_PROTOCOL_H */
