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

#endif /* BFS_CRC32_H */
