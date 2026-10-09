/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — CRC32 (IEEE 802.3 polynomial, same as zlib/ext4)
 */

#ifndef BFS_CRC32_H
#define BFS_CRC32_H

#include "bfs_types.h"

/* Compute CRC32 over a buffer. Use initial=0 for first call,
 * or chain calls by passing previous result as initial. */
uint32_t bfs_crc32(uint32_t initial, const void *data, size_t len);

/* Append length zero bytes to a CRC without materializing the zero buffer. */
uint32_t bfs_crc32_zeros(uint32_t initial, uint32_t length);

/* Bit-identical to bfs_crc32, with inspected long zero runs advanced through
 * the zero transform. Intended for sparse metadata, not ordinary file data.
 * No bytes are omitted: every nonzero byte is hashed normally. */
uint32_t bfs_crc32_sparse(uint32_t initial, const void *data, uint32_t length);

#endif /* BFS_CRC32_H */
