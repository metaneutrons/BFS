/* SPDX-License-Identifier: MPL-2.0 */
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "../../src/amiga/string.h"
#include "../../include/bfs_diagnostics.h"

char *strstr(const char *text, const char *needle);

static BOOL check_format(LONG expected)
{
    struct DevProc *device = GetDeviceProc("DH1:", NULL);
    if (!device) return FALSE;
    struct MsgPort *port = device->dvp_Port;
    char message[BFS_FORMAT_ERROR_MAX];
    memset(message, 0x55, sizeof(message));
    LONG result = DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)message, 1, 0, 0, 0);
    BOOL ok = !result && IoErr() == ERROR_BAD_NUMBER && message[0] == 0x55;
    result = DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)message, sizeof(message), 0, 0, 0);
    ok = ok && IoErr() == 0 && message[sizeof(message) - 1] == 0;
    message[sizeof(message) - 1] = 0;
    if (expected) {
        ok = ok && result && strstr(message, expected == 3 ?
            "version 3 is too new" : "version 2 uses unsupported options 0x80000000");
        if (expected == 3) ok = ok && strstr(message, "supports version 2");
        BPTR diagnosis = Open("SYS:diagnosis.txt", MODE_NEWFILE);
        if (!diagnosis) ok = FALSE;
        else {
            LONG length = 0;
            while (length < (LONG)sizeof(message) && message[length]) length++;
            if (Write(diagnosis, message, length) != length) ok = FALSE;
            if (!Close(diagnosis)) ok = FALSE;
        }
        /* Refuse both ordinary access and explicit format without a requester. */
        BPTR file = Open("DH1:must-not-exist", MODE_NEWFILE);
        if (file) { Close(file); ok = FALSE; }
        else if (IoErr() != ERROR_NOT_IMPLEMENTED) ok = FALSE;
        UBYTE name[] = {4, 'T', 'e', 's', 't', 0};
        result = DoPkt(port, 1020, (LONG)MKBADDR(name), 0, 0, 0, 0);
        ok = ok && !result && IoErr() == ERROR_NOT_IMPLEMENTED;
        if (!Inhibit("DH1:", DOSTRUE)) ok = FALSE;
        result = DoPkt(port, BFS_ACTION_FORMAT_ERROR, (LONG)message, sizeof(message), 0, 0, 0);
        ok = ok && result && IoErr() == 0;
        if (!Inhibit("DH1:", DOSFALSE)) ok = FALSE;
    } else {
        ok = ok && !result && message[0] == 0;
        BPTR root = Lock("DH1:", ACCESS_READ);
        if (!root) ok = FALSE;
        else UnLock(root);
    }
    FreeDeviceProc(device);
    return ok;
}

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    process->pr_WindowPtr = (APTR)-1;
    LONG args[1] = {0};
    struct RDArgs *parsed = ReadArgs("EXPECT/N/A", args, NULL);
    BOOL ok = parsed && check_format(*(LONG *)args[0]);
    if (parsed) FreeArgs(parsed);
    BPTR result = Open("SYS:compatibility.result", MODE_NEWFILE);
    const char *text = ok ? "PASS\n" : "FAIL\n";
    BOOL recorded = result && Write(result, (APTR)text, 5) == 5;
    if (result && !Close(result)) recorded = FALSE;
    if (recorded) {
        BPTR marker = Open("SYS:compatibility.result.done", MODE_NEWFILE);
        if (marker) Close(marker);
    }
    process->pr_WindowPtr = old_window;
    return ok && recorded ? 0 : 20;
}
