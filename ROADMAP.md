# BFS Roadmap — deferred review items

From the deep code-quality review (2026-06-28). All four critical findings and the
high/medium findings are fixed and landed — including the structural refactors
(#28 fs.c split + the `bfs_txn_commit` boundary, and #11/#17 decoupling the B+tree
engine from `bfs_fs_t` via `bfs_free_sink_t`). One latent item and a few low nits
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

## Also open (low severity)

Minor nits not yet addressed: the on-disk `volname` comment says "UTF-8" but names
are handled as ISO-8859-1 (#38); the snapshot BSTR buffer sizes (`[34]`/`[36]`)
aren't derived from `BFS_SNAPSHOT_NAME_MAX` (#40); `btree_free_node` silently drops
a block when `pending_frees` is full instead of syncing (#41); the block-size
validity predicate is written twice with opposite polarity (#42).
