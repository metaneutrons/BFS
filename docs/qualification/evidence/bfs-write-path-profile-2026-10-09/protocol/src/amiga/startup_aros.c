/* SPDX-License-Identifier: MPL-2.0 */
/* Native AROS process entry. The m68k startup.s is not ABI-compatible. */
#include <aros/asmcall.h>
#include <dos/dos.h>
#include <exec/execbase.h>

extern struct ExecBase *SysBase;
extern void EntryPoint(void);

__startup static AROS_PROCH(BfsStartup, argstr, argsize, sysBase)
{
    AROS_PROCFUNC_INIT
    (void)argstr;
    (void)argsize;
    SysBase = sysBase;
    EntryPoint();
    return RETURN_OK;
    AROS_PROCFUNC_EXIT
}
