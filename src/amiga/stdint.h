/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_STDINT_H
#define BFS_AMIGA_STDINT_H

/* Both GCC and VBCC for m68k-amigaos need these types defined.
 * We define them directly rather than including system headers
 * to avoid conflicts between our shims and system headers. */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;
#define UINT32_MAX 0xFFFFFFFFU
#define INT32_MAX  0x7FFFFFFF

#endif
