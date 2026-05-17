/*
 * BFS — File-backed block device emulator
 */

#include "block_device_emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bfs_bio_t base;
    FILE *fp;
} bio_emu_t;

static bfs_err_t emu_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    bio_emu_t *emu = (bio_emu_t *)bio;
    if (blk >= bio->block_count) return BFS_ERR_INVAL;
    if (fseek(emu->fp, (long)blk * bio->block_size, SEEK_SET) != 0)
        return BFS_ERR_IO;
    if (fread(buf, bio->block_size, 1, emu->fp) != 1)
        return BFS_ERR_IO;
    return BFS_OK;
}

static bfs_err_t emu_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    bio_emu_t *emu = (bio_emu_t *)bio;
    if (blk >= bio->block_count) return BFS_ERR_INVAL;
    if (fseek(emu->fp, (long)blk * bio->block_size, SEEK_SET) != 0)
        return BFS_ERR_IO;
    if (fwrite(buf, bio->block_size, 1, emu->fp) != 1)
        return BFS_ERR_IO;
    return BFS_OK;
}

static bfs_err_t emu_sync(bfs_bio_t *bio)
{
    bio_emu_t *emu = (bio_emu_t *)bio;
    return fflush(emu->fp) == 0 ? BFS_OK : BFS_ERR_IO;
}

static void emu_close(bfs_bio_t *bio)
{
    bio_emu_t *emu = (bio_emu_t *)bio;
    if (emu->fp) fclose(emu->fp);
    free(emu);
}

static const bfs_bio_ops_t emu_ops = {
    .read_block  = emu_read,
    .write_block = emu_write,
    .sync        = emu_sync,
    .close       = emu_close,
};

bfs_bio_t *bio_emu_create(const char *path, uint32_t block_size, bfs_blk_t block_count)
{
    if (block_size < BFS_MIN_BLOCK_SIZE || block_size > BFS_MAX_BLOCK_SIZE)
        return NULL;
    /* block_size must be power of 2 */
    if (block_size & (block_size - 1))
        return NULL;

    FILE *fp = fopen(path, "w+b");
    if (!fp) return NULL;

    /* Extend file to full size */
    uint64_t total = (uint64_t)block_size * block_count;
    if (fseek(fp, (long)(total - 1), SEEK_SET) != 0 || fputc(0, fp) == EOF) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    bio_emu_t *emu = calloc(1, sizeof(*emu));
    if (!emu) { fclose(fp); return NULL; }

    emu->base.ops = &emu_ops;
    emu->base.block_size = block_size;
    emu->base.block_count = block_count;
    emu->fp = fp;
    return &emu->base;
}

bfs_bio_t *bio_emu_open(const char *path, uint32_t block_size)
{
    if (block_size < BFS_MIN_BLOCK_SIZE || block_size > BFS_MAX_BLOCK_SIZE)
        return NULL;
    if (block_size & (block_size - 1))
        return NULL;

    FILE *fp = fopen(path, "r+b");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size <= 0 || (size % block_size) != 0) {
        fclose(fp);
        return NULL;
    }

    bio_emu_t *emu = calloc(1, sizeof(*emu));
    if (!emu) { fclose(fp); return NULL; }

    emu->base.ops = &emu_ops;
    emu->base.block_size = block_size;
    emu->base.block_count = (bfs_blk_t)(size / block_size);
    emu->fp = fp;
    return &emu->base;
}
