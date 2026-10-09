# BFS FreeTree allocation goal pilot

The allocator-only resident-root fast path is rejected and restored to the
preceding baseline. All 14 Cachy starts and 28 BFS/PFS3 outputs pass correctness,
but the paired 4KiB growth medians are 4.3% slower normal and 0.8% slower durable.
Only one of four target pairs is faster. Durable first-pass ExAll with 1,000
entries is 30.7%/17.4% slower, breaching the predeclared guard. No extra starts
or second candidate are run in this step.

The diagnostic removes 255 scratch-buffer requests, but that reduction does
not establish a production gain. The slowdown causes are not established.
The <=5x PFS3 objective and preceding sparse/read-regression concern remain open.

## Implementation and fixed scope

Only alloc_data_at_goal tries the current, fully validated resident FreeTree
root leaf before ordinary floor lookup. Nonresident, taller and noncacheable
trees decline to the ordinary path; errors and declines leave outputs unchanged.
The helper stores no pointer or hint across calls. It reuses the existing node
validation context, binary search and layout accessors. Shared node_view and
ordinary bfs_btree_search_floor source remain byte-exact against the preimage.
Goal coverage, free-extent validation, reserve/owner guards and mutation/COW/
durability paths are unchanged. There is no public API or on-disk change.

An independent review approves those invariants. A test-author check identified
NULL-root/nonzero-height inconsistency; the new helper was made shape-first
before measurement and final handler pinning. On a cold root miss it adds an
eligibility peek before the ordinary fallback's peek. That cost is retained.

The [pre-run scope](evidence/bfs-goal-root-pilot-2026-10-09/PILOT_SCOPE.md) fixes
two diagnostics, two production pairs per normal/durable mode with reversed
filesystem and handler order, and one identical-baseline control pair per mode.
Exactly 14 fresh starts retain all 23 workloads and both filesystems. No retries,
outlier exclusions, extensions or pooling with preceding evidence are used.

Cachy uses FS-UAE 3.2.35, A1200/68040-max, 2MiB chip/8MiB fast memory, format v3
and actual RDB buffers 30. Production uses the unchanged pinned schema4 guest,
with no Stack override. Diagnostics separately use schema2, Stack 32768,
ABI16/sidecar1, CRC/write stride1 and detail stride17. Both baseline and candidate
include the preceding provisional sparse-CRC implementation; this is not a
comparison against M5. The growth workloads create a new file, not an existing
file append.

## Production results and decision

Times are microseconds. Lower candidate/base ratios are faster; durable times
include volume flush. Medians are calculated from the two paired ratios.

| Mode and first filesystem | Baseline 4KiB growth | Candidate | Ratio |
| --- | ---: | ---: | ---: |
| Normal 1 BFS | 71,743 | 68,223 | 0.9509 |
| Normal 2 PFS3 | 74,431 | 84,416 | 1.1342 |
| Durable 1 BFS | 74,432 | 74,604 | 1.0023 |
| Durable 2 PFS3 | 73,248 | 74,239 | 1.0135 |

Normal and durable paired medians are 1.0425435364 and 1.0079201068. Retention
requires lower medians in both modes, at least three of four pairs not slower,
fewer diagnostic buffer requests, and no phase at least 10% slower in both
pairs of either mode. Only the buffer condition passes. The
[decision](evidence/bfs-goal-root-pilot-2026-10-09/decision.json) therefore gives
`retain_locally=false`.

| Durable first-pass ExAll 1,000 | Baseline | Candidate | Ratio | PFS3 calibrator ratio |
| --- | ---: | ---: | ---: | ---: |
| Repeat 1 BFS first | 76,864 | 100,480 | 1.3072 | 1.0202 |
| Repeat 2 PFS3 first | 69,696 | 81,792 | 1.1736 | 0.9703 |

Other outcomes remain in the complete
[summary](evidence/bfs-goal-root-pilot-2026-10-09/summary.json). Durable 8MiB
read ratios are 1.0302/1.0178; grown-file read is 0.8304/1.0167. Normal read
ratios are 0.9974/0.9868 and 1.0108/1.0334 respectively. These mixed observations
do not clear the preceding read-regression concern or qualify equivalence.

Identical-baseline baseb/basea growth controls are 0.8147 normal and 1.0289
durable; ExAll-1,000 controls are 0.9432/1.0873. They show variability, not a
noise bound. They do not justify removing observations or attributing candidate
slowdowns to noise. No causal or calibrator correction is applied.

## Diagnostic work reduction

For durable 4KiB growth, scratch requests fall 517 to 262 and releases 521 to
266; cache peeks remain 775. This is not a claim of fewer heap allocations:
the cache reuses four scratch slots. Both work intervals retain 256 data/device
writes, 515 inode reads, 257 inode writes and 256 extent-map/goal calls. They
have zero device reads, updates, metadata writes, commits and node CRCs.
Each explicit flush retains five device writes, three updates, four metadata
writes/write CRCs and one commit, with zero reads/read CRCs.

Diagnostic work is 124.288 to 114.175ms; flush is 1.729 to 1.792ms. One probed
pair is not production qualification. All scope timers remain inclusive and
nested; sampled detail ticks are not extrapolated or combined as exclusive
CPU savings.

## Tests and restoration

The finalized candidate passes 586 tests in 63 host suites and the same 586
under ASAN/UBSAN. Eight new cache-backed tests exercise resident no-scratch
lookup, untouched error/decline outputs, shape/level errors, mutation/rebinding/
invalidation, taller-tree fallback, CRC-consistent invalid key order, actual
goal success/miss/accounting, invalid free-extent length and owner/reserve guards.
The independent test review confirms actual allocator-path coverage. A separate
new cold bad-CRC oracle is not included; existing generic corruption suites
remain active. Eight all-call CRC/write-sidecar probe oracles also pass normally
and under ASAN/UBSAN. Leak detection is disabled on the unsupported Mac runtime.

An early parallel ASAN build began before the test author finalized the file
and captured four failed intermediate assertions. Its log is retained. After
the final test revision, the fresh complete 586-test sanitizer run passes before
emulator execution. This is not a replaced or excluded benchmark observation.
Six summary-adapter tests and six shared predeclared-policy tests pass. The
adapter reuses the sealed strict parsers/policy without duplicating inventories
or changing earlier evidence.

Only this experiment's btree.c, alloc.c and internal-header deltas were restored
byte-for-byte, and its new active test removed. The frozen candidate and all
eight tests remain recoverable in the 229-file protocol snapshot and exact
deltas. Rebuilt production and diagnostic handlers are byte-identical to their
original baselines. Unrelated dirty-worktree changes are preserved.
A fresh restored all-call/write-sidecar host probe passes its eight oracles.

The [evidence bundle](evidence/bfs-goal-root-pilot-2026-10-09/README.md) retains
every raw output, configuration, startup/RDB record, identity receipt and log.
The 67 asset mappings match within each separately pinned guest cohort. Source
checks cover 229 files before/after remote execution, at measurement-time locally,
and in the archived protocol. Format logs are losslessly encoded with decoded
hashes verified; licensed assets, executables, ROMs and HDFs are excluded.
Luna independently audits all actual outputs/receipts and regenerates summary
and decision byte-for-byte. Previously sealed evidence remains unchanged.

The series runs 19:55:27–20:04:26 UTC on 9 October 2026. Restoration completes
20:04:28 UTC with status0. A fresh check finds the Docker listener and Coffin
running, no benchmark emulator/Worker, and system CI units inactive with PID0.
No CI, commit, push, merge, installation or issue mutation is performed.

The two scratch-lease pilots have not justified retention. Do not automatically
repeat that micro-optimization. A subsequent experiment should target a larger
operation-level reduction, with a separately defined correctness contract and
bounded retention test; this evidence supplies no promised speedup.
