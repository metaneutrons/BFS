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

#define ACTION_FORMAT 1020

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
        FreeDeviceProc(dvp);
    }
    if (!port) {
        PutStr("Cannot find handler for ");
        PutStr(drive);
        PutStr("\n");
        FreeArgs(rdargs);
        me->pr_WindowPtr = oldwin;
        return 20;
    }

    /* Build BSTR name */
    UBYTE bstr[34];
    int nlen = 0;
    while (name[nlen] && nlen < 30) { bstr[nlen + 1] = name[nlen]; nlen++; }
    bstr[0] = nlen;

    PutStr("Formatting ");
    PutStr(drive);
    PutStr(" as \"");
    PutStr(name);
    PutStr("\"...\n");

    /* Inhibit, format, un-inhibit */
    Inhibit(drive, DOSTRUE);
    LONG res = DoPkt(port, ACTION_FORMAT, (LONG)MKBADDR(bstr), 0, 0, 0, 0);
    Inhibit(drive, DOSFALSE);

    me->pr_WindowPtr = oldwin;

    if (res) {
        PutStr("Format complete.\n");
    } else {
        PutStr("Format FAILED.\n");
        FreeArgs(rdargs);
        return 20;
    }

    FreeArgs(rdargs);
    return 0;
}
