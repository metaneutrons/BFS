# Inode write-back bound scope

Fixed before execution, 10 October 2026.

Fabian approved inode write-back within a transaction
(`docs/plans/bfs-inode-write-back-v1.md`), including the exception to the rule
against holding inodes between packets. Before the implementation, this run
measures the upper bound of the change on the 68k: a handler whose file handle
keeps the inode its Write packets publish and writes it to the inode tree only
when the handle is closed.

## Handlers

Both are uninstrumented production handlers (`make amiga`, `-O2 -m68020`),
built on the Mac from the same tree: commit `5136708` plus the big-endian load
change retained earlier the same day.

- `reference`: that tree, SHA-256 `ea37a89d…`.
- `bound`: the same tree with `defer-inode.patch`, SHA-256 `c4fc8efc…`. After
  the first Write packet of a handle, later packets neither read nor write the
  inode; ACTION_END writes it. A commit between the packets persists a stale
  inode, so this handler is unsafe and never a release artifact.

The bound omits what the real change adds: the table lookup on every inode
read, the flush before every commit, and the flush when another path touches
the inode tree. It therefore overstates the gain. It saves nothing for a file
written by a single packet.

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
Odd rounds start with `reference`, even rounds with `bound`. Guest
`fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`), ROM (`68f9d3a…`),
formatter (`a34639e…`) and RDB Buffers = 30 are those of the production
baseline pilot. No retries, exclusions or extensions; no pooling with earlier
runs.

## Reported

Nothing is retained or reverted. Reported for every phase: the BFS / PFS3
ratio of each start and its median per handler, and the median of the eight
paired ratios `bound` over `reference` in the same round. All 16 starts must
pass `emulator-test/verify-bench-results.sh` for `compare`; otherwise the run
is reported as failed and not interpreted.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
