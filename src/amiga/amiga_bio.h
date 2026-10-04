/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_BIO_H
#define BFS_AMIGA_BIO_H

#include <exec/types.h>
#include <exec/io.h>
#include <dos/filehandler.h>
#include "bfs_bio.h"
#include "bfs_superblock.h"

typedef struct amiga_bio {
    bfs_bio_t base;
    struct IOExtTD *request;
    struct MsgPort *port;
    uint64_t partition_start_byte;
    uint32_t sector_size;
    uint64_t total_sectors;
    UWORD access_mode;
    /* Accessed by the write path in amiga_bio.c. */
    // cppcheck-suppress unusedStructMember
    bool read_only;
    /* Read by the change-state check in amiga_bio.c. */
    // cppcheck-suppress unusedStructMember
    bool removable;
    /* DMA limits from the DosEnvec, used by multi-block transfers. A zero
     * mask means the Mountlist gave none: caller memory is then bounced. */
    // cppcheck-suppress unusedStructMember
    ULONG max_transfer;
    // cppcheck-suppress unusedStructMember
    ULONG mask;
    // cppcheck-suppress unusedStructMember
    ULONG buf_mem_type;
    /* One block of device-suitable memory for bounced transfers. */
    // cppcheck-suppress unusedStructMember
    UBYTE *bounce;
    // cppcheck-suppress unusedStructMember
    ULONG bounce_size;
} amiga_bio_t;

bfs_err_t bfs_amiga_bio_init(amiga_bio_t *ab, struct IOExtTD *request,
                        struct MsgPort *port, struct DosEnvec *env,
                        bool removable);
bfs_err_t bfs_amiga_bio_set_blocksize(amiga_bio_t *ab, uint32_t fs_block_size);
bfs_err_t bfs_amiga_bio_probe_superblock(amiga_bio_t *ab, bfs_superblock_t *sb);

/* True when ACTION_FORMAT may replace an unsupported medium: it holds only
 * BFS formats older than this driver's. */
bool bfs_amiga_bio_format_replaceable(amiga_bio_t *ab);
void bfs_amiga_bio_set_readonly(amiga_bio_t *ab, bool read_only);
/* Release the bounce buffer; the backend stays usable. */
void bfs_amiga_bio_release(amiga_bio_t *ab);

#endif /* BFS_AMIGA_BIO_H */
