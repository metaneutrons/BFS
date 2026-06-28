/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — On-disk format definitions
 *
 * All on-disk structures are big-endian (68k native).
 * All multi-byte fields must be accessed through bfs_beXX() on host.
 *
 * Disk layout:
 *   Byte 0:       Primary superblock (512 bytes)
 *   Byte 4096:    First data block (B+tree nodes, inodes, data)
 *   Byte part/2:  Backup superblock (512 bytes, at partition midpoint)
 */

#ifndef BFS_ONDISK_H
#define BFS_ONDISK_H


#include "bfs_types.h"

#ifdef __VBCC__
  /* VBCC m68k: structs are naturally packed (no padding for these field sizes) */
  #define BFS_PACKED_BEGIN
  #define BFS_PACKED_END
  #define BFS_PACKED
#else
  #define BFS_PACKED_BEGIN
  #define BFS_PACKED_END
  #define BFS_PACKED __attribute__((packed))
#endif

/*
 * Superblock — written to blocks 0 and 1 (alternating).
 * The one with the higher valid txn_id is current.
 * Occupies the first BFS_SB_SIZE (512) bytes of the block; rest is zero-padded.
 */
#define BFS_VOLNAME_MAX 32

/* Buffer to hold a volume/snapshot name (≤ BFS_VOLNAME_MAX, which also bounds
 * the snapshot name) as an Amiga BSTR (leading length byte + chars) or a
 * NUL-terminated C string: name_max + length-byte + terminator. The standalone
 * Amiga tools can't include this header and define a matching local constant. */
#define BFS_NAME_BSTR_MAX (BFS_VOLNAME_MAX + 2)

/* Format-time option flags */
#define BFS_OPT_DATA_CHECKSUMS  (1u << 0)  /* per-extent data CRC32 */
#define BFS_OPT_SNAPSHOTS       (1u << 1)  /* snapshot support enabled (future) */
#define BFS_OPT_DATA_ORDERED    (1u << 2)  /* flush data to disk before metadata commit */

/* Superblock version */
#define BFS_SB_VERSION  2

/* Emergency pool size (blocks stored in superblock for COW when free tree is exhausted) */
#define BFS_EMERGENCY_POOL_SIZE 32

BFS_PACKED_BEGIN
typedef struct BFS_PACKED {
    uint32_t magic;              /* BFS_SB_MAGIC ('BFS\0') */
    uint32_t version;            /* on-disk format version (BFS_SB_VERSION) */
    uint32_t block_size;         /* bytes per block (1024..65536, power of 2) */
    uint32_t block_count;        /* total blocks on volume */
    uint64_t txn_id;             /* transaction counter, monotonically increasing */

    /* B+tree root block pointers (0 = tree empty/not yet created) */
    uint32_t dir_tree_root;      /* directory B+tree */
    uint32_t extent_tree_root;   /* extent B+tree */
    uint32_t free_tree_root;     /* free space B+tree */
    uint32_t inode_tree_root;    /* inode B+tree */

    /* Snapshot-ready tree roots (0 = disabled/not yet created) */
    uint32_t refcount_tree_root; /* block refcount B+tree (for snapshots) */
    uint32_t snapshot_tree_root; /* snapshot metadata B+tree */

    /* Free space accounting */
    uint32_t free_blocks;        /* total free blocks */
    uint32_t global_reserve;     /* blocks reserved for metadata ops (not for data) */

    /* Format options */
    uint32_t options;            /* BFS_OPT_* flags */

    /* Next inode number to allocate (persisted across mounts) */
    uint32_t next_ino;

    /* Volume label (null-terminated ISO-8859-1 / Amiga "international", padded
     * with zeros) — case-folded via bfs_intl_toupper, not UTF-8. */
    uint8_t  volname[BFS_VOLNAME_MAX];

    /* Byte offset of backup superblock (typically partition_size / 2) */
    uint32_t sb_backup_offset_hi;  /* high 32 bits of byte offset */
    uint32_t sb_backup_offset_lo;  /* low 32 bits of byte offset */

    /* Emergency pool: pre-allocated blocks for COW when free tree is exhausted.
     * These blocks are NEVER in the free tree — they break the COW recursion. */
    uint32_t emergency_pool[BFS_EMERGENCY_POOL_SIZE];
    uint32_t emergency_count;    /* number of valid entries in emergency_pool */

    /* CRC32 of this superblock (must be LAST field) */
    uint32_t crc32;
} bfs_superblock_t;

BFS_PACKED_END
_Static_assert(sizeof(bfs_superblock_t) <= BFS_SB_SIZE, "superblock must fit in one sector");

/*
 * B+tree node header — first bytes of every B+tree block.
 * Followed by keys/values (leaf) or keys/child-pointers (internal).
 */
#define BFS_BTNODE_LEAF     0
#define BFS_BTNODE_INTERNAL 1

BFS_PACKED_BEGIN
typedef struct BFS_PACKED {
    uint32_t magic;       /* BFS_NODE_MAGIC ('BTND') */
    uint32_t crc32;       /* CRC32 of entire block (with this field zeroed) */
    uint64_t txn_id;      /* transaction that last modified this node */
    uint32_t num_keys;    /* number of keys in this node */
    uint16_t level;       /* 0 = leaf, >0 = internal (height from leaf) */
    uint16_t flags;       /* reserved */
    uint32_t right_sibling; /* next node at same level (leaf only, 0=none) */
    /* Followed by: key/value or key/child data */
} bfs_btnode_hdr_t;

BFS_PACKED_END
_Static_assert(sizeof(bfs_btnode_hdr_t) == 28, "btnode header size");

/*
 * Inode — stored as value in directory B+tree (inline) or in
 * dedicated inode blocks for large metadata.
 */
#define BFS_INODE_FILE     0
#define BFS_INODE_DIR      1
#define BFS_INODE_SOFTLINK 2
#define BFS_INODE_HARDLINK 3

BFS_PACKED_BEGIN
typedef struct BFS_PACKED {
    uint32_t inode_nr;        /* unique inode number */
    uint32_t type;            /* BFS_INODE_* */
    uint32_t size_hi;         /* file size high 32 bits */
    uint32_t size_lo;         /* file size low 32 bits */
    uint32_t extent_root;     /* root block of per-file extent B+tree (0=inline/empty) */
    uint32_t link_count;      /* hard link count */
    uint32_t protection;      /* Amiga protection bits */
    uint16_t uid;
    uint16_t gid;
    /* Timestamps: days since 1978-01-01, minutes, ticks (Amiga DateStamp compat) */
    uint16_t create_days;
    uint16_t create_mins;
    uint16_t create_ticks;
    uint16_t modify_days;
    uint16_t modify_mins;
    uint16_t modify_ticks;
} bfs_inode_t;

BFS_PACKED_END
_Static_assert(sizeof(bfs_inode_t) == 44, "inode size");

/*
 * Directory entry key — used in the directory B+tree.
 * Key ordering: parent_id, then name_hash, then name bytes.
 */
#define BFS_NAME_MAX 255

/* Directory name hash (FNV-1a). These constants define on-disk key ordering —
 * changing them makes existing volumes' directory trees unsearchable. Frozen. */
#define BFS_DIR_HASH_FNV_OFFSET 0x811C9DC5u
#define BFS_DIR_HASH_FNV_PRIME  0x01000193u

BFS_PACKED_BEGIN
typedef struct BFS_PACKED {
    uint32_t parent_id;       /* inode number of parent directory */
    uint32_t name_hash;       /* FNV-1a hash of case-folded name */
    uint8_t  name_len;        /* length of name in bytes */
    uint8_t  name[BFS_NAME_MAX]; /* filename bytes (not null-terminated on disk) */
} bfs_dirkey_t;
BFS_PACKED_END

/*
 * Extent entry — used in per-file extent B+tree.
 * Key is file-relative block offset.
 */
BFS_PACKED_BEGIN
typedef struct BFS_PACKED {
    uint32_t file_block;      /* file-relative block offset (key) */
    uint32_t disk_block;      /* physical block number */
    uint32_t length;          /* number of contiguous blocks */
    uint32_t data_crc32;      /* CRC32 of data (only if OPT_DATA_CHECKSUMS) */
} bfs_extent_t;
BFS_PACKED_END

/*
 * Free space entry — used in free space B+tree.
 * Key is the starting block number.
 */
BFS_PACKED_BEGIN
typedef struct BFS_PACKED {
    uint32_t block;           /* starting block number (key) */
    uint32_t length;          /* number of contiguous free blocks */
} bfs_free_extent_t;

BFS_PACKED_END
_Static_assert(sizeof(bfs_free_extent_t) == 8, "free_extent size");

/* ── On-disk geometry helpers (single source of truth) ─────── */

/* First filesystem block — block 0 sits at byte BFS_DATA_OFFSET. Format (the
 * writer) and fsck (the reader) must agree on this, so both derive it here. */
static inline bfs_blk_t bfs_data_start_block(uint32_t block_size)
{
    return (BFS_DATA_OFFSET + block_size - 1) / block_size;
}

/* Default backup-superblock byte offset (partition midpoint). */
static inline uint64_t bfs_default_backup_offset(bfs_blk_t block_count,
                                                 uint32_t block_size)
{
    return (uint64_t)block_count * block_size / 2;
}


#endif /* BFS_ONDISK_H */
