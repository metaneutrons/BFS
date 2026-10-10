# BFS listing metadata in directory entries, v1

Decision state: option A rejected by Fabian on 10 October 2026: the format
takes no duplicate fields. Options B (also a copy) and C (not proposed before
the release) are not pursued; format v3 keeps its listing path through the
inode. Nothing here is implemented.

## Problem

ExAll and ExNext read one inode per entry for size, protection and date.
PFS3 keeps these fields in its directory blocks and reads nothing else. After
the compact directory records, the cache floor and the batched ExAll, the
inode reads are the largest remaining part of a listing.

The uninstrumented handler measured on 10 October
(`docs/qualification/bfs-production-codegen-pilot-2026-10-10.md`) lists at
3.2–6.1 times PFS3 with ExAll and 2.4–3.0 times with ExNext; every other
workload is at most 2.5 times. ExAll over 1,000 entries costs about 20 µs per
entry warm and 40 µs on the first pass, PFS3 5 and 7 µs.

A handler that lists without reading any inode bounds the gain
(`docs/qualification/bfs-listing-metadata-bound-2026-10-10.md`): ExAll over
1,000 entries ten times falls to 0.59 of the reference (4.00 to 2.32 times
PFS3), the first pass to 0.56 (5.78 to 3.21), ExNext ten times to 0.78 (2.63
to 1.97). Phases without listings stay within 5 %. On the host the replica
needs 68 % fewer instructions for ExAll and 38 % fewer for ExNext. The bound
omits the costs below, so the real gain is smaller, and even the bound does
not reach PFS3: the directory scan and the fill remain (ExAll about 12 µs per
entry), and ExNext pays for resuming the scan on every call.

## Scope

Listings on AmigaDOS (ExAll up to `ED_OWNER`, ExNext, Examine) and FUSE
`readdir` with attributes. Open, read, write, lookup and the inode tree's role
as the single source of truth stay as they are. One format change, decided
before release 0.2.0; no option bit.

## Options

| Option | What a listing reads | Cost |
| --- | --- | --- |
| A. Mirror listing fields in the entry head (recommended) | directory leaves only | wider head record, one more record update per attribute change, inode back-reference |
| B. Separate attribute record per entry (kind 3) | directory leaves only | twice the records, two record changes per attribute change |
| C. Inode stored in the entry for single-link files | directory leaves only | every inode access path changes; inode tree becomes an index for open handles and hard links |
| D. No format change | directory and inode leaves | listings stay where the production run puts them |

C removes the duplicate but rewrites how every operation finds an inode; that
is the largest change and the riskiest one this close to a release. B costs
density and doubles every change. A keeps the inode authoritative and adds a
checked copy.

## Layout (option A)

### Entry head, 56-byte value

| Offset | Width | Field |
| ---: | ---: | --- |
| 0 | 4 | `inode_nr` |
| 4 | 1 | `entry_type` (0–3) |
| 5 | 1 | `name_len` (1–255) |
| 6 | 1 | `flags`: bit 0 `ATTRS` (fields 8–29 are valid), bit 1 `COMMENT` (the inode has a comment; only with `ATTRS`) |
| 7 | 1 | zero |
| 8 | 8 | `size` |
| 16 | 4 | `protection` |
| 20 | 2 | `uid` |
| 22 | 2 | `gid` |
| 24 | 6 | `modify_days`, `modify_mins`, `modify_ticks` |
| 30 | 26 | the first `min(name_len, 26)` name bytes |

Without `ATTRS`, bytes 8–29 are zero. Continuation parts carry the next 56
name bytes each, at most five for 255 bytes. A comment record holds its length
and 55 bytes in part 0 and the rest in part 1. The parent link is unchanged
apart from the value size.

| Value bytes | Inline name | Records per 4 KiB leaf | Per 1 KiB leaf | AROS names with a continuation part |
| ---: | ---: | ---: | ---: | ---: |
| 40 (today, no attributes) | 33 | 78 | 19 | 0.36 % |
| 48 | 18 | 67 | 16 | not measured |
| 56 (proposed) | 26 | 59 | 14 | 1.00 % |
| 64 | 34 | 53 | 13 | 0.36 % |

Counts use `floor((B − 28) / (12 + V))` from `docs/on-disk-format/btree.md`;
name shares are those of the directory layout plan. At 56 bytes a packed
1,000-entry directory needs 17 leaves instead of 13, and a listing no longer
touches the about 15 inode leaves of those entries.

### Inode back-reference

The inode gains the key of the one entry that mirrors it:

| Field | Width |
| --- | ---: |
| `entry_owner` | 4 |
| `entry_hash` | 4 |
| `entry_ordinal` | 2 |
| zero | 2 |

`bfs_inode_t` grows from 56 to 68 bytes; a 4 KiB inode leaf holds 56 instead
of 67 inodes. `entry_owner` is 0 when no entry mirrors the inode: the root,
inodes with more than one link, and unlinked inodes that are still open.

### Invariant

For every inode with `entry_owner ≠ 0`, the head record at
`(entry_owner, 0, entry_hash, entry_ordinal, 0)` exists, names this inode, has
`ATTRS` set, and its fields 8–29 and `COMMENT` equal the inode's. Every head
record with `ATTRS` set is the one its inode names. fsck and the format oracle
check both directions; a mismatch is `BFS_ERR_CORRUPT` on read paths that use
the copy.

## Operations

- **Create, create directory, soft link:** the entry is inserted first, which
  fixes its ordinal; the inode is then written with the back-reference. Both
  belong to one all-or-nothing change, as entry and inode do today.
- **Attribute changes** (write, truncate, close stamp, SetProtection, SetDate,
  SetOwner, comment added or removed): one core function writes the inode and
  updates the mirrored head in the same transaction. Every path that writes an
  inode goes through it; a host test checks the invariant after every
  operation of the fault-injection suites.
- **Rename:** the head moves to its new key with `ATTRS`; the inode's
  back-reference follows.
- **Hard link:** when the link count rises above 1, the primary head loses
  `ATTRS` and the inode's back-reference is cleared; the new entry has none.
  The file then lists through its inode, also after the count falls back to 1.
- **Delete:** the head disappears; an inode that stays (other links, open
  handles) loses its back-reference.
- **Snapshots** freeze both trees together; the invariant holds per snapshot.
- **Listings:** an entry with `ATTRS` fills ExAll, ExNext and Examine from the
  head and reads a comment record only with `COMMENT`. An entry without
  `ATTRS` reads its inode as today.

## Costs

- Density: 59 instead of 78 records per 4 KiB leaf.
- Inode size: 68 instead of 56 bytes.
- Write amplification: every attribute change updates one more record. A Write
  packet publishes size and date today, so appends would pay one extra
  directory update per packet. Writing inodes back once per transaction
  (`docs/plans/bfs-inode-write-back-v1.md`) removes most of that; without it,
  appends become slower.
- A second copy of six fields whose agreement every inode write must keep.

## Strongest objection

Two copies of the same fields can diverge, and a missed update path shows a
wrong size or date in listings without any error. The inode stays the only
source of truth for every operation except listing, so a divergence cannot
corrupt data, but it is visible to users and to tools that compare listings
with file contents.

The answer is structural: one function writes inode attributes, and it updates
the copy; the back-reference makes the copy addressable from every path,
including open handles after a rename; fsck and the oracle check the
invariant in both directions; and the host fault suites check it after every
operation. Hard links, the case that cannot be kept cheaply in sync, are
excluded from the copy instead of being handled.

## Recommendation

Option A with a 56-byte head, implemented after the inode write-back of
`docs/plans/bfs-inode-write-back-v1.md`, so that the copy is written once per
commit and not once per Write packet. Listings are the largest remaining gap,
the bound shows a gain of about 40 % for ExAll and 20–25 % for ExNext, and
format v3 can only take it before release 0.2.0. Option D remains valid if
the added invariant is judged too expensive for that gain; then the inode
batch path is the place for further CPU work, without a format change.

## Open questions

1. Value size 56 or 64 (inline name 26 or 34 bytes).
2. Whether FUSE `readdir` should return attributes (readdirplus) from the copy.
