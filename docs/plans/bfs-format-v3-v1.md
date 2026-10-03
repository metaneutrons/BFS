# BFS on-disk format v3: inline extents and inode flags

Decision state: approved and implemented on `format/v3-inline-extents`.
Format v3 replaces v2; this driver will not read or write v2 volumes. The goal is the same as for the write-path stages: at most
five times PFS3 elapsed time in every checked AmigaDOS workload, without
weakening committed-state integrity, snapshot isolation or crash recovery.

## Why a format change

After the write-path stages (`perf/write-path-stages`), the remaining cost of
small-file workloads is the per-file extent tree. Every non-empty file owns at
least one extent-tree node, even when its whole content is one contiguous run.
The probe counters of the final durable-compare runs show:

- Create 40 writes 40 extent-tree roots, one node allocation and one node CRC
  per file in addition to the data block;
- Small Read 40 reads 41 nodes beside its 40 data blocks, most of them extent
  roots that the eight-slot cache has evicted;
- Delete 40 reads 43 nodes with full CRC verification, again mostly extent
  roots, only to learn which data blocks to free.

A one-block file therefore occupies two blocks on disk and costs a node
allocation, a node write and a node read on every create, read and delete.
Delete is the tightest workload (4.8 times PFS3 in the median, single runs up
to 5.0). No cache policy removes this: a node that does not exist needs no
read.

The second cost is the file comment. Comments live in hidden directory
entries, and nothing in the inode says whether one exists. Delete, rmdir,
replacing rename and every `Examine`, `ExNext` and `ExAll` entry search the
directory tree for a comment that almost never exists. The pre-check added in
the last stage reduces that search to one lower-bound descent, which is still
3.5 % of the host instructions of the delete workload, and it remains a
directory-tree descent per examined object.

Both costs need persistent state that v2 cannot express. The format
specification forbids giving v2 padding or legacy fields new meaning, so the
change requires a new format version.

## Scope

v3 changes exactly two things:

1. the inode value grows from 44 to 56 bytes and gains a flags word, an inline
   extent length and an inline data CRC;
2. the superblock version becomes 3.

The superblock layout, the node format, the directory, free-space, refcount
and snapshot records, the file-extent record, the commit protocol and the
recovery rules stay byte-identical to v2.

## Inode value v3

| Offset | Width | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `inode_nr` | Unchanged |
| 4 | 4 | `type` | Unchanged |
| 8 | 4 | `size_hi` | Unchanged |
| 12 | 4 | `size_lo` | Unchanged |
| 16 | 4 | `extent_root` | Extent-tree root, or first physical block of the inline extent when `INLINE_EXTENT` is set |
| 20 | 4 | `link_count` | Unchanged |
| 24 | 4 | `protection` | Unchanged |
| 28 | 2 | `uid` | Unchanged |
| 30 | 2 | `gid` | Unchanged |
| 32 | 12 | timestamps | Unchanged |
| 44 | 4 | `flags` | Bit 0 `INLINE_EXTENT`, bit 1 `HAS_COMMENT`; all other bits zero |
| 48 | 4 | `inline_length` | Block count of the inline extent; zero unless `INLINE_EXTENT` |
| 52 | 4 | `inline_crc32` | Data CRC of a one-block inline extent on a checksummed volume; zero otherwise |

Validation rules for every reader:

- An unknown flag bit makes the inode corrupt, like a nonzero node `flags`
  word. It is not a feature negotiation mechanism.
- With `INLINE_EXTENT` set, the inode type is not a directory, `extent_root`
  is nonzero, `inline_length` is at least one, and the range
  `[extent_root, extent_root + inline_length)` satisfies the same rules as an
  extent-tree record: inside the device, at or after the data-start block,
  outside the block containing backup slot B, the emergency pool and the
  active allocator reserve.
- With `INLINE_EXTENT` clear, `inline_length` and `inline_crc32` are zero and
  `extent_root` is a tree root or zero, as in v2.
- A directory inode has `extent_root` zero and `INLINE_EXTENT` clear. v2 did
  not state this, but no writer ever violated it.

The inode-tree leaf capacity drops from 84 to 67 entries at 4096-byte blocks
and from 20 to 16 at 1024-byte blocks. The internal capacity is unchanged, so
tree height does not change for realistic volumes.

### Alternatives considered for the layout

Deriving the inline length from the file size would save four bytes, but it
cannot represent a trailing hole (a file extended by `SetFileSize` or
`ftruncate`), and the truncate path commits between shrinking the extents and
storing the new size. An explicit length keeps the mapping independent of the
size, as the extent tree already does.

Omitting the inline CRC would keep checksummed volumes on the extent tree.
Checksummed files store one record per block, so their small files are exactly
the one-block case the inline extent covers. Four bytes per inode are a small
price for giving both volume kinds the same structure.

Packing the flags into the upper half of `type` would keep the inode at 52
bytes. A separate word is clearer and keeps `type` a plain enumeration.

## Inline extent semantics

An inode with `INLINE_EXTENT` maps logical blocks `[0, inline_length)` to
physical blocks `[extent_root, extent_root + inline_length)`, exactly like one
extent-tree record with key zero. Blocks beyond `inline_length` are holes. On a
volume with `BFS_OPT_DATA_CHECKSUMS`, `inline_length` is exactly one and
`inline_crc32` follows the v2 rule for `data_crc32`: a nonzero value is
verified, zero disables verification.

The writer chooses the representation:

- a file without extents that receives a mapping at logical block zero stores
  it inline;
- a write that extends the inline extent contiguously on disk extends
  `inline_length` (volumes without data checksums only);
- every other mapping change (a non-contiguous run, a leading or inner hole,
  a second block on a checksummed volume, or copy-on-write of one block inside
  a multi-block inline extent) converts the file to an extent tree in one
  B-tree mutation;
- truncation shortens or clears the inline extent; when an extent tree
  becomes empty, the file is again without extents.

A writer is not required to fold a single-record tree back into the inode.
Readers must accept both representations for any file.

Conversion builds the new root leaf from the inline extent and the new
records in one mutation (a new `bfs_btree` operation that creates a root leaf
in an empty tree). A failure before publication releases the allocated node
through the existing mutation abort and leaves the handle's inline state
unchanged. The new tree becomes reachable only when the inode is written, as
an extent-tree root does today. Data blocks are still written before the inode
that references them, and nothing becomes durable before the commit publishes
the superblock. The deferred-node and owned-node rules of stages A and C apply
unchanged to the new root leaf.

## Comment flag semantics

`HAS_COMMENT` is set exactly when the hidden comment entry
`parent_id = inode_nr | 0x80000000` exists. The entry remains the only place
the comment bytes are stored; v3 does not change its record.

Readers treat the flag as authoritative. With the flag clear, `get_comment`
reports no comment and delete, rmdir and replacing rename do not search the
directory tree. Writers change the entry and the flag in the same transaction:
`set_comment` writes the inode only when the flag changes. A clear flag next to
an existing entry is corruption, which the checker reports; an ordinary
operation would leave that entry orphaned.

## Version handling

`BFS_SB_VERSION` becomes 3. The superblock envelope (magic at byte 0, version
at byte 4, options at byte 56, CRC at byte 236 over bytes 0 to 235) is
unchanged, so released v2 drivers report a v3 volume as "version 3 is too new"
and refuse it without writing, as `format-compatibility.md` requires.

This driver reports a CRC-valid v2 superblock as an intact unsupported format
("version 2 is not supported"): no mount, no file operation, no write, and
`bfs check` leaves the medium byte-identical. There is no in-place migration
and no second codec. Users copy their data with a v0.1 driver and reformat.

Reformatting needs one policy change. Today the handler refuses
`ACTION_FORMAT` on every unsupported medium, and `bfs format` stops after
printing the diagnosis. That protects a newer volume from an outdated driver,
but it would leave a v2 partition impossible to reformat on the Amiga once the
new driver is installed. The handler therefore accepts `ACTION_FORMAT` when
every CRC-valid incompatible superblock copy carries an older version and only
known option bits; a newer version or an unknown option bit in either copy
still refuses. `BFS_ACTION_FORMAT_ERROR` keeps returning the diagnosis and
signals the replaceable case in `res2`, so `bfs format` prints the diagnosis
and then formats instead of stopping. Formatting stays an explicit, destructive
command, as it is for a foreign filesystem on the same partition.

The strongest objection is that existing v2 volumes become unreadable with the
new driver. BFS is at release 0.1.4 and has no production deployments that
would need a migration path, and the compatibility contract already names
copying to a new volume as the preferred first migration. Keeping a v2 codec
would double every inode and extent path, including the checker and the
oracle, for volumes that can be recreated.

`format-compatibility.md` asks a future format to define compatible and
read-only-compatible feature masks. v3 does not add them. The options word
remains the incompatible mask, and inode flags are strict. There is no feature
today that an older v3 driver could safely ignore, and an unused mask would be
untested code with its own preservation rules. The document is revised to
state this decision; Fabian may still decide otherwise before the format is
released (see open points).

## Implementation

### Core

- `include/bfs_ondisk.h`: version 3, inode fields, flag constants, static
  assertions for the 56-byte layout and offsets.
- `src/core/inode.c`: v3 validation rules above.
- `include/bfs_extent.h`, `src/core/extent.c`: the extent handle carries
  either a tree root or an inline extent. New entry points open a handle from
  an inode, store it back into an inode and compare it with an inode.
  `bfs_extent_walk` takes the inode instead of a root. Lookup, run lookup,
  value lookup, CRC update, mapping, remap and truncate handle both forms;
  conversion uses the new root-leaf builder.
- `src/core/btree.c`: create a root leaf in an empty tree in one mutation.
- `src/core/file.c`: open, inode update and refresh use the new handle
  functions. Refresh compares the complete extent descriptor, because an
  inline extent can grow while root and size stay unchanged.
- `src/core/namespace.c`: delete and reap collect inline data blocks; comment
  lookups test the flag; `set_comment` maintains it; the unlinked-inode scan
  validates flags.
- `src/core/snapshot.c`: refcount and reclaim walks pass the inode.
- `src/core/fsck.c`: marks inline data blocks as references, verifies inline
  checksums, checks `HAS_COMMENT` against the hidden entries in both
  directions and rejects malformed inline or directory extent fields.
- `src/core/superblock.c`: unchanged code; the message follows the constant.

### Tools and adapters

- `tools/bfs-format-oracle.py`: v3 superblock and 56-byte inode, inline
  extents in the content digest, strict flags.
- `src/core/superblock.c` classifies an unsupported superblock pair as
  replaceable (only older versions with known options) or not.
- `src/amiga/handler.c`, `tools/bfs_format.c`, `docs/amiga-packets.md`: the
  format policy for older versions described above.
- FUSE and the rest of the handler use only core interfaces and need no format
  code; their tests and the handler's comment path are re-run.

### Tests

- Inode validation: every flag and inline invariant, directory rules.
- Extent layer: inline lookup and run lookup, contiguous growth, conversion
  for each trigger listed above, truncate to inside, to the start and beyond
  the inline extent, CRC update and remap on checksummed volumes, walk of both
  forms.
- File layer: a second handle sees inline growth with unchanged size; reads
  across an inline extent followed by a hole; sparse files.
- Fault injection: crash and write failure at every device write for create,
  append-with-conversion, truncate and delete of inline files, with and
  without snapshots and across the deferred-node limits used for stage C.
- Snapshots: copy-on-write of a block in a shared inline extent, deletion of a
  snapshot whose inodes carry inline extents, refcount checks by the checker.
- Comments: flag set and cleared with the entry, rollback on injected failures,
  checker detection of both mismatch directions, `Examine` without a comment
  performs no directory search.
- Compatibility: a CRC-valid v2 superblock in either slot is refused for
  mounting and file operations with its message and left byte-identical by
  `bfs check`; v4 is "too new"; unknown options still refuse; a mixed v2/v4
  pair is not replaceable. The AROS scenario list gains a v2 case that checks
  the refusal, then formats the medium and mounts the result.
- Byte fixtures: the independent inode vector becomes 56 bytes with flags and
  an inline extent; the superblock vector moves to version 3 with its new CRC.
- Oracle: the conformance suite compares manifests of inline and tree files.
- Existing tests that inspect `extents.tree.root` either force a tree (a hole
  or a second, non-contiguous run) or assert the inline state.

### Documentation

The format chapters become the normative v3 definition: entry point,
superblock (version), B-tree chapter (inode table, capacities, inline extents),
namespace (comment flag), commit and recovery wording, fixtures, and the
compatibility contract (v2 refusal, feature-mask decision). README, platform
contract, failure semantics and conformance documents replace their v2
references. The change is committed as a breaking change so the release notes
name it.

## Measurement

Durable-compare on FS-UAE with six fresh runs per order against the current
`perf/write-path-stages` build, the probe counters for node reads, node writes
and metadata allocations per phase, and the host instruction profile of the
same workload. Lookup and examine are expected to change through the comment
flag, create, small read and delete through the inline extent. Large-file
phases should not regress; an 8 MiB file whose runs are contiguous on disk now
needs no extent tree at all.

## Results

Durable-compare on FS-UAE, six fresh runs per variant, interleaved and
alternating BFS-first and PFS3-first; every run verified. The baseline is
`perf/write-path-stages` at `3571f04` (format v2). Times are BFS medians; the
factor uses the PFS3 median of the same series.

| Phase | Before (v2) | v3 | Change |
| --- | ---: | ---: | ---: |
| Create 40 | 58.0 ms (3.4x) | 42.7 ms (2.7x) | −26 % |
| Lookup 400 | 66.6 ms (1.0x) | 62.8 ms (1.1x) | −6 % |
| Small read 40 | 26.4 ms (1.8x) | 19.8 ms (1.5x) | −25 % |
| Write 8 MiB | 89.9 ms (2.2x) | 74.1 ms (2.0x) | −18 % |
| Read 8 MiB | 262.2 ms (1.0x) | 255.7 ms (1.0x) | −2 % |
| Delete 40 | 41.2 ms (4.8x) | 25.6 ms (3.2x) | −38 % |

Delete, the tightest workload, now ranges from 3.0x to 3.4x per run (before
4.1x to 5.5x). The lookup factor rises although BFS got faster, because the
PFS3 median of the v3 series was lower (57.7 against 67.4 ms); this is
emulator noise between series, not a regression.

One deep-compare run per variant shows the mechanism. Create 40 issues 40
device writes instead of 74 and 45 instead of 85 allocations: only data
blocks remain. Small read 40 reads 40 blocks instead of 72, and delete 40
reads none instead of 35; neither verifies an extent node any more. The
deterministic host profile of the same workload (group commit) drops by
29.7 % (create), 5.4 % (lookup), 42.9 % (small read), 16.3 % (8 MiB write),
3.4 % (8 MiB read) and 33.8 % (delete) instructions.

## Staging

All work lands on one branch, `format/v3-inline-extents`, based on
`perf/write-path-stages`, as separate commits in this order: format constants
and inode validation; root-leaf builder and inline extents; adoption in file,
namespace, snapshot and checker code; the comment flag; the reformat policy;
oracle, fixtures and compatibility tests; documentation. The branch is a separate pull request so
the format change can be reviewed on its own.

## Open points

- Feature masks: this plan keeps the options word as the only feature
  mechanism. If compatible or read-only-compatible masks are wanted, v3 is the
  cheapest moment to add them.
- Tree-side merging of contiguous appends would shorten extent trees of large
  fragmented files. The format already allows it; it is not part of v3.
- The pre-existing refcount error after a crash between superblock publication
  and snapshot reclamation (found during stage C) is unrelated to v3 and stays
  open.
