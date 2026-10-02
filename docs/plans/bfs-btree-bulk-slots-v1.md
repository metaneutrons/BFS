# B-tree bulk slot movement v1

Decision state: completed and rejected after normal workload comparison. M1
passes corrected equivalence/platform gates; M2 rejects the candidate because
write is slower in all four matched pairs (+6.00% mean). The exact production
checkpoint is restored, while equivalent tests and rejected evidence remain.
The [qualification report](../qualification/bfs-btree-bulk-slots-performance-2026-10-01.md)
owns results and scope limitations. The retained production
checkpoint is `449d66a`; its allocator/handler are the qualified
single-block-absence implementation. The previous reserve triangle is rejected
and is not reinstated. The overall performance goal remains unachieved.

The overall requirement remains BFS elapsed at most five times same-run PFS3
in every checked AmigaDOS workload, preserving durability, integrity,
snapshots and crash recovery. Create, write and delete still fail that goal.
This plan owns requirements; a subsequent qualification report owns outcomes.

## Mechanism and exact-byte proof

Four existing private helpers shift keys/values or keys/children one slot at
a time: `leaf_insert_at`, `leaf_remove_at`, `internal_insert_at` and
`internal_remove_at`. Keys, leaf values and internal children each occupy
contiguous arrays under the shared node-layout accessors. Replace only those
slot loops with one overlap-safe `memmove` per affected array.

Insertion moves suffix `[idx,n)` to `[idx+1,n+1)`; removal moves suffix
`[idx+1,n)` to `[idx,n-1)`. For internal insertion, child suffix
`[idx+1,n+1)` moves to `[idx+2,n+2)`; removal moves `[idx+2,n+1)` to
`[idx+1,n)`. Raw child bytes retain their big-endian encoding. Zero-length
suffixes perform no move. The caller already guarantees valid indices,
validated node capacity and room before insertion; movement lengths remain
inside the same arrays and filesystem block.

The resulting bytes must equal the old directional per-slot copies, including
unmodified header/unused slots before existing write canonicalization. Keep
incoming key/value writes, `num_keys`, split/merge policy, all other borrowing
loops, node reads/validation, full-block CRC and error/cleanup order unchanged.
Do not use overlapping `memcpy`, add a private platform algorithm, cache
layout/proof across calls or introduce an assembly kernel in this pilot.

Standard libc `memmove` is the shared mechanism. Check the actual Amiga link
and retained toolchain implementation rather than assuming it is fast. The
static reduction in copy-call sites is not a forecast of elapsed improvement
or sufficient to close the large create/delete gap.

The Amiga local string-header shim lacks the standard declaration; add only
its normal `void *memmove(void *, const void *, size_t)` prototype. The actual
libnix archive provides the symbol. Preserve the first missing-declaration
compile failure; do not suppress diagnostics or introduce a replacement kernel.

## Delivery and acceptance

### M1: Equivalent bytes and platform qualification

Dependencies: frozen retained checkpoint, unchanged old test oracles.

- Inspect actual 68k helper assembly for per-slot calls/layout computation;
  record source-bound findings and any available libc implementation limits.
- A separate Luna author adds public-operation tests for first/middle/last
  insertion and deletion, zero shifts, odd key/value widths and maximum key width,
  split/deeper-tree child ordering, exact values/scan order, independent
  full-block CRC and old-root COW preservation. Existing fault/snapshot/low-space
  tests remain. State any new-case exclusions explicitly.
- Run exact final new tests with old and candidate sources; both must pass.
  This is equivalence, not an old-source RED or runtime copy-count test.
- A reviewer distinct from the author checks the actual production/test diff,
  movement bounds, overlapping semantics and all four helper callers.
- Complete normal and actual ASan/UBSan host suites, local quality/static
  checks, Linux conformance/actual FUSE and reproducible Amiga builds. Preserve
  failures and scope limitations. No linker/compiler fallback may mask failure.

### M2: Normal elapsed decision

Dependencies: M1, frozen source/binary identities and unchanged benchmark.

- Use eight fresh normal systems, four per revision, balanced filesystem order
  and repeat, unchanged normal guest/PFS3/formatter/ROM/geometry. No concurrent
  compiler/emulator. Separate diagnostic runs from normal acceptance.
- Retain all six workload observations, adverse matched pairs and same-run
  threshold failures. Independent extraction precedes the primary summary.
- Structural reads/writes, allocation, fences and publication must not change
  merely because slot bytes are moved differently. A counter change requires
  explanation and review, not silent acceptance.
- Retain only useful observed benefit without material regressions; otherwise
  restore the exact production checkpoint, keep equivalent tests and rejected
  evidence. Pilot completion does not complete the overall five-times goal.

## Exclusions and rollback

No format/API, ownership, allocation placement/refill/stock/warmup, in-place
COW, writeback, cross-packet batching, snapshot, fence, flush or workload
change. No new assembly, rejected-pilot reinstatement, CI, push, PR, merge,
release, external publication, soak or physical power/controller/media claim.
Rollback restores the four helpers and added shim declaration, then rebuilds; preserve
unrelated user changes and any valid additional tests.
