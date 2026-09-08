/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_DIAGNOSTICS_H
#define BFS_DIAGNOSTICS_H

/* Read-only Amiga packet: Arg1 = text buffer, Arg2 = buffer capacity.
 * DOSTRUE returns a NUL-terminated incompatibility diagnosis. DOSFALSE with
 * error 0 means no diagnosis; older handlers return ACTION_NOT_KNOWN. */
#define BFS_ACTION_FORMAT_ERROR 3004
#define BFS_FORMAT_ERROR_MAX 192

#endif
