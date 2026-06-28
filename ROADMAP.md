# BFS Roadmap — deferred review items

From the deep code-quality review (2026-06-28). All four critical findings and the
high/medium findings are fixed and landed — including the structural refactors
(#28 fs.c split + the `bfs_txn_commit` boundary, #11/#17 decoupling the B+tree
engine from `bfs_fs_t` via `bfs_free_sink_t`, and #41 the deferred-free overflow
fix: headroom reserved at safe entry points, a non-silent overflow latch, and the
swap→commit→free compaction reorder). One latent item and one large-volume edge
remain.

---

## 1. Handler bypasses `fs->lock`  (SoC, high — latent) — #10

**Problem.** `handler.c` performs ~25 direct `h->fs.inode_tree` / `h->fs.dir_tree`
/ `h->fs.txn.sb_new` mutations *outside* `fs->lock` (e.g. create-then-stamp-times
is two separate critical sections).

**Why deferred.** No runtime race today: on the Amiga target the lock is a
compile-time no-op and the DOS handler processes packets sequentially. This only
becomes a real data race on a hypothetical multi-threaded Amiga port.

**Suggested approach.** Expose locked core entry points for what the handler needs
(`bfs_fs_set_timestamps`, `bfs_fs_set_volname`, …) and forbid the handler from
touching core trees directly; or explicitly document `fs->lock` as host-test-only.

**Files.** `src/amiga/handler.c`, `src/core/fs.c`

---

## 2. Snapshot create/delete headroom on very large volumes  (med — large-volume edge)

**Problem.** The #41 fix reserves deferred-free queue headroom on the namespace,
file, and compaction paths, and snapshot **delete** reclaim is now bounded by
headroom at the inode boundary (its existing per-batch checkpoint makes that safe).
Two snapshot sub-paths remain unbounded because they can't drain without a
finer-grained checkpoint:
- **Snapshot create** (`snapshot_ref_graph` INC) refcounts the *whole* volume in
  one atomic, rollback-based transaction. It can't commit mid-walk without losing
  the rollback, so on a very large volume (2nd+ snapshot, when the refcount tree is
  COW'd extensively) it overflows the queue → the latch returns `BFS_ERR_NOSPC` and
  the create **rolls back cleanly** (no leak, no corruption — strictly better than
  the old silent leak, but it fails where a huge volume would previously "succeed").
- **Snapshot delete** is bounded *between* inodes but not *within* one. A single
  multi-GB inode's reclaim has two unbounded sub-paths: its shared-block refcount
  decrements (refcount > 2, update-in-place) COW the refcount tree into the
  latching sink with no inline drain, so a long run exhausts the reserve mid-inode
  → `NOSPC` → reclaim stalls; and its freed data blocks self-commit mid-walk via
  `bfs_fs_queue_pending_free`, which (pre-existing) can double-decrement on a crash
  in the window between that self-commit and the per-batch checkpoint. The final
  whole-tree-node DEC walk (> ~16384 nodes, ≈ 160k+ files) is likewise unbounded.

**Why deferred.** Only reachable on very large volumes/files. Create fails *safe*
(clean pre-commit `NOSPC` + rollback). Delete mostly fails *safe and loud*
(`NOSPC` stall), with one latent crash-window double-decrement on the self-commit
sub-path. A proper fix is a resumable, per-block/per-node checkpointed refcount
walk for create and the single-inode reclaim — a snapshot-engine redesign well
beyond #41's scope.

**Files.** `src/core/snapshot.c`

---

## Fixed since (low nits, all done)

The `volname` charset doc (was "UTF-8", actually ISO-8859-1) (#38); the name BSTR
buffer sizes now derive from `BFS_NAME_BSTR_MAX` (#40); `btree_free_node`'s silent
drop is gone (#41, above); the block-size validity predicate is now a single shared
`bfs_block_size_valid()` (#42).
