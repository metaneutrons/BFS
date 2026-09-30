# BFS temporary block buffer reuse

Decision state: bounded local experiment under the existing performance goal.
No new GitHub tracking or publication is authorized. This plan extends the
existing performance qualification, not its acceptance target: BFS must take
at most five times PFS3 elapsed time in every checked AmigaDOS workload, with
unchanged durability, integrity, snapshot and recovery guarantees.

## Outcome and boundaries

The CPU probe records over 2200 direct B-tree heap allocations while creating
40 files. Direct malloc and free intervals consume a material part of the
instrumented time, but are not a prediction of normal-handler speedup.
The [CPU attribution report](../qualification/bfs-cpu-attribution-2026-10-01.md)
owns the measured inputs and interpretation.

Reuse temporary operation buffers, not cached node bytes. Preserve all node
reads, copies, validation, CRCs, COW ownership, writes, commit fences and flushes.
No format change, delayed metadata write, cached validation across mutations,
platform-specific filesystem algorithm or change to the workload is in scope.

## Design and ownership

Add optional paired temporary-buffer allocation/release hooks to the shared
BIO abstraction. A backend without both hooks uses malloc/free. The shared
read cache may retain up to four individually leased, lazily allocated buffers
of exactly one filesystem block each. Occupied slots or other request sizes
use ordinary heap allocation. A failed allocation returns NULL; do not mask it
with a second allocation attempt. Release recognizes exact pool pointers and
otherwise frees ordinary heap storage. NULL release remains a no-op.

No lease may alias another active lease or any resident read-cache slot.
Recursive allocator operations through the same BIO need distinct leases;
exhaustion must fall back safely, not reuse a live pointer. Buffers contain no
persistent validation state and need no clearing beyond the current callers'
initialization. Invalidation must not release an active temporary buffer.
Destroy/reinit occurs outside complete serialized operations and frees all
retained buffers. A live cache is not copied, resized or destroyed during a
lease. These are the existing cache lifecycle/serialization boundaries.

B-tree operation buffers use the paired helpers. Census hash storage and scan
continuation keys retain ordinary heap ownership. Keep all cleanup and error
paths paired, including split/merge aborts and failed reads. No allocator state
or tree ownership may change merely because a buffer is recycled. The maximum
retained payload is four blocks: 16 KiB at the measured 4 KiB geometry and
256 KiB at the maximum supported geometry, plus fixed lease metadata.

An Amiga-only global arena was rejected because it would duplicate policy and
hide cross-filesystem ownership. Borrowing resident node-cache views was
rejected because nested reads, writes and eviction could invalidate them.

## Delivery and acceptance

### BR1 Shared implementation and functional qualification

Dependency: the CPU attribution checkpoint and the retained sealed-settlement
implementation. GitHub issue links remain pending; creating them is not part
of this local experiment.

- BR1-A1: Optional-hook fallback, repeated reuse, four distinct simultaneous
  leases, overflow allocation, non-block-size allocation, NULL release,
  invalidation, destroy/reinit and isolation from cache bytes are tested.
- BR1-A2: Naturally nested B-tree/Free-Tree operations witness distinct live
  leases. Read/write failure and split/merge cleanup leave no busy lease;
  existing strict data, graph, recovery, snapshot and low-space oracles pass.
- BR1-A3: Normal and ASan/UBSan full host suites, local quality/static checks,
  independent source/test review and normal/probe m68k builds pass. Probe
  metrics continue to mean direct calls in btree.c, not all backend heap use.

### BR2 Fresh normal measurements and adoption decision

Dependency: BR1. Use fresh baseline/candidate images in both filesystem orders,
the same normal guest tool and unchanged formatter, PFS3, geometry, ROM and
isolated FS-UAE configuration. Keep every sample and strict verifier result.

- BR2-A1: Four normal runs per version, balanced by filesystem order, are
  complete and data-verified. Report all six workload times, matched changes,
  candidate/PFS3 ratios and every run above five times PFS3.
- BR2-A2: Persist input identities, raw evidence, tests and the adoption or
  rejection decision. Reject an unsafe or ineffective experiment; preserve its
  evidence and restore the qualified checkpoint. A partial speedup is not
  completion of the overall goal.

## Risks and rollback

Retained memory may hurt constrained Amigas. Cap it, allocate lazily, preserve
the existing NULL/error behavior, and measure elapsed time rather than assuming
that fewer heap requests are sufficient. Pool scans and callbacks add overhead.
No quantitative forecast, deadline or statistical precision is promised.
The source checkpoint before this experiment provides an exact rollback base.
Real devices, power cuts, controller behavior, CI, push, PR and release work
remain outside this increment.
