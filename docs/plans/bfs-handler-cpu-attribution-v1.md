# Handler and shared-core CPU attribution v1

M1 and M2 are complete. The normal handler is byte-identical to the retained
baseline, all planned gates and four fresh diagnostic runs pass. The
[qualification report](../qualification/bfs-handler-cpu-scopes-2026-10-01.md)
records gross intervals and their limits. M3 begins with the separate
[native-word-order CRC pilot](bfs-crc-native-word-order-v1.md); no production
optimization or five-times acceptance is claimed by this diagnostic checkpoint.

Decision state: approved for a bounded diagnostic-only local increment by the
primary agent after rejecting both metadata-withdrawal variants. No new
production allocation policy is approved. The overall target remains at most
five times PFS3 in every checked AmigaDOS workload, with unchanged durability,
integrity, snapshots and crash recovery.

## Question and boundaries

The retained timestamp baseline still misses create/write/delete. The
[single-extent experiment](../qualification/bfs-metadata-single-extent-performance-2026-10-01.md)
reduces physical metadata traffic but increases their elapsed means by about
41/30/30 percent. Static root-read counts are not CPU attribution; resident
cache proofs can skip CRC and structural work. Earlier stock, geometry and
exact-set experiments are already recorded and are not revived from this count.

Identify whether the remaining gross time is predominantly handler packet
processing, shared namespace/file mutation, commit/settlement, or allocator
free/reuse checks. Measure the retained baseline first. Do not optimize source,
change workload sizes/Flush/Close, replace persistence guarantees, or introduce
cross-operation cached ownership proof as part of this increment.

## Instrumentation design

Instrumentation exists only under BFS_PERF_PROBE. Production preprocessing,
function bodies and ABI remain unchanged; a normal m68k rebuild must reproduce
the retained handler hash before a diagnostic run is qualified.

Use the existing EClock implementation and stride-one CPU sample policy. Every
scope has call count, sample count and unsigned 64-bit tick sum. Preserve the
existing counters, clock frequency, calibration and CRC stride. Avoid new heap
allocation, device calls, locks, IO callbacks, timing-dependent branches or
control-flow changes in observed functions. No diagnostic helper may consume
or restore filesystem ownership state.

Add one outer HandlePacket scope excluding diagnostic RESET/READ packets.
Classify ordinary packets into open (FINDINPUT/FINDOUTPUT/FINDUPDATE), read,
write, end, delete, flush and other. Categories partition the timed packet count
and ticks, but gross wall time can include scheduling and reply overhead.
Keep all early-return/ReplyPacket paths measured exactly once. RESET/READ must
not contaminate their own snapshots. Existing 64-bit DOS packet handling remains
in the other category and must not be rewritten.

Compute one elapsed tick value after the packet worker returns and record that
same value in both PACKET and its one category. Do not read the clock twice for
those records: exact category tick sums are an identity of recorded values,
not a timing approximation. Snapshot scope names are PACKET, PACKET_OPEN,
PACKET_READ, PACKET_WRITE, PACKET_END, PACKET_DELETE, PACKET_FLUSH, PACKET_OTHER,
CORE_CREATE, CORE_DELETE, CORE_FILE_WRITE, CORE_SYNC, IFACE_FREE and SEAL_COMMIT.
The snapshot header defines their field/name list once for the diagnostic enum,
field declaration, recording switch and guest emission.

Add inclusive shared-core scopes for stamped/un-stamped create-file operation,
delete-file operation, stamped/un-stamped file write, public filesystem sync,
top-level allocator free callback (excluding in_alloc recursion), and sealed
commit preparation/replacement. Wrappers or cleanup paths must preserve all
return values, errors, locks, callback order and failure-atomicity. Do not time
both stamped wrappers and their common body as separate calls to the same scope.
No success-only counting or error-path omission is allowed.

These scopes overlap: file/create/delete/sync include allocator, B-tree, heap,
CRC and device work; seal lies within commit/sync. Do not add them together or
subtract them from end-to-end elapsed time to invent a residual. Reports show
inclusive scopes separately and distinguish callback counts from physical BIO.
Clock calibration can be too coarse for exact overhead subtraction.

The diagnostic snapshot is one SSOT header shared by handler and guest. Bump
its ABI from 11 to 12 and deep-compare TSV schema from 10 to 11 together. Normal
comparison remains schema 1. The verifier must still accept every qualified
older deep schema and strictly reject missing/duplicate/malformed new rows,
call/sample mismatches, nonzero ticks with zero calls, mixed versions, invalid
clock/stride, packet partition inconsistencies and every existing counter or
data/completion violation. Preserve the original guest workloads verbatim.

## Execution and evidence

M1: implement bounded probe scopes and strict schema tests; independent review
must use a different author. Run normal/actual sanitizer host regressions,
quality/static checks and reproducible normal/probe/guest m68k builds. The normal
handler identity is a hard no-production-change oracle, not merely source review.
Retain failed attempts accurately; never overwrite a run log.

M2: two fresh baseline-control deep runs and two fresh extended-probe runs,
both filesystem orders, run serially on isolated Cachy FS-UAE. Preserve existing
ROM, formatter, PFS3, Workbench, 4 KiB BFS geometry, options=0 and 255.5 MiB
partitions. Each snapshot ABI must match its guest. The old control guest and
handler are immutable retained inputs; the extended guest may differ only in
diagnostic snapshot/schema emission, not workload operations. No compiler or
second emulator runs on Cachy during timing.

Strictly check all eight filesystem outputs and data/EOF/completion oracles.
Independently extract source/input identities, exact call counts, partitions
and inclusive timing means. The control comparison can reveal gross probe
disturbance, not unbiased instrumentation overhead or a normal-handler speedup.
Persist all raw results, configurations, errors, source and build identities,
limitations and the resulting bottleneck priority. Archive completed images
recoverably; no useless soak or recurring polling is added.

M3: only after this evidence, choose the next bounded optimization against the
normal-handler elapsed target. Qualify it independently in both host modes,
Linux conformance/FUSE and Amiga, then use balanced fresh normal measurements.
Diagnostic attribution alone never completes the five-times goal.

No CI, GitHub write, push, PR, merge, release, new user thread, real power-cut,
controller-cache-lie or device-bound hardware qualification is authorized.
