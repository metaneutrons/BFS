# BFS resident root floor pilot

The resident-root floor-search candidate is rejected and its active code is
restored to the preceding baseline. All 14 fixed Cachy starts and 28 BFS/PFS3
outputs pass, and the candidate passes correctness tests. However, the modest
4KiB growth improvement does not justify repeated slower deletion and small
ExNext results under the predeclared retention rule. No additional measurement
series is launched to seek a more favorable result.

The paired 4KiB growth median is 0.9953 candidate/base in normal mode and
0.9501 in durable mode: approximately 0.5% and 5.0% lower. Three of four pairs
are not slower. Normal deletion is 14.3%/36.6% slower, and durable first-pass
40-entry ExNext is 35.6%/25.8% slower. These observations do not establish their
cause; they are sufficient to decline this bounded experiment. The previous
sparse-CRC/read-regression concern and overall <=5x PFS3 objective remain open.

## Implementation and scope

The candidate skips a scratch-buffer lease when the current root is a single,
fully validated resident leaf. It reads the floor result directly, stores no
pointer/key/value hint across calls, and keeps ordinary read/CRC/structure/level
validation on a miss. Leaf result selection is shared with the traversal path.
The attempted implementation also refactors shared node-view storage to allow
lazy scratch allocation; existing callers supply their already leased buffer.
No on-disk format, integrity, ownership, COW or durability policy changes.

An independent initial review found omitted node-view detail accounting on hits
and duplicate cache peeks on misses. Both were corrected before measurement by
using the shared instrumented view. The revised code was independently approved.
On cold floor misses the node-view interval now encloses its lazy allocation;
that allocation's timer is nested. This diagnostic scope is slightly broader
than the baseline and must not be treated as exclusive CPU time.

The [pre-run scope](evidence/bfs-floor-resident-pilot-2026-10-09/PILOT_SCOPE.md)
fixes two diagnostic starts, two baseline/candidate pairs per production mode
with reversed filesystem and handler order, and one identical-baseline control
pair per mode: exactly 14 fresh starts. Every start retains all 23 workloads
and both filesystems. There are no retries, excluded observations or extensions.
Both baseline and candidate include the preceding provisional sparse-CRC work;
this is not another M5/sparse comparison.

Cachy uses FS-UAE 3.2.35, A1200/68040-max, 2MiB chip, 8MiB fast memory, format-v3
images and actual RDB capacity 30. Production uses the existing pinned schema4
guest without adding a Stack setting. The two diagnostics use the separate
schema2 guest, Stack 32768, ABI16/sidecar1, CRC/write stride1 and detail stride17.
Production and diagnostic cohorts stay separate; neither is pooled with earlier
evidence. Workload names containing append create and grow a new file, not an
existing-file append.

## Production results and retention decision

Times are microseconds. Candidate/base ratios use matching repeat and filesystem
order; lower is faster. Durable production times include the volume flush.

| Mode / repeat / first filesystem | Baseline 4KiB growth | Candidate 4KiB growth | Ratio |
| --- | ---: | ---: | ---: |
| Normal 1 / BFS | 69,696 | 71,998 | 1.0330 |
| Normal 2 / PFS3 | 73,920 | 70,783 | 0.9576 |
| Durable 1 / BFS | 79,040 | 71,936 | 0.9101 |
| Durable 2 / PFS3 | 74,176 | 73,441 | 0.9901 |

Retention required fewer diagnostic buffer requests, lower paired 4KiB medians
in both modes, at least three of four target pairs not slower, and no phase
at least 10% slower in both pairs of either mode. The first three conditions
pass; the last fails:

| Repeated slower phase | Repeat 1 baseline → candidate µs | Ratio | Repeat 2 baseline → candidate µs | Ratio |
| --- | ---: | ---: | ---: | ---: |
| Normal delete 40 | 17,473 → 19,968 | 1.1428 | 17,857 → 24,384 | 1.3655 |
| Durable first-pass ExNext 40 | 5,568 → 7,552 | 1.3563 | 5,696 → 7,168 | 1.2584 |

The [decision](evidence/bfs-floor-resident-pilot-2026-10-09/decision.json)
therefore returns `retain_locally=false`. All other phase values remain in the
[summary](evidence/bfs-floor-resident-pilot-2026-10-09/summary.json).
Durable 8MiB read is also slower in both pairs, at 1.3810/1.0504; grown-file
read is 1.0709/1.0135. The available observations do not clear read regressions.

Same-baseline baseb/basea 4KiB controls are 1.3466 normal and 1.0174 durable.
Their delete-40 ratios are 1.0257/0.9695 and first-pass ExNext-40 ratios
1.0223/1.0905. Controls and PFS3 calibrators show variability; a single control
pair per mode is not a noise bound or justification to remove slower candidate
results. PFS3 deletion calibrator ratios in the normal pairs are 1.0332/0.9841;
durable ExNext-40 calibrators are 1.0000/1.0000. No causal correction is applied.

## Diagnostic work reduction

For the durable 4KiB growth work, buffer requests fall from 517 to 260 and
releases from 521 to 264. Cache-peek calls stay 775. This is a demonstrated
reduction in scratch-buffer requests, not a measured heap-allocation reduction.
The cache can reuse four scratch slots.

Both diagnostics retain 256 core/data/device writes, 515 logical inode reads,
257 inode writes, 516 generic B-tree searches, 256 extent maps/goal attempts,
and 258 free-space allocations. Work has zero device reads, updates, metadata
writes, commits or node CRCs. Both explicit flushes have five device writes,
three updates, four node writes/write CRCs, one commit and no data writes.

| Inclusive diagnostic interval | Baseline ms | Candidate ms |
| --- | ---: | ---: |
| Work | 105.278 | 99.968 |
| Volume flush | 1.855 | 1.920 |
| Inode read | 16.077 | 16.912 |
| Inode write | 10.584 | 11.545 |
| Extent map | 3.590 | 3.929 |
| Free-space goal | 14.293 | 12.849 |

Scope times convert raw ticks at 709,379Hz. They are inclusive/nested, not
additive savings budgets; sampled detail ticks are not extrapolated. One
diagnostic pair neither qualifies production speed nor establishes the cause
of production phase changes. Reduced buffer requests alone do not satisfy
the performance-retention rule.

## Tests, restoration and evidence

The candidate passes 584 tests in 63 host suites and the same 584 under
ASAN/UBSAN. Six new adversarial floor tests cover custom comparators/value
sizes, untouched outputs on NOTFOUND, no scratch lease on a validated hit,
one peek, cold NOMEM/CRC validation, CRC-consistent bad key order/entries,
wrong levels, mutation/root rebinding/cache invalidation, and taller-tree
predecessor fallback. Nine host probe oracles also pass with all-call CRC and
the write sidecar, including exact hot/cold view/peek/buffer accounting and
real write/Sync/remount/full-data/EOF checks; the same nine pass under ASAN/UBSAN.
Leak detection is disabled because the Mac runtime does not support it.

Six summary and six predeclared-decision oracles pass. Source receipts cover
229 files and match before/after remote execution, the measurement-time local
checkout and the archived protocol. Installed runtime receipts match pre/post
for all runs. Each guest cohort has 67 identical asset mappings; the two
cohorts differ only in the pinned guest. The source export initially needed
its input parent directories created; this was corrected before the launcher
stopped services or started a benchmark. The initial summary test fixture
incorrectly imposed Stack 32768 on production; review corrected both the guard
and fixture to preserve the actual unchanged harness. An early real-data
summary rejected the unfinished final control's missing post-run receipt;
the completed inventory subsequently validates without a benchmark retry.

Luna authored bounded adversarial/summary tests, reviewed the implementation
independently and audited all actual results. The summary and decision both
regenerate byte-for-byte. The report uses the writing skill to keep measured
outcomes distinct from causes and production qualification.

Only this experiment's B-tree delta and probe test were restored byte-for-byte;
its new active floor-test file was removed. Candidate code and all seven added
oracles remain recoverable in the frozen `protocol` snapshot and delta.
Rebuilt production and sidecar handlers match the original baseline binaries
byte-for-byte. A fresh build of the restored all-call/write-sidecar host probe
passes its eight baseline oracles. Unrelated dirty-worktree work is preserved. The
[evidence bundle](evidence/bfs-floor-resident-pilot-2026-10-09/README.md) retains
all raw protocols, startup/configuration/RDB records, receipts, logs, source,
test results and decision. Format logs are losslessly encoded; licensed assets,
executables, ROMs and HDFs are excluded.

The series ran 18:49:51–18:58:51 UTC on 9 October 2026; restoration completed
at 18:58:53 UTC with status0. Fresh checks confirm the Docker listener and
Coffin running, no benchmark emulator/Worker, and system CI units inactive
with PID0. No CI, commit, push, merge or issue mutation was performed.

The next bounded implementation should avoid changing the shared hot node-view
path while targeting FreeTree work. The observed slowdowns do not prove that
the refactor caused them; preserving that hot path is a containment measure,
not a causal finding. No second candidate or extra series is started here.
