# Amiga 48-byte memcpy loop experiment v1

State: bounded next pilot, not a production acceptance. The overall goal stays
at most five times PFS3 in every checked AmigaDOS workload with unchanged
durability, integrity, snapshots and recovery. The native-order CRC trial is
rejected and its original assembly/normal binary restored before this pilot.

The retained memory kernel already uses 44-byte MOVEM chunks. D1 is free in
the memcpy chunk loop and caller-saved; use it as a twelfth payload register
for 48-byte chunks. D0 retains the unsigned remaining byte count. Keep the
44-byte register save frame, argument offsets, destination return, exact
scalar tail and all memset code unchanged. No new CPU instructions, heap,
DMA contract, required alignment or runtime CPU detection is permitted.

The simple threshold change can regress 44–47 byte copies and medium lengths
whose remainder changes. Fewer iterations are not proof of higher throughput.
The filesystem evidence does not isolate total memcpy cost; no dominance or
five-times speedup forecast follows from this candidate.

Use a distinct frozen standalone comparator: independent byte/guard/source/
return checks for aligned/unaligned small and large boundary cases, reused
normal-return ABI register/frame witness and exact register-clobber controls.
Keep the 8 MiB guest memory budget and 60-second bounded run explicit. Allocate
large functional buffers and release them before allocating timing buffers.
No allocation or I/O inside timed loops; alternate old/new order and keep raw
ticks, mismatches and completion checks. The linker-renamed old assembly is
retained exactly; no C compiler builtin may replace the function-pointer calls.

Reject a useless/regressive microcandidate before another broad qualification.
If useful, independently review actual assembly/harness, run planned host/
actual sanitizer/quality/Linux-FUSE/reproducibility gates, then balanced fresh
normal-handler workloads. Preserve every adverse observation and threshold
failure. Any adoption depends on normal measured benefit and safety, not the
instruction count. This pilot cannot complete the overall goal without the
six-workload five-times evidence. No CI/push/PR/merge/release or hardware
qualification is included.
