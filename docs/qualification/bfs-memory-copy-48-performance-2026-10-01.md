# Rejected 48-byte Amiga memcpy experiment

The candidate is rejected after its isolated kernel pilot. The five-times PFS3
goal remains unachieved. Both actual kernels pass 8,908 functional/ABI calls,
but the 48-byte loop regresses the six-sample means for 44, 48, 264 and 4096
bytes. No broad filesystem comparison or production speedup is claimed.
The original assembly and byte-identical normal handler are restored.

## Measured decision

The standalone program links the exact retained 44-byte loop and the candidate
in one binary. Each timing length has six alternating paired samples, 24 pairs
per first-run order overall. The table uses mean EClock microseconds per call;
times include indirect calls, loop/return-checksum work and possible scheduling,
not pure kernel latency or filesystem throughput.

| Bytes | Retained µs/call | Candidate µs/call | Change | Candidate faster samples |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0.501871 | 0.487986 | −2.7667% | 3/6 |
| 44 | 0.623996 | 0.784007 | +25.6429% | 0/6 |
| 48 | 0.851202 | 0.937064 | +10.0871% | 3/6 |
| 264 | 1.619186 | 1.700807 | +5.0408% | 0/6 |
| 512 | 2.886218 | 2.768182 | −4.0897% | 6/6 |
| 4096 | 22.367893 | 23.853612 | +6.6422% | 3/6 |
| 65536 | 379.346624 | 285.468404 | −24.7473% | 5/6 |
| 1048576 | 4612.391026 | 4441.537363 | −3.7042% | 5/6 |

Independent Luna extraction reads all counts and tick arrays before the primary
JSON exists; all raw values, repeat calculations, means and signs agree. The
arrays are noisy, particularly at 48, 4096 and 65536 bytes. A single layout and
fixed timing source/destination offsets 3/5 do not support causal precision or
physical CPU claims. The short/medium regressions and no demonstrated 4 KiB
benefit justify rejecting the pilot before an expensive filesystem matrix;
do not extrapolate the largest mean gain to BFS throughput.

Clock is 709379 Hz. One-call old/new calibrations select the slower duration
and target 20 ms per batch, capped at 20,000 repeats. Four coarse zero-tick
calibrations select that cap. Repeats sum to 94,565; each kernel makes
8 + 6×94,565 = 567,398 timing calls in 56 batches, including eight calibrations.
All 48 sample comparisons agree on return checksums. Input fills and complete
source/destination verification are outside the timed intervals; no heap or
filesystem/block-device work is in the copy loop. Timer reads bracket each
batch. A private summary verifier reproduces repeat/
count equations and passes one positive plus eleven isolated malformed-input
counter-probes. Its exact-number conversion explicitly rejects tick values
beyond JavaScript's safe integer range rather than silently rounding them.

## Kernel, coverage and limitations

The candidate adds caller-saved D1 to the existing MOVEM payload: D1–D7/A2–A6
is 12 registers, 48 bytes. D0 retains the unsigned remaining byte count.
The save frame remains 11 callee-saved registers/44 bytes, with arguments at
48/52/56(sp). Destination return and exact long/byte tails are unchanged;
all memset bytes are identical. No CPU/alignment/DMA, cache, allocation,
format, COW, integrity, fence or feature-policy change is introduced. Fewer
iterations were a hypothesis, not acceptance evidence.

Small coverage is 0–132 bytes across all 64 source/destination offsets 0–7:
8,512 logical cases. Six 4/64 KiB boundary lengths across all 64 pairs add
384 cases. Six large neighborhoods around 44×65536 and 48×65536 across two
explicit pairs (0,0)/(1,7) add 12 cases. Every case runs both kernels through
the reused five-word ABI witness, checks independently generated destination
bytes/guards, unchanged source bytes/guards, exact destination return and
callee-saved registers/balanced normal-return frame. Each kernel reports
8,908 calls and zero ABI/return/data failures. D2/A2 clobber negatives require
exact masks 0x4/0x100 plus preserved destination return; both pass.

One deterministic mixed byte pattern is used. Large neighborhoods cover two
offset pairs, not all 64. Zero-length cases use valid pointers, not NULL.
Arbitrary SP destruction, overlapping memcpy ranges, full UINT32-length
buffers and physical CPUs are not qualified. The witness's original header
names CRC arguments, but the actual three-word target call and five-word
wrapper match the current pointer-typed C declaration without incompatible
function-pointer casts; independent source review confirms it.

Peak functional pair is 6,291,536 bytes, released before allocating the
2,097,230-byte timing pair. The runner uses 8 MiB fast memory plus 2 MiB chip
and explicitly sets Stack 32768. Independent static report bound is 4,683
of 8,192 bytes, including maximum counter/tick/failure text. Main arrays and
report occupy about 9.1 KiB before helper frames. NOMEM/timer failures are
errors, not skipped coverage.

The first frozen C build fails because the local Amiga stdint header has no
uintptr_t. Its exact pre-build source/log are retained. Only five pointer
diagnostic/checksum casts change to the target's 32-bit ULONG; the second
strict build passes without suppressing another warning. Two clean private
builds reproduce the standalone binary byte-for-byte. An explicit assembler
invocation also succeeds. Independent kernel and distinct-author harness
reviews find no source blocker; this does not erase the adverse timing result.

The initial 60-second run produces no report or done marker before its deadline
and is terminated. It is a timeout, not evidence of a kernel assertion failure or an
adverse timing sample. A distinct unchanged-source/binary run changes only
the wall limit to 300 seconds and completes with exact PASS records. Both
attempts are kept. No second emulator/remote compiler is present during timing.

The program requires exact report Write and Close before marker publication,
and nonzero program return on marker write/close failure. The current consumer
uses file markers, not an independent shell command-exit witness: a successful
marker Write followed by failed Close could leave PASS bytes despite a nonzero
return. Publication-fault behavior is therefore not qualified or claimed here.
Normal observed data/count/marker checks pass; future acceptance must not use
those markers alone as proof of fault-safe result publication.

## Restoration and evidence

Retained/restored assembly:
`b1180cee112a9e395f7978a0fee877a03861ac8673cb62bb3f856fdfa3269eb3`.
Rejected candidate:
`afc47138cb081634f0222cf9c25055d03f13d0eff73deeb1dee72a2c268cc2e3`.
Final harness:
`9e7f076d8ccc22a9a70c769d1f184e02c4bbc1e05cc3bef6beac0e577553d018`.
Standalone binary:
`064d6e2ddc51f2488db99a508cae35d83e462164eddb0a526aa8c3e12c0b9b11`.
Restored normal handler:
`572c469e5914c8d7ee71a8e09fe53a5cdbf4b9775931506a49881ad3866a71e3`.
Exact source and rebuilt normal binary comparisons pass. Host/sanitizer/Linux
matrices are not rerun for this rejected Amiga-only pilot; they would not
execute the candidate assembly, and production is unchanged.

[Text evidence](evidence/bfs-memory-copy-48-2026-10-01/) retains all raw kernel
records, marker, failed C build/source, timed-out attempt, configurations,
source snapshots/patch, identities, preparation/run/extraction/counter-probe/
archive scripts and checksums. Log trailing whitespace is normalized; raw
kernel records and required patch-context bytes are unchanged. No binary,
HDF or licensed asset is committed. Both private remote systems are archived
recoverably under `/home/fabian/.cache/bfs-performance/memory-copy-48-2026-10-01/`;
four before/after digest pairs agree and local originals remain.

No CI, push, PR, merge, release, external publication or hardware qualification
is performed. The successor is the separately scoped
[single-block absence proof](../plans/bfs-single-block-absence-v1.md), not a
claim that any remaining gap is exclusively validation or memcpy work.
