# BFS CPU and heap attribution

The overall ≤5× PFS3 goal remains unachieved. This increment adds diagnostic
instrumentation only. The normal handler is byte-identical to the retained
sealed-settlement handler, SHA-256 `f3b593ad…`. Six fresh FS-UAE runs completed
with strict data, completion and counter verification. No speedup is claimed.

## Measured intervals and next experiment

The four new probe runs record a mean of 2283.75 direct B-tree malloc requests
while creating 40 files. Sequential direct malloc/free intervals consume
83.26 ms on average, versus 360.50 ms instrumented create elapsed time. Write
records 98.77 ms and delete 42.00 ms in these heap intervals. This identifies
repeated temporary-buffer allocation as a bounded optimization candidate, not
as a promise that all of that time can be removed. It is insufficient on its
own to meet the target. The next experiment is
[shared temporary block-buffer reuse](../plans/bfs-temporary-buffer-reuse-v1.md).

Means below are µs across four new probes; ranges retain every observation.
Heap counts combine direct malloc and free calls, not allocation pairs.

| Phase | Direct heap µs mean and range | Heap calls mean and range | Interface allocation µs | Free-space allocation µs | Device read write update µs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 83257.3 [77797.6–88673.3] | 4567.5 [4542–4590] | 97833.5 | 110054.7 | 49333.3 |
| Lookup 400 | 27000.4 [26096.1–28422.0] | 1600 [1600–1600] | 0 | 0 | 255.9 |
| Small read 40 | 12562.4 [12089.4–13541.4] | 800 [800–800] | 0 | 0 | 6878.6 |
| Write 8 MiB | 98769.1 [88139.1–112646.4] | 6820 [6796–6868] | 93939.2 | 72263.2 | 100691.9 |
| Read 8 MiB | 85062.4 [75678.9–94904.1] | 4620 [4620–4620] | 0 | 0 | 76250.8 |
| Delete 40 | 42002.2 [39335.8–44570.0] | 2582 [2528–2734] | 59596.5 | 58486.0 | 31192.1 |

These columns are **not additive**. Interface allocation includes its nested
Free-Tree, heap, CRC and device work. Public free-space allocation includes
metadata and payload extent requests and can lie inside interface allocation.
Direct heap intervals exclude caller copies/initialization and all other
translation units' heap use. Read/write/update calls are distinct device
operations, but their intervals may lie inside allocator scopes. CRC retains
stride-64 sampling; no sampled CRC total is extrapolated as an unbiased CPU
estimate. Large parts of elapsed time remain unattributed.

All CPU calls are timed (stride 1), avoiding fixed-stride aliasing with the
128-request workload. EClock is 709379 Hz in every new run. The per-phase
256-pair calibration averages truncate to 0 or 1 ticks. This is too coarse
to subtract exact overhead from the gross intervals; no correction is made.
Each wrapper includes clock-call overhead. Recursive reserve/emergency
interface callbacks are inside the outer interval, not separate samples.
Every public free-space call, including invalid/error returns, retains its
existing call-count semantics and now records its entire return path.

## Fresh controls and instrumentation limits

Run sequence was control BFS-first, control PFS3-first, then new probe
PFS3-first, BFS-first, repeat BFS-first and repeat PFS3-first. All partitions
were fresh; guests ran sequentially with no other emulator/compiler process.
Controls use the previous ABI-10 probe and schema-9 tool. New runs use ABI 11
and deep-compare schema 10. Formatter, PFS3 handler, assets, ROM, options=0,
255.5 MiB partitions, 4 KiB BFS blocks and Cachy FS-UAE 3.2.35 A1200/68040
configuration are unchanged. Host scheduling remains uncontrolled.

Two controls versus four new probes are a descriptive overhead check only,
not a randomized or statistically precise causal comparison. PFS3 itself
varies substantially. New/old BFS elapsed means change by +8.38% create,
+9.54% lookup, +3.11% small read, +1.74% write, +2.35% read and +10.23% delete;
the corresponding PFS3 means change +10.45%, +7.19%, +15.06%, +37.69%, +3.24%
and +13.72%. Do not interpret probe times as normal-handler acceptance.

All elapsed tuples below are µs in order create40, lookup400, small-read40,
write8MiB, read8MiB, delete40. Adverse samples are retained without reruns.

| Run | BFS elapsed tuple | PFS3 elapsed tuple |
| --- | --- | --- |
| Control BFS-first | 358525, 125823, 60671, 617660, 686333, 217215 | 16384, 66880, 15423, 43520, 282493, 8065 |
| Control PFS3-first | 306750, 124094, 59584, 495293, 596349, 210934 | 17280, 66687, 14335, 36104, 261374, 8052 |
| New PFS3-first | 381951, 134591, 58176, 523964, 717948, 253566 | 16960, 67904, 19970, 38400, 294142, 12736 |
| New BFS-first | 327371, 144191, 64703, 557245, 595516, 228670 | 24576, 83584, 14655, 43777, 264063, 8179 |
| New repeat BFS-first | 386238, 134527, 59585, 550334, 723708, 232061 | 15615, 68286, 19072, 66623, 271615, 7999 |
| New repeat PFS3-first | 346430, 134206, 65535, 633149, 588413, 229630 | 17215, 66560, 14784, 70463, 293169, 7744 |

## Verification and reproducibility

The source wrappers preserve allocator results, errors, state and ownership.
Only the B-tree translation unit wraps direct malloc/free; profiler heap
operations are not wrapped recursively. Normal host and ASan/UBSan runs each
pass 43 suites / 425 tests. They exercise unchanged production code, not the
Amiga probe wrappers. Probe verification instead includes independent wrapper
review, strict m68k compilation and four real checked guest workloads.
Leak detection is explicitly disabled for this macOS sanitizer setup.

Local quality checks pass 70 tests, ShellCheck and static analysis. Schema 10
requires every emitted CPU counter, stride exactly 1, exact samples=calls and
zero ticks when samples=0. Missing, duplicate, malformed, stride and each
category's inconsistent samples/ticks have negative tests. All older schema
4–9 behavior remains supported. Schema 10 also retains the complete v9
settlement equations and v7/v8 run/batch/classification gates; specific negative
tests cover their inheritance. A retained-fixture test checks all six real
outputs. The regular compare schema 1 and workloads, and profile schema 1,
remain unchanged.

[Raw evidence](evidence/bfs-cpu-attribution-2026-10-01/) includes all TSVs,
machine inventories, completion markers, configurations, emulator/runner logs,
build/test logs, source and binary identities, and SHA256SUMS. Copies of logs
have only trailing horizontal whitespace removed. Normal handler `f3b593ad…`,
new probe `95dcb48e…` and tool `1fccf207…` bind the measured source. Controls
use probe `2154d5ad…` and tool `afc18697…`. Full SHA-256 values and fixed input
identities are in `input-identities.log`.

No ROM, Workbench binaries, HDFs or handler binaries are committed. No CI,
push, PR, merge, release or hardware qualification was performed.
`coffin.service` remains stopped, not disabled, for continued qualification.
