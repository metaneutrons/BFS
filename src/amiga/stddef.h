/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_STDDEF_H
#define BFS_AMIGA_STDDEF_H

typedef unsigned long size_t;
#ifndef NULL
  #define NULL ((void *)0)
#endif
#ifndef offsetof
  #define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#endif
