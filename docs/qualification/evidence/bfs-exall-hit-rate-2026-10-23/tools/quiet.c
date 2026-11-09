/* SPDX-License-Identifier: MPL-2.0 */
/* quiet OUTFILE COMMAND...
 *
 * Diagnostic: makes DOS fail quietly instead of showing "System Request"
 * windows for the library loader (ramlib) and for itself, runs the command and
 * prints its return code. A command that waits behind a requester on a screen
 * nobody sees then returns with the error instead. The report goes to OUTFILE,
 * and the command's own output to the same file. */
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/types.h>
#include <utility/tagitem.h>
#include <proto/dos.h>
#include <proto/exec.h>

static ULONG length(const char *s) { ULONG n = 0; while (s[n]) n++; return n; }
static BPTR out;
static void emit(const char *s) { Write(out, (APTR)s, length(s)); }
static void number(long v)
{
    char d[14]; int n = 0; unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v;
    if (v < 0) emit("-");
    if (!u) d[n++] = '0';
    while (u) { d[n++] = (char)('0' + u % 10); u /= 10; }
    while (n) { char c = d[--n]; Write(out, &c, 1); }
}

int main(int argc, char **argv)
{
    char command[256];
    ULONG n = 0;
    if (argc < 3) return 10;
    out = Open((STRPTR)argv[1], MODE_NEWFILE);
    if (!out) return 10;
    for (int i = 2; i < argc && n + 2 < sizeof(command); i++) {
        if (i > 2) command[n++] = ' ';
        for (const char *p = argv[i]; *p && n + 2 < sizeof(command); p++) command[n++] = *p;
    }
    command[n] = 0;
    struct Process *ramlib = (struct Process *)FindTask((STRPTR)"ramlib");
    if (ramlib) ramlib->pr_WindowPtr = (APTR)-1;
    ((struct Process *)FindTask(0))->pr_WindowPtr = (APTR)-1;
    emit("quiet: running "); emit(command); emit("\n");
    LONG rc = SystemTags(command, SYS_Output, out, SYS_Input, 0, TAG_DONE);
    emit("quiet: return code "); number(rc); emit(", IoErr "); number(IoErr()); emit("\n");
    Close(out);
    return 0;
}
