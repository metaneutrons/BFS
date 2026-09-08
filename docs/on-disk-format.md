# BFS v2 On-Disk Format

Status: normative for format version 2. This document defines the bytes a
reader or writer must understand. It does not qualify a particular device,
operating system, driver release, or maximum volume size.

All rules here were reconciled against the v0.1.3 source baseline
`431ead6159e5d4217f029ac2b6dd02a51db7d8a2`. Where an implementation has a
known limitation, it is stated as a limitation rather than being promoted to a
format guarantee.

## Reading this specification

All numeric fields are unsigned and big-endian unless a table says otherwise.
`u8`, `u16`, `u32`, and `u64` mean an exact-width unsigned integer. Byte
offsets start at zero. A block number is an absolute physical filesystem block
number; its byte offset is `block_number * block_size`.

`MUST`, `MUST NOT`, `SHOULD`, and `MAY` express format requirements. A reader
MUST reject a malformed committed structure rather than treating it as an
alternate layout. A writer MUST NOT use unused bytes, reserved fields, or a
v2 option bit as a private extension mechanism.

The chapters are deliberately split by concern:

- [Volume and superblocks](on-disk-format/superblock.md)
- [B+tree blocks and records](on-disk-format/btree.md)
- [Namespace and metadata conventions](on-disk-format/namespace.md)
- [Commit, recovery, and checker boundaries](on-disk-format/commit-recovery.md)
- [Hand-reviewed byte fixtures](on-disk-format/fixtures.md)

The layout chapters above are the only normative definition of v2. The
[compatibility contract](format-compatibility.md) defines recognition and
refusal policy; [failure semantics](failure-semantics.md) defines observable
operation and power-loss behavior. Neither substitutes a second layout.

## Global invariants

- The format version is exactly `2`. A CRC-valid superblock with another
  version, or with an unknown option bit, is an intact unsupported format, not
  damaged v2 media.
- The only legal block sizes are powers of two from 1024 through 65536 bytes.
  Formatting requires at least 256 physical blocks. A device size that would
  require more than `UINT32_MAX` physical blocks at the selected block size is
  outside the representable v2 geometry.
- Block number zero is the null pointer sentinel. It is never a B+tree root,
  child, metadata node, or allocation result.
- Metadata and file-data block addresses are 32 bit. At 4096-byte blocks the
  address-space ceiling is `(2^32 - 1) * 4096`, or 16 TiB minus 4 KiB. This is
  a representation limit, not a promise that every platform can qualify that
  size. Larger supported block sizes increase the representable byte range but
  do not change the 32-bit address count.
- File size is 64 bit, stored as two `u32` words. A file is additionally
  limited by its 32-bit file-block index: its maximum byte length is
  `UINT32_MAX * block_size + (block_size - 1)`.
- CRC32 protects accidental damage; it is not an authenticity mechanism.

## Volume map

Let `B` be the selected block size and `N` the stored physical block count.
The device byte length is `N * B`.

| Region | Byte range or rule | Status |
| --- | --- | --- |
| Primary superblock slot A | `[0, 511]` | Required |
| Bootstrap area | `[512, 4095]` | Reserved; no v2 metadata may be allocated here |
| First eligible physical block | `ceil(4096 / B)` | First possible metadata or data block, subject to the reservations below |
| Backup superblock slot B | `[N * B / 2, N * B / 2 + 511]` | Required |
| Physical block containing slot B | `floor((N * B / 2) / B)` | Reserved in its entirety from allocation |
| Emergency-pool blocks | Listed in the committed superblock | Reserved from the free-space tree |

Slot B is 512-byte aligned but, when `N` is odd, need not be aligned to `B`.
The containing physical block remains unavailable in either case. Therefore an
on-disk block address always names the device block itself, not a compacted
post-bootstrap address.

The filesystem has no boot block, journal, allocation bitmap, or fixed inode
table. Every active root is discovered from the selected superblock.

## Compatibility boundary

The 240-byte superblock payload and its 512-byte slot are frozen. The remaining
272 bytes of the slot are written as zero and are not extension space. Existing
v2 padding, node flags, legacy fields, and recognized-but-unused option bits
must be preserved or left at their documented v2 values; none may acquire new
meaning.

A format that needs wider addresses, different records, different checksum
semantics, or new persistent state requires a new format version. It must not
silently reinterpret v2 bytes or make a valid v2 superblock describe a newer
filesystem. A newer driver should retain a valid legacy recognition envelope
only when it can guarantee that an older v2 driver cannot mount stale state as a
usable filesystem after interruption.
