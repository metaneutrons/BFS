/* SPDX-License-Identifier: MPL-2.0 */
/*
 * bfsformat — Format a BFS partition from AmigaOS
 *
 * Usage: bfsformat DRIVE/A NAME/A
 * Example: bfsformat DH1: BFSTest
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "../include/bfs_diagnostics.h"

#define ACTION_FORMAT 1020

/* BFS_VOLNAME_MAX (32) + BSTR length byte + NUL. Mirrors BFS_NAME_BSTR_MAX in
 * bfs_ondisk.h, which this standalone tool can't include. */
#define BFS_NAME_BSTR_MAX 34

int main(void)
{
    struct RDArgs *rdargs;
    LONG args[2] = {0, 0};
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;

    rdargs = ReadArgs("DRIVE/A,NAME/A", args, NULL);
    if (!rdargs) {
        PutStr("Usage: bfsformat DRIVE NAME\nExample: bfsformat DH1: BFSTest\n");
        me->pr_WindowPtr = oldwin;
        return 20;
    }

    const char *drive = (const char *)args[0];
    const char *name = (const char *)args[1];

    /* Get handler port via GetDeviceProc (works even if volume not mounted) */
    struct MsgPort *port = NULL;
    struct DevProc *dvp = GetDeviceProc(drive, NULL);
    if (dvp) {
        port = dvp->dvp_Port;
    }
    if (!port) {
        PutStr("Cannot find handler for ");
        PutStr(drive);
        PutStr("\n");
        if (dvp) FreeDeviceProc(dvp);
        FreeArgs(rdargs);
        me->pr_WindowPtr = oldwin;
        return 20;
    }

    char format_message[BFS_FORMAT_ERROR_MAX] = {0};
    if (DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)format_message,
              sizeof(format_message), 0, 0, 0)) {
        format_message[sizeof(format_message) - 1] = 0;
        PutStr(format_message);
        PutStr("\n");
        FreeDeviceProc(dvp);
        FreeArgs(rdargs);
        me->pr_WindowPtr = oldwin;
        return 20;
    }

    /* Build BSTR name */
    UBYTE bstr[BFS_NAME_BSTR_MAX];
    int nlen = 0;
    while (name[nlen]) nlen++;
    if (nlen < 1 || nlen >= 32) {
        PutStr("Volume names must contain 1 to 31 characters.\n");
        FreeDeviceProc(dvp);
        FreeArgs(rdargs);
        me->pr_WindowPtr = oldwin;
        return 10;
    }
    for (int i = 0; i < nlen; i++) bstr[i + 1] = (UBYTE)name[i];
    bstr[0] = nlen;

    PutStr("Formatting ");
    PutStr(drive);
    PutStr(" as \"");
    PutStr(name);
    PutStr("\"...\n");

    /* Inhibit, format, un-inhibit */
    if (!Inhibit(drive, DOSTRUE)) {
        PrintFault(IoErr(), "bfsformat");
        FreeDeviceProc(dvp);
        FreeArgs(rdargs);
        me->pr_WindowPtr = oldwin;
        return 20;
    }
    LONG res = DoPkt(port, ACTION_FORMAT, (LONG)MKBADDR(bstr), 0, 0, 0, 0);
    LONG format_error = IoErr();
    LONG uninhibited = Inhibit(drive, DOSFALSE);
    LONG uninhibit_error = IoErr();
    if (!res && format_error == ERROR_NOT_IMPLEMENTED) {
        DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)format_message,
              sizeof(format_message), 0, 0, 0);
        format_message[sizeof(format_message) - 1] = 0;
    }
    FreeDeviceProc(dvp);

    me->pr_WindowPtr = oldwin;

    if (!uninhibited) {
        PrintFault(uninhibit_error, "bfsformat");
        FreeArgs(rdargs);
        return 20;
    } else if (res) {
        PutStr("Format complete.\n");
    } else {
        if (format_message[0]) { PutStr(format_message); PutStr("\n"); }
        else PrintFault(format_error, "bfsformat");
        FreeArgs(rdargs);
        return 20;
    }

    FreeArgs(rdargs);
    return 0;
}
