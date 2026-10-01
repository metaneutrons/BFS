# BFS inode timestamp coalescing performance

The operation-scoped timestamp increment is retained locally. It combines
Amiga creation dates with the first inode insert and positive-write modification
dates/archive-bit clearing with the necessary final inode update. The normal
comparison shows useful create/write reductions without changing the tested
COW, reclamation, checksum or publication rules. The five-times PFS3 goal
remains unachieved; this increment is not performance parity.

## Normal measurements

Four fresh normal runs per revision, balanced by filesystem order, ran serially.
Elapsed microseconds below include all observations; negative changes mean
less candidate time.

| Workload | Baseline BFS | Candidate BFS | Change | Candidate PFS3 | Ratio of candidate means |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 274574.75 | 221118.25 | −19.47% | 16975.75 | 13.026 |
| Lookup 400 | 112895 | 121519.5 | +7.64% | 69355.5 | 1.752 |
| Small read 40 | 49152 | 49247.5 | +0.19% | 16716.5 | 2.946 |
| Write 8 MiB | 410495.75 | 374205.75 | −8.84% | 39520 | 9.469 |
| Read 8 MiB | 542140.75 | 521369.75 | −3.83% | 266474.75 | 1.957 |
| Delete 40 | 182799 | 174926.75 | −4.31% | 8079.75 | 21.650 |

All four matched writes are faster; three of four creates are faster. Eight
of 24 order/repeat-matched BFS changes are adverse, including three lookups,
one create, one small read, two large reads and one delete. Every candidate
create, write and delete exceeds five times its same-run PFS3 time; the other
phases remain below five in these four observations. No sample is trimmed.
The [complete measurements](evidence/bfs-inode-stamp-2026-10-01/measurement-summary.md)
retain every raw value, matched change, ratio and threshold failure.
Independent Luna extraction and primary calculations agree.

Four observations do not establish statistical or causal precision. In
particular, the lookup increase cannot be declared caused by timestamp code,
and the favorable means do not establish absence of regressions. Retention is
a bounded engineering decision based on removing redundant mutations, the
observed create/write benefit and the successful safety gates, not a precise
latency promise. Physical-host scheduling and PFS3 variation are uncontrolled.

## Mechanism and boundaries

The portable core owns the host-order three-field stamp, its on-disk encoder,
and operation-scoped create/write entry points. The non-failing callback fully
initializes the stamp under the exclusive filesystem lock; it must not reenter
BFS or mutate BIO/cache state. Existing APIs remain NULL/zero wrappers. Linux
FUSE uses those wrappers and keeps its existing timestamp policy. No inode
metadata pointer or policy state is cached in a persistent file handle.

Create samples after parent validation and inode reservation, before the first
insert. Final write reads the fresh inode and overlays size, extent root,
modification fields and only the requested protection-clear mask. Intermediate
commits do not sample or apply the optional metadata. Zero progress and
read-only/snapshot/preflight rejection do not sample it. A late duplicate-name
create can sample before the later directory insertion rejects the name.
Amiga still checks protection freshly; dirty/notification flags are set after
a positive combined result.

Sampling moves from after a redundant inode mutation to immediately before the
necessary publication: create records creation-start rather than the later
second update, and write records completion just before its inode update. Date
encoding and the 16-bit conversion remain unchanged. The removed independent
timestamp failure point changes one error-notification side effect: formerly
MarkFileChanged could set dirty/notify before its separate update failed;
combined publication failure now returns an error without those new flags.
Successful-return behavior is retained. No hardware-clock or external OS
notification test is claimed.

All old-root protection, CRC/structural validation, COW writes, pending/free
guards, commit points, fences and flushes remain. An uncertain transaction is
rejected/recovered before optional metadata publication. Combined inode-write
failure follows the existing reload-committed/error path, not success with
uncertain metadata. A positive prefix receives metadata only when its final
inode publication succeeds.

Two separately verified probes show create inode writes 128→80 and total node
writes 690/691→548. Large-write inode writes fall 150/151→129 and total node
writes 421/422→401. The reduction is not 128 writes: identical-update elision
already skipped many legacy same-tick changes. Delete inode writes stay at 40;
its reserve-refill difference is order-sensitive. Commits, sealed metadata
fences, superblock publications and BIO updates are unchanged. The
[probe summary](evidence/bfs-inode-stamp-2026-10-01/probe-summary.md) records exact
counts and non-additive instrumentation limits. Probe elapsed time is not used
for acceptance.

## Qualification

Independent production and test review found no blocker. Seven functional
cases cover exact encoding/remount, legacy/NULL equivalence, limited fresh
metadata updates, zero/preflight/read-only rejection, snapshots and open-unlinked
inodes. Four fault/ordering cases cover full and prefix-only combined inode
BIO failures, exact committed graph/data restoration, stale-handle rejection,
a positive 4096-byte partial write, checker/remount integrity and actual
intermediate commits before the single final callback. Superblock BIO-magic
observations establish emulator/BIO ordering, not real-device flush honesty.
One narrow untested combination is a nonzero write whose first data BIO fails
before any progress; zero-progress/preflight and positive-partial paths are
covered separately.

The final source passes 48 host suites with 460 tests in normal and actual
ASan/UBSan builds, 70 quality tests plus shell/static checks, 20 Linux
conformance tests plus actual FUSE qualification, and normal/probe m68k builds.
Two independent clean m68k builds reproduce both hashes. The first focused
functional test had a wrong mask oracle; its failed log is retained. Correcting
the test mask to clear actually set bits precedes all final gates.

## Reproduction and evidence

Baseline production is unchanged from `8b35eec`; its normal handler SHA-256 is
`c4d4241a45b84c768ec9498e77144e650812e31576c952925f4ed1de65be0d5a`.
Candidate normal is
`572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`;
candidate probe is
`070b162d85ebbe2aa67774b48dea2543db7b0e864270c1c05096680498660389`.
The production-only zero-context patch replays against `2ef2317` using
`git apply --unidiff-zero`. Final source/test and unchanged input identities
are recorded alongside the logs.

All ten images were prepared before timing, using the unchanged normal guest
`1fccf207…`, fixed formatter, ROM/Workbench/PFS3, options=0, 4 KiB BFS geometry,
255.5 MiB partitions and Cachy A1200/68040 FS-UAE. The RDB requests 30 cache
slots. Preflight checks every handler/guest/PFS3 hash and finds no other guest
emulator/compiler; none was started concurrently by this workflow.

The last image transfer hit a tmpfs user quota despite free filesystem bytes;
no guest had started. Eight completed cache-hint images and two completed
post-buffer probes were moved to recoverable home-directory archives, with
before/after TSV hashes identical. A first post-buffer archive attempt used
wrong TSV filenames and stopped before any move. The corrected script accepted
the verified empty archive, moved only the two exact completed targets, and
the one partial image transfer resumed. The final input/idle preflight passed.

[Raw evidence](evidence/bfs-inode-stamp-2026-10-01/) preserves all twenty TSVs,
completion/phase files, emulator configurations/logs, build/test gates, failed
attempts, preparation/archive/resume logs, scripts, source patch, summaries and
SHA256SUMS. TSVs are unmodified; only copied log trailing horizontal whitespace
is removed. No HDF, binary or licensed asset is committed. No CI, push, PR,
merge, release or real-device power/controller/media qualification ran.
