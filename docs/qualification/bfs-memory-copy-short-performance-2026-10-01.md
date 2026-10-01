# Short Amiga memcpy qualification

The short-copy dispatch is rejected after normal filesystem comparison.
The isolated kernel passes data/ABI checks and has useful short-copy means,
but normal create/write slow in all four matched pairs (+10.43/+8.92% means).
A lower delete mean does not justify those regressions. Exact retained
assembly and the byte-identical normal handler are restored; additional
equivalent harness checks and rejected evidence remain. The overall goal
of at most five times same-run PFS3 in every checked workload is unachieved.

## Normal elapsed decision

Eight fresh normal systems provide four runs per revision, balanced by order
and repeat. Baseline is the qualified single-block-absence handler at local
checkpoint `4a80e0f`. All sixteen schema-1 raw TSVs pass the unchanged strict
data/completion verifier. Arithmetic means are microseconds; no sample is
trimmed and diagnostic probes are excluded from acceptance.

| Workload | Baseline BFS µs | Candidate BFS µs | Change | Candidate PFS3 µs | BFS/PFS3 mean ratio | Adverse matched pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 188302.50 | 207949.75 | +10.4339% | 17295.75 | 12.023170 | 4/4 |
| Lookup 400 | 118240.00 | 113771.25 | −3.7794% | 67295.25 | 1.690628 | 2/4 |
| Small read 40 | 53712.25 | 47535.25 | −11.5002% | 14704.00 | 3.232811 | 1/4 |
| Write 8 MiB | 325759.00 | 354814.00 | +8.9192% | 35712.50 | 9.935289 | 4/4 |
| Read 8 MiB | 542397.25 | 551279.75 | +1.6376% | 267329.50 | 2.062173 | 2/4 |
| Delete 40 | 182830.75 | 170949.50 | −6.4985% | 8064.25 | 21.198438 | 2/4 |

Fifteen of 24 matched BFS pairs are adverse. In initial BFS-first, initial
PFS3-first, repeat BFS-first, repeat PFS3-first order, create deltas are
+8257, +53248, +3647 and +13437 µs; write deltas +21438, +65152, +5311 and
+24319 µs. Delete deltas −5632, +411, −42432 and +128 µs show that its lower
mean is not a consistent per-pair improvement. Sequential read has two large
initial-order regressions (+59327/+155134 µs) and favorable repeat pairs
(−97342/−81589 µs). Every observation remains.

Create, write and delete exceed five times same-run PFS3 in every candidate
run: twelve of 24 ratios fail. Their ranges are respectively
10.439323–14.552912, 8.682327–11.525347 and 20.894909–21.590526.
Lookup, small read and sequential read have zero failures in this sample.
Ratios of means do not replace the individual-run gate. PFS3 mean drift is
+4.6436% create, +1.9382% lookup, +3.9630% small read, −0.5465% write,
−5.9807% read and −3.4395% delete. Four runs per revision with uncontrolled
scheduling do not establish causal or statistical precision. The results
do not isolate dispatch cost, code layout, scheduling or the reason that
the microbenefit fails to translate. They are sufficient to reject adoption.

Independent Luna extraction reads all sixteen raw TSVs before primary normal
JSON exists. All means, matched deltas, ratios and adverse/threshold counts
agree. Chronological order is baseline BFS-first, candidate PFS3-first,
baseline PFS3-first, candidate BFS-first, repeat candidate BFS-first, repeat
baseline PFS3-first, repeat candidate PFS3-first, repeat baseline BFS-first.
Both separate candidate probes also pass: ten systems/twenty TSVs total.
Every structural and other non-time counter agrees with retained absence-
shortcut controls in both orders. Those are not contemporaneous normal
timing controls. Inclusive wall scopes overlap and do not provide exclusive
CPU attribution. No claim that IO, CRC or memcpy alone explains the gap follows.

## Isolated kernel evidence

One standalone guest links exact retained and candidate assembly together.
All 8,908 cases per kernel pass: 8,512 lengths0–132 across all64 source/destination
offsets0–7, 384 cases at 4/64 KiB boundaries and12 large 44/48×65536 neighborhoods
across two explicit offset pairs. Each call checks independently generated
bytes, complete source/destination guards, unchanged source, exact destination
return and the reused normal-return register/SP witness. D2/A2-clobber controls
require masks0x4/0x100 and correct destination return; both pass. There are zero
ABI, return, data, allocation or timing-checksum failures.

The opt-in timing mode uses14 lengths, six alternating paired samples each:
84 pairs,42 per first-kernel order. Each kernel has14 one-call calibrations and
98 batches. Repeats sum to195454, so calls equal14+6×195454=1172738 per kernel.
Nine lengths clamp at20000 repeats. EClock frequency is709379 Hz; the target
is 20 ms based on the slower one-call calibration. Coarse zero-tick calibrations
and longer single-call calibrations at 32/48 bytes make that target imperfect;
the source of those longer observations is not isolated.

Arithmetic means below include indirect calls, loop/checksum work and possible
scheduling; they are not exclusive kernel latency or filesystem throughput.
Every sample, including adverse ones, remains in the raw record.

| Bytes | Retained µs/call | Candidate µs/call | Change | Faster/slower/tied pairs |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0.412802 | 0.283734 | −31.2664% | 6/0/0 |
| 1 | 0.458829 | 0.353596 | −22.9351% | 6/0/0 |
| 4 | 0.618123 | 0.446940 | −27.6939% | 5/1/0 |
| 8 | 0.497090 | 0.394794 | −20.5790% | 5/1/0 |
| 16 | 0.629870 | 0.473043 | −24.8984% | 6/0/0 |
| 32 | 0.645609 | 0.556263 | −13.8391% | 5/0/1 |
| 43 | 0.870374 | 0.772777 | −11.2132% | 5/1/0 |
| 44 | 0.694398 | 0.751996 | +8.2946% | 1/5/0 |
| 48 | 0.648216 | 0.699378 | +7.8927% | 0/2/4 |
| 264 | 1.784542 | 1.735473 | −2.7497% | 2/3/1 |
| 512 | 3.068887 | 2.868880 | −6.5172% | 1/5/0 |
| 4096 | 20.723631 | 19.911899 | −3.9169% | 4/2/0 |
| 65536 | 341.363337 | 316.480011 | −7.2894% | 3/3/0 |
| 1048576 | 5143.512847 | 4682.640262 | −8.9603% | 4/2/0 |

Overall53 pairs are faster,25 slower,6 tied. For unchanged≥44-byte bodies,
15 are faster,22 slower,5 tied. In particular, a lower512-byte mean conceals
five adverse pairs; unchanged-body gains are not attributed to the dispatch.
The44/48 regressions remain explicit. A single layout, fixed timing offsets3/5
and six observations do not establish causal or statistical precision. Useful
short means without adverse264/4096 means justify a bounded normal-handler
trial, not performance acceptance or a forecast of the five-times goal.

A separate Luna worker validates and extracts every raw count, calibration,
sample, repeat/clamp equation and mean before primary actual JSON exists.
All14 totals,1172738 calls, nine clamps and53/25/6 signs agree with the later
primary extraction. Raw report/marker SHA-256 are respectively
`a22e2753eb97e4a0a8a00521781439887daa5b6be10fed7359ce1bc01f369ed5` and
`1e57f53f9888fe32e54fdbd7b7ac7938cbdae0764baf60a32ca7dfb8e7550393`.

## Kernel and harness proof boundaries

The private candidate dispatches unsigned length below44 before saving eleven
callee-saved registers. The scalar path uses only D0/D1/A0/A1, leaves SP
unchanged, copies floor(n/4) longs plus n mod4 bytes and returns destination
from4(sp). Zero length has no data dereference. Length44 or above takes the
original save frame, offsets48/52/56(sp), full unsigned byte-count loop and
tails. All memset source remains unchanged. Independent source/disassembly
review finds no blocker. Actual emitted opcode decoding proves the original
84-byte large memcpy body and124-byte memset body are byte-identical; the
56-byte inserted entry still adds runtime dispatch overhead.

This is distinct from the rejected48-byte MOVEM-width experiment. No new
instruction set, required alignment, DMA/cache/heap contract, large payload,
shared-core algorithm, format, COW/CRC or durability policy is introduced.
Physical CPUs, arbitrary SP destruction, overlapping memcpy ranges, full
UINT32-length buffers and C NULL semantics are not qualified. Zero cases use
valid pointers. Large neighborhoods have two offset pairs, not all64.

A separate author adds only compile-time `BFS_MEMORY_COPY_SHORT_TIMING=1`
to the existing shared harness; default keeps the original eight lengths.
All prior functional/ABI oracles remain. The interim runtime-variable approach
and wrong count member names are corrected before freezing/building; neither
is a final-source or runtime qualification result. The source is frozen at
`c55343fa79edcbe808c4d0a65a87c421f84904f4fa402e343cbcc2a1d7f5a0b3`.
Two unique clean builds reproduce standalone binary
`193d0a1564bfaabb2ebcdb39cda6732734ddcb93c66898c285e55daa8b2de512`
byte-for-byte. Strict flags include O2/m68020/-fno-builtin and the opt-in define.

The initial consumer accepts one positive and rejects23 malformed synthetic
fixtures but incorrectly ignores a second malformed expected counter. Root's
independent valid clock_hz plus clock_hz=-1 probe is RED. Exact first source,
fixture script and failed log remain. The author corrects only the consumer
and fixtures, not the frozen C/kernel/binary. The final consumer registers
names before conversion and rejects duplicates, malformed/unknown fields,
missing/extra/reordered rows, overflows, bad repeats/totals/order, mismatches
and failed/malformed markers. All29 synthetic fixtures pass, and the separately
rebound exact RED case plus former fixtures passes25/25. These are parser
behavior, not real target or timing evidence. Full-width ticks remain strings
until the primary mean extractor verifies JavaScript's exact integer range.

Direct replay of the shortened evidence filenames initially fails with
MODULE_NOT_FOUND, not a semantic or target failure; its log remains. The
offline replay wrapper reconstructs the original relative parser filenames
in unique temporary directories without altering source bytes. It preserves
first24 fixtures plus the semantic duplicate-counter RED/exit1, final29
fixtures and independently rebound25 GREEN cases. Those expected outcomes
all pass; only the wrapper's own temporary directory is removed.

The reviewer is distinct from both production and harness authors. Conservative
report bound is about7230 of8192 bytes; main report/arrays about9.8 KiB before
small locals, within Stack32768. Observed report is3686 bytes. Peak functional
buffers are6291536 bytes, released before allocating2097230 timing-buffer bytes.
NOMEM/timer failures are errors, not skipped cases. The300s bounded runner
finishes with complete PASS records and terminates its own emulator. A wrapper
exit zero is not an independent guest command-exit witness: successful PASS
marker Write followed by failed Close could leave PASS bytes despite guest
failure return. Result-publication fault behavior is not qualified.

## Normal handler and platform qualification

The production-source trial exactly matches the privately qualified candidate:
`f9ecf90f6dc4716570f2789a29bbfdc54b27a76a871376075e907b14336bc9c3`.
Retained assembly is
`b1180cee112a9e395f7978a0fee877a03861ac8673cb62bb3f856fdfa3269eb3`.
All shared core sources, allocator policy, fences, CRC, snapshot and recovery
logic remain unchanged. Both host normal and actual ASan/UBSan pass49 suites/
472 cases; leak detection is explicitly off. They do not execute Amiga assembly.
All78 quality tests, ShellCheck, actionlint and clang analysis pass. Linux
passes20 conformance tests and two actual FUSE runs, default and options7
pressure/interrupted daemon; this is not a complete geometry matrix or an
Amiga assembly runtime check. Private Linux source identities are recorded.

Two independent Amiga directories reproduce normal/probe handlers:

- Retained normal: `7ec433160c6aa37c3ebbb5345097ef549638bde634dd8a3aa34e1b898a4a7d19`.
- Trial normal: `ad1f747822bbc8ba63b981480d93dae4fb7b7baeaf6c8d1136eaa6b92537f62e`.
- Trial probe: `70ff502b9d386840a27570e6bd1a16937984ff809e081e56d9d8362b2d7f4c9c`.
- Normal/probe guest: unchanged `1fccf207…` / `5126e864…`; copied, not rebuilt.

Fresh normal inputs are Cachy FS-UAE3.2.35, A1200/68040 max,8 MiB fast/2 MiB
chip, unchanged ROM/PFS3/formatter/guests,255.5 MiB partitions and BFS4 KiB/
options0/30 buffers. Preflight verifies all ten image inputs and no competing
remote compiler/emulator; coffin.service is inactive, not disabled.

## Restoration and evidence

Only the production entry/short path is withdrawn via exact patch. Source
matches retained b1180cee… byte-for-byte; a rebuilt normal handler matches
the measured retained7ec43316… exactly. Shared core remains unchanged.
The additional compile-time timing mode and stronger independent count
oracles remain because both actual kernels pass them; keeping tests is not
acceptance of the rejected assembly. No host/Linux matrix is rerun solely
for restoring Amiga assembly: those do not execute it and already pass on
unchanged shared-core sources. Restoration hashes/build/cmp are recorded.

The [plan](../plans/bfs-memory-copy-short-v1.md) owns requirements; this report
owns the rejection. The [text evidence](evidence/bfs-memory-copy-short-2026-10-01/)
contains exact old/candidate kernel and harness, opcode/disassembly proof,
raw micro records, first/fixed parsers and RED/GREEN fixtures, all platform
gates, raw normal/probe outputs, configurations, extractors and identities.
No object, executable, HDF or licensed asset enters the evidence.

All ten completed normal/probe systems are recoverably moved to
`/home/fabian/.cache/bfs-performance/memory-copy-short-2026-10-01/` with all
twenty before/after raw TSV hashes identical. The one completed kernel system
and unused reproducibility preparation are separately recoverably archived
under `memory-copy-short-kernel-2026-10-01/`; both completed micro record/marker
hashes remain identical. No unused second preparation is claimed as a run.
Copied raw records remain byte-identical. Only new copied formatter C1 CSI
text is ASCII-CSI normalized, with original/output hashes; copied log trailing
whitespace is trimmed. The new scoped text-asset check remains distinct from
the known global audit failure on older unchanged C1 formatter text. No old
evidence or checker is weakened. No correctness, durability, integrity,
snapshot, crash-recovery, allocation, warmup, fence, flush or workload promise
is weakened to pursue a better elapsed number.

No CI, push, PR, merge, release, external publication, new soak or real power/
controller/media qualification. Earlier rejected experiments remain rejected.

Independent final report review finds no blocker and agrees with the raw
normal/micro counts, gates, rejection, restoration and stated limits. Source
interpretation notes explicitly identify themselves as primary transcriptions
of worker review, not captured tool transcripts. Raw-copy verification covers
22 new records plus four retained-control TSVs; the exact text inventory and
SHA-256 manifest are checked before the local evidence-only checkpoint.
