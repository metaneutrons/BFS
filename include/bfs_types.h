/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Portable type definitions
 *
 * On host (macOS/Linux): uses standard C99 stdint.h
 * On Amiga (m68k-amigaos-gcc): maps to exec/types.h where needed
 */

#ifndef BFS_TYPES_H
#define BFS_TYPES_H

#ifdef __VBCC__
  #define _Static_assert(cond, msg)
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* Block number — 32-bit, max 16TB with 4K blocks */
typedef uint32_t bfs_blk_t;

/* Inode number */
typedef uint32_t bfs_ino_t;

/* Transaction ID */
typedef uint64_t bfs_txn_id_t;

/* File size — 64-bit for future-proofing, stored as two uint32_t on disk */
typedef uint64_t bfs_fsize_t;

/* Return codes */
typedef enum {
    BFS_OK = 0,
    BFS_ERR_IO = -1,
    BFS_ERR_NOMEM = -2,
    BFS_ERR_CORRUPT = -3,
    BFS_ERR_NOSPC = -4,
    BFS_ERR_NOTFOUND = -5,
    BFS_ERR_EXISTS = -6,
    BFS_ERR_NOTEMPTY = -7,
    BFS_ERR_INVAL = -8,
    BFS_ERR_AGAIN = -9,   /* operation incomplete, caller should retry */
} bfs_err_t;

/* On-disk structures use big-endian (68k native byte order).
 * On host we need conversion helpers.
 */
#if defined(__amigaos__) || defined(__VBCC__) || defined(BFS_AMIGA)
  /* 68k is big-endian — no conversion needed */
  #define BFS_CPU_BE 1
#elif defined(__APPLE__)
  #include <machine/endian.h>
  #if BYTE_ORDER == BIG_ENDIAN
    #define BFS_CPU_BE 1
  #else
    #define BFS_CPU_BE 0
  #endif
#else
  #include <endian.h>
  #if __BYTE_ORDER == __BIG_ENDIAN
    #define BFS_CPU_BE 1
  #else
    #define BFS_CPU_BE 0
  #endif
#endif

#if BFS_CPU_BE
  static inline uint16_t bfs_be16(uint16_t v) { return v; }
  static inline uint32_t bfs_be32(uint32_t v) { return v; }
  static inline uint64_t bfs_be64(uint64_t v) { return v; }
#else
  static inline uint16_t bfs_be16(uint16_t v) {
      return (v >> 8) | (v << 8);
  }
  static inline uint32_t bfs_be32(uint32_t v) {
      return ((v >> 24) & 0xff) | ((v >> 8) & 0xff00) |
             ((v << 8) & 0xff0000) | ((v << 24) & 0xff000000);
  }
  static inline uint64_t bfs_be64(uint64_t v) {
      return ((uint64_t)bfs_be32((uint32_t)v) << 32) |
             bfs_be32((uint32_t)(v >> 32));
  }
#endif

static inline uint16_t bfs_load_be16(const void *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return bfs_be16(v);
}

static inline uint32_t bfs_load_be32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return bfs_be32(v);
}

static inline uint64_t bfs_load_be64(const void *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return bfs_be64(v);
}

static inline void bfs_store_be16(void *p, uint16_t v)
{
    v = bfs_be16(v);
    memcpy(p, &v, sizeof(v));
}

static inline void bfs_store_be32(void *p, uint32_t v)
{
    v = bfs_be32(v);
    memcpy(p, &v, sizeof(v));
}

static inline void bfs_store_be64(void *p, uint64_t v)
{
    v = bfs_be64(v);
    memcpy(p, &v, sizeof(v));
}

/* Min block size 1024 (264-byte dir keys need at least 3 entries per node) */
#define BFS_MIN_BLOCK_SIZE  1024
#define BFS_MAX_BLOCK_SIZE  65536

/* Minimum volume size in blocks (metadata overhead: superblocks, trees, reserve pool) */
#define BFS_MIN_VOLUME_BLOCKS 256

/* Superblock locations (fixed byte offsets, independent of block size) */
#define BFS_SB_OFFSET_A   0          /* Byte 0: Primary superblock */
#define BFS_SB_SIZE       512        /* Superblock occupies 512 bytes on disk */
#define BFS_DATA_OFFSET   4096       /* Byte 4096: First data block (block 0) */
/* Superblock B is at partition_size / 2 (set at format time, stored in SB) */

/* Magic numbers */
#define BFS_SB_MAGIC     0x42465300  /* 'BFS' */
#define BFS_NODE_MAGIC   0x42544E44  /* 'BTND' */

/* Null block pointer */
#define BFS_BLK_NULL     0

#endif /* BFS_TYPES_H */
