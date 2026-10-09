# BFS write-path attribution on Cachy

All four predefined starts and all eight BFS/PFS3 outputs validate. The new
optional write probe separates inode reads, inode writes, extent mapping and
goal-directed free-space allocation. No filesystem optimization is implemented
by this investigation. The production handler and the existing diagnostic
handler identities are unchanged.

For durable 4KiB growth, 256 writes produce 515 logical inode reads, 257 inode
writes, 256 extent-map calls and 256 goal-allocation attempts. Their inclusive
intervals are respectively 16.977–18.306ms, 9.862–11.612ms, 3.120–3.855ms and
14.641–15.605ms. Work performs no device reads or node CRCs; its device-write
interval is 11.141–12.456ms. The next bounded optimization candidate is the
repeated free-space predecessor lookup, subject to unchanged validation and
ownership rules. These intervals are not additive savings estimates.

The 514 steady-state B-tree buffer requests do **not** establish 514 heap
allocations. The cache can reuse four scratch buffers. This evidence does not
qualify a production speedup, clear the previous read-regression concern, or
close the <=5x PFS3 objective. The sparse-CRC candidate remains provisional.

## Fixed inventory and measurement contract

The [schedule](evidence/bfs-write-path-profile-2026-10-09/results/schedule.tsv)
contains exactly four fresh starts, in this order:

1. Normal, BFS first.
2. Durable, BFS first.
3. Durable, PFS3 first.
4. Normal, PFS3 first.

Every start retains all 23 checked workloads for BFS and PFS3. There is one
unchanged sparse candidate, not an M5/candidate comparison or an identical-
handler control series. Two observations per mode are bounded attribution,
not statistical equivalence. No result is retried, removed or corrected for
an assumed outlier. PFS3 outputs remain in the raw records and full summary.
Do not pool these runs with the preceding twelve-run CRC/read cohort or the
production-performance cohorts: both handler instrumentation and guest identity
have changed.

Cachy uses FS-UAE 3.2.35, A1200/68040-max, 2MiB chip and 8MiB fast memory,
fresh format-v3 images, actual RDB capacity 30 and guest Stack 32768. The main
probe remains ABI16, with all-call CRC measurement. A separate version-1
sidecar uses packet 3013 and records every call to the four new scopes; it is
compiled only with `BFS_PERF_WRITE_DETAIL`. Existing detail scopes sample
every seventeenth call. Their call counts are exact, but their sampled ticks
are not extrapolated into total time. The clock is 709,379Hz.

New guest modes are `split-write` and `split-write-durable`, emitting schema2.
Schema1 modes and inventories remain accepted unchanged. Work/volume flush,
guest Open/Read/Write/handle Flush/Close/byte verification, I/O and node CRCs
remain separately recorded. All data, EOF and Close checks remain enabled.
Normal mode requests no volume flush. `US = WORK_US + VOLUME_FLUSH_US`, excluding
probe reset/snapshot gaps; a handle Flush is not a volume flush.

All core, device and sidecar intervals are inclusive and may nest. Never add
them or subtract them into exclusive CPU budgets. All-call instrumentation has
observer overhead and can affect transaction/coalescing timing. Positive calls
with zero ticks do not prove zero cost.

## 4KiB growth attribution

The workload creates a new file and grows it to 1MiB through 256 writes; it
does not append to a pre-existing file. Every observation has 515/257/256/256
work calls and samples for inode read/write, extent map and free-space goal.

All intervals below are milliseconds. Sidecar and device values convert raw
ticks at the recorded clock frequency; work/guest/flush values use guest
microseconds. The [summary](evidence/bfs-write-path-profile-2026-10-09/summary.json)
retains every underlying counter and tick value for all phases.

| Mode / first filesystem | Work | Volume flush | Guest Write | Inode read | Inode write | Extent map | Free-space goal | Device write |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Normal / BFS | 114.111 | 0 | 111.523 | 17.542 | 11.199 | 3.750 | 15.336 | 17.522 |
| Normal / PFS3 | 205.660 | 0 | 200.217 | 32.791 | 17.563 | 7.280 | 25.621 | 40.678 |
| Durable / BFS | 109.182 | 1.983 | 105.611 | 18.306 | 11.612 | 3.855 | 15.605 | 12.456 |
| Durable / PFS3 | 101.311 | 2.240 | 98.273 | 16.977 | 9.862 | 3.120 | 14.641 | 11.141 |

Both durable work intervals perform 256 data/device writes and zero device
reads, device updates, node writes, commits or CRCs. Each explicit flush performs
five device writes, three updates, four node writes/write CRCs and one commit,
with no data writes or new sidecar calls. Its four CRCs total 182/140 ticks,
or 0.2566/0.1974ms. Inclusive core-write times are 80.903/73.750ms and packet-
write times 98.172/91.178ms. These also must not be added to the sidecar times.

Normal BFS-first is different: a commit occurs inside work. It records two
device reads, 261 device writes, three updates, four node writes, four write
CRCs (182 ticks), one commit and the same 256 data writes. Normal PFS3-first
records only the 256 data/device writes, with no reads, updates, metadata
writes, commits or CRCs. The slower 205.660ms observation is retained in full;
these two runs do not identify its cause. No normal volume flush is requested.
Transaction placement from the preceding cohort must not overwrite these
observed counters.

## 1KiB growth cross-check

This workload also makes 256 writes, growing a newly created file to 256KiB.
The work sidecar counts remain 515 inode reads and 257 inode writes, while
extent-map/goal attempts fall to 64/64. Every work interval performs 64 data/
device read-modify-write reads, 256 data/device writes and no metadata writes,
updates, commits or node CRCs.

| Mode / first filesystem | Work ms | Volume flush ms | Inode read ms | Inode write ms | Extent map ms | Free-space goal ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Normal / BFS | 89.378 | 0 | 17.132 | 10.255 | 1.059 | 3.743 |
| Normal / PFS3 | 111.871 | 0 | 20.340 | 12.002 | 1.425 | 4.257 |
| Durable / BFS | 89.727 | 2.944 | 17.985 | 11.919 | 1.257 | 3.700 |
| Durable / PFS3 | 84.223 | 1.793 | 17.459 | 10.303 | 0.802 | 3.581 |

Each durable flush has two device reads, five writes, three updates, four node
writes/write CRCs and one commit; no data writes or sidecar calls. Read CRC
calls are zero in both work and flush. Its write CRCs total 317/182 ticks
(0.4469/0.2566ms). The approximately fourfold goal/extent-call difference is
consistent with four 1KiB writes per allocated 4KiB block. It does not isolate
the CPU cost of predecessor lookup from range mutation or observer overhead.

## Code interpretation and next bounded candidate

The handler's write protection check reads the inode once per Write packet.
The core then refreshes it under its operation lock before writing. These two
paths explain 512 of the 515 logical reads; the three surrounding reads are
not separately packet-attributed here. Logical inode reads are not device
reads. Existing operation-local reuse already avoids another publication-time
read: the refreshed inode is seeded into the write's final metadata update.
Do not cache rights or inode state across packets without preserving the
lock, root, generation, recovery, unlink and snapshot rules.

The 4KiB phase records 516 generic B-tree searches and 258 index-hint hits.
Free-space goal allocation uses predecessor (`bfs_btree_search_floor`) lookup,
which is not counted as a generic search. The first null-goal attempt is
counted by the sidecar before fallback; subsequent contiguous goals still
search and modify the FreeTree. No non-inode/non-directory tree node views
are observed during these work intervals; the current short file uses inline
extents. This is not evidence that large or fragmented files avoid extent-tree
work.

Normal PFS3-first records 514 B-tree buffer requests and 514 releases; both
durable work intervals record 517/521 and their flushes 4/4. These scopes wrap
`bfs_bio_alloc_buffer`/`bfs_bio_free_buffer`. The Amiga handler uses the cache
callback, which can lazily allocate and then reuse four block-sized scratch
slots, falling back to malloc for other sizes or occupied slots.
`BTREE_MALLOC_CALLS=0` in those work intervals counts direct malloc in
`btree.c`, not the helper/callback's heap activity. Neither counter establishes
the real heap-allocation count. Code inspection suggests a steady 258 generic
inode-search plus 256 allocation-path buffer requests; that categorization is
an inference, not a per-call measured classification.

The first implementation candidate is a narrowly bounded predecessor fast
path using an already resident, validated FreeTree leaf to avoid repeated
search traversal and block copying. It must preserve structure/level/parent/
bounds and CRC checks, tree/root/generation identity, ownership, COW,
allocation accounting and fallback behavior. The measured 14.641–15.605ms
goal interval includes range removal/mutation; it is not all lookup and is
not a promised speedup. Inode writes are substantial but perform required
state publication. Inline extent mapping is a smaller interval. No additional
CRC optimization can directly accelerate the durable 4KiB work, which has
zero CRC calls.

This report selects a hypothesis, not an implemented or approved fast path.
A later implementation needs adversarial host tests and a fixed paired
performance comparison at identical workload/build identities before retention.

## Verification and evidence preservation

The [evidence bundle](evidence/bfs-write-path-profile-2026-10-09/README.md)
contains all eight raw result tables, all 23 phases, completion/mount records,
startup sequences, emulator configurations, actual RDB inspection, full run
logs, receipts and exact summary. All installed handler/guest/PFS3 hashes match
before and after each run. All 67 non-handler asset path/hash mappings match.
All 228 frozen source/critical harness entries pass checks before/after the
remote series and in the retained `protocol` snapshot. At measurement time all
228 local entries also matched. One local post-run correction changes only
missing-artifact guidance to name the dedicated write-probe build targets;
the frozen executed harness and its diagnostic-only diff are retained.

The first archival copy filtered source extensions too narrowly and omitted
eight JSON/replay/definition/requirements files. The failed copy check is
retained; all eight were copied from the unchanged frozen export, then all 228
snapshot hashes passed. No source or timing record was reconstructed. Format
logs are losslessly base64-encoded and all four decoded byte hashes verified.
Licensed guest assets, ROMs, executables and HDFs are excluded from the bundle.

Local checks pass:

- Eight host probe oracles, including early-return/reset behavior and 256 real
  4KiB writes followed by Sync/unmount/remount/full-data/EOF verification.
- The same eight under ASAN/UBSAN, with leak detection disabled: the initial
  leak-enabled launch aborts before tests because this Mac runtime does not
  support LeakSanitizer. Both logs remain; no leak coverage is claimed.
- 49 schema1/schema2 split-verifier tests, 66 existing verifier tests and eight
  exact-inventory/metadata/negative-case summary tests.
- Shell syntax and ShellCheck for the harnesses and stop/restore runners.
- Bitwise reproduction of production/default-stride/CRC1 handlers and of the
  dedicated write-probe/guest aliases; the preceding twelve-run summary is
  reproduced exactly with the evolved verifier.

The first sidecar build rejected packet 3012 because it already names commit
mode. Packet 3013 is used in every measured binary; the failed compile log is
retained. New real-data direct-core oracles intentionally observe one inode
read per write, not the additional Amiga protection read. These are diagnostic
checks, not a rerun of the complete filesystem qualification suite.

Luna implemented the optional core wrappers and parser oracles, independently
reviewed integration, then verified every raw result, exact summary regeneration,
metadata guards and the buffer-allocation interpretation. The root agent
integrated the sidecar/guest/harness, checked table values against raw records
and selected the bounded next hypothesis. The writing skill separated measured
facts, code inferences and qualification limits in this report.

The authorized idle Docker runner and Coffin were stopped for the fixed series.
It ran from 18:24:29 to 18:27:14 UTC on 9 October 2026; restoration completed
at 18:27:17 UTC with status 0. Fresh checks show the Docker listener and Coffin
running again, no benchmark emulator or Worker, and both system CI units
loaded/inactive/dead with PID 0. No CI, commit, push, merge or issue change was
performed. There is no new core optimization or production performance release.
