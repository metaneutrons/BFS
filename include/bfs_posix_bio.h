/* SPDX-License-Identifier: MPL-2.0 */
/* Production POSIX block transport for host tools and the FUSE adapter. */

#ifndef BFS_POSIX_BIO_H
#define BFS_POSIX_BIO_H

#include "bfs_bio.h"

typedef struct {
    uint64_t byte_offset; /* Beginning of the selected image or partition range. */
    uint64_t byte_length; /* Zero selects the complete remaining backing store. */
    uint32_t block_size;  /* Valid BFS block size used for the initial probe. */
    bool writable;
    bool lock;            /* Acquire a non-blocking advisory range lock. */
} bfs_posix_bio_options_t;

typedef struct {
    uint64_t read_calls;
    uint64_t write_calls;
    uint64_t sync_calls;
} bfs_posix_bio_stats_t;

/* Open an existing regular image or supported block device range. The function
 * never creates, truncates, formats, or discovers partitions by pathname. */
bfs_bio_t *bfs_posix_bio_open(const char *path, const bfs_posix_bio_options_t *options);

/* Read transport counters. They are primarily evidence for the read-only
 * lifecycle tests and remain valid until bfs_bio_close(). */
bfs_err_t bfs_posix_bio_get_stats(const bfs_bio_t *bio, bfs_posix_bio_stats_t *stats);

#endif /* BFS_POSIX_BIO_H */
