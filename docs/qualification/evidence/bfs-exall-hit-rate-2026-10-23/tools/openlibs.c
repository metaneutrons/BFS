/* SPDX-License-Identifier: MPL-2.0 */
/* openlibs OUTFILE NAME...
 *
 * Diagnostic: opens each named library with DOS requesters suppressed for the
 * library loader and for itself, and writes for each name whether it opened,
 * with its version, or the DOS error that stopped it. The return code is 5 if
 * any library failed, so that a script can test it with IF WARN. The kit uses it
 * to find out whether Workbench can start. */
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/libraries.h>
#include <exec/types.h>
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
    if (argc < 3) return 10;
    out = Open((STRPTR)argv[1], MODE_NEWFILE);
    if (!out) return 10;
    struct Process *ramlib = (struct Process *)FindTask((STRPTR)"ramlib");
    if (ramlib) ramlib->pr_WindowPtr = (APTR)-1;
    ((struct Process *)FindTask(0))->pr_WindowPtr = (APTR)-1;
    int failed = 0;
    for (int i = 2; i < argc; i++) {
        SetIoErr(0);
        struct Library *lib = OpenLibrary((STRPTR)argv[i], 0);
        emit(argv[i]); emit("\t");
        if (lib) {
            emit("opened "); number(lib->lib_Version); emit("."); number(lib->lib_Revision);
            CloseLibrary(lib);
        } else {
            emit("FAILED, IoErr "); number(IoErr());
            failed++;
        }
        emit("\n");
    }
    Close(out);
    return failed ? 5 : 0;
}
