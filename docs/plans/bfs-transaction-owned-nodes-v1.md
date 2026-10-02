# BFS transaction-owned nodes and delayed group commit

Decision state: design approved for staged implementation. The delayed commit
policy is the chosen default, with a synchronous mode retained as an option.
Nothing in this plan is implemented on `main`. The prototype numbers below come
from compile-time experiments on `perf/group-commit-inplace-experiments`
(`1d7483a`) and are indications, not qualification evidence. The goal remains
at most five times PFS3 elapsed time in every checked AmigaDOS workload while
keeping committed-state integrity, snapshot isolation and crash recovery.

## Why the current path is slow

The probe counters of the retained deep-compare runs show three structural
costs that the micro-optimisations since #75 did not address.

First, the handler commits after every `ACTION_END` and after every standalone
metadata packet. Forty 1 KiB creates perform 41 commits, 40 superblock
publications and about 120 `CMD_UPDATE` barriers; forty deletes look the same.
PFS3 instead commits from a timer (`boot.c`: first `UpdateDisk` 0.2 s after the
first dirtying packet, extended in 0.7 s steps). Sending `ACTION_FLUSH` to PFS3
at the end of a benchmark phase costs it about 1 ms, so the comparison is fair;
PFS3 simply amortises one commit over the whole phase.

Second, every B-tree node change is copy-on-write, including nodes that the
live transaction itself allocated and wrote moments earlier. `cow_node`
allocates a replacement block, writes it, and frees the old block at once
because it belongs to the live transaction. Each such step mutates the
self-hosting free tree, which again copies its own nodes through the reserve.
With commits batched, 40 creates still make 193 metadata allocations and 148
immediate frees for 288 node writes.

Third, metadata is write-through. Every node change costs a full-node CRC and a
device write even when the same leaf changes again in the next operation. With
group commit and in-place rewriting already applied, CRC32 over written nodes
is still 44 % (create), 35 % (8 MiB write) and 66 % (delete) of the host
instructions in a callgrind replay of the workload. On the Amiga probe a node CRC takes 64–96 µs and a device write
25–55 µs.

| Workload (durable, BFS/PFS3) | Baseline | Group commit | In-place | Both |
| --- | ---: | ---: | ---: | ---: |
| Create 40 | 11.6 | 8.6 | 10.2 | 5.6 |
| Write 8 MiB | 8.5 | 8.5 | 6.6 | 6.9 |
| Delete 40 | 21.3 | 14.0 | 19.8 | 10.0 |

Six fresh FS-UAE runs per column, alternating BFS-first and PFS3-first, with a
volume flush at the end of each mutating phase inside the timed region. Lookup,
small read and 8 MiB read are unchanged by these mechanisms; the data path is a
separate later stage.

## Invariant

A block may be rewritten in place only if no superblock that mount could select
references it, directly or through any tree, and no snapshot record references
it. Today this holds for a block exactly when the live transaction allocated it
as a metadata node and has not yet published a superblock: `live_txn_id` only
advances after `bfs_txn_write_sb` succeeded, and every allocator path already
relies on the same property when it writes a freshly allocated block.
Snapshot creation commits before it captures roots, so captured roots never
contain live-transaction nodes.

The on-disk node header is not a sufficient proof. `node_read` does not check a
node's `txn_id` against the committed superblock, so a crafted or damaged image
can carry committed nodes whose header equals the live transaction. The
existing immediate free of "current" nodes in `btree_free_node` already trusts
the header; in-place rewriting must not extend that trust.

## Stage A: ownership registry and in-place rewrite

Keep a filesystem-wide registry of metadata blocks allocated by B-tree
mutations of the live transaction. `mutation_alloc` registers every block it
hands out, every dealloc of a live-transaction block removes it, a successful
superblock publication (`update_tree_txns`) clears it, and reload, abandon and
unmount discard it. The registry is an open-addressing set of block numbers. If
it cannot grow, registration is skipped and the block is copied on write as
today, which is always safe.

`cow_node` rewrites a node in place only when the block is registered and its
header `txn_id` equals the live transaction. Every other node keeps the
existing copy-on-write path, including all snapshot-shared and committed nodes.

The prototype wrote owned nodes immediately and broke per-mutation atomicity:
after an injected write failure a root block was already modified and snapshot
deletion resume reported `BFS_ERR_CORRUPT`. The production rule is therefore:

- an owned node's new image is copied into mutation-owned storage when
  `cow_node` accepts it, because path and sibling buffers are reused and freed
  before `mutation_commit`;
- owned images are published only after every allocation, every new-block
  write and every other fallible step of the mutation has succeeded, in
  bottom-up order, deduplicated by block;
- a failure before publication aborts the mutation exactly as today, with no
  owned node changed;
- a device write failure during publication leaves the live transaction's
  graph uncertain. It latches `free_sink_err` like other ownership-uncertain
  failures, so callers reload the committed state. The committed state is never
  touched.

This keeps today's guarantee that a failed mutation leaves committed state
intact. It narrows one availability property: a write error while publishing
owned nodes discards the uncommitted transaction instead of only the failing
operation. With synchronous commits that transaction contains at most the
current operation; with delayed commits it contains the operations since the
last commit, which is the same loss a power cut at that moment would cause.

Two behaviours visible to callers change and need tests and documentation:
the extent-tree root of a file no longer changes on every write, so
`file_refresh_unlocked` must not rely on a root change to detect content
changes (it already compares size and root and reads through the tree); and
allocation counts per operation drop, which several tests assert directly.

## Stage B: delayed group commit in the Amiga handler

The handler stops committing in `ACTION_END` and after standalone metadata
packets. It commits

- after 200 ms without a dirtying packet, and at the latest 1 s after the first
  uncommitted change (`timer.device`, as in the prototype);
- before replying to `ACTION_FLUSH`, `ACTION_INHIBIT(DOSTRUE)`, `ACTION_DIE`,
  formatting and snapshot packets;
- from a reset handler before a warm reboot, as PFS3 does in `resethandler.c`;
- whenever the core needs a commit for deferred-free headroom, as today.

Notifications are sent when the namespace change succeeds, not when it is
committed, because they report visibility, not durability. Disk change keeps
its current behaviour: the old medium is gone and its uncommitted state is
abandoned. A synchronous mode restores today's commit points. It is selected
per mount through the Mountlist `Control` string (`COMMIT=SYNC`) and can be
changed at runtime by a `bfs` subcommand; it is not stored on disk, so no
format change is needed. Linux FUSE keeps its contract of committing at
`fsync`, `fsyncdir` and unmount.

The stronger objection is durability: today a successful `Close` is durable,
afterwards it is durable within one second or at the next flush. This matches
PFS3 and the documented core rule that writes "are not durable until a
successful sync"; crash consistency is unchanged because COW and the dual
superblock protocol stay the only publication mechanism. The default is a
policy decision and is documented in the README and `failure-semantics.md`.

## Stage C: write-back of transaction-owned nodes

Owned nodes stay dirty in the node cache instead of being written on every
change. Publishing an owned image in stage A becomes a cache update. A dirty
slot may be written early when the cache needs space; that write is safe at any
time because the block is unreferenced by committed state. The commit writes
every dirty slot before the existing metadata fence and superblock
publication, so the sealed-settlement ordering "graph durable before publish"
is unchanged. A failed write-back latches recovery like any other commit
failure. Reads of owned nodes must hit the dirty slot; dirty slots are never
dropped without being written.

The cache needs a dirty budget independent of `Buffers` (currently 8 by
default, at most 128 slots). When the budget is exhausted the handler commits
early instead of growing without bound. CRC is computed once per node at
write-back, not per change.

## Out of scope here

Multi-block device transfers directly from and to the caller's buffer, an
extent cursor in the file handle, and inline extents for small files are
separate stages. Inline extents would change the on-disk format.

## Tests and measurement

- Registry: registration, removal and clearing at every lifecycle point, and a
  crafted image whose committed nodes carry the live `txn_id`, which must be
  copied on write and never overwritten.
- Mutation atomicity: fault injection at every write ordinal for insert,
  update, rekey, delete with borrow and merge, and splits, with owned nodes
  present. The result is either the complete mutation or a latched reload;
  remount and `bfs check` must show the committed state.
- Existing always-COW assertions are adapted by committing before the
  operation under test (old nodes) or by asserting the new owned behaviour. The
  `test_hwfail` coverage assertions keep their intent by dirtying the tree in
  the same transaction before the faulted operation.
- Crash injection and the model checker run with several operations per
  transaction and random commit points.
- AROS integration covers delayed visibility across remount, explicit flush and
  the synchronous mode; the reset path is checked on FS-UAE with Kickstart 3.x.
- Every stage is measured with the durable compare mode (at least six fresh
  runs, both orders) and a deterministic host instruction profile of the same
  workload, and reports node writes, metadata allocations and commits from the
  probe handler.

## Staging

1. Measurement tooling: durable compare mode and the host profile.
2. Stage A with its tests. No policy change.
3. Stage B, delayed commit as default, synchronous mode as option.
4. Stage C.
5. Data path stages.

Each stage is a separate pull request with its own measurement.
