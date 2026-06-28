/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Superblock read/write/validate
 *
 * Superblock layout on disk:
 *   Byte 0:              Superblock A (512 bytes, zero-padded)
 *   Byte 4096:           First data block (block 0 of the filesystem)
 *   Byte partition/2:    Superblock B (512 bytes, for disaster recovery)
 *
 * On commit: write to the older of A/B (alternating).
 * On mount: read both, pick highest valid txn_id.
 */

#include "bfs_superblock.h"
#include "bfs_crc32.h"
#include <string.h>
#include <stdlib.h>

/* Raw byte-offset I/O for superblock access (before block_size is known) */
static bfs_err_t bio_read_raw(bfs_bio_t *bio, uint64_t byte_offset, void *buf, uint32_t len)
{
    uint32_t blk = (uint32_t)(byte_offset / bio->block_size);
    uint32_t off = (uint32_t)(byte_offset % bio->block_size);
    uint8_t *tmp = malloc(bio->block_size);
    if (!tmp) return BFS_ERR_NOMEM;
    bfs_err_t err = bfs_bio_read(bio, blk, tmp);
    if (err == BFS_OK) memcpy(buf, tmp + off, len);
    free(tmp);
    return err;
}

static bfs_err_t bio_write_raw(bfs_bio_t *bio, uint64_t byte_offset, const void *buf, uint32_t len)
{
    uint32_t blk = (uint32_t)(byte_offset / bio->block_size);
    uint32_t off = (uint32_t)(byte_offset % bio->block_size);
    uint8_t *tmp = malloc(bio->block_size);
    if (!tmp) return BFS_ERR_NOMEM;
    /* Read-modify-write */
    bfs_err_t err;
    if (off == 0 && len >= bio->block_size) {
        /* Full block write: skip read */
        memcpy(tmp, buf, bio->block_size);
        err = bfs_bio_write(bio, blk, tmp);
    } else {
        err = bfs_bio_read(bio, blk, tmp);
        if (err == BFS_OK) {
            memcpy(tmp + off, buf, len);
            err = bfs_bio_write(bio, blk, tmp);
        }
    }
    free(tmp);
    return err;
}

uint32_t bfs_sb_compute_crc(const bfs_superblock_t *sb)
{
    size_t crc_offset = offsetof(bfs_superblock_t, crc32);
    return bfs_crc32(0, sb, crc_offset);
}

bfs_err_t bfs_sb_validate(const bfs_superblock_t *sb)
{
    if (bfs_be32(sb->magic) != BFS_SB_MAGIC)
        return BFS_ERR_CORRUPT;
    if (bfs_be32(sb->version) != BFS_SB_VERSION)
        return BFS_ERR_CORRUPT;

    uint32_t bs = bfs_be32(sb->block_size);
    if (bs < BFS_MIN_BLOCK_SIZE || bs > BFS_MAX_BLOCK_SIZE || (bs & (bs - 1)))
        return BFS_ERR_CORRUPT;

    if (bfs_be32(sb->crc32) != bfs_sb_compute_crc(sb))
        return BFS_ERR_CORRUPT;

    return BFS_OK;
}

/* Read superblock from a byte offset */
static bfs_err_t read_sb_at(bfs_bio_t *bio, uint64_t byte_offset, bfs_superblock_t *sb)
{
    uint8_t buf[BFS_SB_SIZE];
    memset(buf, 0, sizeof(buf));
    bfs_err_t err = bio_read_raw(bio, byte_offset, buf, BFS_SB_SIZE);
    if (err != BFS_OK) return err;
    memcpy(sb, buf, sizeof(*sb));
    return BFS_OK;
}

/* Get backup superblock offset from a superblock */
static uint64_t get_backup_offset(const bfs_superblock_t *sb)
{
    return ((uint64_t)bfs_be32(sb->sb_backup_offset_hi) << 32) |
           bfs_be32(sb->sb_backup_offset_lo);
}

bfs_err_t bfs_sb_read(bfs_bio_t *bio, bfs_superblock_t *sb_out)
{
    bfs_superblock_t sb_a, sb_b;

    /* Always read A from byte 0 */
    bfs_err_t e_a = read_sb_at(bio, BFS_SB_OFFSET_A, &sb_a);
    int v_a = (e_a == BFS_OK && bfs_sb_validate(&sb_a) == BFS_OK);

    /* Read B from the offset stored in A, or try partition midpoint as fallback */
    int v_b = 0;
    uint64_t b_off = 0;
    if (v_a) {
        b_off = get_backup_offset(&sb_a);
    }
    if (b_off == 0) {
        /* Fallback: try partition midpoint */
        b_off = bfs_default_backup_offset(bio->block_count, bio->block_size);
    }
    if (b_off > 0) {
        bfs_err_t e_b = read_sb_at(bio, b_off, &sb_b);
        v_b = (e_b == BFS_OK && bfs_sb_validate(&sb_b) == BFS_OK);
    }

    if (v_a && v_b) {
        *sb_out = (bfs_be64(sb_a.txn_id) >= bfs_be64(sb_b.txn_id)) ? sb_a : sb_b;
    } else if (v_a) {
        *sb_out = sb_a;
    } else if (v_b) {
        *sb_out = sb_b;
    } else {
        return BFS_ERR_CORRUPT;
    }
    return BFS_OK;
}

bfs_err_t bfs_sb_write(bfs_bio_t *bio, bfs_superblock_t *sb)
{
    /* Compute CRC */
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));

    uint64_t backup_off = get_backup_offset(sb);

    /* Read both to determine which is older */
    bfs_superblock_t sb_a, sb_b;
    bfs_err_t e_a = read_sb_at(bio, BFS_SB_OFFSET_A, &sb_a);
    int v_a = (e_a == BFS_OK && bfs_sb_validate(&sb_a) == BFS_OK);

    int v_b = 0;
    if (backup_off > 0) {
        bfs_err_t e_b = read_sb_at(bio, backup_off, &sb_b);
        v_b = (e_b == BFS_OK && bfs_sb_validate(&sb_b) == BFS_OK);
    }

    /* Write to the older slot */
    uint64_t target;
    if (v_a && v_b)
        target = (bfs_be64(sb_a.txn_id) <= bfs_be64(sb_b.txn_id))
                 ? BFS_SB_OFFSET_A : backup_off;
    else if (v_b)
        target = BFS_SB_OFFSET_A;
    else
        target = backup_off > 0 ? backup_off : BFS_SB_OFFSET_A;

    /* Write 512-byte superblock */
    uint8_t buf[BFS_SB_SIZE];
    memset(buf, 0, BFS_SB_SIZE);
    memcpy(buf, sb, sizeof(*sb));

    bfs_err_t err = bio_write_raw(bio, target, buf, BFS_SB_SIZE);
    if (err != BFS_OK) return err;

    return bfs_bio_sync(bio);
}

bfs_err_t bfs_sb_write_raw(bfs_bio_t *bio, uint64_t byte_offset, const bfs_superblock_t *sb)
{
    uint8_t buf[BFS_SB_SIZE];
    memset(buf, 0, BFS_SB_SIZE);
    memcpy(buf, sb, sizeof(*sb));
    return bio_write_raw(bio, byte_offset, buf, BFS_SB_SIZE);
}