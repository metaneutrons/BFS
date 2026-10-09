# BFS read, flush, I/O and CRC attribution on Cachy

All twelve predefined diagnostic starts complete and validate. Guest Read,
byte verification and explicit volume flush are measured separately, together
with BFS I/O and all-call CRC counters. No filesystem optimization is added
by this follow-up. The production handlers and prior cohorts remain unchanged.

The 8MiB BFS read spends 86.79–89.22% of its instrumented work interval in
the guest's byte comparison; the grown-file read spends 81.44–84.08% there.
Whole read-phase time is not filesystem read time. Neither read phase performs
a volume flush. The 8MiB read has no node CRC calls; grown-file reading has
exactly one. Sparse and M5 have identical I/O/CRC call counts in every phase.

This does **not** clear the preceding production regression concern. Durable
grown-file work ratios are 0.9618 and 1.0309, rather than the previous 1.2503
median, but the actual Read ratios are 0.8731 and 1.1471. Durable 8MiB Read
ratios range from 0.9403 to 1.3468. Two pairs cannot establish equivalence or
show that earlier disadvantages were noise. The sparse candidate remains
provisional and the overall <=5x PFS3 objective remains open.

## Fixed scope and controls

The run inventory is fixed before execution: two M5/sparse pairs in normal
mode, two in durable mode, and one identical-M5 control pair per mode. Main
repeat 1 uses BFS-first and M5-before-sparse; repeat 2 reverses both orders.
The [schedule](evidence/bfs-read-flush-cachy-2026-10-09/results/schedule.tsv)
contains exactly twelve starts. Every start contains all 23 checked workloads
for both BFS and PFS3; none is filtered or retried.

Cachy uses FS-UAE 3.2.35, A1200/68040-max, 2MiB chip and 8MiB fast memory,
fresh format-v3 images and actual RDB capacity 30. The new split guest alone
uses `Stack 32768`. Both BFS diagnostic probes use ABI16 with CRC stride 1;
the default stride 64 and production binaries are unchanged. This is a new
instrumented cohort, not another production-performance qualification. Do not
pool it with the earlier Cachy production cohorts or the local Mac checks.

`WORK_US` includes the workload and operation-timer overhead. The original
durable volume-flush packet is recorded as `VOLUME_FLUSH_US`; normal mode
adds none. `US` is their sum, excluding probe reset/snapshot gaps. Guest
Open/Read/Write/Flush(handle)/Close and byte comparison remain separate.
All data, EOF and Close checks are retained. A handle Flush is not a volume
flush. Device/data, CRC, core and packet tick intervals are inclusive and
nested: do not sum them or report differences as exclusive CPU time. Zero
ticks with positive calls do not establish zero cost in this emulator.

## Read decomposition

These ranges include all twelve BFS runs, including the four identical-handler
controls. They are descriptive ranges, not confidence intervals.

| BFS workload | Read calls | Verify calls | Read ms | Verify share of work | Work device/data reads | Read CRC calls | Volume flush |
| --- | ---: | ---: | ---: | ---: | --- | ---: | ---: |
| 8MiB | 129 | 128 | 26.159–35.264 | 86.79–89.22% | 128 / 128 | 0 | 0 |
| Two grown files, 1280KiB total | 22 | 20 | 5.696–7.485 | 81.44–84.08% | 51 / 50 | 1 | 0 |

Neither workload writes, updates the device, commits a transaction or computes
a write CRC. The extra device read in the grown-file phase is metadata, not
an extra data read. The normal Mac check previously recorded 50 device reads
and no read CRC; that single-run observation must not replace these Cachy
counters.

Every pair below is sparse/M5; 1 means equal. The filesystem order and handler
order are reversed together between repeats, so their effects cannot be
estimated independently from this small diagnostic inventory.

| Mode / repeat / filesystem first | 8MiB work | 8MiB Read | 8MiB Verify | Grown work | Grown Read | Grown Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Normal 1 / BFS | 1.0535 | 1.1422 | 1.0426 | 1.0615 | 1.0089 | 1.0639 |
| Normal 2 / PFS3 | 1.0192 | 1.0185 | 1.0146 | 1.0144 | 1.0280 | 1.0177 |
| Durable 1 / BFS | 1.0108 | 0.9403 | 1.0210 | 0.9618 | 0.8731 | 0.9838 |
| Durable 2 / PFS3 | 1.0790 | 1.3468 | 1.0497 | 1.0309 | 1.1471 | 1.0044 |

In durable repeat 2, 8MiB Read rises from 26.183 to 35.264ms while inclusive
device-read time rises from 12.628 to 20.378ms. There is no CRC or I/O-count
increase to explain it. This associates much of the changed interval with
device-read timing; it does not establish a host-latency cause or exclusive
filesystem CPU cost. Grown-file Read rises from 6.079 to 6.973ms in that pair;
device-read time rises from 2.674 to 4.283ms, with the same single CRC call.
Nested timers cannot be subtracted into a causal decomposition.

Identical-M5 controls are M5-B/M5-A, not sparse/M5:

| Control mode | 8MiB work / Read / Verify | Grown work / Read / Verify |
| --- | --- | --- |
| Normal | 0.9953 / 0.9449 / 1.0020 | 0.9903 / 1.0559 / 0.9949 |
| Durable | 0.9828 / 1.0699 / 0.9690 | 1.0057 / 1.0089 / 1.0155 |

The independently unchanged PFS3 calibrator also changes between runs. Its
8MiB Read sparse-run/M5-run ratios are 1.1338/1.1132 in normal mode and
0.8860/1.1789 in durable mode; grown-file ratios are 1.0389/1.0637 and
0.7365/1.0840. All calibrators remain in the summary; none is used to erase
a slower BFS result. Across these diagnostics, within-run BFS/PFS3 Read-call
ratios span 1.034–1.978 for 8MiB and 1.511–2.381 for the grown files. These
instrumented observations do not qualify production parity.

## Append work versus explicit volume flush

Both append workloads create a new file and grow it through 256 writes; they
do not append to a pre-existing file. The durable paired timings are:

| Workload / repeat | M5 work ms | Sparse work ms | Work ratio | M5 flush ms | Sparse flush ms | Flush ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 4KiB growth / 1 | 95.679 | 104.383 | 1.0910 | 1.983 | 1.920 | 0.9682 |
| 4KiB growth / 2 | 94.142 | 100.159 | 1.0639 | 1.857 | 1.920 | 1.0339 |
| 1KiB growth / 1 | 80.960 | 81.664 | 1.0087 | 1.984 | 1.889 | 0.9521 |
| 1KiB growth / 2 | 112.448 | 85.119 | 0.7570 | 1.857 | 1.856 | 0.9995 |

The durable 4KiB disadvantage is in work, not a consistently slower explicit
volume flush. Work performs 256 data/device writes and no device reads,
updates, node writes, transactions or CRCs. Flush performs five device writes,
three updates, four node writes/write CRCs, one transaction and one flush
packet, with no data writes. These counts are identical in both variants.
The 1KiB work additionally performs 64 data/device read-modify-write reads;
its flush has two metadata reads and the same writes/updates/CRCs as above.

In normal mode, 4KiB work still has 256 data/device writes and no CRC; 1KiB
work has 66 device reads, 64 data reads, 261 device writes, 256 data writes,
three updates, four write CRCs and one commit. The metadata commit occurs
inside that later work interval, not a separately requested volume flush.
Normal volume-flush counters and times are zero throughout.

Create 40 durable work ratios are 1.0165/1.0111 and flush ratios 1.0673/1.0830.
Both variants have 40 data writes during work and ten device writes, three
updates and nine write CRCs at flush. The identical-M5 durable create control
varies by 1.5596 in work and 2.1565 in flush despite equal counts. That is an
observed control fluctuation, not a bound on noise or proof against regression.

No unequal cell exists in all 1,840 primary comparisons: 23 phases × two
scopes × ten I/O/CRC call counters × four sparse/M5 pairs. Timing differences
must not be described as extra I/O or CRC work in this cohort.

## Next bounded investigation

Further CRC optimization cannot directly accelerate the measured 8MiB Read
or 4KiB growth work: neither interval calls a node CRC. The 4KiB write path
is a more relevant next attribution point. For durable repeat 2 it records
256 core writes, 516 B-tree searches, 258 index-hint hits, 515 logical inode
read calls and 256 extent maps in both variants. These logical calls are not
disk reads: work has zero device reads. Inclusive core-write time is
68.914/75.479ms, packet-write time 83.856/89.490ms and device-write time
11.964/18.088ms for M5/sparse. Inspect repeated inode/extent lookup and device
timing separately before selecting another optimization; do not add or
subtract those nested budgets to claim exclusive cost.

The current evidence is sufficient to choose that focused investigation,
but not to approve the sparse candidate or close the <=5x PFS3 objective.
No such code change or additional benchmark series is performed here.

## Verification, preservation and restoration

The [text evidence bundle](evidence/bfs-read-flush-cachy-2026-10-09/README.md)
retains every raw TSV, completion/mount record, config, startup sequence,
RDB inspection and run log. All before/after handler, guest and PFS3 receipts
match their pinned inputs; all 67 non-handler asset path/hash mappings match
across all twelve runs. All 223 exported source/harness entries still match
after execution. Locally, 222 of those 223 entries match the export. The sole
difference is `tests/test_perf_probe.c`: the local file already contains the
additional cached/uncached CRC sampling assertions recorded in the preceding
local report; the earlier export does not. Both versions and their diff are
retained. The test is not part of the measured handler or guest build. All
runtime sources, headers and harness entries match; no source changed during
the remote series. The full validated summary retains every phase, raw counter,
timer, primary pair and control pair. Licensed assets, binaries and HDFs are
not copied into this repository bundle. Format logs are losslessly encoded
and their decoded hashes verified.

All 21 summary oracles, 26 split-verifier tests and 66 existing verifier tests
pass locally; runner shell syntax and ShellCheck pass. Luna implemented the
bounded summary support and independently checked all 24 raw BFS/PFS3 outputs,
read-field extraction and summary reproduction. The root agent checked the
reported timings and counter comparisons separately. No CI was dispatched.

An initial Docker preflight used a `docker top` format without its required
PID field. It failed before stopping any service or starting any benchmark;
its log is retained. The corrected preflight includes PID and refuses an
active Worker. After measurement, the summary initially rejected asset-receipt
ordering because Cachy's `en_US.UTF-8` sort differs from Python ASCII sorting.
Receipt order is not an identity property. The checker now compares exact
path/hash mappings and still rejects malformed, duplicate, missing or changed
entries; all raw receipts remain unchanged. Both are recorded diagnostic-tool
corrections, not omitted runs or benchmark retries.

The authorized idle Docker runner and Coffin were stopped for this inventory.
The series ran from 17:32:42 to 17:40:51 UTC on 9 October 2026. Restoration
completed at 17:40:54 UTC with status 0. Fresh process/service checks confirm
the Docker listener and Coffin running again, no benchmark emulator or Worker,
and both system CI units still loaded/inactive/dead with PID 0. There was no
commit, push, merge, issue change or filesystem optimization in this follow-up.
