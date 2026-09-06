/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Superblock read/write/validate
 */

#ifndef BFS_SUPERBLOCK_H
#define BFS_SUPERBLOCK_H

#include "bfs_ondisk.h"
#include "bfs_bio.h"

/* Compute CRC32 for a superblock (sets sb->crc32 = 0 during computation) */
uint32_t bfs_sb_compute_crc(const bfs_superblock_t *sb);

/* Validate a superblock: checks magic, version, CRC32, and block size.
 * Returns BFS_OK if valid. */
bfs_err_t bfs_sb_validate(const bfs_superblock_t *sb);

/* Read the best (highest valid txn_id) superblock from the device.
 * Tries the primary and partition-midpoint backup copies and requires their
 * recorded geometry to match the device.
 * Returns BFS_OK on success, BFS_ERR_CORRUPT if neither is valid. */
bfs_err_t bfs_sb_read(bfs_bio_t *bio, bfs_superblock_t *sb_out);

/* Write superblock to the older of the two alternating slots.
 * Computes and stores CRC32 before writing. */
bfs_err_t bfs_sb_write(bfs_bio_t *bio, bfs_superblock_t *sb);

/* Write superblock to a specific byte offset (used during format). */
bfs_err_t bfs_sb_write_raw(bfs_bio_t *bio, uint64_t byte_offset, const bfs_superblock_t *sb);

#endif /* BFS_SUPERBLOCK_H */
