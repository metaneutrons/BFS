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
#include "bfs_bio.h"

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

typedef struct amiga_bio {
    bfs_bio_t base;
    struct IOExtTD *request;
    struct MsgPort *port;
    ULONG partition_start_byte;
    ULONG sector_size;
    ULONG total_sectors;
    UWORD access_mode;
} amiga_bio_t;

/* ── Implementation ────────────────────────────────────────── */

static bfs_err_t amiga_read(bfs_bio_t *bio, bfs_blk_t blk, void *buf)
{
    amiga_bio_t *ab = (amiga_bio_t *)bio;
    struct IOExtTD *req = ab->request;
    unsigned long long byte_off = (unsigned long long)ab->partition_start_byte +
                                  (unsigned long long)blk * bio->block_size;

    /* Select command based on detected access mode */
    req->iotd_Req.io_Command = (ab->access_mode == ACCESS_NSD) ? NSCMD_TD_READ64 :
                               (ab->access_mode == ACCESS_TD64) ? TD_READ64 : CMD_READ;
    req->iotd_Req.io_Data = buf;
    req->iotd_Req.io_Length = bio->block_size;
    req->iotd_Req.io_Offset = (ULONG)byte_off;

    /* For 64-bit modes, upper 32 bits go into io_Actual */
    if (ab->access_mode != ACCESS_STD)
        req->iotd_Req.io_Actual = (ULONG)(byte_off >> 32);

    return DoIO((struct IORequest *)req) ? BFS_ERR_IO : BFS_OK;
}

static bfs_err_t amiga_write(bfs_bio_t *bio, bfs_blk_t blk, const void *buf)
{
    amiga_bio_t *ab = (amiga_bio_t *)bio;
    struct IOExtTD *req = ab->request;
    unsigned long long byte_off = (unsigned long long)ab->partition_start_byte +
                                  (unsigned long long)blk * bio->block_size;

    req->iotd_Req.io_Command = (ab->access_mode == ACCESS_NSD) ? NSCMD_TD_WRITE64 :
                               (ab->access_mode == ACCESS_TD64) ? TD_WRITE64 : CMD_WRITE;
    req->iotd_Req.io_Data = (APTR)buf;
    req->iotd_Req.io_Length = bio->block_size;
    req->iotd_Req.io_Offset = (ULONG)byte_off;

    if (ab->access_mode != ACCESS_STD)
        req->iotd_Req.io_Actual = (ULONG)(byte_off >> 32);

    return DoIO((struct IORequest *)req) ? BFS_ERR_IO : BFS_OK;
}

static bfs_err_t amiga_sync(bfs_bio_t *bio)
{
    amiga_bio_t *ab = (amiga_bio_t *)bio;
    struct IOExtTD *req = ab->request;

    /* Flush device buffers and ensure data is physically written */
    req->iotd_Req.io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)req);

    /* Turn off the floppy motor if applicable (standard Amiga behavior) */
    req->iotd_Req.io_Command = TD_MOTOR;
    req->iotd_Req.io_Length = 0;
    DoIO((struct IORequest *)req);

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
    struct NSDeviceQueryResult nsdqr;
    req->iotd_Req.io_Command = NSCMD_DEVICEQUERY;
    req->iotd_Req.io_Data = &nsdqr;
    req->iotd_Req.io_Length = sizeof(nsdqr);
    nsdqr.SizeAvailable = 0;
    nsdqr.DevQueryFormat = 0;

    if (DoIO((struct IORequest *)req) == 0 && nsdqr.SizeAvailable >= 16 &&
        nsdqr.DeviceType == NSDEVTYPE_TRACKDISK) {
        UWORD *cmds = nsdqr.SupportedCommands;
        if (cmds) {
            for (int i = 0; cmds[i]; i++) {
                if (cmds[i] == NSCMD_TD_READ64) return ACCESS_NSD;
            }
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

void bfs_amiga_bio_init(amiga_bio_t *ab, struct IOExtTD *request,
                        struct MsgPort *port, struct DosEnvec *env)
{
    /* Calculate geometry from MountList environment vector */
    ULONG sector_size = env->de_SizeBlock << 2;
    ULONG sectors_per_cyl = env->de_Surfaces * env->de_BlocksPerTrack;
    ULONG total_sectors = (env->de_HighCyl - env->de_LowCyl + 1) * sectors_per_cyl;
    ULONG start_sector = env->de_LowCyl * sectors_per_cyl;

    ab->base.ops = &amiga_bio_ops;
    ab->base.block_size = sector_size; /* initial block size = sector size */
    ab->base.block_count = total_sectors;
    ab->request = request;
    ab->port = port;
    ab->partition_start_byte = start_sector * sector_size;
    ab->sector_size = sector_size;
    ab->total_sectors = total_sectors;

    /* Detect device capabilities */
    ab->access_mode = detect_access_mode(request);
}

void bfs_amiga_bio_set_blocksize(amiga_bio_t *ab, uint32_t fs_block_size)
{
    ab->base.block_size = fs_block_size;
    ab->base.block_count = (ab->total_sectors * ab->sector_size) / fs_block_size;
}
