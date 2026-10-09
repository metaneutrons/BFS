# BFS read and flush diagnostic profile

Read calls, byte verification, handle operations, volume flushes, device I/O
and node CRC work are now recorded separately. Two complete local diagnostic
runs pass the strict protocol checks. In these instrumented runs, the byte
comparison in the guest accounts for about 92% of the 8MiB read phase and
86–87% of the grown-file read phase. Whole read-phase time is therefore not
a direct measurement of filesystem read performance.

This does **not** resolve the preceding Cachy sparse-CRC regression concern.
The local checks are one normal M5 run and one durable sparse run, not paired
M5/sparse comparisons. No production speedup or regression clearance follows
from them. The provisional sparse candidate and the <=5x PFS3 goal remain open.

## Measurement boundaries

The new `split-compare` and `split-durable-compare` modes retain the existing
23 checked workloads and their order. File data verification, EOF checks and
close checks remain mandatory. Guest wrappers time Open, Read, Write,
Flush(handle), Close and the original byte comparison independently. They
are active only inside the timed workload, not fixture setup or TSV output.
There are 129 Read calls and 128 comparisons for 8MiB, and 22 Read calls and
20 comparisons for the two grown files, including their EOF reads.

`WORK_US` includes the workload and per-operation timer overhead. A separate
`VOLUME_FLUSH_US` covers the same `ACTION_FLUSH` that the original durable
workload requested. Probe reset/snapshot gaps are excluded from the diagnostic
sum `US = WORK_US + VOLUME_FLUSH_US`. Normal mode adds no volume flush. Read
phases add none in either mode. A client `Flush(handle)` is not equated with
a handler volume-flush packet.

BFS resets and captures its probe separately for work and volume flush. The
schema includes device/data I/O counts and ticks, node CRC calls/samples/ticks,
packet/core scopes and lookup counters. CRC sampling is explicitly changed
from the default 1:64 to 1:1 in separate diagnostic builds. Probe ABI16 and the
default sampling stay unchanged. Packet/core, device and CRC intervals are
inclusive and nested: **do not add them or infer exclusive CPU time**.

Zero elapsed microseconds or zero EClock ticks with positive call counts are
observable in this emulator, including one read CRC in the durable grown-file
read. They do not prove zero execution cost. Every call is sampled, but short
intervals remain limited by timer resolution and observer overhead.

## Local read decomposition

These are single-run diagnostics on the Mac, FS-UAE 3.2.35, A1200/68040-max,
the existing licensed assets, format v3 and actual RDB capacity 30. Both runs
use BFS-first ordering. The successful normal check used the CLI default
stack; the durable check uses the new split-only `Stack 32768` safeguard.
The Mac formatter has a different executable identity from the Cachy
formatter. These runs must not be pooled with the prior Cachy production
cohorts or used as balanced handler comparisons.

| BFS run and workload | Work ms | Read calls ms | Verify ms | Verify share | Volume flush ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Normal M5, 8MiB read | 111.999 | 8.623 | 102.744 | 91.74% | 0 |
| Durable sparse, 8MiB read | 112.319 | 7.727 | 103.340 | 92.01% | 0 |
| Normal M5, grown-file read | 18.367 | 2.115 | 15.937 | 86.77% | 0 |
| Durable sparse, grown-file read | 18.176 | 2.044 | 15.682 | 86.28% | 0 |

The 8MiB BFS read makes 128 data/device reads in both runs and no node CRC
calls. Grown-file reading makes 50 data reads in both runs: normal has 50
device reads and no read CRC; durable has 51 device reads and one read CRC,
whose sampled duration is zero ticks. Neither read phase writes, updates
the device, commits a transaction or calls a volume-flush packet.

For context within each instrumented run, PFS3 Read-call totals are 5.226ms
and 5.595ms for 8MiB and 0.895ms and 0.579ms for the grown files, respectively.
Its verification shares range from 93.69% to 94.74%. Even these operation-only numbers are
diagnostic observations, not a new PFS3 parity qualification.

The guest byte comparison is a plausible contributor to variability in the
old whole read-phase values, but this dataset does not establish the cause
of the earlier 25% Cachy disadvantage. Keep read-call and verification
intervals separate in the next same-host confirmation; do not weaken data
verification to obtain a faster result.

## Durable flush work

The durable sparse run assigns deferred metadata work to the explicit
volume flush instead of folding it into a write-phase total.

| Phase | Work ms | Volume flush ms | Flush device writes | Flush updates | Flush write CRC calls | Inclusive write CRC ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 15.104 | 1.471 | 10 | 3 | 9 | 0.544 |
| Write 8MiB | 58.751 | 0.640 | 6 | 3 | 5 | 0.257 |
| Delete 40 | 8.577 | 0.896 | 5 | 3 | 4 | 0.065 |
| Grow in 4KiB steps | 42.111 | 0.704 | 5 | 3 | 4 | 0.128 |
| Grow in 1KiB steps | 30.463 | 0.832 | 5 | 3 | 4 | 0.127 |

Each recorded volume flush has exactly one flush packet. The 4KiB workload
performs 256 data writes during work, with no node CRC or device update there;
its four node writes/CRCs and three updates occur in the separate flush.
The 1KiB workload adds 64 read-modify-write data reads during work. The normal
run has zero volume-flush intervals and counters, as required by its contract.

## Verification and retained evidence

The [evidence bundle](evidence/bfs-read-flush-profile-2026-10-09/summary.json)
contains every phase and counter from both complete runs, their raw BFS/PFS3
TSVs, completion and mount records, startup scripts, RDB records, emulator
logs, source snapshots and input digests. The summary validates raw inputs
before calculating diagnostic shares; it does not filter phases or outliers.

The first local attempt reset the emulator and subsequently overwrote its BFS
output with `FAIL mkdir`. The verifier rejected it. Its remaining raw outputs
and logs are retained under `failed-stack-guest`, outside the two valid runs.
The split snapshots were then moved to serial static scratch storage; the
compiled phase frame decreased from 2784 to 1960 bytes. The following normal
run completed. An explicit 32KiB stack now applies only to the new split modes.
The exact reset cause is not established by this sequence.

There are 26 new strict-parser tests and 66 existing verifier tests, all
passing. Six host probe oracles pass independently with default sampling and
all-call sampling; they cover cached write CRCs and actual uncached read/write
CRCs, including exact sample counts and the synthetic tick oracle. An initial
positive read-CRC assertion incorrectly used a warm cache and was replaced
with a separate uncached oracle. Warning-clean m68k builds and shell syntax/
ShellCheck checks pass. Luna implemented the focused parser tests and reviewed
workload attribution and static-snapshot safety independently. A temporary
parser/test key-prefix mismatch was corrected before the accepted local runs.
This is diagnostic qualification, not another full filesystem test campaign.

Both production handlers and both existing default probes reproduce their
previous binary identities exactly. Only diagnostic inputs are new:

- M5 all-call probe: `4818ea526841a3a253cb437f925f949fcd14b9a5d780169b93a31bd8958c9277`.
- Sparse all-call probe: `33a77819f0f7a5e6a3b4dbe9fd4d2ae2367f51f03ce910bf5be0957ccf4f7141`.
- Accepted split guest: `81c98af386792325a6e85f5debc6450895a48c76910816edae8716e1053d6bb4`.

Build these disposable inputs with `AMIGA_CC='m68k-amigaos-gcc
-DBFS_PERF_CRC_SAMPLE_STRIDE=1'` for both `amiga-perf-probe-handler` and
`amiga-fs-compare-bench`, using fresh isolated build paths. Do not substitute
the new guest for the pinned production reference guest in the earlier cohorts.

## Subsequent same-host diagnostic

The separately authorized twelve-run Cachy inventory has now completed.
Its [report](bfs-read-flush-cachy-2026-10-09.md) and separate evidence bundle
retain every paired Read/Verify/flush/I/O/CRC result and identical-M5 controls.
The idle Docker runner and Coffin were paused for measurement and restored
afterwards. No CI, commit, push, merge or issue mutation was performed.

This report and its sealed local Mac bundle remain the preceding protocol
checks, not part of the Cachy cohort. The original bundle's README and runner
remain an immutable record of preparation before authorization; their pending
status is historical. The Cachy diagnostic does not establish equivalence or
clear the preceding sparse-candidate performance concern.
