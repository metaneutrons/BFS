/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Superblock read/write/validate
 *
 * Superblock layout on disk:
 *   Byte 0:              Superblock A (512 bytes, zero-padded)
 *   Byte 4096:           First eligible region; physical block is
 *                        ceil(4096 / block_size)
 *   Byte partition/2:    Superblock B (512 bytes, for disaster recovery)
 *
 * On commit: write to the older of A/B (alternating).
 * On mount: read both, pick highest valid txn_id.
 */

#include "bfs_superblock.h"
#include "bfs_crc32.h"
#include <string.h>
#include <stdlib.h>

static uint64_t get_backup_offset(const bfs_superblock_t *sb);

/* Raw byte-offset I/O for superblock access (before block_size is known) */
static bfs_err_t bio_read_raw(bfs_bio_t *bio, uint64_t byte_offset, void *buf, uint32_t len)
{
    if (!bio || !bio->ops || !bio->ops->read_block || !buf || len == 0 ||
        !bfs_block_size_valid(bio->block_size))
        return BFS_ERR_INVAL;
    uint64_t device_bytes = (uint64_t)bio->block_count * bio->block_size;
    if (byte_offset > device_bytes || len > device_bytes - byte_offset)
        return BFS_ERR_INVAL;
    uint32_t blk = (uint32_t)(byte_offset / bio->block_size);
    uint32_t off = (uint32_t)(byte_offset % bio->block_size);
    if (len > bio->block_size - off) return BFS_ERR_INVAL;
    uint8_t *tmp = malloc(bio->block_size);
    if (!tmp) return BFS_ERR_NOMEM;
    bfs_err_t err = bfs_bio_read(bio, blk, tmp);
    if (err == BFS_OK) memcpy(buf, tmp + off, len);
    free(tmp);
    return err;
}

static bfs_err_t bio_write_raw(bfs_bio_t *bio, uint64_t byte_offset, const void *buf, uint32_t len)
{
    if (!bio || !bio->ops || !bio->ops->read_block || !bio->ops->write_block ||
        !buf || len == 0 || !bfs_block_size_valid(bio->block_size))
        return BFS_ERR_INVAL;
    uint64_t device_bytes = (uint64_t)bio->block_count * bio->block_size;
    if (byte_offset > device_bytes || len > device_bytes - byte_offset)
        return BFS_ERR_INVAL;
    uint32_t blk = (uint32_t)(byte_offset / bio->block_size);
    uint32_t off = (uint32_t)(byte_offset % bio->block_size);
    if (len > bio->block_size - off) return BFS_ERR_INVAL;
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
    if (!sb) return 0;
    size_t crc_offset = offsetof(bfs_superblock_t, crc32);
    return bfs_crc32(0, sb, crc_offset);
}

static bool sb_root_valid(bfs_blk_t root, uint32_t block_size,
                          bfs_blk_t block_count, bfs_blk_t backup_block)
{
    if (root == BFS_BLK_NULL) return true;
    return root >= bfs_data_start_block(block_size) && root < block_count &&
           root != backup_block;
}

bfs_err_t bfs_sb_validate(const bfs_superblock_t *sb)
{
    if (!sb) return BFS_ERR_INVAL;
    if (bfs_be32(sb->magic) != BFS_SB_MAGIC)
        return BFS_ERR_CORRUPT;
    if (bfs_be32(sb->crc32) != bfs_sb_compute_crc(sb))
        return BFS_ERR_CORRUPT;

    /* Classify incompatibility only after verifying the frozen v2 envelope.
     * A bit flip in version/options must still permit ordinary recovery. */
    const uint32_t known_options = BFS_OPT_DATA_CHECKSUMS | BFS_OPT_SNAPSHOTS |
                                   BFS_OPT_DATA_ORDERED;
    if (bfs_be32(sb->version) != BFS_SB_VERSION ||
        (bfs_be32(sb->options) & ~known_options) != 0)
        return BFS_ERR_UNSUPPORTED;

    uint32_t bs = bfs_be32(sb->block_size);
    if (!bfs_block_size_valid(bs))
        return BFS_ERR_CORRUPT;

    bfs_blk_t block_count = bfs_be32(sb->block_count);
    uint64_t device_bytes = (uint64_t)block_count * bs;
    uint64_t backup_offset = get_backup_offset(sb);
    if (block_count == 0 || backup_offset > device_bytes ||
        BFS_SB_SIZE > device_bytes - backup_offset ||
        (backup_offset % BFS_SB_SIZE) != 0)
        return BFS_ERR_CORRUPT;
    bfs_blk_t backup_block = (bfs_blk_t)(backup_offset / bs);

    if (bfs_be64(sb->txn_id) == 0 ||
        bfs_be32(sb->free_blocks) > block_count ||
        bfs_be32(sb->global_reserve) > block_count ||
        bfs_be32(sb->next_ino) <= 1u ||
        bfs_be32(sb->next_ino) > 0x80000000u)
        return BFS_ERR_CORRUPT;

    bool terminated = false;
    for (uint32_t i = 0; i < BFS_VOLNAME_MAX; i++) {
        uint8_t c = sb->volname[i];
        if (c == 0) {
            terminated = true;
            break;
        }
        if (c == ':' || c == '/') return BFS_ERR_CORRUPT;
    }
    if (!terminated || sb->volname[0] == 0)
        return BFS_ERR_CORRUPT;

    const uint32_t roots[] = {
        sb->dir_tree_root, sb->extent_tree_root, sb->free_tree_root,
        sb->inode_tree_root, sb->refcount_tree_root, sb->snapshot_tree_root,
    };
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if (!sb_root_valid(bfs_be32(roots[i]), bs, block_count, backup_block))
            return BFS_ERR_CORRUPT;
    }
    if (bfs_be32(sb->snapshot_tree_root) == BFS_BLK_NULL &&
        bfs_be32(sb->refcount_tree_root) != BFS_BLK_NULL)
        return BFS_ERR_CORRUPT;

    uint32_t emergency_count = bfs_be32(sb->emergency_count);
    if (emergency_count > BFS_EMERGENCY_POOL_SIZE)
        return BFS_ERR_CORRUPT;
    for (uint32_t i = 0; i < emergency_count; i++) {
        bfs_blk_t blk = bfs_be32(sb->emergency_pool[i]);
        if (!sb_root_valid(blk, bs, block_count, backup_block) ||
            blk == BFS_BLK_NULL)
            return BFS_ERR_CORRUPT;
        for (uint32_t j = 0; j < i; j++) {
            if (bfs_be32(sb->emergency_pool[j]) == blk)
                return BFS_ERR_CORRUPT;
        }
    }

    return BFS_OK;
}

/* The freestanding Amiga handler has no hosted stdio runtime. */
static void message_text(char **cursor, const char *end, const char *text)
{
    while (*text && *cursor < end) *(*cursor)++ = *text++;
    **cursor = 0;
}

static void message_number(char **cursor, const char *end, uint32_t number,
                            bool hex)
{
    char digits[11];
    char *p = digits + sizeof(digits) - 1;
    *p = 0;
    unsigned base = hex ? 16 : 10;
    do {
        *--p = "0123456789abcdef"[number % base];
        number /= base;
    } while (number);
    if (hex) while (digits + sizeof(digits) - 1 - p < 8) *--p = '0';
    message_text(cursor, end, p);
}

void bfs_sb_describe_unsupported(const bfs_superblock_t *sb,
                                 char message[BFS_FORMAT_ERROR_MAX])
{
    if (!message) return;
    message[0] = 0;
    if (bfs_sb_validate(sb) != BFS_ERR_UNSUPPORTED) return;
    uint32_t version = bfs_be32(sb->version);
    char *cursor = message;
    const char *end = message + BFS_FORMAT_ERROR_MAX - 1;
    message_text(&cursor, end, "BFS format version ");
    message_number(&cursor, end, version, false);
    if (version != BFS_SB_VERSION) {
        message_text(&cursor, end, version > BFS_SB_VERSION ?
                     " is too new.\n" : " is not supported.\n");
        message_text(&cursor, end, "This driver supports version ");
        message_number(&cursor, end, BFS_SB_VERSION, false);
        message_text(&cursor, end, version > BFS_SB_VERSION ?
                     ".\nUse a newer BFS driver." : ".\nUse a compatible BFS driver.");
    } else {
        message_text(&cursor, end, " uses unsupported options 0x");
        message_number(&cursor, end, bfs_be32(sb->options) &
                       ~(BFS_OPT_DATA_CHECKSUMS | BFS_OPT_SNAPSHOTS | BFS_OPT_DATA_ORDERED), true);
        message_text(&cursor, end, ".\nUse a compatible BFS driver.");
    }
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

static bool sb_matches_device(const bfs_superblock_t *sb, const bfs_bio_t *bio)
{
    return bfs_be32(sb->block_size) == bio->block_size &&
           bfs_be32(sb->block_count) == bio->block_count &&
           get_backup_offset(sb) ==
               bfs_default_backup_offset(bio->block_count, bio->block_size);
}

bfs_err_t bfs_sb_read(bfs_bio_t *bio, bfs_superblock_t *sb_out)
{
    if (!bio || !bio->ops || !bio->ops->read_block || !sb_out ||
        !bfs_block_size_valid(bio->block_size) || bio->block_count == 0)
        return BFS_ERR_INVAL;
    bfs_superblock_t sb_a, sb_b;

    /* Always read A from byte 0 */
    bfs_err_t e_a = read_sb_at(bio, BFS_SB_OFFSET_A, &sb_a);
    bfs_err_t check_a = e_a == BFS_OK ? bfs_sb_validate(&sb_a) : e_a;
    if (check_a == BFS_ERR_UNSUPPORTED) { *sb_out = sb_a; return check_a; }
    int v_a = (check_a == BFS_OK &&
               sb_matches_device(&sb_a, bio));

    /* Read B from the offset stored in A, or try partition midpoint as fallback */
    int v_b = 0;
    bfs_err_t e_b = BFS_OK;
    uint64_t b_off = 0;
    if (v_a) {
        b_off = get_backup_offset(&sb_a);
    }
    if (b_off == 0) {
        /* Fallback: try partition midpoint */
        b_off = bfs_default_backup_offset(bio->block_count, bio->block_size);
    }
    if (b_off > 0) {
        e_b = read_sb_at(bio, b_off, &sb_b);
        bfs_err_t check_b = e_b == BFS_OK ? bfs_sb_validate(&sb_b) : e_b;
        if (check_b == BFS_ERR_UNSUPPORTED) { *sb_out = sb_b; return check_b; }
        v_b = (check_b == BFS_OK &&
               sb_matches_device(&sb_b, bio));
    }

    if (v_a && v_b) {
        *sb_out = (bfs_be64(sb_a.txn_id) >= bfs_be64(sb_b.txn_id)) ? sb_a : sb_b;
    } else if (v_a) {
        *sb_out = sb_a;
    } else if (v_b) {
        *sb_out = sb_b;
    } else {
        if (e_a != BFS_OK) return e_a;
        if (e_b != BFS_OK) return e_b;
        return BFS_ERR_CORRUPT;
    }
    return BFS_OK;
}

bfs_err_t bfs_sb_probe(bfs_bio_t *bio, uint64_t device_bytes,
                       bfs_superblock_t *sb_out)
{
    if (!bio || !bio->ops || !bio->ops->read_block || !sb_out)
        return BFS_ERR_INVAL;
    uint32_t saved_size = bio->block_size;
    bfs_blk_t saved_count = bio->block_count;
    bfs_err_t result = BFS_ERR_CORRUPT;
    bool tried = false;
    for (uint32_t bs = BFS_MIN_BLOCK_SIZE; bs <= BFS_MAX_BLOCK_SIZE; bs *= 2u) {
        bfs_err_t err = bfs_bio_set_geometry(bio, device_bytes, bs);
        if (err != BFS_OK) {
            if (!tried) result = err;
            continue;
        }
        if (!tried) result = BFS_ERR_CORRUPT;
        tried = true;
        err = bfs_sb_read(bio, sb_out);
        if (err == BFS_OK) return BFS_OK;
        if (err == BFS_ERR_UNSUPPORTED) {
            result = err;
            break;
        }
        if (err == BFS_ERR_IO || err == BFS_ERR_NOMEM) result = err;
    }
    bio->block_size = saved_size;
    bio->block_count = saved_count;
    return result;
}

bfs_err_t bfs_sb_write(bfs_bio_t *bio, bfs_superblock_t *sb)
{
    if (!bio || !bio->ops || !bio->ops->read_block || !bio->ops->write_block ||
        !bio->ops->sync || !sb || !bfs_block_size_valid(bio->block_size) ||
        !sb_matches_device(sb, bio))
        return BFS_ERR_INVAL;
    /* Compute CRC */
    sb->crc32 = bfs_be32(bfs_sb_compute_crc(sb));
    bfs_err_t check = bfs_sb_validate(sb);
    if (check != BFS_OK) return check;

    uint64_t backup_off = get_backup_offset(sb);

    /* Read both to determine which is older */
    bfs_superblock_t sb_a, sb_b;
    bfs_err_t e_a = read_sb_at(bio, BFS_SB_OFFSET_A, &sb_a);
    if (e_a != BFS_OK) return e_a;
    bfs_err_t check_a = bfs_sb_validate(&sb_a);
    if (check_a == BFS_ERR_UNSUPPORTED) return check_a;
    int v_a = (check_a == BFS_OK &&
               sb_matches_device(&sb_a, bio));

    int v_b = 0;
    if (backup_off > 0) {
        bfs_err_t e_b = read_sb_at(bio, backup_off, &sb_b);
        if (e_b != BFS_OK) return e_b;
        bfs_err_t check_b = bfs_sb_validate(&sb_b);
        if (check_b == BFS_ERR_UNSUPPORTED) return check_b;
        v_b = (check_b == BFS_OK &&
               sb_matches_device(&sb_b, bio));
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
    if (!sb) return BFS_ERR_INVAL;
    uint8_t buf[BFS_SB_SIZE];
    memset(buf, 0, BFS_SB_SIZE);
    memcpy(buf, sb, sizeof(*sb));
    return bio_write_raw(bio, byte_offset, buf, BFS_SB_SIZE);
}
