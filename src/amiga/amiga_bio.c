/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — AmigaOS block I/O backend
 *
 * Implements the bfs_bio_t interface for AmigaOS using trackdisk.device
 * compatible IORequests. Handles standard 32-bit offset commands as well
 * as 64-bit extensions (TD64 and NSD) for large partitions.
 *
 * Sector vs Block mapping:
 *   Amiga partitions use a native "sector size" (usually 512 bytes).
 *   BFS uses a "filesystem block size" (usually 4096 bytes).
 *   The bio layer handles the translation of block indices to byte offsets.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <dos/filehandler.h>
#include <devices/trackdisk.h>
#include <proto/exec.h>
#include "amiga_bio.h"

/* ── Constants for 64-bit extensions ───────────────────────── */

#ifndef IOERR_NOCMD
#define IOERR_NOCMD -3
#endif
#ifndef TD_READ64
#define TD_READ64  24
#define TD_WRITE64 25
#endif
#ifndef NSCMD_DEVICEQUERY
#define NSCMD_DEVICEQUERY 0x4000
#endif
#ifndef NSCMD_TD_READ64
#define NSCMD_TD_READ64  0xC000
#define NSCMD_TD_WRITE64 0xC001
#endif
#ifndef NSDEVTYPE_TRACKDISK
#define NSDEVTYPE_TRACKDISK 5
#endif

struct NSDeviceQueryResult {
    ULONG DevQueryFormat; ULONG SizeAvailable;
    UWORD DeviceType; UWORD DeviceSubType; UWORD *SupportedCommands;
};

/* Access modes detected at startup */
#define ACCESS_STD  1 /* Standard CMD_READ/WRITE (32-bit, max 4GB) */
#define ACCESS_TD64 2 /* TD64 extensions (TD_READ64/WRITE64) */
#define ACCESS_NSD  3 /* New Style Device extensions (NSCMD_TD_READ64/WRITE64) */

/* ── Implementation ────────────────────────────────────────── */

static uint64_t partition_size_bytes(const amiga_bio_t *ab)
{
    return ab->total_sectors * (uint64_t)ab->sector_size;
}

static bfs_err_t amiga_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    amiga_bio_t *ab = (amiga_bio_t *)bio;
    struct IOExtTD *req = ab->request;
    uint64_t byte_off;

    if (blk >= bio->block_count) return BFS_ERR_INVAL;
    if (bio->block_size == 0) return BFS_ERR_INVAL;
    byte_off = ab->partition_start_byte + (uint64_t)blk * bio->block_size;
    uint64_t part_end = ab->partition_start_byte + partition_size_bytes(ab);
    uint64_t io_end = byte_off + bio->block_size;
    if (part_end < ab->partition_start_byte || io_end < byte_off ||
        byte_off < ab->partition_start_byte || io_end > part_end)
        return BFS_ERR_INVAL;
    if (ab->access_mode == ACCESS_STD && (byte_off >> 32) != 0)
        return BFS_ERR_INVAL;

    /* Select command based on detected access mode */
    req->iotd_Req.io_Command = (ab->access_mode == ACCESS_NSD) ? NSCMD_TD_READ64 :
                               (ab->access_mode == ACCESS_TD64) ? TD_READ64 : CMD_READ;
    req->iotd_Req.io_Data = buf;
    req->iotd_Req.io_Length = bio->block_size;
    req->iotd_Req.io_Offset = (ULONG)byte_off;

    /* For 64-bit modes, upper 32 bits go into io_Actual */
    if (ab->access_mode != ACCESS_STD)
        req->iotd_Req.io_Actual = (ULONG)(byte_off >> 32);

    if (DoIO((struct IORequest *)req) || req->iotd_Req.io_Actual != bio->block_size)
        return BFS_ERR_IO;
    return BFS_OK;
}

static bfs_err_t amiga_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    amiga_bio_t *ab = (amiga_bio_t *)bio;
    struct IOExtTD *req = ab->request;
    uint64_t byte_off;

    if (ab->read_only) return BFS_ERR_UNSUPPORTED;
    if (blk >= bio->block_count) return BFS_ERR_INVAL;
    if (bio->block_size == 0) return BFS_ERR_INVAL;
    byte_off = ab->partition_start_byte + (uint64_t)blk * bio->block_size;
    uint64_t part_end = ab->partition_start_byte + partition_size_bytes(ab);
    uint64_t io_end = byte_off + bio->block_size;
    if (part_end < ab->partition_start_byte || io_end < byte_off ||
        byte_off < ab->partition_start_byte || io_end > part_end)
        return BFS_ERR_INVAL;
    if (ab->access_mode == ACCESS_STD && (byte_off >> 32) != 0)
        return BFS_ERR_INVAL;

    req->iotd_Req.io_Command = (ab->access_mode == ACCESS_NSD) ? NSCMD_TD_WRITE64 :
                               (ab->access_mode == ACCESS_TD64) ? TD_WRITE64 : CMD_WRITE;
    req->iotd_Req.io_Data = (APTR)buf;
    req->iotd_Req.io_Length = bio->block_size;
    req->iotd_Req.io_Offset = (ULONG)byte_off;

    if (ab->access_mode != ACCESS_STD)
        req->iotd_Req.io_Actual = (ULONG)(byte_off >> 32);

    if (DoIO((struct IORequest *)req) || req->iotd_Req.io_Actual != bio->block_size)
        return BFS_ERR_IO;
    return BFS_OK;
}

static bfs_err_t amiga_sync(bfs_bio_t *bio)
{
    amiga_bio_t *ab = (amiga_bio_t *)bio;
    struct IOExtTD *req = ab->request;

    if (ab->read_only) return BFS_ERR_UNSUPPORTED;
    /* Flush device buffers and ensure data is physically written */
    req->iotd_Req.io_Command = CMD_UPDATE;
    req->iotd_Req.io_Data = NULL;
    req->iotd_Req.io_Length = 0;
    req->iotd_Req.io_Offset = 0;
    req->iotd_Req.io_Actual = 0;
    if (DoIO((struct IORequest *)req) != 0)
        return BFS_ERR_IO;

    /* Turn off the floppy motor if applicable (standard Amiga behavior) */
    if (ab->removable) {
        req->iotd_Req.io_Command = TD_MOTOR;
        req->iotd_Req.io_Length = 0;
        DoIO((struct IORequest *)req);
    }

    return BFS_OK;
}

static void amiga_close(bfs_bio_t *bio) { (void)bio; }

static const bfs_bio_ops_t amiga_bio_ops = {
    .read_block = amiga_read,
    .write_block = amiga_write,
    .sync = amiga_sync,
    .close = amiga_close,
};

/* ── Hardware detection ────────────────────────────────────── */

/* Probe the device for 64-bit support. Checks NSD first, then TD64. */
static UWORD detect_access_mode(struct IOExtTD *req)
{
    /* 1. Try New Style Device (NSD) query */
    struct NSDeviceQueryResult nsdqr = {0};
    req->iotd_Req.io_Command = NSCMD_DEVICEQUERY;
    req->iotd_Req.io_Data = &nsdqr;
    req->iotd_Req.io_Length = sizeof(nsdqr);
    nsdqr.SizeAvailable = 0;
    nsdqr.DevQueryFormat = 0;

    if (DoIO((struct IORequest *)req) == 0 && nsdqr.SizeAvailable >= 16 &&
        nsdqr.DeviceType == NSDEVTYPE_TRACKDISK) {
        UWORD *cmds = nsdqr.SupportedCommands;
        if (cmds) {
            bool can_read = false, can_write = false;
            for (int i = 0; i < 256 && cmds[i]; i++) {
                if (cmds[i] == NSCMD_TD_READ64) can_read = true;
                if (cmds[i] == NSCMD_TD_WRITE64) can_write = true;
            }
            if (can_read && can_write) return ACCESS_NSD;
        }
    }

    /* 2. Try TD64 command (TD_READ64) with dummy params */
    req->iotd_Req.io_Command = TD_READ64;
    req->iotd_Req.io_Data = NULL;
    req->iotd_Req.io_Length = 0;
    req->iotd_Req.io_Offset = 0;
    req->iotd_Req.io_Actual = 0;
    if (DoIO((struct IORequest *)req) == 0 || req->iotd_Req.io_Error != IOERR_NOCMD) {
        return ACCESS_TD64;
    }

    /* 3. Fallback to standard 32-bit commands */
    return ACCESS_STD;
}

/* ── Public API ────────────────────────────────────────────── */

bfs_err_t bfs_amiga_bio_init(amiga_bio_t *ab, struct IOExtTD *request,
                        struct MsgPort *port, struct DosEnvec *env,
                        bool removable)
{
    if (!ab || !request || !port || !env || env->de_TableSize < DE_NUMBUFFERS ||
        env->de_SizeBlock < 128 || env->de_SizeBlock > BFS_MAX_BLOCK_SIZE / 4u ||
        (env->de_SizeBlock & (env->de_SizeBlock - 1u)) != 0 ||
        !env->de_Surfaces || !env->de_BlocksPerTrack ||
        env->de_HighCyl < env->de_LowCyl)
        return BFS_ERR_INVAL;
    /* Calculate geometry from MountList environment vector */
    uint32_t sector_size = (uint32_t)env->de_SizeBlock << 2;
    uint64_t sectors_per_cyl = (uint64_t)env->de_Surfaces * (uint64_t)env->de_BlocksPerTrack;
    if (sectors_per_cyl > UINT64_MAX / sector_size)
        return BFS_ERR_INVAL;
    uint64_t cylinder_bytes = sectors_per_cyl * sector_size;
    if ((uint64_t)env->de_HighCyl + 1u > UINT64_MAX / cylinder_bytes)
        return BFS_ERR_INVAL;
    uint64_t total_sectors = ((uint64_t)env->de_HighCyl - (uint64_t)env->de_LowCyl + 1) * sectors_per_cyl;
    uint64_t start_sector = (uint64_t)env->de_LowCyl * sectors_per_cyl;

    ab->base.ops = &amiga_bio_ops;
    ab->base.block_size = sector_size; /* initial block size = sector size */
    /* Unrepresentable sector geometry stays inactive until filesystem probing. */
    ab->base.block_count = (total_sectors > UINT32_MAX) ? 0 : (bfs_blk_t)total_sectors;
    ab->request = request;
    ab->port = port;
    ab->partition_start_byte = start_sector * sector_size;
    ab->sector_size = sector_size;
    ab->total_sectors = total_sectors;
    ab->removable = removable;
    ab->read_only = false;

    /* Standard commands are both sufficient and most compatible below 4 GiB. */
    uint64_t partition_end = ab->partition_start_byte + partition_size_bytes(ab);
    if (partition_end >= ab->partition_start_byte && partition_end <= (1ULL << 32))
        ab->access_mode = ACCESS_STD;
    else
        ab->access_mode = detect_access_mode(request);
    if (partition_end > (1ULL << 32) && ab->access_mode == ACCESS_STD)
        return BFS_ERR_INVAL;
    return BFS_OK;
}

void bfs_amiga_bio_set_readonly(amiga_bio_t *ab, bool read_only)
{
    if (ab) ab->read_only = read_only;
}

bfs_err_t bfs_amiga_bio_set_blocksize(amiga_bio_t *ab, uint32_t fs_block_size)
{
    if (!ab) return BFS_ERR_INVAL;
    return bfs_bio_set_geometry(&ab->base, partition_size_bytes(ab), fs_block_size);
}

bfs_err_t bfs_amiga_bio_probe_superblock(amiga_bio_t *ab, bfs_superblock_t *sb)
{
    if (!ab || !sb) return BFS_ERR_INVAL;
    return bfs_sb_probe(&ab->base, partition_size_bytes(ab), sb);
}
