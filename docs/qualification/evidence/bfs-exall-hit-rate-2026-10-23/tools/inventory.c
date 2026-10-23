/* SPDX-License-Identifier: MPL-2.0 */
/* inventory
 *
 * Lists the modules of the Kickstart ROM (name and version of every resident
 * module) and tries to open a few libraries, printing the version that opens.
 * Used to find out what the ROM carries when LoadWB fails. */
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

static ULONG length(const char *s) { ULONG n = 0; while (s[n]) n++; return n; }
static void emit(const char *s) { Write(Output(), (APTR)s, length(s)); }
static void number(unsigned long v)
{
    char d[12]; int n = 0;
    if (!v) d[n++] = '0';
    while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) { char c = d[--n]; Write(Output(), &c, 1); }
}

int main(void)
{
    struct ExecBase *sys = *(struct ExecBase **)4;
    emit("kickstart "); number(sys->LibNode.lib_Version); emit("."); number(sys->SoftVer); emit("\n");
    emit("-- resident modules\n");
    ULONG *list = (ULONG *)sys->ResModules;
    for (; *list; list++) {
        if (*list & 0x80000000UL) list = (ULONG *)(*list & 0x7fffffffUL);
        struct Resident *r = (struct Resident *)*list;
        if (!list || !*list) break;
        emit(r->rt_Name ? r->rt_Name : "?"); emit("\t"); number(r->rt_Version); emit("\n");
    }
    emit("-- OpenLibrary\n");
    static const char *const names[] = {"workbench.library", "icon.library", "intuition.library",
        "asl.library", "locale.library", "utility.library", "gadtools.library", "commodities.library",
        "datatypes.library", "iffparse.library", "diskfont.library", "bullet.library", "amigaguide.library", 0};
    for (int i = 0; names[i]; i++) {
        struct Library *lib = OpenLibrary((STRPTR)names[i], 0);
        emit(names[i]); emit("\t");
        if (lib) { number(lib->lib_Version); emit("."); number(lib->lib_Revision); CloseLibrary(lib); }
        else emit("not available");
        emit("\n");
    }
    return 0;
}
