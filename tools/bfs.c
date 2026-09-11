/* SPDX-License-Identifier: MPL-2.0 */
/* Canonical AmigaOS administration command for BFS. */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "bfs_command.h"

#ifndef BFS_VERSION
#define BFS_VERSION "development"
#endif

#define VSTRING "bfs " BFS_VERSION
#define VERSTAG "\0$VER: " VSTRING

static const char version[] = VERSTAG;

static void usage(void)
{
    bfs_put(VSTRING "\n\n");
    bfs_put("Usage:\n");
    bfs_put("  bfs format DRIVE: NAME\n");
    bfs_put("  bfs snapshot create DRIVE: NAME\n");
    bfs_put("  bfs snapshot delete DRIVE: NAME\n");
    bfs_put("  bfs snapshot list DRIVE:\n");
    bfs_put("  bfs snapshot dir DRIVE: NAME [DIRS|FILES]\n");
    bfs_put("  bfs snapshot inspect DRIVE: NAME [DIRS|FILES]\n");
    bfs_put("  bfs snapshot mount DRIVE: NAME TARGET:\n");
    bfs_put("  bfs snapshot unmount TARGET:\n");
    bfs_put("  bfs check DRIVE:\n");
    bfs_put("  bfs info DRIVE:\n");
}

int bfs_info_command(const char *drive)
{
    struct MsgPort *port = DeviceProc(drive);

    if (!port) {
        bfs_put("Cannot find handler for ");
        bfs_put(drive);
        bfs_put("\n");
        return 20;
    }
    bfs_put(VSTRING "\n");
    bfs_put("Drive: ");
    bfs_put(drive);
    bfs_put("\n");
    return 0;
}

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    struct RDArgs *rdargs;
    LONG arguments[5] = {0, 0, 0, 0, 0};
    const char *command;
    const char *first;
    const char *second;
    const char *third;
    const char *fourth;
    int result;

    (void)version;
    process->pr_WindowPtr = (APTR)-1;
    rdargs = ReadArgs("COMMAND/A,ARG1,ARG2,ARG3,ARG4", arguments, NULL);
    if (!rdargs) {
        usage();
        process->pr_WindowPtr = old_window;
        return 5;
    }

    command = (const char *)arguments[0];
    first = (const char *)arguments[1];
    second = (const char *)arguments[2];
    third = (const char *)arguments[3];
    fourth = (const char *)arguments[4];
    if (bfs_equal_nocase(command, "format")) {
        if (!first || !second || third || fourth) {
            usage();
            result = 10;
        } else {
            result = bfs_format_command(first, second);
        }
    } else if (bfs_equal_nocase(command, "snapshot")) {
        if (!first || !second) {
            usage();
            result = 10;
        } else {
            result = bfs_snapshot_command(first, second, third, fourth);
        }
    } else if (bfs_equal_nocase(command, "info")) {
        if (!first || second || third || fourth) {
            usage();
            result = 10;
        } else {
            result = bfs_info_command(first);
        }
    } else if (bfs_equal_nocase(command, "check")) {
        if (!first || second || third || fourth) {
            usage();
            result = 10;
        } else {
            result = bfs_check_command(first);
        }
    } else {
        usage();
        result = 10;
    }

    FreeArgs(rdargs);
    process->pr_WindowPtr = old_window;
    return result;
}
