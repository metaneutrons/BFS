/*
 * BFS — File-backed block device emulator for host testing
 */

#ifndef BLOCK_DEVICE_EMU_H
#define BLOCK_DEVICE_EMU_H

#include "bfs_bio.h"

/*
 * Create a file-backed block device.
 * If the file doesn't exist, it is created and zero-filled.
 * If it exists, it is opened as-is.
 * Returns NULL on failure.
 */
bfs_bio_t *bio_emu_create(const char *path, uint32_t block_size, bfs_blk_t block_count);

/*
 * Open an existing file-backed block device.
 * block_size and block_count are inferred from file size.
 * Returns NULL on failure.
 */
bfs_bio_t *bio_emu_open(const char *path, uint32_t block_size);

#endif /* BLOCK_DEVICE_EMU_H */
