# BFS Roadmap — deferred review items

From the deep code-quality review (2026-06-28). The four critical findings and
~18 high/medium findings are fixed and landed. The items below were **deliberately
deferred** because each is a larger or riskier change that wants its own focused
pass rather than being rushed alongside the rest. Listed roughly by value.

---

## 1. Decouple the B+tree engine from `bfs_fs_t`  (SoC, high) — #11/#17

**Problem.** `btree.c` and `extent.c` `#include "bfs_fs.h"` and cast the opaque
`tree->fs_ctx` back to `bfs_fs_t *` to push freed blocks onto `fs->pending_frees[]`.
The "generic" COW B+tree thus has compile-time knowledge of the top-level
filesystem aggregate and its post-commit reclamation policy — it is not reusable
in isolation, and `bfs_btree.h` leaks a `void *fs_ctx` whose only valid value is a
`bfs_fs_t *`.

**Why deferred.** Modularity-only (the engine isn't reused elsewhere, so there's no
runtime/correctness benefit), and a clean abstraction is entangled with the
capacity-aware truncate batching: `bfs_extent_truncate_batch` must know the
`pending_frees` headroom *up front* to return `BFS_ERR_AGAIN` and let the caller
sync between batches. A naive callback drops that property and risks the COW
reclaim path.

**Suggested approach.** Add a `void (*queue_free)(void *ctx, bfs_blk_t)` plus a
headroom query to `bfs_btree_ops_t` (or `bfs_btree_t`); `fs.c` supplies the impl;
then drop `#include "bfs_fs.h"` from `btree.c`/`extent.c`. Preserve the
truncate-batch AGAIN/retry semantics exactly; verify under ASan + the corruption
tests.

**Files.** `src/core/btree.c`, `src/core/extent.c`, `include/bfs_btree.h`, `src/core/fs.c`

---

## 2. Handler bypasses `fs->lock`  (SoC, high — latent) — #10

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
