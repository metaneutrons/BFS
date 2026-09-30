# Immutable nibble CRC transforms — 2026-09-30

## Result and acceptance target

The overall target remains ≤5× PFS3 in every checked AmigaDOS workload with
unchanged guarantees. It is **not achieved**. This increment changes only
the exact zero-byte CRC state-advance computation and its tests. It does not
reduce COW counts, change checksum coverage or alter commit frequency.

Eight normal-handler runs compare four fresh images per version, interleaved
in both filesystem orders. All samples are retained. Times are microseconds.

| Workload | Baseline BFS mean | Candidate BFS mean | Change | Candidate PFS3 mean | Ratio of means | Candidate range | Runs >5× |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 492603.75 | 442285.5 | -10.2% | 17264.5 | 25.62× | 23.90–29.21× | 4/4 |
| Lookup 400 | 144272.25 | 124971.5 | -13.4% | 73024.25 | 1.71× | 1.38–1.89× | 0/4 |
| Read 40 | 61580.5 | 58815.5 | -4.5% | 20927.75 | 2.81× | 1.96–4.08× | 0/4 |
| Write 8 MiB | 551692.5 | 516764.5 | -6.3% | 62540 | 8.26× | 5.21–13.63× | 4/4 |
| Read 8 MiB | 627279.75 | 599580.25 | -4.4% | 286494 | 2.09× | 1.77–2.29× | 0/4 |
| Delete 40 | 434765.5 | 388237.75 | -10.7% | 8096 | 47.95× | 46.31–49.82× | 4/4 |

Create is faster in three of four order-matched pairs, delete in all four,
but large write in only two. The baseline 645500 µs write remains included.
PFS3 write samples 77234 and 93311 µs in candidate runs substantially lower
the apparent BFS/PFS3 gap; they are not BFS gains. Candidate write times are
485885–556348 µs and every one still fails 5×. Read/lookup computation is
unchanged, so its mean changes also demonstrate material runtime variation,
not an attributable CRC improvement. Four samples do not establish precise
percentages, a confidence interval or a general absence of regressions.
The bounded CRC substitution is retained, not a promise of those mean gains.

The [complete measurement summary](evidence/bfs-crc-nibble-2026-09-30/measurement-summary.md)
contains all raw timing tuples, paired deltas and diagnostic results. Cachy,
FS-UAE 3.2.35, A1200/68040, Kickstart 47.102, Workbench 47.2 and the fixed
formatter are unchanged from the
[atomic run-carving comparison](bfs-root-shift-performance-2026-09-30.md).
Each run uses equal fresh 256 MiB partitions. No other guest emulator/compiler
ran; the physical KVM host's scheduling is not controlled. No cross-host or
physical-hardware performance qualification is claimed.

## Exact computation and memory tradeoff

Each byte-length power retains the same IEEE reflected CRC-32 linear map.
Instead of testing up to 32 individual state bits and XORing columns, combine
eight independently transformed input nibbles with eight fixed lookups/XORs.
The immutable table is 32×8×16 uint32 entries, derived by applying the
polynomial `0xEDB88320` to basis states and repeatedly squaring the zero-byte
map. There is no mutable initialization, heap allocation or I/O.

Complement conventions and all uint32 byte lengths, including zero and
UINT32_MAX, are unchanged. The stored node checksum is still the exact
full-block CRC; arbitrary legacy unused bytes remain validated by the existing
read path. No format, feature, CLI, durability, snapshot, flush or error policy
changes. The extra static data is **12 KiB**: 16 KiB versus 4 KiB. The stripped
normal handler grows from 115828 to 128232 bytes (+12404 including code/layout).
Real machines with different cache/memory configurations remain unqualified.

## Diagnostic limits

The two deep runs both pass the strict v8 verifier. Create/write/delete emit
865/432/604 metadata nodes in the baseline and 875/433/606 in the candidate;
Free-Tree nodes are 626/147/488 versus 632/146/490. Timestamp and allocation
history change these small differences; this CRC-only change claims no
structural node reduction. Publications remain **120/3/120**.

Sampled CRC-write ticks per sampled call for create/write/delete are
730/13, 886/6, 435/9 in the baseline and 832/13, 649/6, 592/9 in the candidate
(clock 709379 Hz, stride 64). Only the large-write sample mean is lower;
create/delete samples are higher. Fixed-stride samples have different node
populations and are not an unbiased total-CPU estimator. Instrumented elapsed
times are also higher in the candidate run and are retained, but are not
normal-handler timing evidence. These observations limit attribution; they
do not justify a uniform CRC speedup claim or ignoring the normal outliers.

## Verification and provenance

Twelve CRC tests pass, including a runtime polynomial-derived matrix oracle
for every high power, basis states, UINT32 boundaries and composition. A new
public-API test exercises all **4096** nibble table entries independently.
The main agent additionally reconstructed all candidate constants from the
polynomial before integration. Independent Luna review found no shift,
indexing, complement or API-convention blocker.

The isolated positive CRC target passes 12/12. An isolated copy changes only
the non-basis entry `[power0][nibble0][value3]`, `0x990951ba` to `0x990951bb`.
Running only the all-nibble test fails at that test with exit 1, as required.
The primary agent reran both counterprobes and verified their exit codes;
no production table was damaged.

Complete normal and ASan/UBSan suites, local `make check`, B-tree compatibility
tests and normal/probe m68k builds pass. All ten fresh guest runs pass strict
mount/completion/data checks. Final rebuilt handlers match every measured
candidate image exactly. This is not a macOS LeakSanitizer result.

SHA-256 identities:

- Baseline core source: `5172505`; the intervening `dc9d3dc` changes only the SF plan.
- Baseline normal: `197c4faff73e2f7c3e7af60659a7654992417bdc1ace3c5ac4ae53a18a430556`.
- Baseline probe: `f5b907b2de11db8e1d7b045e79837d36a4d224f07e0798a7f10e428154b7bedb`.
- Candidate normal: `031b76eacf998a52e249cbfcea15ae20253c74e0243174de8951fc879a22ced9`.
- Candidate probe: `b974f66cda5d143154883979272ed50c2f2fd39536ba18bc48ab0f0c52fca3bd`.
- Candidate CRC source: `2c026e3a3d20910d2aaf6a0237342318a3d9aa3b496c0033168689e8df0dcd5f`.
- Candidate CRC tests: `f0a60e68fe4d4f3975a7f520f8e3e82a016d477f484c3cd7a5d026443869a4a0`.
- Fixed formatter: `0f87a0638791a4a2371083339ed014d4d6659dc85a2270bd993f9df1ddf13cc6`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

[Raw text evidence](evidence/bfs-crc-nibble-2026-09-30/) includes every normal
and diagnostic run, qualification/counterprobe logs, measured input identities
and SHA256SUMS. Copied trailing whitespace is normalized. No ROM, HDF or
licensed binary is committed. No CI, push, GitHub or hardware mutation ran.
`coffin.service` remains stopped for continued qualification, not disabled.
