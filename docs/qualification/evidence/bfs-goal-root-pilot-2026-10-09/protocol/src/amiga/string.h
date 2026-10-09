/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_STRING_H
#define BFS_AMIGA_STRING_H

#ifdef __VBCC__
  #include <string.h>
#else
  #include "stddef.h"
  void *memcpy(void *dst, const void *src, size_t n);
  void *memset(void *s, int c, size_t n);
  int memcmp(const void *a, const void *b, size_t n);
  size_t strlen(const char *s);
  int strcmp(const char *a, const char *b);
  char *strcpy(char *dst, const char *src);
#endif

#endif
