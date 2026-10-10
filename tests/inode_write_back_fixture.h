/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Fixture of the inode write-back tests: a mounted file system on an
 * emulated device, file contents of numbered chunks, and views of the pending
 * inode table. The including test defines WB_IMAGE.
 */

#ifndef INODE_WRITE_BACK_FIXTURE_H
#define INODE_WRITE_BACK_FIXTURE_H

#include "test_harness.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_internal.h"
#include "bfs_inode.h"
#include "block_device_emu.h"
#include <string.h>
#include <unistd.h>

#ifndef WB_IMAGE
#error "define WB_IMAGE before including inode_write_back_fixture.h"
#endif

#define WB_BLOCK_SIZE 4096
#define WB_BLOCK_COUNT 4096
#define WB_CHUNK 4096

typedef struct {
    bfs_fs_t fs;
    bfs_bio_t *bio;
} wb_fixture_t;

static uint8_t chunk[WB_CHUNK];
static uint8_t readback[8 * WB_CHUNK];

static inline bool fixture_open(wb_fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    (void)unlink(WB_IMAGE);
    f->bio = bio_emu_create(WB_IMAGE, WB_BLOCK_SIZE, WB_BLOCK_COUNT);
    if (!f->bio) return false;
    if (bfs_fs_format(f->bio, "WriteBack", 0) != BFS_OK) return false;
    return bfs_fs_mount(&f->fs, f->bio) == BFS_OK;
}

static inline void fixture_close(wb_fixture_t *f)
{
    if (f->fs.mounted) (void)bfs_fs_unmount(&f->fs);
    if (f->bio) bfs_bio_close(f->bio);
    f->bio = NULL;
    (void)unlink(WB_IMAGE);
}

static inline void fill_chunk(uint32_t index)
{
    for (uint32_t i = 0; i < WB_CHUNK; i++) chunk[i] = (uint8_t)(index * 31u + i);
}

static inline uint64_t inode_size(const bfs_inode_t *inode)
{
    return ((uint64_t)bfs_be32(inode->size_hi) << 32) | bfs_be32(inode->size_lo);
}

/* The size the API reports, which includes a pending copy. */
static inline uint64_t read_size(bfs_fs_t *fs, uint32_t ino)
{
    bfs_inode_t inode;
    if (bfs_inode_read(&fs->inode_tree, ino, &inode) != BFS_OK) return UINT64_MAX;
    return inode_size(&inode);
}

/* The size stored in the tree itself, without the pending copy. */
static inline uint64_t tree_size(bfs_fs_t *fs, uint32_t ino)
{
    bfs_inode_t inode;
    uint32_t key = bfs_be32(ino);
    if (bfs_btree_search(&fs->inode_tree, &key, &inode) != BFS_OK) return UINT64_MAX;
    return inode_size(&inode);
}

static inline uint32_t pending_count(const bfs_fs_t *fs, bool dirty_only)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < BFS_INODE_PENDING_SLOTS; i++) {
        const bfs_inode_pending_slot_t *slot = &fs->inode_pending.slot[i];
        if (slot->ino != 0 && (!dirty_only || slot->dirty)) count++;
    }
    return count;
}

/* The table's count of occupied slots matches its slots. */
static inline void check_used(const bfs_fs_t *fs)
{
    TEST_ASSERT_EQ(fs->inode_pending.used, pending_count(fs, false));
}

/* The new file's inode number, 0 on failure. */
static inline uint32_t create_file(wb_fixture_t *f, const char *name)
{
    uint32_t ino = 0;
    if (bfs_fs_create_file(&f->fs, BFS_ROOT_INO, name, (uint8_t)strlen(name), /* Flawfinder: ignore */
                           &ino) != BFS_OK)
        return 0;
    return ino;
}

static inline void write_chunks(bfs_file_t *file, uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        fill_chunk(first + i);
        TEST_ASSERT_EQ(bfs_file_write(file, chunk, WB_CHUNK), WB_CHUNK);
    }
}

static inline void check_contents(bfs_fs_t *fs, uint32_t ino, uint32_t chunks)
{
    bfs_file_t reader;
    TEST_ASSERT_EQ(bfs_file_open(&reader, fs, ino), BFS_OK);
    TEST_ASSERT_EQ(reader.size, (uint64_t)chunks * WB_CHUNK);
    TEST_ASSERT_EQ(bfs_file_read(&reader, readback, chunks * WB_CHUNK),
                   (int32_t)(chunks * WB_CHUNK));
    for (uint32_t i = 0; i < chunks; i++) {
        fill_chunk(i);
        TEST_ASSERT_MEM_EQ(readback + i * WB_CHUNK, chunk, WB_CHUNK);
    }
}

#endif /* INODE_WRITE_BACK_FIXTURE_H */
