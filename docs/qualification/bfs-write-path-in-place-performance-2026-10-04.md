# Write path in place and small-step appends — 2026-10-04

## Result

The target remains **≤5× PFS3 in every checked AmigaDOS workload**, with
unchanged durability, integrity, snapshot and recovery guarantees. This round
adds appends in small steps as checked workloads. Before the changes below,
growing a file in 4 KiB writes was the slowest workload, at 4.8× PFS3 with
three of eight runs above 5×. After them, the median of every workload is at
most 3.6× PFS3, and no compare run of any workload exceeds 5×. This is an
emulated comparison on one host, not a hardware qualification.

Final series, eight fresh runs per handler, medians of the BFS phase time and
of the per-run ratio to PFS3:

| Workload | Before (ms) | Before | After (ms) | After | Runs >5× after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 43.5 | 2.47× | 29.5 | 1.72× | 0/8 |
| Lookup 400 | 73.1 | 1.10× | 74.5 | 1.06× | 0/8 |
| Read 40 | 20.0 | 1.31× | 20.1 | 1.45× | 0/8 |
| ExNext listing 400 | 34.8 | 2.84× | 32.1 | 2.69× | 0/8 |
| ExAll listing 400 | 10.0 | 3.90× | 8.8 | 3.62× | 0/8 |
| Write 8 MiB | 74.1 | 1.75× | 39.5 | 0.95× | 0/8 |
| Read 8 MiB | 276.3 | 1.02× | 278.4 | 1.02× | 0/8 |
| Delete 40 | 24.5 | 3.02× | 16.7 | 2.08× | 0/8 |
| Append 1 MiB in 4 KiB writes | 165.2 | 4.82× | 72.1 | 2.15× | 0/8 |
| Append 256 KiB in 1 KiB writes | 99.1 | 4.16× | 63.4 | 2.57× | 0/8 |
| Read both appended files | 58.8 | 1.43× | 43.5 | 1.05× | 0/8 |

With `durable-compare`, where each mutating phase ends with an
`ACTION_FLUSH` inside the measured time:

| Workload | Before | After | Runs >5× after |
| --- | ---: | ---: | ---: |
| Create 40 | 2.71× | 1.88× | 0/8 |
| Write 8 MiB | 1.81× | 1.01× | 0/8 |
| Delete 40 | 3.16× | 2.17× | 0/8 |
| Append in 4 KiB writes | 4.88× | 2.36× | 0/8 |
| Append in 1 KiB writes | 4.21× | 2.65× | 1/8 |

The reading phases match the compare series. The single run above 5× is
5.05×: six of the eight 1 KiB append runs took 60–64 ms, two took 94 and
125 ms. Single phases do vary on this host (another run measured the
read-only Lookup at 128 ms instead of 74 ms); a delayed commit that falls into
the phase would be another explanation. Neither is established for these two
runs.

## What changed

**Small-step appends in the benchmark.** Schema 3 of `FS_COMPARE_BENCH` and
`FS_DURABLE_COMPARE` grows a file to 1 MiB in 4 KiB writes and one to 256 KiB
in 1 KiB writes, then reads both in 64 KiB reads and checks every byte. The
phases follow the deletes, so the earlier phases run on the same volume state
as before. `verify-bench-results.sh` accepts schemas 1 to 3; the host replica
`tools/core-workload.c` runs the same phases.

**File data continues behind its last block.** Single data blocks used to come
from the tail of the highest free extent, where metadata blocks come from. A
file grown block by block therefore lay backwards, interleaved with metadata,
with one extent per block. `bfs_freespace_alloc_data` takes the block after
the file's previous block when it is free, and otherwise the first fit at or
after a roving pointer of its own; it never takes the metadata tail. A head
taken from an extent moves the extent's key in place
(`bfs_btree_update_key`), which keeps one path rewrite. In a host probe,
files written in 4 KiB, 1 KiB or 512-byte steps now occupy one extent each;
before, each block was an extent of its own.

**Emergency pool blocks return to the pool.** The new data layout placed data
blocks next to the emergency pool and exposed an existing fault: the
post-publication reclaim freed pending pool blocks together with neighbouring
blocks as a range or a leaf batch, so they entered the free tree. With the pool
short, every reclaim pass retired the free-tree root it had just rewritten,
and `bfs_fs_mount` returned `BFS_ERR_AGAIN` after 256 passes when it resumed
an interrupted snapshot deletion. Pool blocks in the pending queue are now
freed one by one, which returns them to the pool, before the rest.

**Staged node images move into the cache.** When a mutation rewrites a node
its transaction owns, it stages a private image and then copied it into the
cache at publication. The cache now takes the image itself and hands back its
previous buffer (`adopt_node_block`).

**Owned leaves change in place.** A leaf that the live transaction owns and
the cache holds dirty is referenced by no committed state. A value update, a
key move inside its leaf, an insert that needs no split and a delete that
needs no rebalancing now change that cached image directly
(`modify_dirty_node`): the descent only views inner nodes, no path buffer is
copied, no image is staged, and nothing can fail after the change. Any other
case keeps the copy-on-write path. Commits still write these images through
the finalizing hook, before the superblock.

**Enumerations resume at their stop entry.** The scan cursor records the
entry at which a callback stopped the scan. A following scan that starts at
that key, as ExNext does with the name it returned last, begins there after a
single comparison instead of a search of the leaf.

**Scans copy only used entries.** Before callbacks run, a scan copies its
leaf, because a callback may cause I/O. It now copies the header and the used
keys and values. A directory leaf holds at most 14 entries of 264-byte keys
and is often half full.

**Directory entries are checked when a leaf is validated.** Every directory
scan recomputed the name hash of every entry. The check, with the inode and
type checks, is now part of leaf validation (`entry_ok`), so a validated leaf
holds only acceptable entries and a forged entry makes the whole leaf
unreadable. Changes in place check the entry they write.

## Series

All series ran with `emulator-test/bench-series.sh`, eight fresh runs per
handler, handlers in turn, odd runs with BFS first and even runs with PFS3
first. Medians of the BFS phase time:

| Workload | Base | Allocation, adopt, stop entry | Without adopt | In place | In place, partial copy | Final |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 43.4 | 41.1 | 45.1 | 29.6 | 28.2 | 29.5 |
| Delete 40 | 24.4 | 23.8 | 24.8 | 16.5 | 16.5 | 16.7 |
| ExNext 400 | 34.7 | 33.5 | 32.8 | 33.0 | 33.0 | 32.1 |
| ExAll 400 | 9.9 | 10.1 | 10.0 | 9.9 | 9.6 | 8.8 |
| Write 8 MiB | 73.8 | 57.9 | 62.2 | 38.5 | 38.3 | 39.5 |
| Append in 4 KiB writes | 155.4 | 106.4 | 114.8 | 95.7 | 72.1 | 72.1 |
| Append in 1 KiB writes | 97.8 | 80.0 | 86.2 | 60.8 | 59.9 | 63.4 |
| Read appended files | 57.9 | 46.7 | 44.3 | 43.1 | 43.3 | 43.5 |

Columns two to four come from one series of four handlers, columns five and
six from a second series with its own base (46.9 ms Create, 159.4 ms 4 KiB
append), the final column from the final series. Within a series the
handlers ran interleaved. Without the stop entry, ExNext took 34.8 ms against
33.5 ms with it, a difference within the run-to-run spread.

The 4 KiB append column for "In place" is bimodal: four runs at 70–79 ms and
four at 113–152 ms. Partial leaf copies do not touch this phase; the runs of
that handler lay between 71 and 87 ms, and the final handler's median matches
them.

## Host copy counts

The host replica with counters at the three whole-node copies (cache to path
buffer, staging, staged image to cache), one commit per mutating phase as the
delayed commit gives:

| Phase | Before (`5aff558`) | After |
| --- | ---: | ---: |
| Create 40 | 643 | 39 |
| Delete 40 | 412 | 100 |
| Write 8 MiB | 662 | 13 |
| Append in 4 KiB writes | 2334 | 15 |
| Append in 1 KiB writes | 1297 | 141 |

The remaining copies come from the first change of a node in a transaction,
from splits and rebalancing, and for 1 KiB writes from reading the partly
filled data block.

A 4 KiB copy costs about 22 µs on the emulated 68040
([memory copy report](bfs-memory-copy-48-performance-2026-10-01.md)).

## Tests

New tests cover the data allocator (ascending start, goal, split, end of
extent, fallback), the key move (in place, refused at a neighbour or a stale
separator), resuming at the stop entry across deletes and inserts, a forged
directory hash that makes a CRC-valid leaf unreadable, and pending pool
blocks that must return to the pool; the last fails with the previous
reclaim. Two tests assumed the old layout (a file written in one call had
two extents) and now force or expect the contiguous layout. The verifier
tests cover schema 3.

`make host-test`, `make sanitize`, `make analyze`, `make quality-gates`,
`make conformance-test`, `make ci-test` (50 checks), the same checks under
Kickstart 3.2 and `compatibility-test` under AROS and Kickstart 3.2 pass.

## Environment and identities

Cachy, a CachyOS KVM guest (kernel 7.1.3), FS-UAE 3.2.35 under Xvfb, A1200,
68040 at maximum speed, 2 MiB chip and 8 MiB fast memory, Kickstart 47.102,
Workbench 3.2 commands from the original `Workbench3.2.adf`, equal fresh
256 MiB RDB images. The host's GitHub runner container was stopped. The guest
was otherwise idle; the KVM host is not controlled, and single runs vary by up
to a factor of two in either filesystem.

| Binary | SHA-256 |
| --- | --- |
| Base handler (`5aff558`, Mac toolchain) | `9383c83bbe314ae710d33104156d9aeeee27b4d4f5da54d43e2ada0978a6d75c` |
| Final handler | `a124bea0a2670df124eb97294d4a02ca892a5593bf3e762a2b719a6d472e4f61` |
| `fs-compare-bench` guest, schema 3 | `3946157638aa66b4bf4907d5daa0f15b01103e7455b9777194c78fe316e1480f` |
| `pfs3aio` | `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7` |
| Kickstart 47.102 | `68f9d3a36c2866d054f41c29b600618642a4807dfb6bca7354d4879a36c9aa7c` |

The [evidence directory](evidence/bfs-write-path-in-place-2026-10-04/) holds the
result files of the final compare and durable series with a SHA-256 manifest.
Kickstart, Workbench and PFS3 files are licensed and not part of the
repository.

A confirmation series of the 3 October handler on the Mac (FS-UAE 3.2.35,
schema 2, six runs) matched the cloud factors of that report: ExNext 3.11×,
ExAll 3.69×, Create 2.55×, Delete 3.37×, no run above 5×.

## Not done

**Sorted inode lookups for ExAll.** The inode tree of the benchmark volume is a
single leaf, so the lookups have no descent to save. A host profile attributes
about 44 % of the listing work to the inode read itself (search, copy,
validation); sorting would not remove it.

**Handing path buffers to `cow_node`.** Changes in place leave staging only for
the first change of a node in a transaction and for splits and merges.

**Parent rewrites above an unchanged child.** On the copy-on-write path an
update still rewrites every ancestor even if the leaf stays in place. With
trees of height one, as on the benchmark volume, this costs nothing; on larger
volumes it does.
