# Bounded metadata withdrawal batch v1

Decision state: the first repeated-tail prototype was implemented, independently
reviewed, safety-qualified and rejected for normal performance regressions.
Its production changes were withdrawn and its complete source/evidence retained
in the [qualification report](../qualification/bfs-metadata-withdrawal-batch-performance-2026-10-01.md).
The single-extent follow-on was also implemented and safety-qualified, then
rejected for slower create/write/delete elapsed times. Its source was withdrawn
and its [complete evidence retained](../qualification/bfs-metadata-single-extent-performance-2026-10-01.md).
Neither optional stock policy is performance-accepted. The next approved
increment is [diagnostic handler/core CPU attribution](bfs-handler-cpu-attribution-v1.md)
on the retained timestamp baseline.
No epic/milestone issue is created: external tracking and publication are outside
the present authorization. This plan is a bounded follow-on to the existing
[Free-Tree plan](bfs-free-tree-multiedit-v1.md), not a second overall performance
acceptance authority.

## Outcome and boundaries

The overall target remains at most five times PFS3 in every checked AmigaDOS
workload, with unchanged durability, integrity, snapshots and recovery. The
[timestamp checkpoint](../qualification/bfs-inode-stamp-performance-2026-10-01.md)
records candidate mean ratios 13.026/9.469/21.650 for create/write/delete.
Its create/delete probes still contain 233/116 allocation-body Free-Tree node
writes. Top-level metadata requests account for 193/116 allocator-interface
calls. These counts motivate a bounded allocation experiment, not a predicted
speedup or an assumption that all those writes disappear.

Public data allocation, snapshots, deeper Free-Trees, standalone/format
allocators, on-disk format and CLI contracts remain unchanged. This is not
general multi-edit, in-place reuse of a live node, early committed-node reclaim,
a dirty metadata cache, commit batching or weaker synchronous error reporting.

## First prototype design and retained guards

### Provenance and eligibility

Add an explicit non-owning mounted-owner pointer to the allocator, wired only
by the common working-state load/reload path; generic allocator fixtures retain
NULL. Add one volatile successful-batch transaction ID, never persisted.
Do not cast an arbitrary free-sink context to a filesystem owner.

Factor and share only the owner portion of the sealed-commit predicate; keep
its height/root/published-root checks in the sealing wrapper with unchanged
meaning. The batch separately requires a nonempty height-one tree before
preflight and delays its published-root rejection until after refill.
It needs that verified mounted writable active owner, exact
live/working/committed IDs, matching BIO/interface/state/free-sink bindings,
no snapshot/refcount/global-extent state and a nonempty height-one Free-Tree.
Sealing still additionally rejects a last-published Free-Tree root. Batch-only
verification compares the full sink tuple with `bfs_fs_free_sink(owner)`, without
invoking its callbacks; the existing seal eligibility is not broadened.

Before any optional refill/write, read the validated root leaf's transaction
ID and reject a future ID as corruption. An older valid root may be refilled.
Reject an equal-live tag on a committed superblock root before refill too:
retirement must not infer ownership of a published block from that tag.
After refill, require the root ID to equal the live ID exactly and its block
not to be a published root before granting batch permission. The early check
is necessary because ordinary COW writes stamp the live ID and could otherwise
mask a future tag. None of these checks authorizes committed-block reclamation.

Only the first top-level metadata request of a live transaction attempts this
batch, and only when stock is no higher than the normal recursion floor.
Other requests/contexts retain the existing eight-request warmup policy. A
successful batch ID allows ordinary surplus pop and caller-owned unpublished
metadata stash for that exact eligible transaction, without waiting for eight
requests. A new live ID clears this permission; owner/snapshot/recovery gates
continue to apply on every request. No successful batch means no new permission.

### Refill with bounded ordinary surplus

Factor the existing refill loop into one common helper with an explicit target;
the public refill wrapper continues to use the unchanged height-dependent
recursion floor. Only the eligible first metadata request asks that helper for
the floor plus at most eight surplus blocks. This combines prefix refill and
metadata preparation instead of paying a second highest-tail mutation.

Bound the extra target by reserve array headroom and free space above global
reserve plus the existing one-block guard. Include the entire missing prefix
and a possible committed-root COW scratch loss in the conservative availability
bound. Fewer than two ordinary surplus blocks declines before optional writes.
Eight is an experimental amortization parameter, not an optimality claim.
Fragmented highest extents may require the existing repeated tail withdrawals;
never assume one physical write or one contiguous run will always suffice.
Before optional start, require the shared B-tree delete pending-free budget
plus one slot for an initial older root. An owner-bound read-only headroom
callback provides the existing sink's available slots; tuple matching itself
does not invoke callbacks. Insufficient headroom declines to the unchanged
ordinary allocation, which can require less than the combined refill.

Before the optional refill, validate existing stock and the complete validated
root leaf against the current/committed roots, reserved geometry including both
backup offsets, reserve entries and every historical emergency slot. Use common
predicates, not duplicated ownership/geometry implementations. Share the
corresponding sealed leaf preflight portion, retaining its full checks. Existing
stock/reserved-block checks compare backup quotients at their full width;
existing old-leaf alias checks intentionally narrow to bfs_blk_t and are
conservative on malformed high offsets. Preserve both behaviors and the
qualified high-quotient test; this experiment does not repair that distinction.
Keep sealing's pending-input validation and error
precedence before an inactive-alias fallback; sharing must not mask a later
corruption check. A live-owner/reserve alias is an error; an inactive
historical-pool alias declines before optional writes. Complete-leaf validation
prevents refill from consuming an alias before a later surplus-only check. The
ordinary path remains available only before the optional refill starts.

The common refill retains the shared highest-tail allocator's normal accounting,
different-block COW/retirement and recursion behavior. After successful refill
and the exact-current root check, grant the transaction permission and return
one ordinary surplus block through the shared validated pop path. Factor that
pop's checks from its warmup/permission eligibility so the first authorized pop
can run before setting the successful-batch tag; do not expose the tag early.
Keep the
entire recursion floor; all remaining stock remains represented once and absent
from the free tree. Do not introduce a second accounting/pop implementation.
Metadata placement may differ and can indirectly change data placement; the
public data-allocation algorithm itself is unchanged. Preserve the original
roving hint on decline/refill-only paths, then
apply the ordinary successful single-metadata-allocation roving rule. The old
root remains readable on pre-swap failure.

Any physical/ownership failure after optional refill starts is propagated, not converted
into UNSUPPORTED and retried via the ordinary allocator. If the working root
has changed or scratch retirement is uncertain, latch the mounted recovery
error before another tree/allocator call can clear a local error. Do not stage
or return a successful block from that path. Existing reload-committed behavior
remains responsible for recovery. No node-byte transaction tag substitutes for
the caller's ownership of an allocated/retired block.

The tag is not set merely on entering refill or from root bytes: it records a
successful checked surplus stage/pop for this mounted owner and exact live ID.
Post-refill accounting, capacity or ownership contradictions are recovery
errors, not a decline that retries allocation against the mutated state.

### Commit and fallback

Unused stock follows the existing reserve/sealed-commit paths; the batch never
represents pending committed blocks as free before the established publication.
Sealed allocation freeze, fences, flushes, pending-tail ownership and the
256-pass settlement limit are untouched. A batch is not proof that sealing
will remain eligible: later fragmentation or pool state can force the ordinary
commit fallback. Qualification must exercise that fallback, not merely the
fresh height-one benchmark.

The old surplus-allocation prototype failed settlement and the early unbounded
reuse policy regressed small-file timing. Neither is an accepted precedent.
The current sealed-commit checkpoint changes the experiment's environment,
but only new convergence/fault and normal timing evidence can justify retention.
Repeated failed settlement or material regressions reject this candidate.

## Follow-on: one fitting extent, one COW mutation

The first prototype reduced body writes but increased reserve refills and heap
work. All four matched create/write/delete timings regressed. Requesting a
larger highest-tail refill is therefore not an accepted amortization policy.
This follow-on overrides only that refill mechanism and its range accounting;
all owner, complete-leaf, stock, root-tag, sink-binding and recovery guards above
remain. It is not permission to weaken their tests.

Factor the existing tail mutation into one helper taking a validated extent
key and length. The ordinary highest-key search delegates to that helper with
unchanged short-tail behavior. Share the in-allocation/phase/accounting/stock
append step between ordinary refill and the optional batch; do not create a
second withdrawal or append implementation. Ordinary/public-data allocation
and the normal recursion floor retain their existing policy.

For the batch alone, reverse-walk the already validated height-one leaf to
select the highest-key extent fitting a single draw. Let T be floor plus the
bounded extra and R the starting reserve count. Draw T-R, plus one if an older
root consumes reserve scratch, minus one if a current root with empty reserve
returns its replaced root to stock after emergency scratch. Thus the post-COW
reserve must equal T and total_free must fall by exactly that draw. Emergency
count decreases exactly when R is zero; an older root contributes one pending
retirement, while a current unpublished root returns to stock. Require the
existing global-reserve guard, stock capacity, emergency availability and
conservative shared delete headroom before optional start.

Decline before mutation if no extent fits, or an exact fit would delete the
sole leaf entry. Do not assemble a batch from multiple short extents. A valid
height-one update or nonempty-result delete makes exactly one different-block
COW write. After optional start, any physical, retirement or accounting error
fails without ordinary fallback; uncertain state remains sticky. Verify the
new/live/nonpublished root and exact reserve/free/emergency/pending accounting
before the first validated pop and successful permission tag.

The fragmented fixture with a large lower run must now witness one COW write,
not multiple highest-tail writes. Preserve second-write physical fault coverage
in an explicit no-fit ordinary-fallback fixture, with matching successful
control and ordinary-path recovery semantics. It is not a second mutation in
the single-COW batch. Retain full/prefix first-write batch faults and committed
graph/data remount oracles. A test-only owner-capacity contradiction injected
after successful scratch BIO may additionally exercise the real mounted
retirement callback: disclose that artificial state fault, assert the real
callback failure, and do not call it a naturally reachable valid-context error
or a controller/hardware power test. No production failure hook is authorized.

## Delivery and acceptance

### M1: bounded implementation and ownership proof

Dependencies: the locally qualified timestamp checkpoint `dfe4f53`.
Execution: local experiment only; issue tracking is not authorized.

- M1-A1: independently review the shared owner gate, preflight, draw/stage
  accounting, exact transaction permission and error latch. Original sealing
  eligibility remains equivalent apart from any separately justified checks.
- M1-A2: focused positive and negative tests prove exact distinct ownership,
  full recursion floor, free/stock accounting, one successful permission epoch,
  transaction reset, low-space/capacity bounds and no new public data policy.
  Cover every block size supported by the allocator where geometry differs.
- M1-A3: demonstrate absent/mismatched owner, standalone, read-only, frozen,
  recovery, snapshot option/runtime state, deeper/empty/older/future roots,
  reserved/committed/current/reserve aliases and inactive pool fallback.
  At least one mismatched full sink tuple directly proves the batch-only gate.
  Do not silently weaken existing reuse or sealed preflight tests to accept
  the new policy; generic allocator tests must still exercise their old mode.
- M1-A4: full/partial selected Free-Tree write failures preserve the old
  readable root or enter sticky recovery; failed node scratch, double frees
  and post-swap retirement failure cannot publish an ambiguous working state.
  Checker/remount graph/data oracles, not only counters, establish recovery.
  Distinguish single-COW batch faults, ordinary multi-tail fallback faults and
  deliberately injected owner contradictions; do not relabel one as another.

### M2: convergence and measured decision

Dependencies: M1 proof and independent review.
Execution: local experiment only.

- M2-A1: repeated create/delete/overwrite/commit workloads, tiny pending queues,
  low space, fragmented/full root leaves and actual sealed-commit fallback
  terminate within the unchanged bound. Every retained pending/free fault,
  snapshot and crash-cut oracle remains meaningful and passes.
- M2-A2: final production/tests pass complete normal and ASan/UBSan host suites,
  local quality checks, normal/probe m68k builds and Linux conformance plus
  actual FUSE qualification. No CI run is required for this local increment.
- M2-A3: serial fresh-image probes and four normal runs per revision, balanced
  by filesystem order with unchanged inputs, establish the decision. Retain
  every raw/adverse sample and same-run threshold failure; a node count alone
  is not elapsed evidence. Independently verify extraction and input identities.
- M2-A4: persist the source/input identities, gates, fault/ordering proof limits,
  discarded attempts and accept/reject rationale. Overall completion still
  requires every checked workload at ≤5×; retaining this increment alone does
  not complete that goal.

## Migration, risks and verification cost

There is no format migration. Rejecting the source candidate and rebuilding
requires no volume conversion. Focused tests and source review come first;
full platform matrices belong to final qualification. No deadline or speedup
is estimated. Additional stock can change placement/fragmentation, increase
validation work or break settlement convergence despite safe block ownership.
Those are measured rejection conditions, not reasons to relax guards. No CI,
GitHub write, push, PR, merge, release or device-bound hardware qualification
is authorized by this plan.

## Design review decisions

On 2026-10-01 independent Luna review found that a post-refill future-tag check
could be masked by the refill's own live-tag COW write. The requirement now
checks the validated tag before optional mutation. The primary agent selected
a combined refill target to avoid a separate metadata withdrawal mutation,
complete-leaf preflight before that refill, full batch-only sink binding checks,
and a factored validated pop that cannot publish success permission early.
The original sealing predicate, its error precedence and both existing backup
comparison behaviors are preserved. Remaining convergence/latency uncertainty
belongs to M1/M2 qualification, not to an assumption of successful sealing.

The implementation review additionally rejected an equal-live tag on a
published root before refill, and required conservative pending-free headroom
before optional start. The delete budget is factored in the common B-tree
header and used by the unchanged generic delete preflight, avoiding a second
formula. These are batch-specific qualification guards, not a claim that all
generic tree paths now reject every forged transaction tag.
