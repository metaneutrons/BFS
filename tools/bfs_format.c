/* SPDX-License-Identifier: MPL-2.0 */
/* Formatting subcommand for the AmigaOS BFS administration command. */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <proto/dos.h>

#include "../include/bfs_diagnostics.h"
#include "bfs_command.h"

#define ACTION_FORMAT 1020

int bfs_format_command(const char *drive, const char *name)
{
    struct MsgPort *port = NULL;
    struct DevProc *device = GetDeviceProc(drive, NULL);
    char format_message[BFS_FORMAT_ERROR_MAX] = {0};
    bfs_bstr_t bstr;
    LONG result, format_error, uninhibited, uninhibit_error;

    if (device) port = device->dvp_Port;
    if (!port) {
        bfs_put("Cannot find handler for ");
        bfs_put(drive);
        bfs_put("\n");
        if (device) FreeDeviceProc(device);
        return 20;
    }

    if (DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)format_message,
              sizeof(format_message), 0, 0, 0)) {
        format_message[sizeof(format_message) - 1] = 0;
        bfs_put(format_message);
        bfs_put("\n");
        FreeDeviceProc(device);
        return 20;
    }

    if (!bfs_build_name_bstr(bfs_bstr_bytes(&bstr), name)) {
        bfs_put("Volume names must contain 1 to 31 characters.\n");
        FreeDeviceProc(device);
        return 10;
    }

    bfs_put("Formatting ");
    bfs_put(drive);
    bfs_put(" as \"");
    bfs_put(name);
    bfs_put("\"...\n");

    if (!Inhibit(drive, DOSTRUE)) {
        PrintFault(IoErr(), "bfs");
        FreeDeviceProc(device);
        return 20;
    }
    result = DoPkt(port, ACTION_FORMAT, (LONG)MKBADDR(bfs_bstr_bytes(&bstr)), 0, 0, 0, 0);
    format_error = IoErr();
    uninhibited = Inhibit(drive, DOSFALSE);
    uninhibit_error = IoErr();
    if (!result && format_error == ERROR_NOT_IMPLEMENTED) {
        DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)format_message,
              sizeof(format_message), 0, 0, 0);
        format_message[sizeof(format_message) - 1] = 0;
    }
    FreeDeviceProc(device);

    if (!uninhibited) {
        PrintFault(uninhibit_error, "bfs");
        return 20;
    }
    if (result) {
        bfs_put("Format complete.\n");
        return 0;
    }
    if (format_message[0]) {
        bfs_put(format_message);
        bfs_put("\n");
    } else {
        PrintFault(format_error, "bfs");
    }
    return 20;
}
