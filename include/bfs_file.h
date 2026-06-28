/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — File I/O operations
 *
 * Provides read/write/seek/truncate on open files.
 * Uses the extent tree for block mapping and the free space
 * allocator for data block allocation.
 */

#ifndef BFS_FILE_H
#define BFS_FILE_H

#include "bfs_extent.h"
#include "bfs_fs.h"

typedef struct {
    bfs_fs_t          *fs;
    bfs_extent_tree_t  extents;
    uint32_t            inode_nr;
    uint64_t            size;       /* file size in bytes */
    uint64_t            offset;     /* current read/write position */
} bfs_file_t;

/* Open a file by inode number. Reads inode from inode tree for extent_root/size. */
bfs_err_t bfs_file_open(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr);
bfs_err_t bfs_file_open_unlocked(bfs_file_t *f, bfs_fs_t *fs, uint32_t inode_nr);

/* Read up to 'len' bytes at current offset.
 * Returns bytes read (>=0), or negative bfs_err_t on error if no bytes were read. */
int32_t bfs_file_read(bfs_file_t *f, void *buf, uint32_t len);
int32_t bfs_file_read_unlocked(bfs_file_t *f, void *buf, uint32_t len);

/* Write 'len' bytes at current offset. Extends file if needed. Returns bytes written. */
int32_t bfs_file_write(bfs_file_t *f, const void *buf, uint32_t len);
int32_t bfs_file_write_unlocked(bfs_file_t *f, const void *buf, uint32_t len);

/* Seek. mode: 0=SET, 1=CUR, 2=END. Returns new offset or <0 on error. */
int64_t bfs_file_seek(bfs_file_t *f, int64_t offset, int mode);

/* Truncate file to 'new_size' bytes. */
bfs_err_t bfs_file_truncate(bfs_file_t *f, uint64_t new_size);
bfs_err_t bfs_file_truncate_unlocked(bfs_file_t *f, uint64_t new_size);

/* Get current extent root and size (for updating inode after modifications). */
static inline bfs_blk_t bfs_file_extent_root(const bfs_file_t *f) {
    return f->extents.tree.root;
}

#define BFS_SEEK_SET 0
#define BFS_SEEK_CUR 1
#define BFS_SEEK_END 2

#endif /* BFS_FILE_H */
