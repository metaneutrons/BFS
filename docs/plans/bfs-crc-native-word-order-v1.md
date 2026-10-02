# Native-word-order Amiga CRC32 experiment v1

Decision state: rejected after complete qualification and fresh normal-handler
comparison. The original assembly and byte-identical normal handler are restored.
See [qualification evidence](../qualification/bfs-crc-native-word-order-performance-2026-10-01.md).
The overall target remains at most five times PFS3 in every checked AmigaDOS
workload, with unchanged integrity, durability, snapshots and crash recovery.

## Reason and limits

The new handler/core attribution places the gross gap predominantly within
shared mutation operations, not packet glue or the remaining direct heap calls.
This does not isolate total CRC time: stride-64 CRC sampling is nonuniform and
is not an unbiased estimator. A small algebraically exact kernel experiment is
worth measuring before another allocator ownership policy. It cannot be assumed
to solve the much larger create/delete/write gap on its own.

The retained Amiga kernel already uses slicing by four. Keep that algorithm,
four immutable 1 KiB tables, the reflected IEEE polynomial, complements,
seed/chaining convention and every covered byte. Hold internal CRC state and
table results in native big-endian word order. A full input word then requires
no per-word byte-order reversal; convert the CRC state at entry and return.
Trailing bytes use the equivalent high-byte native-order recurrence.

Only src/amiga/crc32_68k.s changes production behavior. No allocation, mutable
table initialization, CLI, on-disk format, COW count, ownership proof, fence,
flush, settlement or checksum-coverage change is allowed. Do not optimize the
portable C or zero-byte-advance kernels in this experiment. Preserve D2-D7 and
A2-A6, stack balance, zero-length/no-read semantics, exact tails and existing
68020+ unaligned-load behavior. No new instructions beyond the current target.

## Gates

1. Independently reconstruct all 1024 table constants from the polynomial;
   validate the transformed recurrence against an independent bitwise model.
   A host model is not 68k qualification.
2. Execute both retained and candidate assembly in AmigaOS: seeded, aligned and
   unaligned vectors, small lengths and tails, 4/64 KiB boundaries, chaining and
   ABI register/stack witnesses. The old assembly is renamed only for linking.
   Preserve failing logs, completion/data oracles and exact source identities.
3. Collect balanced same-process raw EClock old/new microtimes with no I/O or
   heap inside timed kernel loops. No normal-filesystem speedup follows from
   a microbenchmark. Reject a kernel regression before broad qualification.
4. If the pilot is useful, freeze source; independently review actual assembly,
   run normal/actual sanitizer host and Linux conformance/FUSE gates, reproducible
   normal/probe Amiga builds, then balanced fresh normal-handler comparisons.
   Keep every adverse observation and threshold failure. Acceptance requires
   measured normal-workload benefit without safety regressions; it does not
   complete the goal unless every checked workload meets the five-times target.

Persist exact source, mathematical and real-68k evidence. Archive completed
emulator images recoverably. No CI, push, PR, merge, release, new user thread
or physical hardware qualification is included.

## Completed decision

Both the native-order kernel and its empty-span fast-return revision pass the
actual Amiga oracle/ABI probe. The second revision reduces timed-loop elapsed
by about 8–11% at 44 bytes and larger, but still regresses 1–4 byte calls.
That size-dependent evidence justified measuring the normal workloads; it
did not justify adoption or a blanket faster-kernel claim. All such regressions
remain in the report. Four fresh normal runs per revision show every matched
8 MiB write slower, with +7.78% mean elapsed. Mean creation is +13.93%; deletion
is −5.53%, but 14/24 matched BFS observations are adverse and 12/24 candidate
same-run PFS3 ratios exceed five. Host scheduling and PFS3 variability prevent
causal precision. The observations do not establish a reliable normal-workload
benefit, so the production experiment is not retained. Tests and evidence remain.
