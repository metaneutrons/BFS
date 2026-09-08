# Volume and Superblocks

This chapter is normative for the v2 volume envelope. See
[the format entry point](../on-disk-format.md) for terminology and geometry.

## Superblock selection

Each superblock occupies a 512-byte slot. Its 240-byte payload starts at byte
zero of the slot; bytes 240 through 511 are zero on every v2 write. Slot A is
at byte zero. Slot B is at `block_count * block_size / 2`, and that exact
offset is stored in both copies.

To select a committed state, a reader MUST:

1. Read both slots, using A's stored B offset only after A is valid; otherwise
   use the device midpoint.
2. Verify magic and CRC before classifying version or option bits.
3. Treat a CRC-valid, incompatible version or unknown option bit in either
   copy as `unsupported`, and refuse the volume. It MUST NOT fall back to a
   lower transaction v2 copy.
4. For compatible copies, verify their stored geometry and B offset match the
   actual device and its midpoint.
5. Select the valid compatible copy with the greatest transaction ID. Equal
   IDs select A.

If neither copy is valid and compatible, the volume is corrupt. A damaged copy
does not veto recovery from a valid compatible peer. Fresh formatting writes a
completed filesystem to both slots.

## Payload layout

All fields are big-endian. The payload CRC is the last field.

| Offset | Width | Field | v2 meaning and validation |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | `0x42465300` (`BFS\0`) |
| 4 | 4 | `version` | Exactly `2` for a compatible v2 mount |
| 8 | 4 | `block_size` | Power of two in `[1024, 65536]` |
| 12 | 4 | `block_count` | Physical device blocks; nonzero and matching the selected geometry |
| 16 | 8 | `txn_id` | Nonzero monotonically increasing committed-state identifier |
| 24 | 4 | `dir_tree_root` | Root of the global directory tree, or zero only for an empty/uninitialized tree |
| 28 | 4 | `extent_tree_root` | Legacy global extent root. The current v2 writer leaves it zero; file extents are rooted by each inode. Readers validate its range but do not traverse it. Do not assign new semantics. |
| 32 | 4 | `free_tree_root` | Root of the free-space tree |
| 36 | 4 | `inode_tree_root` | Root of the inode tree |
| 40 | 4 | `refcount_tree_root` | Root of the refcount tree; must be zero when `snapshot_tree_root` is zero |
| 44 | 4 | `snapshot_tree_root` | Root of the snapshot tree; zero means no snapshots |
| 48 | 4 | `free_blocks` | Committed free-space accounting value; it must not exceed `block_count` |
| 52 | 4 | `global_reserve` | Committed allocator metadata-reserve target; it must not exceed `block_count` |
| 56 | 4 | `options` | Bit set described below; all other bits are incompatible |
| 60 | 4 | `next_ino` | Next allocation candidate, in `[2, 0x80000000]` |
| 64 | 32 | `volname` | Nonempty NUL-terminated byte string, zero padded; `:` and `/` are forbidden before its terminator |
| 96 | 4 | `sb_backup_offset_hi` | High half of the byte offset of B |
| 100 | 4 | `sb_backup_offset_lo` | Low half of the byte offset of B |
| 104 | 128 | `emergency_pool[32]` | Up to 32 preallocated physical blocks, each unique, nonzero, in range, and outside the B-containing block |
| 232 | 4 | `emergency_count` | Number of leading valid pool entries, in `[0, 32]` |
| 236 | 4 | `crc32` | CRC32 over bytes 0 through 235 |

Every nonzero root and every emergency-pool entry MUST be at or beyond
`ceil(4096 / block_size)`, below `block_count`, and outside the physical block
containing B. The current reader does not require different roots to be
distinct; graph ownership validation belongs to the checker.

## Options

| Bit | Name | v2 behavior |
| ---: | --- | --- |
| 0 | `BFS_OPT_DATA_CHECKSUMS` | File writes record a data CRC in extent values and reads validate nonzero stored CRCs. A stored CRC of zero means no validation, including the rare case where a computed CRC is zero. |
| 1 | `BFS_OPT_SNAPSHOTS` | Recognized legacy option bit. Snapshot presence is determined by `snapshot_tree_root`, not this bit. The current core does not gate snapshot operations on it. |
| 2 | `BFS_OPT_DATA_ORDERED` | The transaction path requests a block-device sync before publishing a metadata superblock. It depends on the backend honoring that barrier. |

An unknown set bit is incompatible even if every other superblock field is
valid. A writer MUST retain the known option word exactly unless it implements
the documented effect of the changed bit.

## CRC32

BFS uses CRC-32/ISO-HDLC: reflected polynomial `0xEDB88320`, reflected input
and output, initial register `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. The exposed
streaming interface starts an independent calculation with initial value zero.
For example, the CRC of ASCII `123456789` is `0xCBF43926`.

Superblock CRC input is precisely the first 236 raw slot bytes. It neither
includes the stored CRC field nor the zero-filled tail of the 512-byte slot.
Node and data-extent CRC domains are specified in the B+tree chapter.
