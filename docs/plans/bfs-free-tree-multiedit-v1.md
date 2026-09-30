# Free-tree multi-edit plan v1

The dated baseline below predates the subsequently qualified root-retirement,
CRC and unchanged-update increments. Increment 2 still requires a separate
allocation/reclaim design; later evidence does not imply its acceptance. The
[phase-attribution measurement](../qualification/bfs-free-tree-phase-profile-2026-09-30.md)
puts reserve return first for small-file work; post-publication reclaim is a
smaller share than anticipated. The later
[node-validation report](../qualification/bfs-validation-cache-performance-2026-09-30.md)
retains the latest allocator attribution; ≤5× remains unmet in each mutating phase.
The [reserve/commit follow-up](../qualification/bfs-reserve-commit-profile-2026-09-30.md)
found over 96% singleton reserve-return runs in both small-file phases and
roughly three post-publication reclaim passes per commit. A design must reduce
this cycle without breaking its termination or old-root ownership rules.

The [mixed-reserve increment](../qualification/bfs-mixed-reserve-performance-2026-09-30.md)
is locally qualified before the first publication only. Enabling it throughout
post-publication settlement failed the existing 256-pass convergence checks;
that prototype was rejected. The retained change reduces small-file Free-Tree
writes further, but still leaves measured ratios of 98×/161× for create/delete
and 31× for the 8 MiB write. The acceptance target is at most 5× PFS3 in every
checked phase with unchanged guarantees, not a percentage improvement.

## Problem and baseline

The qualified A1200/FS-UAE comparison after the reserve quickwins still records
3,002 free-tree node writes for 40 small-file creates, 1,086 for an 8 MiB
sequential write, and 2,839 for 40 small-file deletes. The corresponding BFS
times remain hundreds of times above PFS3 in the small-file phases. These
counts identify free-tree maintenance as one cost, not the entire latency gap.

Every B-tree mutation currently COW-writes a new leaf and its ancestors.
Post-publication reclaim already sorts pending blocks and coalesces adjacent
ones, but each distinct range performs a separate free-tree mutation. Simply
reusing a current-transaction node in place is not safe: an interrupted write
could damage the working root before its replacement is publishable.

## Invariants

1. A committed root must never reference an overwritten or prematurely reused
   block. Old tree nodes remain deferred until after the first superblock
   publication.
2. A batch owns its newly allocated nodes until its complete replacement root
   is written. On allocation or write failure, the old root remains readable;
   partial post-publication reclaim is recovery-required, not retryable in the
   same live state.
3. Reserve and active emergency blocks are absent from the free tree. Batched
   input must be sorted, distinct, in range and disjoint from both pools and
   existing free extents. Accounting increases only for blocks represented by
   a successfully published replacement root.
4. Reclaim must continue until the pending queue is empty. Replacing a
   free-tree root can itself retire the previous root, so one batch and one
   secondary superblock write cannot be assumed sufficient.
5. Snapshot refcounts are resolved before any shared block is returned to the
   free tree. The initial batch does not change that path.

## Increment 1: bounded root-leaf batch

On a height-one free tree, merge the sorted pending block set with the leaf's
existing extents in memory. Reject overlaps, combine adjacency, and use one
atomic COW root-leaf replacement when the result fits the leaf. Use the
existing range-by-range path for deeper trees, full leaves and emergency-pool
blocks. Limit this batch to the first reclaim pass; subsequent passes use the
established path to settle root retirement. The on-disk format is unchanged.

This is a deliberately narrow proof of the multi-edit ownership model. It
must be retained only if the checked AmigaOS counters and timings establish a
useful gain without material regressions. It cannot solve allocation-side
COW traffic or general fragmented free trees.

## Increment 2: general atomic multi-edit

Build a bounded B-tree multi-edit mutation that owns a single private root for
multiple sorted free-space changes. It must preflight COW scratch and
pending-free headroom for the entire batch, handle leaf splits and merges across
parent boundaries, and publish its root only after every new node write
succeeds. Range normalization and accounting stay in the allocator; the B-tree
engine owns structural edits and node retirement. Chunking must preserve exact
pending-tail ownership on error. The existing publish/reclaim/publish fixed
point remains in place.

This increment requires a separate review before implementation. In
particular, it must prove that a bulk COW does not create an endless
root-retirement cycle or silently exchange a leak for apparent throughput.

The verified-node write-retention experiment did not reduce free-tree writes:
the checked create, 8 MiB write and delete phases still wrote 3,029, 1,089
and 2,660 free-tree nodes. A height-two reclaim-only leaf batch could help
deletes, but cannot improve creates or sequential writes, which allocate
blocks before post-publication reclaim. Before changing the general B-tree
engine, split these free-tree writes by allocation, reserve refill/return and
pending-free settlement. The allocation side needs a bounded ownership model
for consecutive draws; the reclaim side needs one for sorted multi-leaf
changes. Neither may overwrite an old-root-reachable node. The previous
current-transaction node-reuse prototype failed the pending-free fault tests,
so that shortcut is excluded.

A second local prototype served top-level single-block requests from the
surplus reserve pool before refilling it in a batch. The focused allocator
test passed, but the handler's repeated-overwrite workflow hit the commit
settlement limit with one pending block left (`BFS_ERR_AGAIN` after 256
iterations). The change was removed. A future allocation-side optimization
must prove not only block ownership, but also termination of the
publish/reclaim fixed point under repeated commits.

## Next bounded experiment: fold an unpublished root's retirement

Status: implemented and locally qualified; the 5× target remains unmet. See
[the result and retained evidence](../qualification/bfs-root-fold-performance-2026-09-30.md).
The mixed-reserve
increment still executes over 800 ordinary return runs per small-file phase.
The candidate below targets that tail directly; it does not introduce a dirty
cache or weaken synchronous operation-error reporting.

On a height-one Free-Tree, use a different emergency-origin block for the
replacement root. Return all ordinary reserve blocks together. If the old
root is an ordinary, unpublished node, include that block once in the same
merged free leaf rather than appending it to the reserve after COW. If it is
an unpublished emergency-origin node, return it directly to its pool slot
after the root swap. Either case removes the recursive ordinary-root tail.

Required proof boundaries:

- Eligibility requires a validated leaf's node transaction ID to equal the
  live ID exactly, and its block to differ from the last published Free-Tree
  root. Future tags are not accepted by a `>=` shortcut. Committed roots keep
  the ordinary deferred-retirement path. Snapshots do not share the Free-Tree.
- A bounded B-tree replacement primitive transfers the old current root's
  ownership to the caller only after a successful different-block write and
  root swap. It neither deallocates nor defers that old root. The caller must
  account for it exactly once in the free leaf or emergency pool. The normal
  replacement API keeps its existing retirement behavior.
- Scratch selection considers all emergency slots. Prefer a retired slot
  already held in reserve; otherwise take one active slot and stage it as
  reserve scratch. Never select the old root or another live owner. If none
  is available, or the merged leaf does not fit, use the qualified fallback.
- Snapshot the original reserve and emergency state. Before-swap failure must
  restore them exactly after verifying the allocator returned the scratch;
  after-swap state must never be restored as though the batch had not applied.
- Successful accounting adds ordinary reserve blocks plus the old root only
  when the latter is ordinary. Emergency blocks stay out of free extents.
  The pending queue must be unchanged by the transferred-current-root step.
- Existing multi-block range reclamation can leave an inactive pool-origin
  block in an ordinary free extent. Such unrelated stale-slot aliases make
  the fold unsupported, not corrupt. Active-slot, current-root and staged
  reserve aliases remain corruption; scan all extents before choosing the
  fallback so an inactive alias cannot conceal a later live-owner alias.
- Post-publication use is conditional on that same current-root proof. It
  must eliminate a tail rather than produce another retired committed root.
  The 256-pass limit and recovery-required error propagation remain unchanged.

Test gates include ordinary and emergency old-root ownership, committed and
future-tag rejection, no-headroom current-root transfer, exact read/write
failure rollback, absent-scratch/capacity fallback, small-cap pending storms,
snapshots, remount/fsck and every crash cut around the replacement/publication.
Keep the experiment only if checked counters and idle normal comparisons
show a material improvement. A second possible reduction is exact CRC-state
advancement over canonically zeroed unused write bytes; full-block read CRC
validation and format compatibility would remain unchanged. It is separate
from this ownership experiment and was subsequently qualified in the
[CRC report](../qualification/bfs-crc-zero-performance-2026-09-30.md).
[Identical-update elision](../qualification/bfs-noop-update-performance-2026-09-30.md)
and [resident structural validation](../qualification/bfs-validation-cache-performance-2026-09-30.md)
are separate mechanisms; none establishes the overall 5× target.

## UR: unpublished metadata reserve reuse

Status: implemented and locally qualified; the overall 5× target is not met.
See [the retained and rejected revisions](../qualification/bfs-metadata-reuse-performance-2026-09-30.md).

This bounded allocation experiment depends on the qualified root-retirement
fold; it is not general multi-edit or permission to overwrite a live node.
The latest diagnostic contains 546 allocation-body and 286 other Free-Tree
writes in the 8 MiB phase. The proposal reduces short-lived metadata scratch
traffic, not data allocation or older-node deferral.

Only the B-tree allocator's single-block interface participates. Outside
Free-Tree recursion, caller-owned retired unpublished metadata may enter the
existing volatile reserve instead of the Free-Tree. A later metadata allocation
may take an ordinary block only when stock is strictly above the **entire**
existing height-dependent refill target. Current-root retirement seeds the
spare naturally; no spare is assumed in a new/one-operation transaction or
low space. The recursion floor is not spent.

Initial eligibility: mounted allocator with working/committed superblocks and
a height-one Free-Tree, with the live tree ID equal to the newer working
superblock ID. Explicit pointers to the owning filesystem's mounted and runtime
snapshot state distinguish mounted work from format/standalone use. Both the
snapshot option bit and runtime state (including snapshot creation on an
optionless volume) disable reuse. Older nodes retain deferred retirement and
snapshot handling. All historical emergency slots, including inactive ones, are excluded
from ordinary reuse; their established return path remains. In-allocation
recursion and the public data/multi-block allocator stay unchanged.
Unsupported shape, absent spare or full stock take the existing path.

The unbounded prototype reduced large-write Free-Tree traffic, but its repeated
normal comparison increased mean create/delete time by 8.4%/6.9% and increased
their Free-Tree writes. It is not accepted. The bounded revision starts reuse
only after eight top-level metadata allocation requests in the same live
transaction. Its volatile counter saturates and resets on a changed live ID;
ineligible contexts do not count. This is an experimentally selected
amortization policy, not a safety requirement or an optimality claim. Short
transactions use the original interface path before warmup; earlier warmed
transactions may still affect later free-space fragmentation and settlement. Ownership,
stock validation and the whole recursion floor still apply after warmup.
Qualification must include actual warmed mounted fault cuts, not only short
namespace operations that now exercise the legacy path in both format modes.

Acceptance criteria before retaining the experiment:

- **UR-1 ownership:** exact range/duplicate/reserve/emergency and Free-Tree
  separation checks before stash/pop, with shared validation rather than copied
  logic. Never select a live Free-Tree root or committed-root-reachable metadata.
  The allocator's deallocation contract must establish caller ownership for
  current-tag retirements and unpublished abort/compaction scratch.
  Ownership is supplied by that existing caller contract, not inferred from
  node bytes: abort scratch may be unwritten or partially written. This
  experiment does not change the B-tree's pre-existing tag retirement rule.
- **UR-2 accounting:** stash changes reserve count, not Free-Tree total; pop
  consumes one entry. Publication and reserve return retain one representation
  per block. Full-floor, low-space, historical-pool and capacity boundaries
  require negative tests.
- **UR-3 errors:** preserve roots/accounting on preflight errors and scratch
  ownership through partial writes and multi-node aborts. Do not hide cleanup/
  ownership failures or retry an ambiguous publication.
- **UR-4 termination:** repeated overwrites, tiny pending queues, snapshots,
  low-space create/delete, compaction and repeated commits settle within the
  existing bound. The rejected top-level surplus allocator is not evidence
  for this narrower interface change.
- **UR-5 evidence:** focused ownership tests, normal/sanitizer suites, affected
  write-cut/remount/checker tests and fresh normal-handler Amiga comparisons
  in both orders with durable counters and identities. Retain a useful gain
  without material regressions; overall acceptance remains ≤5× in each phase
  with unchanged guarantees.

No format or CLI change is planned. A source rollback and rebuild need no
volume migration. This plan authorizes no CI, GitHub mutation or hardware
qualification.

## Qualification gates

- Allocator and B-tree tests: adjacency, gaps, overlap, duplicate input,
  capacity fallback, accounting, reserve/emergency separation, write failure,
  and a height greater than one.
- Filesystem tests: repeated small-cap pending-free storms, low-space
  operations, snapshot references, remount and clean fsck after successful
  settlement.
- Failure tests: every write cut in a multi-object reclaim operation; the
  visible namespace must be wholly old or wholly committed. Leaks before
  repair may be accepted only if fsck repairs them and a second check is clean.
- AmigaOS: build and checked FS-UAE comparison on fresh images in both run
  orders. Report free-tree writes, total BIO writes, commit counts and phase
  times against the unchanged baseline. No parity claim follows from one
  workload or emulator alone.

Real power cuts, controllers that lie about flush completion and specific
hardware media belong to separate, device-bound qualification and are not
prerequisites for this software optimization.
