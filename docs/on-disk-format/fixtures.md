# Hand-Reviewed Byte Fixtures

These fixtures make the layout concrete without relying on C packing or the
production endian helpers. The independent host test
`tests/test_format_fixtures.c` validates the same raw byte vectors with local
big-endian, FNV-1a, and CRC32 routines. It does not include any `bfs_*.h`
header or call a production codec.

`00 x N` means exactly N zero bytes and is used only to keep fixed-size records
readable. The test source uses explicit array sizes and designated initializers
for those zero regions.

## CRC vectors

| Input | Expected CRC32 |
| --- | ---: |
| Empty byte string | `00000000` |
| ASCII `123456789` | `CBF43926` |
| Four `FF` bytes | `FFFFFFFF` |
| v2 test superblock bytes 0..235 | `10EB3CFC` |
| 1024-byte free-tree leaf below, CRC field zeroed | `748BE97E` |
| 1024-byte free-tree internal node below, CRC field zeroed | `C18D6088` |

## Record fixtures

All groups are big-endian hexadecimal bytes.

| Record | Bytes |
| --- | --- |
| Inode, 44 bytes | `00000002 00000000 00000001 23456789 00000010 00000002 0000000F 0123 0456 000A 0014 001E 0028 0032 003C` |
| Directory key, 264 bytes | `00000001 32543B0B 05 48656C6C6F 00 x 250` |
| Directory value, 8 bytes | `00000002 00000000` |
| Extent key/value, 16 bytes | `00000003 00000020 00000002 CAFEBABE` |
| Free-space key/value, 8 bytes | `00000020 00000002` |
| Refcount key/value, 8 bytes | `00000020 00000003` |
| Snapshot key/value, 56 bytes | `00000007 00000010 00000020 00000001 00000002 00000000 736E6170 00 x 28` |

The directory fixture is parent 1 and original spelling `Hello`. Its folded
byte sequence is `HELLO`, whose required FNV-1a hash is `32543B0B`.

The snapshot table's final row is a four-byte key followed by its 52-byte
value: directory root `0x10`, inode root `0x20`, captured transaction ID
`0x0000000100000002`, zero deletion cursor, and name `snap`.

## 1024-byte node fixtures

The following nonzero regions define complete 1024-byte blocks; every omitted
byte is zero. They use the four-byte key/four-byte value free-space tree so the
leaf and internal value/child-array offsets are visible.

| Node | Nonzero ranges |
| --- | --- |
| Leaf | `00:42544E44`, `04:748BE97E`, `08:0000000000000002`, `16:00000001`, `28:00000020`, `524:00000010` |
| Internal | `00:42544E44`, `04:C18D6088`, `08:0000000000000002`, `16:00000001`, `20:0001`, `28:00000020`, `524:00000010`, `528:00000030` |

The leaf has one key `0x20` and one value `0x10`; its value starts at
`28 + 124 * 4 = 524`. The internal node has one separator `0x20`, level one,
and children `0x10` and `0x30`, starting at the same fixed offset. These
vectors detect an erroneous interleaved key/value implementation.

## Superblock fixture

The 512-byte v2 slot has these nonzero ranges; all other bytes are zero:

| Offset | Bytes |
| ---: | --- |
| 0 | `42465300` |
| 4 | `00000002` |
| 8 | `00001000` |
| 12 | `00000040` |
| 16 | `0000000000000001` |
| 48 | `0000003E` |
| 60 | `00000002` |
| 64 | `54657374566F6C` |
| 100 | `00020000` |
| 236 | `10EB3CFC` |

It describes a 64-block, 4096-byte-block TestVol with backup slot B at byte
131072. The CRC covers bytes 0 through 235 and the 272-byte slot tail is zero.

## Malformed vectors

The independent test rejects all of these inputs:

- The superblock fixture with any changed byte in its covered CRC domain or
  stored CRC field.
- The leaf fixture with a nonzero `flags` word, even when its node CRC is
  recomputed to match.
- The leaf fixture with `num_keys = 0`, even when its node CRC is recomputed.
- An internal node with a null child or a leaf with an out-of-range sibling.

These are test vectors, not a replacement for full graph checking. The latter
also needs the selected superblock geometry and tree-specific comparators.
