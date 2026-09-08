/* SPDX-License-Identifier: MPL-2.0 */
#include "test_harness.h"
#include "bfs_fs.h"
#include "block_device_emu.h"
#include <unistd.h>

#define IMAGE "test_compatibility.img"
#define BLOCK_SIZE 4096u
#define BLOCK_COUNT 256u

typedef struct {
    bfs_bio_t base;
    bfs_bio_t *inner;
    unsigned writes;
    unsigned syncs;
} counted_bio_t;

static bfs_err_t counted_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    return bfs_bio_read(((counted_bio_t *)bio)->inner, blk, buf);
}

static bfs_err_t counted_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    counted_bio_t *counted = (counted_bio_t *)bio;
    counted->writes++;
    return bfs_bio_write(counted->inner, blk, buf);
}

static bfs_err_t counted_sync(bfs_bio_t *bio)
{
    counted_bio_t *counted = (counted_bio_t *)bio;
    counted->syncs++;
    return bfs_bio_sync(counted->inner);
}

static const bfs_bio_ops_t counted_ops = {
    .read_block = counted_read, .write_block = counted_write, .sync = counted_sync,
};

static void test_incompatible_copies_never_write(void)
{
    for (unsigned option = 0; option < 2; option++) {
        for (unsigned slots = 1; slots <= 3; slots++) {
            for (unsigned newer = 0; newer < 2; newer++) {
                unlink(IMAGE);
                bfs_bio_t *bio = bio_emu_create(IMAGE, BLOCK_SIZE, BLOCK_COUNT);
                TEST_ASSERT(bio != NULL);
                TEST_ASSERT_EQ(bfs_fs_format(bio, "Compatibility", 0), BFS_OK);
                bfs_superblock_t current, future, out;
                TEST_ASSERT_EQ(bfs_sb_read(bio, &current), BFS_OK);
                future = current;
                if (option) future.options = bfs_be32(0x80000000u);
                else future.version = bfs_be32(BFS_SB_VERSION + 1);
                future.txn_id = bfs_be64(newer ? UINT64_MAX : 1);
                future.crc32 = bfs_be32(bfs_sb_compute_crc(&future));
                TEST_ASSERT_EQ(bfs_sb_validate(&future), BFS_ERR_UNSUPPORTED);
                uint64_t backup = bfs_default_backup_offset(BLOCK_COUNT, BLOCK_SIZE);
                if (slots & 1)
                    TEST_ASSERT_EQ(bfs_sb_write_raw(bio, 0, &future), BFS_OK);
                if (slots & 2)
                    TEST_ASSERT_EQ(bfs_sb_write_raw(bio, backup, &future), BFS_OK);
                counted_bio_t counted = { .base = *bio, .inner = bio };
                counted.base.ops = &counted_ops;
                TEST_ASSERT_EQ(bfs_sb_read(&counted.base, &out), BFS_ERR_UNSUPPORTED);
                TEST_ASSERT_MEM_EQ(&out, &future, sizeof(out));
                bfs_fs_t fs;
                TEST_ASSERT_EQ(bfs_fs_mount(&fs, &counted.base), BFS_ERR_UNSUPPORTED);
                TEST_ASSERT(!fs.mounted);
                TEST_ASSERT_EQ(bfs_sb_write(&counted.base, &current), BFS_ERR_UNSUPPORTED);
                TEST_ASSERT_EQ(bfs_sb_write(&counted.base, &future), BFS_ERR_UNSUPPORTED);
                TEST_ASSERT_EQ(counted.writes, 0);
                TEST_ASSERT_EQ(counted.syncs, 0);
                bfs_bio_close(bio);
                unlink(IMAGE);
            }
        }
    }
}

static void test_damaged_version_or_options_still_recover(void)
{
    for (unsigned option = 0; option < 2; option++) {
        for (unsigned slot = 0; slot < 2; slot++) {
            unlink(IMAGE);
            bfs_bio_t *bio = bio_emu_create(IMAGE, BLOCK_SIZE, BLOCK_COUNT);
            TEST_ASSERT(bio != NULL);
            TEST_ASSERT_EQ(bfs_fs_format(bio, "Recovery", 0), BFS_OK);
            bfs_superblock_t current, damaged, out;
            TEST_ASSERT_EQ(bfs_sb_read(bio, &current), BFS_OK);
            damaged = current;
            if (option) damaged.options = bfs_be32(0x80000000u);
            else damaged.version = bfs_be32(BFS_SB_VERSION + 1);
            TEST_ASSERT_EQ(bfs_sb_validate(&damaged), BFS_ERR_CORRUPT);
            uint64_t offset = slot ? bfs_default_backup_offset(BLOCK_COUNT, BLOCK_SIZE) : 0;
            TEST_ASSERT_EQ(bfs_sb_write_raw(bio, offset, &damaged), BFS_OK);
            TEST_ASSERT_EQ(bfs_sb_read(bio, &out), BFS_OK);
            TEST_ASSERT_MEM_EQ(&out, &current, sizeof(out));
            bfs_fs_t fs;
            TEST_ASSERT_EQ(bfs_fs_mount(&fs, bio), BFS_OK);
            TEST_ASSERT_EQ(bfs_fs_unmount(&fs), BFS_OK);
            bfs_bio_close(bio);
            unlink(IMAGE);
        }
    }
}

/* Sparse synthetic device: exercise address limits without allocating a volume. */
typedef struct {
    bfs_bio_t base;
    bfs_superblock_t copies[2];
    uint64_t backup;
    unsigned reads;
    bfs_err_t read_error;
} sparse_bio_t;

static bfs_err_t sparse_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    sparse_bio_t *sparse = (sparse_bio_t *)bio;
    sparse->reads++;
    if (sparse->read_error != BFS_OK) return sparse->read_error;
    if (bio->block_size < sizeof(bfs_superblock_t)) return BFS_ERR_INVAL;
    memset(buf, 0, bio->block_size);
    uint64_t offset = (uint64_t)blk * bio->block_size;
    /* The caller allocates block_size bytes; the header fits as checked above. */
    if (offset == 0) memcpy(buf, &sparse->copies[0], sizeof(bfs_superblock_t)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (sparse->backup >= offset && sparse->backup - offset <=
        bio->block_size - sizeof(bfs_superblock_t))
        /* The condition above bounds both the offset and the full header. */
        memcpy((uint8_t *)buf + (size_t)(sparse->backup - offset), /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
               &sparse->copies[1], sizeof(bfs_superblock_t));
    return BFS_OK;
}

static const bfs_bio_ops_t sparse_ops = { .read_block = sparse_read };

static void init_sparse(sparse_bio_t *sparse, uint32_t bs, uint32_t count)
{
    memset(sparse, 0, sizeof(*sparse));
    sparse->base.ops = &sparse_ops;
    sparse->base.block_size = 512;
    sparse->backup = bfs_default_backup_offset(count, bs);
    bfs_superblock_t *sb = &sparse->copies[0];
    sb->magic = bfs_be32(BFS_SB_MAGIC);
    sb->version = bfs_be32(BFS_SB_VERSION);
    sb->block_size = bfs_be32(bs);
    sb->block_count = bfs_be32(count);
    sb->txn_id = bfs_be64(1);
    sb->next_ino = bfs_be32(2);
    /* A seven-byte literal, including NUL, fits the 32-byte label. */
    memcpy(sb->volname, "Sparse", 7); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    sb->sb_backup_offset_hi = bfs_be32((uint32_t)(sparse->backup >> 32));
    sb->sb_backup_offset_lo = bfs_be32((uint32_t)sparse->backup);
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    sparse->copies[1] = *sb;
}

static void test_geometry_boundaries(void)
{
    bfs_bio_t bio = {0};
    for (uint32_t bs = BFS_MIN_BLOCK_SIZE; bs <= BFS_MAX_BLOCK_SIZE; bs *= 2) {
        uint64_t limit = (uint64_t)UINT32_MAX * bs;
        TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, limit - bs, bs), BFS_OK);
        TEST_ASSERT_EQ(bio.block_count, UINT32_MAX - 1);
        TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, limit, bs), BFS_OK);
        TEST_ASSERT_EQ(bio.block_count, UINT32_MAX);
        TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, limit + bs, bs), BFS_ERR_OVERFLOW);
        TEST_ASSERT_EQ(bio.block_count, UINT32_MAX);
        TEST_ASSERT_EQ(bio.block_size, bs);
        TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, UINT64_MAX, bs), BFS_ERR_OVERFLOW);
    }
    TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, 0, 4096), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, 4096, 0), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, 4096, 3000), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_bio_set_geometry(NULL, 4096, 4096), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_bio_set_geometry(&bio, 4096 + 512, 4096), BFS_OK);
    TEST_ASSERT_EQ(bio.block_count, 1);
}

static void test_probe_large_geometries(void)
{
    for (uint32_t bs = BFS_MIN_BLOCK_SIZE; bs <= BFS_MAX_BLOCK_SIZE; bs *= 2) {
        sparse_bio_t sparse;
        init_sparse(&sparse, bs, UINT32_MAX);
        bfs_superblock_t out;
        TEST_ASSERT_EQ(bfs_sb_probe(&sparse.base, (uint64_t)UINT32_MAX * bs, &out), BFS_OK);
        TEST_ASSERT_EQ(sparse.base.block_size, bs);
        TEST_ASSERT_EQ(sparse.base.block_count, UINT32_MAX);
    }
    sparse_bio_t sparse;
    init_sparse(&sparse, 4096, 256);
    bfs_superblock_t out;
    TEST_ASSERT_EQ(bfs_sb_probe(&sparse.base, (UINT64_C(1) << 32) * BFS_MAX_BLOCK_SIZE,
                               &out), BFS_ERR_OVERFLOW);
    TEST_ASSERT_EQ(sparse.reads, 0);
    TEST_ASSERT_EQ(sparse.base.block_size, 512);
    TEST_ASSERT_EQ(sparse.base.block_count, 0);
}

static void test_probe_stops_on_incompatible_copy(void)
{
    for (unsigned option = 0; option < 2; option++) {
        for (unsigned slot = 0; slot < 2; slot++) {
            sparse_bio_t sparse;
            init_sparse(&sparse, 4096, 256);
            bfs_superblock_t *future = &sparse.copies[slot];
            if (option) future->options = bfs_be32(0x80000000u);
            else future->version = bfs_be32(BFS_SB_VERSION + 1);
            future->crc32 = bfs_be32(bfs_sb_compute_crc(future));
            bfs_superblock_t out;
            TEST_ASSERT_EQ(bfs_sb_probe(&sparse.base, 4096 * 256, &out), BFS_ERR_UNSUPPORTED);
            TEST_ASSERT_EQ(sparse.reads, slot + 1);
            TEST_ASSERT_EQ(sparse.base.block_size, 512);
            TEST_ASSERT_EQ(sparse.base.block_count, 0);
        }
    }
}

static void test_probe_failure_restores_geometry(void)
{
    sparse_bio_t sparse;
    init_sparse(&sparse, 4096, 256);
    bfs_superblock_t out;
    sparse.read_error = BFS_ERR_IO;
    TEST_ASSERT_EQ(bfs_sb_probe(&sparse.base, 4096 * 256, &out), BFS_ERR_IO);
    TEST_ASSERT_EQ(sparse.read_error, BFS_ERR_IO);
    TEST_ASSERT_EQ(sparse.base.block_size, 512);
    sparse.read_error = BFS_OK;
    memset(sparse.copies, 0, sizeof(sparse.copies));
    TEST_ASSERT_EQ(bfs_sb_probe(&sparse.base, 4096 * 256, &out), BFS_ERR_CORRUPT);
    TEST_ASSERT_EQ(sparse.base.block_size, 512);
    TEST_ASSERT_EQ(bfs_sb_probe(&sparse.base, 0, &out), BFS_ERR_INVAL);
    TEST_ASSERT_EQ(bfs_sb_probe(NULL, 4096, &out), BFS_ERR_INVAL);
}

static void test_version_diagnostics(void)
{
    sparse_bio_t sparse;
    init_sparse(&sparse, 4096, 256);
    bfs_superblock_t *sb = &sparse.copies[0];
    char message[BFS_FORMAT_ERROR_MAX];
    sb->version = bfs_be32(3);
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    bfs_sb_describe_unsupported(sb, message);
    TEST_ASSERT(strstr(message, "version 3 is too new") != NULL);
    TEST_ASSERT(strstr(message, "supports version 2") != NULL);
    sb->version = bfs_be32(1);
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    bfs_sb_describe_unsupported(sb, message);
    TEST_ASSERT(strstr(message, "version 1 is not supported") != NULL);
    sb->version = bfs_be32(UINT32_MAX);
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    bfs_sb_describe_unsupported(sb, message);
    TEST_ASSERT(strstr(message, "version 4294967295 is too new") != NULL);
    sb->version = bfs_be32(BFS_SB_VERSION);
    sb->options = bfs_be32(0x80000000u | BFS_OPT_DATA_ORDERED);
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    bfs_sb_describe_unsupported(sb, message);
    TEST_ASSERT(strstr(message, "version 2 uses unsupported options 0x80000000") != NULL);
    sb->options = 0; /* Now the checksum is invalid: do not claim a newer format. */
    bfs_sb_describe_unsupported(sb, message);
    TEST_ASSERT_EQ(message[0], 0);
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    bfs_sb_describe_unsupported(sb, message);
    TEST_ASSERT_EQ(message[0], 0);
    bfs_sb_describe_unsupported(NULL, message);
    TEST_ASSERT_EQ(message[0], 0);
    bfs_sb_describe_unsupported(sb, NULL);
}

TEST_SUITE_BEGIN("Format Compatibility")
    TEST_RUN(test_incompatible_copies_never_write);
    TEST_RUN(test_damaged_version_or_options_still_recover);
    TEST_RUN(test_geometry_boundaries);
    TEST_RUN(test_probe_large_geometries);
    TEST_RUN(test_probe_stops_on_incompatible_copy);
    TEST_RUN(test_probe_failure_restores_geometry);
    TEST_RUN(test_version_diagnostics);
TEST_SUITE_END()
