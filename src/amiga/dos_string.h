/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_DOS_STRING_H
#define BFS_DOS_STRING_H

#include <exec/types.h>

/* The NDK and the AROS SDK declare the string parameters of exec, dos and
 * intuition as pointers to unsigned char (STRPTR, CONST_STRPTR). BFS keeps C
 * strings as char and converts them only where they cross that boundary. */
#define DOS_CSTR(text) ((CONST_STRPTR)(text))
#define DOS_STR(text) ((STRPTR)(text))

#endif /* BFS_DOS_STRING_H */
