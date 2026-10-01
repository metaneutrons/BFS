# Amiga short memcpy dispatch v1

State: completed and rejected. M1 passes functional/ABI and independently
extracted kernel pilot; M2 rejects the trial because normal create/write
regress in all four matched pairs (+10.43/+8.92% means). The exact retained
assembly/normal handler are restored. Equivalent harness improvements and
all rejected evidence remain; the overall performance goal is unachieved.
The [qualification report](../qualification/bfs-memory-copy-short-performance-2026-10-01.md)
owns the outcomes and limits.
Short0–43means improve11–31%, but44/48mean regress8.29/7.89%; all adverse
samples remain. Larger264/4096means do not regress, but scheduling and sample
disagreement preclude attributing them to the unchanged bodies. A separate
normal workload decision rejects adoption despite those short-copy means.
The retained checkpoint remains qualified single-block-absence `4a80e0f`.
Bulk B-tree slot movement and the prior
48-byte large-copy kernel are rejected; neither is reinstated. The overall
requirement remains elapsed BFS at most five times same-run PFS3 in every
checked AmigaDOS workload, preserving all durability, integrity, snapshot
and crash-recovery guarantees. This plan owns requirements, not outcomes.

## Distinct mechanism and constraints

Retained `src/amiga/memcpy_68k.s` saves eleven callee-saved registers before
checking any length, including zero and 4/8-byte keys. For unsigned length
below 44, dispatch before that prologue to a scalar path using only
caller-saved D0/D1/A0/A1. Keep SP unchanged, load public arguments at 4/8/12(sp),
copy exactly floor(n/4) longs and n mod 4 bytes, and return original destination
from 4(sp). Zero copies do not dereference data. Standard memcpy still
requires valid nonoverlapping ranges; do not assert additional C NULL or
overlap semantics.

For length at least 44, retain the exact existing 44-byte MOVEM payload,
eleven-register save frame, offsets, loop, full unsigned byte count and tails.
The added dispatch has an overhead even there; unchanged body is not a claim
of unchanged latency. Preserve all memset bytes. No new CPU instructions,
alignment requirement, heap/DMA/cache contract, COW/CRC/policy/API/format,
large-kernel width or shared-core algorithm change is permitted.

First author a private candidate file, never substitute it into production
before isolated qualification. Assembly is root-authored. A separate Luna
author adds an opt-in timing shape to the existing comparison harness,
preserving default behavior and every functional/ABI oracle. A different
reviewer checks actual emitted assembly, harness/consumer and source hashes.

## M1: actual kernel pilot

Link private candidate as memcpy and exact retained source as renamed
memcpy_baseline in one standalone guest with -fno-builtin. Reuse all 8908
cases per kernel: lengths0–132 across all64 src/dst offsets, 4/64KiB boundaries,
large 44/48×65536 neighborhoods, destination/source guards and return checks,
callee-saved register/SP witness and exact D2/A2-clobber negative controls.
Zero cases use valid pointers; large neighborhoods have two pairs, not64.
No arbitrary SP destruction, overlapping ranges or physical CPU qualification.

Opt-in timing lengths are0,1,4,8,16,32,43,44,48,264,512,4096,65536,1048576,
with six alternating paired samples each, unchanged 20ms calibration target
and repeat cap. Record every raw EClock value, repeat/clamp/count equation,
adverse pair and marker. Timing includes indirect calls/loop/checksum and
scheduling, not exclusive kernel cost. Functional allocation/verification
remain outside timed intervals. Safely bound report and stack sizes.

Strict short-mode extraction must reject incomplete/extra/duplicate rows,
bad values, overflow, bad repetitions/totals/order, mismatches and failed or
malformed markers. Exercise the consumer with independent synthetic malformed
fixtures, explicitly not actual kernel qualification. Result marker publication
faults lack an independent exit witness in the existing consumer and are not
qualified. Never weaken an oracle to rescue a candidate.

Use Cachy with unchanged FS-UAE3.2.35 A1200/68040 max, 8MiB fast/2MiB chip,
licensed ROM and Stack32768. No concurrent compiler/emulator. Bound the first
run at300s; preserve failures/timeouts. Two private clean builds must reproduce
the guest. Only textual sources/logs/records enter versioned evidence.

## M2: conditional production and normal elapsed decision

Dependency: useful short-copy observations without unacceptable regressions
at unchanged large lengths, source/harness independent review and M1 PASS.
Reject a useless/regressive microcandidate before the filesystem matrix.
If useful, apply only the same candidate dispatch to production, complete
proportionate normal/actual sanitizer/quality/Linux-FUSE/reproducible Amiga
qualification, and use eight fresh normal images, four per revision, balanced
order/repeat and unchanged normal workload. Probes are separate evidence.
Retain every adverse pair and same-run five-times failure. Static savings or
microtimes cannot establish the six-workload goal. Reject material normal
regressions and restore exact source/handler, preserving equivalent tests
and all rejected evidence.

No CI, push, PR, merge, release, external publication, new soak or real power/
controller/media qualification. No wider-copy or other rejected experiment
is restored. The create/write/delete gap may need a larger architectural
improvement; this isolated dispatch makes no dominance or speedup forecast.
