/* SPDX-License-Identifier: MPL-2.0 */
/* AROS CLI entry for the shared Amiga-family administration command. */
#include <aros/asmcall.h>
#include <dos/dos.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <proto/exec.h>

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
extern int main(void);

__startup static AROS_PROCH(BfsCliStartup, argstr, argsize, sysBase)
{
    AROS_PROCFUNC_INIT
    (void)argstr;
    (void)argsize;
    SysBase = sysBase;
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 37);
    if (!DOSBase) return RETURN_FAIL;
    int result = main();
    CloseLibrary((struct Library *)DOSBase);
    return result;
    AROS_PROCFUNC_EXIT
}
