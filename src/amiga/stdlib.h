/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_STDLIB_H
#define BFS_AMIGA_STDLIB_H

#ifdef __VBCC__
  /* VBCC has native malloc/free via amiga.lib */
  #include <exec/memory.h>
  #include <clib/exec_protos.h>
  #define malloc(s)   AllocVec((s), MEMF_CLEAR)
  #define free(p)     do { if (p) FreeVec(p); } while(0)
  #define calloc(n,s) AllocVec((n)*(s), MEMF_CLEAR)
#else
  /* GCC: use inline functions */
  #include "stddef.h"
  #include <exec/memory.h>
  #include <proto/exec.h>
  static inline void *malloc(size_t size) {
      return AllocVec(size, MEMF_CLEAR);
  }
  static inline void free(void *ptr) {
      if (ptr) FreeVec(ptr);
  }
  static inline void *calloc(size_t n, size_t size) {
      if (n && size > (size_t)-1 / n) return NULL;
      return AllocVec(n * size, MEMF_CLEAR);
  }
#endif

#endif
