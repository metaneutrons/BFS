/* SPDX-License-Identifier: MPL-2.0 */
/* The record the candidate of docs/plans/bfs-exall-result-cache-v1.md keeps per
 * cached entry, excluding the name: what an ExAll fill needs from the inode
 * (size saturated as ed_Size is, protection, modify date, comment flag) plus
 * the inode number, entry type and the entry's place in key order. Compiled
 * only to read its size from the m68k compiler (memory-probe.sh). */
#include <stdint.h>

typedef struct {
    uint32_t ino;
    uint32_t hash;        /* place in key order: hash */
    uint32_t size;        /* ed_Size, saturated at INT32_MAX */
    uint32_t protection;
    uint16_t ordinal;     /* place in key order: ordinal */
    uint16_t days;
    uint16_t mins;
    uint16_t ticks;
    uint16_t name_offset; /* into the cache's name area */
    uint8_t type;
    uint8_t name_len;
    uint8_t flags;        /* bit 0: the inode has a comment */
} candidate_record_t;

const char candidate_record_bytes[sizeof(candidate_record_t)];
