# B+Tree Blocks and Records

All persistent indexing in BFS v2 uses one copy-on-write B+tree block format.
This chapter defines that format and every key/value record. It is independent
of host C structure packing.

## Common node header

Every tree node consumes exactly one physical block. Its header begins at the
block's first byte.

| Offset | Width | Field | Requirement |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | `0x42544E44` (`BTND`) |
| 4 | 4 | `crc32` | CRC over the complete block with bytes 4 through 7 zeroed |
| 8 | 8 | `txn_id` | Transaction identifier written when the node was last emitted; readers retain it for COW reclamation but do not require it to equal the selected superblock ID |
| 16 | 4 | `num_keys` | Number of populated, strictly ordered keys; at least one and no more than the type-specific capacity |
| 20 | 2 | `level` | Zero for a leaf; positive for an internal node; must be below 32 |
| 22 | 2 | `flags` | Reserved and exactly zero |
| 24 | 4 | `right_sibling` | Null or an in-range block number, only permitted on a leaf |

The `right_sibling` field is a legacy leaf hint. The current reader validates
its shape but scans through parent/child links, never by following it. COW can
therefore leave it pointing into an older tree generation. A new reader or
writer MUST NOT make correctness depend on it.

All node CRCs use the CRC32 algorithm from the superblock chapter over all
`block_size` bytes. A writer zeroes a newly created block before populating it;
a reader must instead accept any CRC-valid unused bytes because COW and deletion
can leave historical bytes outside `num_keys`.

## Physical node layout

Let `H = 28`, `B = block_size`, `K = key_size`, and `V = value_size`.

For a leaf:

```text
leaf_capacity = floor((B - H) / (K + V))
keys           = bytes [H, H + leaf_capacity * K)
values         = bytes [H + leaf_capacity * K,
                         H + leaf_capacity * K + leaf_capacity * V)
```

Key `i` starts at `H + i * K`. Its value starts at
`H + leaf_capacity * K + i * V`. The two arrays are not interleaved.

For an internal node:

```text
internal_capacity = floor((B - H - 4) / (K + 4))
keys              = bytes [H, H + internal_capacity * K)
children          = bytes [H + internal_capacity * K,
                            H + internal_capacity * K +
                            (internal_capacity + 1) * 4)
```

There are `num_keys + 1` populated child pointers. For separator key `i`,
child `i` contains keys smaller than that key; child `i + 1` contains keys
greater than or equal to it. Every populated child must be nonzero and in
range. A root's header determines the tree height; each descending level must
decrease by one, and the implementation rejects height 32 or greater.

At the minimum 1024-byte block size, capacities are:

| Tree | K | V | Leaf capacity | Internal capacity |
| --- | ---: | ---: | ---: | ---: |
| Directory | 264 | 8 | 3 | 3 |
| Inode | 4 | 44 | 20 | 124 |
| File extent | 4 | 12 | 62 | 124 |
| Free-space | 4 | 4 | 124 | 124 |
| Refcount | 4 | 4 | 124 | 124 |
| Snapshot | 4 | 52 | 17 | 124 |

The same formula, not these example counts, is authoritative for larger block
sizes. All leaf keys are strictly ordered under that tree's comparator.

## Directory tree

The superblock's `dir_tree_root` names one global tree. Its key is a fixed
264-byte directory key and its value is eight bytes.

| Key offset | Width | Field |
| ---: | ---: | --- |
| 0 | 4 | `parent_id` |
| 4 | 4 | `name_hash` |
| 8 | 1 | `name_len` |
| 9 | 255 | `name` bytes; bytes after `name_len` are zero when written |

`name_hash` is 32-bit FNV-1a over the case-folded name, with offset basis
`0x811C9DC5` and prime `0x01000193`. Folding maps ASCII `a` through `z` to
uppercase and maps bytes `0xE0` through `0xFE`, excluding `0xF7`, down by
`0x20`. All other bytes are unchanged.

The comparator orders `parent_id`, then numeric `name_hash`, then folded name
bytes, then length. It does not order names lexically before hashing. A reader
MUST recompute the hash and reject a mismatch. Case aliases compare equal, so a
directory cannot contain two entries that differ only under this folding.

| Value offset | Width | Field |
| ---: | ---: | --- |
| 0 | 4 | `inode_nr`, nonzero and below `0x80000000` |
| 4 | 4 | `entry_type`, one of the inode type codes below |

## Inode tree

The superblock's `inode_tree_root` names a tree keyed by a big-endian `u32`
inode number. Valid public inode numbers are `1` through `0x7FFFFFFF`; root is
inode 1. The 44-byte leaf value is:

| Offset | Width | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `inode_nr` | Must equal the tree key |
| 4 | 4 | `type` | `0` file, `1` directory, `2` soft link, `3` legacy hard-link type |
| 8 | 4 | `size_hi` | High half of logical byte size |
| 12 | 4 | `size_lo` | Low half of logical byte size |
| 16 | 4 | `extent_root` | Per-file extent-tree root, or zero for no extents |
| 20 | 4 | `link_count` | Nonzero link count |
| 24 | 4 | `protection` | Amiga protection bitmap, carried as an opaque `u32` by the core |
| 28 | 2 | `uid` | Amiga owner UID |
| 30 | 2 | `gid` | Amiga owner GID |
| 32 | 2 | `create_days` | Amiga DateStamp days field |
| 34 | 2 | `create_mins` | Amiga DateStamp minutes field |
| 36 | 2 | `create_ticks` | Amiga DateStamp ticks field |
| 38 | 2 | `modify_days` | Amiga DateStamp days field |
| 40 | 2 | `modify_mins` | Amiga DateStamp minutes field |
| 42 | 2 | `modify_ticks` | Amiga DateStamp ticks field |

The core checks the type, link count, inode identity, and extent-root range. It
does not validate a DateStamp's calendar range or interpret protection and
owner values. A hard link made by the current writer is another directory entry
for a type-0 file and increments `link_count`; it does not emit type 3.

## Per-file extent trees

Each nonempty file or soft-link may have an extent tree rooted by its inode.
Its key and 12-byte value are:

| Part | Offset | Width | Field |
| --- | ---: | ---: | --- |
| Key | 0 | 4 | `file_block`, a file-relative block index |
| Value | 0 | 4 | `disk_block`, first physical block |
| Value | 4 | 4 | `length`, count of contiguous physical and logical blocks |
| Value | 8 | 4 | `data_crc32` |

An entry maps logical blocks `[file_block, file_block + length)` to physical
blocks `[disk_block, disk_block + length)`. `length` is nonzero and the mapped
range must stay within the device, start at or after the data-start block, and
avoid the B-containing block, emergency pool, and active allocator reserve.
The current writer commonly emits one-block extents; a valid reader must handle
longer representable extents.

With `BFS_OPT_DATA_CHECKSUMS`, `data_crc32` is the CRC of the full physical
data block for a single-block checked extent. A stored zero disables validation.
The value is otherwise metadata, not an additional trailer in the data block.
Sparse ranges have no extent entry and read as holes through the file layer.

## Free-space and refcount trees

The free-space root names a tree with a big-endian `u32` start-block key and a
big-endian `u32` length value. It represents nonoverlapping, coalesced free
ranges. `free_blocks` is its committed aggregate accounting. The tree excludes
the bootstrap area, the block containing B, and emergency-pool blocks.

The refcount root names a tree with a big-endian `u32` physical-block key and a
big-endian `u32` count value. Absent keys have implicit reference count one;
present values must be nonzero and describe shared blocks. It is valid only
when a snapshot tree exists. The absence of a refcount root when snapshots are
present is allowed only when every referenced block has implicit count one.

## Snapshot tree

The snapshot root names a tree keyed by a big-endian `u32` snapshot ID. Its
52-byte value is:

| Offset | Width | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `dir_tree_root` | Root captured for the snapshot |
| 4 | 4 | `inode_tree_root` | Inode root captured for the snapshot |
| 8 | 4 | `txn_id_hi` | High half of captured transaction ID |
| 12 | 4 | `txn_id_lo` | Low half of captured transaction ID |
| 16 | 4 | `cursor` | Zero for a normal snapshot; last fully reclaimed inode during deletion |
| 20 | 32 | `name` | NUL-terminated, zero-padded snapshot name |

Snapshot names contain one through 31 bytes and may not contain `/` or `:`.
The current implementation identifies an interrupted deletion by the exact
name prefix `.deleting_`; normal snapshot creation rejects that prefix. During
deletion it writes that name and uses `cursor` as the last fully reclaimed
inode. This field is not a DateStamp. A list operation hides deleting records;
mount resumes their deletion before it reports success.
