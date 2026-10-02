# Rejected native-word-order Amiga CRC32 experiment

The candidate is rejected. The five-times PFS3 goal remains unachieved.
Both assembly revisions pass their real Amiga result/ABI probes, but a faster
standalone timed loop does not translate into a demonstrated normal-handler
benefit. Every matched 8 MiB write is slower. The original production assembly
is restored, and a clean rebuilt handler is byte-identical to the retained
timestamp baseline. Tests and complete evidence are kept, not a production
speedup claim.

## Normal workload decision

Four fresh normal runs per revision use the unchanged schema-1 guest. Values
are microseconds; the ratios below divide candidate BFS and PFS3 four-run means.

| Workload | Baseline BFS | Candidate BFS | BFS elapsed change | Candidate PFS3 | BFS/PFS3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 190690.25 | 217262.25 | +13.9346% | 18979.00 | 11.447508 |
| Lookup 400 | 112344.00 | 128687.50 | +14.5477% | 86991.50 | 1.479311 |
| Small read 40 | 47071.75 | 51026.25 | +8.4010% | 17904.25 | 2.849952 |
| Write 8 MiB | 363038.25 | 391277.75 | +7.7787% | 40941.00 | 9.557113 |
| Read 8 MiB | 504655.75 | 512493.50 | +1.5531% | 276609.00 | 1.852772 |
| Delete 40 | 180191.00 | 170223.25 | −5.5318% | 8496.25 | 20.035104 |

Explicit same-order/repeat candidate-minus-baseline BFS deltas, in BFS-first,
PFS3-first, repeat BFS-first, repeat PFS3-first order:

| Workload | Four deltas (µs) | Adverse pairs |
| --- | --- | ---: |
| Create | +95359, −6336, +18610, −1345 | 2/4 |
| Lookup | −18495, +14432, +1470, +67967 | 3/4 |
| Small read | +2304, −1024, −5238, +19776 | 2/4 |
| Write | +72256, +19327, +5760, +15615 | 4/4 |
| Read | +13887, −8266, +55872, −30142 | 2/4 |
| Delete | −31038, −26432, −6208, +23807 | 1/4 |

There are 14/24 adverse matched BFS observations. All four same-run candidate
create, write and delete ratios exceed five: 12/24 threshold failures. Other
checked ratios stay below five. The JSON retains all 24 ratios and both
filesystems' paired changes, not only the aggregate means. Independent Luna
extraction read all sixteen normal TSVs before the primary summary existed;
all raw tuples, means, matched BFS deltas and ratios agree.

The sequence was baseline BFS-first, candidate PFS3-first, baseline PFS3-first,
candidate BFS-first, repeat candidate BFS-first, repeat baseline PFS3-first,
repeat candidate PFS3-first, repeat baseline BFS-first, then candidate probes
in BFS-first/PFS3-first order. All ten fresh systems and twenty filesystem
outputs pass completion, data/EOF and strict schema/counter verification.
No adverse run was discarded or rerun away. The sizeable PFS3 variation and
uncontrolled physical-host scheduling preclude causal/statistical precision;
this is a conservative rejection of unproven benefit, not proof that the
algebraic transformation itself causes every slowdown. Do not pool probe or
microtimes into normal elapsed measurements.

## Assembly and actual-kernel pilots

The retained kernel already uses slicing by four. The experiment keeps four
immutable 1 KiB tables, all checksum bytes, the polynomial, complements,
seed/chaining API and 68020+ instruction/unaligned-load contract. Internal CRC
state and table results are byte-swapped into native big-endian word order.
It removes three word-order instructions per full input word, but introduces
entry/return conversions. No format, cache, ownership, COW count, allocation,
flush, fence, recovery or feature-policy change is part of this experiment.
The portable C and zero-byte-advance kernels are unchanged.

Pilot 1 SHA256: `a3dac0a8f33589b350fb11928445adb3223562c6bdce2f9de33f2c23820d92cf`.
Pilot 2: `46b89a015bd771dbdc48acdedc4765e07ed2567710bf09eb228d0d139d370af6`.
The second changes only empty-span return before the save/setup frame, reuses
caller-saved D1's loaded nonempty length, and removes the late zero check.
Body and all tables are otherwise byte-identical. Independent source reviews
find no blocker in either frozen revision.

Each actual Amiga probe executes both kernels: 17,024 small vectors (four
patterns, eight offsets, lengths 0–132, four seeds); 320 large boundary vectors
(ten lengths near 4/64 KiB, eight offsets, four seeds, mixed pattern); 17,024
small-chain cases; 84 large-chain cases (three lengths, valid splits, four
seeds, mixed pattern, fixed offset three); four NULL/zero cases and the known
IEEE vector. Each kernel has 51,565 checked calls and zero CRC/ABI failures.
Both deliberately register-clobbering controls require and return exactly
D2 mask 0x4 or A2 mask 0x100 with preserved result/stack.

Expected CRCs use an independent bitwise oracle, not the baseline output as
the sole oracle. Source-derived totals are 34,597 oracle invocations over
19,238,465 bytes; those two totals are not separately runtime-reported counters.
Large-pattern and large-chain offset limits are explicit; full UINT32-length
buffers, physical CPUs and arbitrary destructive SP damage are not tested.
The witness checks callee registers, frame guard and balanced normal returns;
unsafe SP corruption can prevent return or safe frame access. Negatives cover
register damage, not such SP damage. The runner explicitly sets Stack 32768
for the fixed 8 KiB report buffer and additional 768-byte tick arrays; a default-stack standalone run is not
qualified. Exact report write/close and done publication checks pass.

Both pilots measure eight lengths with six pairs each, balanced 24/24 by
call order at 709379 Hz. Each kernel makes 651,480 timing calls; no batch
checksum mismatches occur. Values below are candidate-versus-baseline elapsed
changes for inclusive EClock loops with return-checksum work, not pure kernel
latency or filesystem throughput.

| Bytes | Pilot 1 change | Pilot 2 change | Pilot 2 faster samples |
| ---: | ---: | ---: | ---: |
| 0 | +12.3776% | −46.7219% | 6/6 |
| 1 | +14.2160% | +11.7299% | 0/6 |
| 3 | +13.4317% | +9.6044% | 0/6 |
| 4 | +1.3004% | +2.5722% | 2/6 |
| 44 | −7.1874% | −8.4785% | 6/6 |
| 256 | −9.0027% | −9.4422% | 6/6 |
| 4096 | −9.4204% | −9.6487% | 6/6 |
| 65536 | −9.4445% | −11.2582% | 6/6 |

The independent reviewer extracts both complete raw reports before seeing
the primary JSON; all counts, markers, tick arrays, means and signs agree.
The aggregate XOR checksum is zero for matching pairs and is not an additional
correctness oracle. Volatile calls/sink, per-batch comparisons and separate
functional oracle checks are the actual controls. The pilot measures only one
timing alignment and seed and one binary layout per revision; those limitations
do not disappear because the functional vectors cover more alignments/seeds.

## Mathematical evidence, other gates and restoration

An independent polynomial-derived model reconstructs all 1024 original table
constants and the portable C base table, then validates the proposed transformed
recurrence with 46,048 bitwise-oracle cases, including seed-basis and chaining
cases. It reads the retained assembly, not the candidate instructions. Its
16 conceptual NULL/zero checks do not execute actual machine memory accesses.
The original model and log are preserved. After production editing, the primary
adds only an input-path override; explicitly rerunning against the retained
baseline produces the identical passing log. Actual instruction/ABI evidence
comes from the two Amiga runs, not this model. A reviewer initially mixed those
scopes; the retained review record includes the clarification.

Normal and actual ASan/UBSan host regressions pass 48 suites/460 cases each;
leak detection is disabled. Quality/static passes 78 tests, ShellCheck,
actionlint and clang analysis. Linux conformance passes 20 tests plus the actual
FUSE qualification. These use unchanged portable production C; they do not
execute the changed Amiga assembly. Normal/probe/guest Amiga binaries reproduce
byte-for-byte in two clean build directories. Independent actual-assembly and
harness reviews plus the real Amiga vectors/ABI and mounted outputs cover the
changed target. No new physical-hardware qualification is claimed.

The first harness build fails under Werror on a uint32_t*/ULONG* mismatch;
the author changes only the run_timing parameter to ULONG*. The failure log
is preserved. Its exact original source is reconstructed by that single inverse
edit and verified against the original frozen SHA; it is labelled reconstructed,
not an untouched pre-build snapshot. No compiler warning is suppressed to pass.
Separate assembler attempt 2 explicitly records command/exit/source/object;
attempt 1's empty stdout is preserved but is not an independent runtime gate.

Candidate normal handler:
`968f7d00f4aafafb832af70bff6b8c65b669a9dd9dd62f4e2f83414ceeb08b3e`.
Candidate probe:
`04b5b982291e772d9dd538114aaf4c256058affd7f34f16423242c6a2092df23`.
Probe guest:
`5126e8640256e19bc2f5661d8b735f7961c4882edf3d8046b26bd2badfc86277`.
Normal guest remains the retained schema-1 binary; it is never overwritten.

Restored assembly:
`5611527d918ac1196160dcec955e60045e25d41ef479340586d36470777dd090`.
Restored/retained normal handler:
`572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`.
Restoration uses a hash-guarded apply_patch of the known candidate file only,
not a destructive checkout. Exact source comparison and rebuilt binary comparison
pass. No other production changes are introduced by this increment.

## Evidence and scope

[Raw evidence](evidence/bfs-crc-native-word-order-2026-10-01/) retains all twenty
filesystem TSVs, ten configurations/completion records, both kernel reports
and exact markers, failed build, mathematical sources/logs, frozen source
snapshots, production patches, gates, input identities, extraction/preparation/
run/archive scripts, independent-review transcription and SHA256SUMS. The
transcription is identified as primary-authored, not a shell transcript.
Log trailing horizontal whitespace/final blank lines are normalized. In the
ten copied formatter logs only, Amiga C1 CSI byte 0x9b is mechanically converted
to its equivalent ASCII ESC-[ representation; original/output hashes are
retained and local originals are untouched. The first normalizer invocation
fails its directory-count assertion before any write; the corrected invocation
selects directories explicitly. Raw TSVs, kernel reports and patch context
bytes remain unchanged. No executable binary, HDF or licensed asset is committed.

The scoped repository asset audit passes for this increment. The global asset
audit still rejects older committed formatter text with the same C1 encoding
as unknown-8bit. This is an existing text-encoding limitation, not a passing
global gate or a reason to alter older evidence in this experiment.

Both kernel runs use isolated SYS directories without filesystem test partitions.
The ten filesystem systems use the retained ROM/Workbench/PFS3/formatter,
options=0, 4 KiB BFS blocks, 30 buffers and 255.5 MiB partitions on Cachy
FS-UAE 3.2.35 A1200/68040. Inputs were prepared before timing; no second emulator
or remote compiler runs concurrently. All twenty before/after archive TSV hashes
agree. Completed remote systems move recoverably to
`/home/fabian/.cache/bfs-performance/crc-native-order-2026-10-01/`;
original local systems remain. No CI, push, PR, merge, release, external
publication or real-media/power/controller qualification is performed.
