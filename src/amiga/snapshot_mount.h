/* SPDX-License-Identifier: MPL-2.0 */
/* Versioned private startup and packet contract for BFS snapshot volumes. */

#ifndef BFS_AMIGA_SNAPSHOT_MOUNT_H
#define BFS_AMIGA_SNAPSHOT_MOUNT_H

#include <exec/types.h>
#include <dos/filehandler.h>

#include "bfs_snapshot.h"

#include "snapshot_protocol.h"

#define BFS_SNAPSHOT_STARTUP_MAGIC      0x42534d31u /* "BSM1" */

/* dp_Arg5 marks a private ACTION_STARTUP packet. dp_Arg4 then points at this
 * structure. The ordinary DOS startup packet and FileSysStartupMsg fields
 * retain their documented meanings. */
#define BFS_SNAPSHOT_STARTUP_PACKET_MAGIC 0x42535031u /* "BSP1" */

/* Owned by the source handler from creation until the snapshot handler has
 * completely shut down and released its pin. The embedded FileSysStartupMsg
 * is a normal DOS startup message; only the private startup packet refers to
 * this extension. */
typedef struct bfs_snapshot_startup {
    ULONG magic;
    ULONG version;
    ULONG size;
    ULONG pin;
    struct MsgPort *source_port;
    struct DeviceNode *device_node;
    struct DosList *volume_node;
    struct FileSysStartupMsg fssm;
    struct DosEnvec envec;
    UBYTE device_bstr[108];
    char mount_name[BFS_VOLNAME_MAX + 1];
    /* DOS device entries and mounted volume names share the same global
     * namespace.  Keep the worker device private so TARGET: resolves through
     * the snapshot volume entry rather than the source-style device entry. */
    char device_name[BFS_VOLNAME_MAX + 16];
    bfs_snapshot_record_t record;
} bfs_snapshot_startup_t;

#endif /* BFS_AMIGA_SNAPSHOT_MOUNT_H */
