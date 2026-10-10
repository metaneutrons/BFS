# Inode write-back pilot scope

Fixed before execution, 10 October 2026.

Fabian approved inode write-back within a transaction
(`docs/plans/bfs-inode-write-back-v1.md`) and its exception to the rule
against holding inodes between packets. The bound measured earlier the same
day (`bfs-inode-write-back-bound-2026-10-10`) put appends at 0.72–0.77 of the
reference. This run measures the implementation.

## Handlers

Both are uninstrumented production handlers (`make amiga`, `-O2 -m68020`),
built on the Mac with m68k-amigaos-gcc 6.5.0b.

- `reference`: commit `5136708` plus the retained big-endian load change,
  SHA-256 `ea37a89d…` (171,240 bytes).
- `candidate`: the same plus inode write-back, SHA-256 `2c3645ba…`
  (172,504 bytes). A table of eight pending inodes on the live inode tree;
  the first publication of an inode in a transaction writes the tree, later
  ones replace the pending copy; every read returns it; every other write or
  delete supersedes it; the commit, fsck and the reload of the committed state
  write or drop the table. The working tree also carries the release flag
  change, which does not affect `make amiga`.

Before this scope was fixed, the candidate passed `make host-test` including
twelve new write-back tests (each of seven mutations of the mechanism is
caught), the same tests under ASAN/UBSAN, the quality gates with a new check
against raw access to inode trees, `make analyze`, the FS-UAE integration
suite on Cachy (52 of 52) and the FUSE qualification on Cachy. An independent
review is running in parallel; if it leads to a code change, that change is
measured in a new run with its own scope.

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
Odd rounds start with `reference`, even rounds with `candidate`. Guest
`fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`), ROM (`68f9d3a…`),
formatter (`a34639e…`) and RDB Buffers = 30 are those of the production
baseline pilot. No retries, exclusions or extensions; no pooling with earlier
runs.

## Retention rule

For each phase, the eight paired ratios are the candidate's BFS time over the
reference's BFS time in the same round. The candidate is retained if all of
the following hold:

1. All 16 starts pass `emulator-test/verify-bench-results.sh` for `compare`.
2. APPEND_4K_1M and APPEND_1K_256K each have a median paired ratio of at most
   0.90.
3. The geometric mean over all phases of the per-phase median paired ratio is
   at most 1.00.
4. No phase has a median paired ratio of at least 1.10.

Otherwise the write-back change is reverted. Reported, not deciding: the
BFS / PFS3 ratios of both handlers and every phase's paired ratios.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
