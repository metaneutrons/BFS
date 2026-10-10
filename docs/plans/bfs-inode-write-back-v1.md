# BFS inode write-back within a transaction, v1

Decision state: approved by Fabian on 10 October 2026, including the
exception to the rule against holding inodes between packets. Implemented on
`feat/compact-directory-records` (uncommitted); the bound and the pilot are in
`docs/qualification/bfs-inode-write-back-bound-2026-10-10.md` and
`docs/qualification/bfs-inode-write-back-pilot-2026-10-10.md`. No on-disk
change.

## Problem

Every Write packet reads the file's inode to refresh the handle and writes it
back with the new size, extent root and date: one search and one
copy-on-write update of the inode tree per packet. The production run of
10 October (`docs/qualification/bfs-production-codegen-pilot-2026-10-10.md`)
puts appends at 2.2–2.5 times PFS3: about 280 µs per 4 KiB Write packet
against 127 µs. The write-path diagnostic of 9 October attributed roughly
40 µs per packet to the inode write and, before the write guard, twice that
to inode reads; these are sampled, inclusive probe times and only indicate the
order of magnitude.

Listing fields in directory entries (`docs/plans/bfs-listing-metadata-v1.md`)
would add a directory record update to every inode write, so they need this
change first.

## Design

The live inode tree carries a table of eight pending inodes
(`bfs_inode_pending_t` in `include/bfs_inode.h`, attached through
`tree->inode_pending`; snapshot views have none):

- `bfs_inode_publish`, used by `src/core/file.c` when a write or truncate
  publishes size, extents and date of a linked inode, writes the tree on the
  first publication of that inode in a transaction and records a clean copy
  with the transaction id. Later publications in the same transaction replace
  only the copy. The first write leaves the leaf owned by the transaction, so
  writing the copy later is an in-place update without allocation.
- Every read (`bfs_inode_read`, `_read_unlinked`, `_read_sorted`) returns the
  copy when there is one, with the usual validation.
- Every other write or delete of the inode (`bfs_inode_write`,
  `_write_unlinked`, `_delete`) writes the tree and then drops the copy. Its
  caller read the inode through the same functions, so it writes the newest
  state. If the tree operation fails without changing the tree, the copy
  stays, because it is still the newest state.
- `bfs_txn_commit` writes all copies first; a failure there sets
  `recovery_error` and the transaction is never committed. Writing a copy
  needs no deferred-free headroom: `bfs_btree_update` counts only path nodes
  of older transactions, and the copy's path was written in this one.
  `bfs_fs_check` stays read-only and checks the pending copies in place of the
  tree values (`bfs_inode_pending_peek`). The reload of the committed state
  drops them, also when it fails early. Snapshot creation commits before its
  walk; the unlinked scan runs at mount, before any publication.
- When all slots hold newer copies, a publication of another inode writes the
  tree directly, as before.

A pending inode is not a cache of disk state. It is the current state of the
open transaction, like a dirty B+tree node in the cache, and exists only until
the next commit, which the delayed-commit timer bounds.

## Strongest objection

The handoff rules say that inodes and permissions must not be cached between
packets without safeguards, and a single path that reads or writes the inode
tree directly would see stale size and extents. A write based on such a read
could replace the newer pending state when the table is flushed.

The safeguard is structural: every access to an inode goes through
`src/core/inode.c`, which knows the table, and a quality-gate check
(`tests/quality/test_inode_tree_access.py`) fails when code outside it calls a
raw B+tree function on an inode tree beyond the three reviewed walks.
Permission checks keep reading the current inode under the operation's lock;
they now get the pending copy, which is the newest state. Sixteen tests in
`tests/test_inode_write_back.c` and `tests/test_inode_write_back_recovery.c`
cover readers, sorted reads, other writers
and their failures, reload, crash, fsck, a full table and a full free queue,
snapshots, a failed flush and unlinked handles. An independent review found
that an early drop of the copy lost it when another write failed; that and
two smaller findings are fixed, and each fix is held by a test that fails
when the fix is reverted.

## Effect

One inode search and one inode tree update fewer per Write packet after the
first in each transaction. Files written with one packet are unchanged. The
bound puts appends at 0.72–0.77 of the previous time; the allocation of each
new block, the other large part of the write path, is unchanged.
