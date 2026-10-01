# Split and free-tree coalescence attribution

Neither proposed shortcut is a substantial performance lever for the six
checked AmigaDOS workloads. Both fresh opposite-order diagnostic runs show
only two right-side directory leaf split read/rewrite paths during 40 creates.
Every measured two-sided/right-only free-tree coalescence counter is zero.
No optimization is implemented or accepted; the qualified normal handler
remains byte-identical. The goal of <=5 times same-run PFS3 in every checked
workload remains unachieved.

## Observations and decision

The table is per run, not a sum of both runs. Every path matrix agrees exactly
across orders. Counts describe the instrumented sites, not physical I/O.

| Workload | Leaf split attempts | Right read/rewrite calls | Two-edit coalescence attempts/completions | Total node writes | FreeTree node writes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 4 | 2 / 2 | 0 / 0 | 548 | 353 |
| Lookup 400 | 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| Small read 40 | 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| Write 8 MiB | 0 | 0 / 0 | 0 / 0 | 401 | 142 |
| Read 8 MiB | 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| Delete 40 | 0 | 0 / 0 | 0 / 0 | 313 | 197 |

The only nonzero cells belong to `SPLIT_LEAF/DIR_TREE`. Root shape has one
attempt/initial-write call and no right-side selection. Deep shape has three
attempts/initial-write calls and two selections, reads and rewrites. All
internal split cells and all other roles are zero. All 140 matrix rows per
phase are retained, including zeros; BFS has 1,488 TSV rows, 840 of them matrix
rows. PFS3 has its schema header, drive, six elapsed rows and terminal PASS.

Keeping the unpublished split buffer could remove at most two node-read
calls, two node-write calls and two write-CRC calls during 40 creates. Two of 548
node writes is 0.364964%; two of 75 directory writes is 2.666667%. These are work
bounds, not latency percentages. Create has only one BIO-read and one
node-read-CRC call overall, so two recorded node-read calls must not be
presented as two removable device reads or two read-CRC calculations. Cached
paths and validation remain relevant. Buffer reuse would still require an
explicit read-back-validation and fault design.

Height-one/non-null-root coalescence eligibility is also zero. A root-leaf
fusion of the measured delete+update/delete+insert branches has no observed
hit to optimize here. Zero does not prove the branches unreachable or cover
other workloads, histories, options or geometries. The host fixtures do
exercise these branches. Next diagnosis should inspect the frequent
non-split allocation/free edits and metadata-write costs, particularly 353,
142 and 197 FreeTree writes in create/write/delete. These aggregate counts do
not themselves identify a removable write. The prior audit still rules out
omitting `cow_node`'s already-final write.

A Luna reviewer independently extracted all four raw TSVs before the primary
path summary or conclusions, without reading the consumer, primary JSON or
run logs. Raw identities, ordered matrices, nonzero cells and zero phases
agree with later primary extraction. Separately, all 483 pre-existing non-time
fields per order agree with retained single-block-absence controls, with
empty difference arrays. Those controls are not contemporaneous normal
elapsed controls. No instrumented timing, ratio or inclusive scope interval
is used to claim a speedup or <=5 times acceptance.

## Instrumentation and qualification

One private source copy of checkpoint `bdb3ae0` adds split/coalescence matrices
only under `BFS_PERF_PROBE`. The same header macros generate indices,
dimensions and emitted names; one tree-pointer classifier serves both old
node-write roles and new path counters. The probe ABI is 13, deep schema 12;
old evidence continues through the unchanged schema-11 verifier. Target
compiler layout evidence gives 536 to 1,096 snapshot bytes: 140 four-byte `ULONG`s
added. Handler version/size checks, whole-snapshot reset, guest buffer bounds
and the 32 KiB handler stack are reviewed. This is not a physical stack-usage
measurement.

Split hooks record threshold attempt, initial-right-write call, right
selection, read call and rewrite call. Coalescence attempt follows validation
and precedes the first edit; completion follows both successful edits.
Root/deep shape is saved before edits, including singleton deletion that
temporarily empties the root. Attempt is not successful physical I/O;
completion is not proof of durable commit. Normal algorithms, allocation
policy, format, COW, retirement, validation, rollback, publication, fences,
flushes, snapshots and recovery are unchanged.

Two independent Amiga build directories reproduce both handlers and the
diagnostic guest. Both normal handlers match qualified `7ec43316…` exactly.
The qualified normal guest `1fccf207…` is not overwritten. Full native and
actual ASan/UBSan gates each pass 50 suites/475 cases. All 78 quality tests,
ShellCheck, actionlint and clang analysis pass. Linux passes 20 conformance
tests and two actual FUSE runs: default and options 7 with disk pressure and
an interrupted daemon. This is not a geometry or hardware matrix.

The final quality/static gates pass again with the new evidence staged. The
scoped asset check passes for the new evidence, report and plan. The whole-
repository asset audit still exits 1 on 68 older `format-pfs3.txt` records
containing Amiga C1 byte `0x9b`; every reported file is byte-identical to
checkpoint `bdb3ae0`. None belongs to this experiment. This existing failure
is retained and attributed, not repaired or bypassed.
The staged whitespace, branch/size guards and secret-scan hooks pass normally.

Thirteen focused counter scenarios/974 assertions pass normally and under
actual ASan/UBSan, compiling all 17 private core translation units with the
probe enabled. They cover left/right/no split, allocation failure, root/deep
internal cascades, both two-edit coalescences, zero-hit controls, deeper shape,
singleton-root success and first/second-edit failures with rollback. Host
timing services are inert stubs, not Amiga timing qualification. Singleton
setup directly deletes a tree entry and adjusts accounting; reserve refill
consumes five blocks of its synthetic extent. Whole-write faults are used,
not torn media writes. Successful merge cases do not separately assert every
old-neighbor absence; no exhaustive fault-ordinal matrix is claimed.

The strict consumer derives rows from the header, checks guest emission order,
requires complete ordered matrices, validates unsigned widths, split chains,
per-role split-write budgets and coalesce completion/roles, then projects old
fields to the unchanged schema-11 verifier. All 23 synthetic accept/reject
cases pass; root replays pass too. FREE/OTHER coalesce roles remain valid for
tracked/standalone free-space objects; DIR/INODE/REFCOUNT are rejected.
There is no dedicated negative attempt-versus-initial-write fixture, although
the check exists and the host allocation-fault fixture exercises attempt-only
semantics outside a successful-run TSV. No malformed-SSOT mutation matrix is
claimed. A reviewer distinct from the authors finds no remaining blocker.

Preliminary fixture errors are preserved: a duplicate cursor retry returned
`BFS_ERR_EXISTS`, and singleton expectations omitted reserve-refill cost.
These are corrected fixture setup/oracle errors, not algorithm repairs.
The unsupported macOS LeakSanitizer option is retained; ASan/UBSan pass with
leak detection off. Initial consumer range, non-time-label, write-budget and
impossible-role gaps are corrected before actual measurement, with earlier
sources/logs retained. Shell helper warnings are fixed without bypassing gates.

## Runs, transport and evidence

The experiment has exactly two fresh diagnostic systems/four new TSVs, first
BFS-first, then PFS3-first. Cachy uses FS-UAE 3.2.35, Linux 7.1.3-2-cachyos,
A1200/68040 max, 8 MiB fast/2 MiB chip, 255.5 MiB partitions, BFS 4 KiB/options 0/30 buffers.
ROM, PFS3, formatter, geometry and data-checked workload are unchanged. Fresh
identity/idle checks pass; coffin.service is inactive, not disabled.

After the first guest completes, large SSH stdout and a subsequent rsync
download stall. Only owned local SSH/rsync processes are terminated; the
guest already has its completion marker and no emulator remains. That
runner's final transport exit is not a successful-run return-code receipt.
Partial output and interrupted transport status remain. No first-run data
is discarded or rerun. A 512-byte-paced 16 KiB read succeeds where an unpaced
read delivers zero before termination. Text-only gzip bundles are fetched
with 512-byte pacing and exact SHA-256 verification; both reconstructed raw
systems pass the strict consumer. The second runner writes output on Cachy
and has captured exit 0. The underlying transport cause is not established;
it is not attributed to BFS. No network configuration is changed.

Both completed systems are recoverably moved to
`/home/fabian/.cache/bfs-performance/split-coalesce-attribution-2026-10-01/`.
All four TSV hashes agree before/after, and no emulator remains. Binary
handlers, HDFs and licensed assets are not committed. The
[text evidence](evidence/bfs-split-coalesce-attribution-2026-10-01/) retains
raw results, configs, source snapshots/patch, commands, build/gate/identity
logs, strict consumer/fixtures, independent reviews, explicit equivalence and
primary summary. Lossless base64 packages preserve source/context whitespace
and non-ASCII formatter-control records. Raw TSV bytes remain exact.

The offline replay verifies all 40 decoded source/log/patch/hook-log identities,
re-runs both strict consumers, and reproduces both consumer JSON files,
retained-counter equivalence JSON and the primary summary byte for byte.
`SHA256SUMS` covers the exact archive inventory except the manifest itself.
Run `node docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01/replay.mjs`
from the repository root with Node, Python 3 and Bash available; the replay requires
no emulator, network, handler build or licensed runtime asset.

The [plan](../plans/bfs-split-coalesce-attribution-v1.md) records disposition.
No CI, push, PR, merge, release, new user chat, soak, real power interruption,
controller-cache lie or device-bound media qualification is performed.
