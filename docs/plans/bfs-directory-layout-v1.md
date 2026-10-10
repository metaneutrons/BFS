# BFS directory tree layout, v1

Decision state: accepted by Fabian on 10 October 2026 after an independent
review; implemented on branch `feat/compact-directory-records`. Format v3 is
unreleased; Fabian decided that it may change in place and keep version number
3, and that release 0.2.0 waits for this change.

## Problem

Every directory record is a fixed 264-byte key, because the name field is
padded to `BFS_NAME_MAX` (255) bytes, plus an 8-byte value. A 4 KiB leaf holds
14 records and an internal node 15 separators; at the minimum 1 KiB block size
both hold 3. A directory of 1,000 files occupies about 72 leaves when packed
full, and about 104 at the 69% fill that hash-order splits leave.

The listing diagnostic of 10 October
(`docs/qualification/bfs-listing-diagnostic-2026-10-10.md`) measured ten
repeated ExAll passes over 1,000 entries on `main`: 2,794 directory node views,
1,153 of them device reads with CRC checks, at about 115 microseconds per
sampled view; and 10,100 inode reads at about 50 microseconds per sampled read,
of which 560 missed the location hints and read their inode leaf from the
device. The directory records' size causes the first block directly and
probably part of the second, because the directory leaves crowd the inode
leaves out of the 30 buffers.

## Scope

BFS is an Amiga filesystem. On Linux, the FUSE adapter serves access to Amiga
files; BFS is not designed as a general Linux filesystem. Name lengths are
therefore those of Amiga and AROS installations, also under Linux.

## Requirements kept

- Names of 1 to 255 bytes, compared case-insensitively with the existing byte
  folding, original spelling preserved, case-only rename in place.
- FNV-1a hash of the folded name, as today.
- Hard links (several records for one inode), the parent link of every non-root
  directory, the root record, and one comment of 1 to 79 bytes per inode with
  the `HAS_COMMENT` flag.
- ExNext and ExAll resume after an entry that has since been removed; FUSE
  readdir.
- COW, node CRCs, snapshot read-only views, recovery and fsck coverage.
- One directory tree root in the superblock; no other tree changes.
- The failure contract of `namespace.c`: a failed `bfs_dir_*` call changes
  nothing (see "Failure contract").

## Options

All counts use the node formula of `docs/on-disk-format/btree.md`; "inline"
is the number of name bytes stored in the head record itself.

| Option | Key / value bytes | Records per 4 KiB leaf | Per 1 KiB leaf | Separators per 4 KiB node | Name limit | B+tree engine change |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Today | 264 / 8 | 14 | 3 | 15 | 255 | none |
| A. Variable-length records (slotted leaf) | variable | about 100–145 for 12–24-byte names | about 25–35 | depends on separators | 255 | new node format |
| B. Compact fixed records, long names in continuation parts (chosen) | 12 / 40 | 78 | 19 | 254 | 255 (33 inline) | none |
| B with 32-byte value | 12 / 32 | 92 | 22 | 254 | 255 (25 inline) | none |
| B with 48-byte value | 12 / 48 | 67 | 16 | 254 | 255 (41 inline) | none |
| C. Name limit 107 | 116 / 8 | 32 | 8 | 33 | 107 | none |

Option A packs best, but it needs a second node format with a slot directory,
byte-based split and merge, free-space accounting inside the node, its own
validation and its own fsck checks. The engine (`src/core/btree.c`, about 3,000
lines) assumes one fixed key and value size per tree throughout. That is the
largest and riskiest change of the three.

Option C only shrinks the padding, and a 1,000-entry directory still needs
about 32 leaves, which together with the inode leaves does not fit 30 buffers.

Option B keeps the fixed-size engine. The tree gets a new key and value
definition and a byte-order comparator; everything else is in `dir.c`, the
namespace code, fsck and the adapters.

## Recommended layout (option B, 40-byte value)

### Key

Twelve bytes, all fields big-endian, so that the plain byte order (`memcmp`) is
the record order and the comparator needs no case folding:

| Offset | Width | Field |
| ---: | ---: | --- |
| 0 | 4 | `owner`: the parent directory for entries, the inode itself for its parent link and comment |
| 4 | 1 | `kind`: 0 entry, 1 parent link, 2 comment |
| 5 | 4 | `name_hash`: FNV-1a of the folded name for entries, 0 otherwise |
| 9 | 2 | `ordinal`: distinguishes entries of one directory with equal hash, 0 otherwise |
| 11 | 1 | `part`: 0 for the head record, 1 to 6 for name continuations, 0 or 1 for comments |

For a directory D the order is: D's entries, each head record followed by its
continuation parts; then D's parent link; then D's own comment. A child's
comment and parent link are owned by the child and lie in the child's range.

### Records

All bytes not listed below are zero, and readers reject nonzero padding.

| Record | Key constraints | Value |
| --- | --- | --- |
| Entry head | kind 0, part 0, owner < 0x80000000 | `inode_nr` u32 (nonzero, < 0x80000000), `entry_type` u8 (0–3), `name_len` u8 (1–255), `flags` u8 (0), then the first `min(name_len, 33)` name bytes |
| Name continuation | kind 0, part 1 to 6 | the next up to 40 name bytes |
| Parent link | kind 1, hash 0, ordinal 0, part 0; owner is a directory other than the root | parent inode u32 |
| Comment | kind 2, hash 0, ordinal 0, part 0 or 1 | part 0: length u8 (1–79) and up to 39 bytes; part 1, present exactly when the length is above 39: up to 40 bytes |
| Root | owner 0, kind 0, hash of `"/"`, ordinal 0, part 0 | head with `inode_nr` 1, type directory, name `"/"` |

Owner 0 appears only in the root record. An entry has exactly
`ceil((name_len - 33) / 40)` continuation parts for names longer than 33 bytes,
numbered from 1 without gaps; at most six. No entry is named `"."` or `".."`:
the parent link replaces today's `".."` entry, and `ino | 0x80000000` owners
disappear.

### Validation

Each record passes `entry_ok` on its own: kind at most 2; the key constraints
and value ranges above; zero padding. A name assembled by lookup, scan or
insert is checked as a whole: the number and order of continuation parts, a
zero tail after the last name byte, and the hash recomputed from the full
folded name. Any mismatch returns `BFS_ERR_CORRUPT`, as a hash mismatch does
today.

### Operations

- **Lookup by name:** hash the folded name, search `(parent, 0, hash, 0, 0)`
  and walk the heads with that hash, usually one. Compare the folded name with
  the inline bytes and, for longer names, the continuation parts that follow
  the head in the same leaf or the next.
- **Insert:** the same walk detects a case-equivalent name (`BFS_ERR_EXISTS`)
  and finds the lowest free ordinal. If all 65,536 ordinals of one hash are
  taken, which only deliberate collisions can reach, insert returns
  `BFS_ERR_OVERFLOW`.
- **Remove:** delete the head, then its continuation parts.
- **Replace:** keeps the stored spelling and rewrites only value bytes 0 to 4
  of the head.
- **Case-only rename:** the folded name, the hash and the length are unchanged,
  so the keys and the number of parts stay; only the stored spelling changes.
- **Listing:** scan the range `(owner, kind 0)` in key order; each entry is a
  head followed by its parts, so a long name needs no second cursor and no
  random lookup.
- **Emptiness and `may_have_entries`:** a directory is empty when its
  `(owner, kind 0)` range is empty. Every scan, resume and emptiness check is
  bounded by `(owner, kind 0)`, not by the owner alone as today; otherwise the
  parent link and the comment would count as entries.
- **Resume:** the handler's lock cursor stores the last consumed name today,
  and `fib_DiskKey` and `eac_LastKey` carry a count of consumed entries that
  must match it. The cursor stores `(hash, ordinal)` instead and resumes after
  all parts of that entry; the counts stay. Without a matching cursor the
  handler counts from the first entry, as today.

The listing order stays hash order, as today. Collisions of the 32-bit hash
within one directory are rare (about 1.2 × 10^-4 for 1,000 names) but legal,
and the ordinal handles them without comparing names in the comparator.
Deliberately colliding names make lookup within their group linear; today it is
logarithmic.

One new anomaly is accepted: inside a collision group, deleting an entry and
creating another with the same hash reuses the freed ordinal, so a paused
ExNext can list a recreated entry twice or miss one created into a lower
ordinal. Name-based resume does not have this case. It needs a hash collision
within one directory and is covered by a forced-collision test; undo paths that
re-insert an entry restore its original ordinal.

### Failure contract

`namespace.c` treats a failed `bfs_dir_insert` or `bfs_dir_remove` as "nothing
changed": it undoes only its own earlier steps and latches no error. With up to
seven records per entry, every `bfs_dir_*` call must therefore be
all-or-nothing in memory:

- Insert writes the continuation parts first and the head last, so a partial
  state is never a visible entry; remove deletes the head first.
- On a failure after the first record, the call removes or restores what it
  already changed. If that undo fails, it sets the sticky recovery error that
  `fs_namespace_result` already reads, so the transaction is not published.
- Crash atomicity is unchanged: the commit that makes room runs before a
  namespace operation, never inside it.
- The deferred-free reserve (`bfs_fs.h`) counts directory records: a
  namespace operation changes at most 35 of them, counting the undo of failed
  steps (a directory rename that fails to remove its source and then fails to
  remove the destination it inserted). With one bound each for the inode,
  free-space and reference-count trees the reserve is 38 × 65 = 2,470 of the
  16,384 queue slots, against 325 before.
- Namespace undo paths that re-insert a removed entry (delete and rmdir after a
  failed inode delete) use `bfs_dir_insert_at` and restore its ordinal.

Tests inject a failure at every record step of insert, remove, rename and
comment changes and check that the tree is unchanged or the recovery error is
set; crash injection alone does not cover this.

### Old-layout v3 images

An image formatted by today's driver is also version 3, and there is no option
bit or other marker for the new layout: Fabian decided on 10 October that only
his development images use the old layout and that he rebuilds them. The new
driver and the old driver therefore detect the other's directory tree only in
node validation, as "corrupt", not as an unsupported format.
`docs/format-compatibility.md` gets a note that this applies to the
unreleased layout only.

## Inline name length

The inline length is 33 bytes (40-byte value). The maximum name length stays
255 bytes: it costs no space in this layout and matches the BSTR length limit
of AmigaDOS packets, so no per-volume limit is needed.

Measured name lengths, 10 October 2026:

| Source | Names | Median | 99th percentile | Longest | Longer than 25 / 31 / 33 / 41 |
| --- | ---: | ---: | ---: | ---: | --- |
| AROS-NX x86_64 system (`SYS:` without `Developer`) | 4,696 | 12 | 26 | 46 | 1.00 / 0.43 / 0.36 / 0.26 % |
| AROS-NX SDK (`Developer`) | 1,766 | 11 | 26 | 51 | 1.53 / 0.51 / 0.51 / 0.11 % |
| Workbench 3.2 test assets | 68 | 7 | 25 | 25 | 0 % for all |

Between 25 and 41 inline bytes, a packed 1,000-entry directory needs 11 to 15
leaves, against 72 today; continuation parts are rare at every choice. 33 bytes
holds every name of a PFS3 volume with its default limit of 31 characters
(pfs3aio `format.c`; the limit can be raised per volume to 106 with
`ACTION_SET_FNSIZE`) and splits a 79-byte comment into exactly two parts.

`fib_FileName` holds 108 bytes including its terminator, and the handler
truncates longer names to 107 bytes in Examine and ExNext, where they cannot be
reopened under the listed name. Fabian decided on 10 October that both adapters
refuse new names longer than 107 bytes by default, each with an option to allow
them. The format keeps 255 bytes.

- The core enforces the limit, so that both adapters share one rule: a mount
  sets the name limit (107 by default, 255 with the option), and every
  operation that creates or changes a name (create, create directory, rename
  target including a case-only rename, hard and soft link) checks it and
  returns a new `BFS_ERR_NAME_TOO_LONG`.
- AmigaDOS: the Mountlist `Control` word `LONGNAMES` sets 255, next to the
  existing `COMMIT=SYNC`; the handler maps the error to
  `ERROR_INVALID_COMPONENT_NAME`.
- FUSE: the mount option `--long-names` sets 255; the adapter maps the error to
  `ENAMETOOLONG`. The limit applies to the decoded name bytes, not to the
  escaped form FUSE shows.
- Names that already exist stay reachable; the check applies only to new names.

## Strongest objection

Names longer than 33 bytes cost continuation parts: extra records to create,
copy and remove, and up to seven records whose all-or-nothing handling the
failure contract has to guarantee. A workload with many long names would lose
part of the density gain.

This is accepted. BFS stores Amiga files on every platform, and the measured
installations have at most about 0.5% such names. Continuation parts sit
directly behind their head, so they cost extra records, not extra lookups. A
per-volume inline length would make the record size a volume property, with a
superblock field and tests for every size, for no measured benefit.

## Relation to inode lookups

The layout leaves the inode tree unchanged. ExAll still reads one inode per
entry for size, protection and dates. The 10 October diagnostic shows two
parts of that cost: the inode search averages roughly 35 microseconds per
sampled call, almost all of them hinted lookups, and 560 misses per ten passes
read an inode leaf from the device. The layout
should reduce the misses, because 13 instead of 72 directory leaves leave room
in the 30 buffers; the hinted cost remains. Batched, sorted lookups per ExAll
buffer address that remainder, need no format change, and are best measured
after the layout change, when the cache state they run in is the final one.

The provisional sparse-key CRC exists because 264-byte keys are mostly zero
padding. It skips only aligned zero runs of at least 64 bytes; the new records
have none. Its open read-regression question would then no longer matter for
the directory tree, and the code could be removed.

## Changes

- **Format documentation:** `docs/on-disk-format.md` (the v3 description and
  the frozen statement), `docs/on-disk-format/btree.md` (directory tree section,
  capacity table), `docs/on-disk-format/namespace.md` (root, parent link and
  comment records), `docs/on-disk-format/fixtures.md`,
  `docs/format-compatibility.md`, and the Mountlist description of
  `LONGNAMES` in `README.md`.
- **Core:** `include/bfs_ondisk.h` (the directory key type), `include/bfs_dir.h`
  (value structure, a scan callback that exposes hash and ordinal),
  `src/core/dir.c` (key, value, comparator, `entry_ok`, every operation, cursor
  and resume), `src/core/namespace.c` (parent links and comments use the new
  kinds instead of `ino | 0x80000000` and `".."` entries; emptiness checks;
  the name-limit check), `src/core/fs.c` (root record at format and mount; the
  per-mount name limit), `include/bfs_types.h` (`BFS_ERR_NAME_TOO_LONG`),
  `src/core/fsck.c`.
  Snapshot views open the directory tree through `bfs_dir_init` and walk it
  with the generic engine; `snapshot.c` needs no change, but fsck's per-snapshot
  pass does.
- **Adapters:** the AmigaDOS handler (ExNext and ExAll resume; the `".."`
  lookups in `handler.c`; the name-length check and the `LONGNAMES` Control
  word) and the FUSE adapter (comment lookup, readdir, the
  `".."` lookups in `bfs_fuse.c`; `--long-names` and the error mapping).
- **Tools and tests:** `tools/bfs-format-oracle.py`, the byte fixtures,
  `tests/conformance/test_conformance.py` (which hard-codes 264), the
  conformance and compatibility tests, `tests/test_inline_extent.c`, every
  directory test and the fault and corruption qualification.

## Validation and acceptance

- **fsck:** one pass over the whole directory tree that dispatches by kind,
  for the live tree and each snapshot. Invariants: heads sorted and unique per
  `(owner, hash, ordinal)`; no two case-equivalent names in one directory; hash
  recomputed from the full name; continuation parts complete and contiguous,
  without orphans; zero padding; exactly one root record; one parent link per
  non-root directory, pointing to the directory that holds its entry, and none
  for the root or for non-directories; kinds 0 and 1 only under directory
  owners; entry type equal to the inode type; comment parts present exactly
  when `HAS_COMMENT` is set, and their count matching the stored length; one
  entry per directory for each directory inode.
- **Tests:** forced hash collisions, including the ordinal-reuse case; names of
  1, 33, 34, 73, 74, 107, 108 and 255 bytes, with and without the long-name
  option in both adapters; case-only rename of long names;
  removal during a paused ExNext and ExAll; comments of 1, 39, 40 and 79 bytes;
  1 KiB and 64 KiB blocks; failure injection at every record step; crash
  injection during long-name inserts.
- **Diagnostic acceptance:** in the listing diagnostic, directory leaf counts
  and the device reads attributed to the directory tree per repeated ExAll pass
  over 1,000 entries fall by at least the ratio of measured leaf counts. Total
  device reads include about 560 inode-tree reads per ten passes that this
  layout does not address directly, so they are reported, not used as the
  criterion. The production comparison follows the pilot design used so far,
  with its rule fixed before the run.
- **Pilot result, 10 October:** not accepted under the fixed rule
  (`docs/qualification/bfs-directory-layout-pilot-2026-10-10.md`). Directory
  device reads in repeated ExAll over 1,000 entries fell from 1,153 to 211
  (bound 240), directory leaf views from 1,171 to 321 (bound 244). Every
  listing phase became faster; the decision on retention is Fabian's.
- **Validation rework, 10 October:** retained
  (`docs/qualification/bfs-directory-validation-pilot-2026-10-10.md`). Table
  case folding, word-wise key comparison and zero checks in `dir.c`, and a
  slicing-by-8 CRC for hosts and AROS: ExAll over 1,000 entries ten times
  needs 34% fewer host instructions; the emulated handler keeps every
  compared count and shows no regression.
- **Cache floor and inode batches, 10 October:** retained
  (`docs/qualification/bfs-listing-cache-batch-pilot-2026-10-10.md`). The
  handler keeps at least 64 nodes (at most 256 KiB for the floor) and ExAll
  reads its inodes in ascending batches: repeated ExAll over 1,000 entries
  reads nothing from the device and takes 40–44% of its previous time,
  5.5–5.9 times PFS3.

## Decisions

- 10 October: format v3 changes in place and keeps version number 3; release
  0.2.0 waits for this layout.
- 10 October: inline name length 33 bytes, maximum name length 255 bytes.
- 10 October: no option bit for the new layout (see "Old-layout v3 images").
- 10 October: both adapters refuse new names longer than 107 bytes unless
  `LONGNAMES` (AmigaDOS) or `--long-names` (FUSE) is set; the core enforces the
  limit.

- 10 October, after the pilot: Fabian keeps the layout despite the missed
  leaf-view bound; the validation cost of full directory leaves is reworked
  and measured again under a new rule fixed before that run.
- 10 October, during implementation: the handler still resolves a `..`
  component between others (`../leaf`, qualified by test `path_45`) through
  the parent link; a final `..` component is no longer resolved.
- 10 October, during implementation: the AROS CI volume is mounted with
  `LONGNAMES` and runs `bfs-test LONGNAMES`; the FS-UAE suite runs with the
  default and checks that 108- and 251-byte names are refused.

## Open questions

1. Whether every resume path of the handler keeps its lock cursor (for
   example after a disk change or remount), or whether the count fallback
   remains the only guarantee there, as today.
