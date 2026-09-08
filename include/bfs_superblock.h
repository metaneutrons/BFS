/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Superblock read/write/validate
 */

#ifndef BFS_SUPERBLOCK_H
#define BFS_SUPERBLOCK_H

#include "bfs_ondisk.h"
#include "bfs_bio.h"
#include "bfs_diagnostics.h"

/* Compute CRC32 over the bytes preceding crc32, without modifying sb. */
uint32_t bfs_sb_compute_crc(const bfs_superblock_t *sb);

/* Validate a superblock: checks magic, version, CRC32, and block size.
 * Returns BFS_OK if valid, BFS_ERR_UNSUPPORTED for an intact but unknown
 * version/options in the v2 envelope, or BFS_ERR_CORRUPT for damaged data. */
bfs_err_t bfs_sb_validate(const bfs_superblock_t *sb);

/* Describe an intact unsupported envelope, including its actual version.
 * Other inputs produce an empty string. */
void bfs_sb_describe_unsupported(const bfs_superblock_t *sb,
                                 char message[BFS_FORMAT_ERROR_MAX]);

/* Read the best (highest valid txn_id) superblock from the device.
 * Tries the primary and partition-midpoint backup copies and requires their
 * recorded geometry to match the device.
 * An intact unsupported copy vetoes fallback to the other copy and is returned
 * in sb_out for diagnostics only; it must not be used as mounted state.
 * Returns BFS_OK on success, BFS_ERR_CORRUPT if neither is valid. */
bfs_err_t bfs_sb_read(bfs_bio_t *bio, bfs_superblock_t *sb_out);

/* Probe representable filesystem block sizes against the full partition size.
 * Unsupported formats stop probing. Failure restores the original geometry. */
bfs_err_t bfs_sb_probe(bfs_bio_t *bio, uint64_t device_bytes,
                       bfs_superblock_t *sb_out);

/* Write superblock to the older of the two alternating slots.
 * Computes and stores CRC32 before writing. */
bfs_err_t bfs_sb_write(bfs_bio_t *bio, bfs_superblock_t *sb);

/* Write superblock to a specific byte offset (used during format). */
bfs_err_t bfs_sb_write_raw(bfs_bio_t *bio, uint64_t byte_offset, const bfs_superblock_t *sb);

#endif /* BFS_SUPERBLOCK_H */
