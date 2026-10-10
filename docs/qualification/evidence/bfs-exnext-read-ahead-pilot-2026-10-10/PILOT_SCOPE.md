# ExNext read-ahead pilot scope

Fixed before execution, 10 October 2026.

Fabian asked for ExNext read-ahead (lever 3 of the performance plan) after the
inode write-back. This run measures it together with the corrections that the
independent review of the write-back required, against the write-back handler
retained earlier the same day.

## Handlers

Both are uninstrumented production handlers (`make amiga`, `-O2 -m68020`),
built on the Mac with m68k-amigaos-gcc 6.5.0b.

- `reference`: the retained write-back candidate of the inode write-back
  pilot, SHA-256 `2c3645ba…` (172,504 bytes).
- `candidate`: SHA-256 `95a26862…` (173,924 bytes), the reference plus:
  - ExNext read-ahead in the handler: one scan gathers the entries of the
    next calls (8 at first, doubling to 32 while batches are used up), served
    while the directory tree is unchanged, each entry's inode read when it is
    returned; a change halves the next batch; ExAll on the lock drops it.
  - Review corrections of the write-back: another write or delete drops the
    pending copy only once the tree holds its result; the commit writes
    pending copies with a headroom check that counts only path nodes of older
    transactions (`bfs_btree_update_path_headroom`); fsck reads pending copies
    instead of writing them.
  - A count of used slots, so that inode reads skip an empty table.

Before this scope was fixed, the candidate passed `make host-test` (fifteen
write-back tests; every reverted review correction makes one fail), the same
tests under ASAN/UBSAN, the quality gates, `make analyze`, the FS-UAE
integration suite on Cachy (52 of 52, including ExNext with deletion of each
listed entry and ExAll alternating with ExNext on one lock) and the FUSE
qualification on Cachy. The host replica mirrors the read-ahead; callgrind
on Cachy counts 45 % fewer instructions for ExNext over 1,000 entries ten
times and 1.3 % more for ExAll than before both changes (`host-profile`).

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
reference's BFS time in the same round. The ExNext read-ahead is retained if
all of the following hold:

1. All 16 starts pass `emulator-test/verify-bench-results.sh` for `compare`.
2. LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL and
   LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL each have a median paired ratio of at
   most 0.90.
3. The geometric mean over all phases of the per-phase median paired ratio is
   at most 1.00.
4. No phase has a median paired ratio of at least 1.10.

Otherwise the read-ahead in `src/amiga/handler.c` and its replica in
`tools/core-workload.c` are reverted. The review corrections and the slot count
fix defects and an overhead of the retained write-back; they stay either way.
Reported, not deciding: the BFS / PFS3 ratios of both handlers and every
phase's paired ratios.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
