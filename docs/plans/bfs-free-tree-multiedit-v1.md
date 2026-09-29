# Free-tree multi-edit plan v1

Status: increment 1 locally qualified; increment 2 requires a separate
allocation/reclaim design and is not implemented (2026-09-29).

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
