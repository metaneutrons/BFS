/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_AMIGA_BIO_H
#define BFS_AMIGA_BIO_H

#include <exec/types.h>
#include <exec/io.h>
#include <dos/filehandler.h>
#include "bfs_bio.h"

typedef struct amiga_bio {
    bfs_bio_t base;
    struct IOExtTD *request;
    struct MsgPort *port;
    uint64_t partition_start_byte;
    uint32_t sector_size;
    uint64_t total_sectors;
    UWORD access_mode;
} amiga_bio_t;

void bfs_amiga_bio_init(amiga_bio_t *ab, struct IOExtTD *request,
                        struct MsgPort *port, struct DosEnvec *env);
void bfs_amiga_bio_set_blocksize(amiga_bio_t *ab, uint32_t fs_block_size);

#endif /* BFS_AMIGA_BIO_H */
