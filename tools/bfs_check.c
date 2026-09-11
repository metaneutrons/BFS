/* SPDX-License-Identifier: MPL-2.0 */
/* Read-only AmigaOS front-end for the portable BFS checker. */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>

#include "../include/bfs_diagnostics.h"
#include "bfs_command.h"

int bfs_check_command(const char *drive)
{
    struct DevProc *device = GetDeviceProc(drive, NULL);
    struct MsgPort *port = device ? device->dvp_Port : NULL;
    ULONG report[BFS_CHECK_REPORT_WORDS] = {0, 0, 0, 0};

    if (!port) {
        bfs_put("Cannot find handler for ");
        bfs_put(drive);
        bfs_put("\n");
        if (device) FreeDeviceProc(device);
        return 20;
    }

    if (!DoPkt(port, BFS_ACTION_CHECK, (LONG)report, sizeof(report), 0, 0, 0)) {
        PrintFault(IoErr(), "bfs");
        FreeDeviceProc(device);
        return 20;
    }
    FreeDeviceProc(device);

    bfs_put("Errors: ");
    bfs_putu64(report[0], 0);
    bfs_put("  Warnings: ");
    bfs_putu64(report[1], 0);
    bfs_put("\nLeaked blocks: ");
    bfs_putu64(report[2], 0);
    bfs_put("\n");
    if (report[0]) {
        bfs_put("ERRORS FOUND\n");
        return 20;
    }
    if (report[1]) {
        bfs_put("Minor issues\n");
        return 5;
    }
    bfs_put("CLEAN\n");
    return 0;
}
