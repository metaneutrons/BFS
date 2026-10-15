/* SPDX-License-Identifier: MPL-2.0 */
/* Native MorphOS process entry. The m68k startup.s does not apply: DOS starts
 * the handler through the 68k shim in front of the PowerPC code, which calls
 * the ELF entry point. The handler needs no C runtime, so the link uses no
 * startup file and EntryPoint opens what it uses itself. Link this object
 * first: the entry point must lead the text. */
extern void EntryPoint(void);

int __start(void)
{
    EntryPoint();
    return 0;
}
