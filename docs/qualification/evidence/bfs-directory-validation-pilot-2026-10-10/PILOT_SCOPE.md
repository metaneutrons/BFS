# Directory validation pilot scope

Fixed before execution, 10 October 2026.

Fabian kept the compact directory layout and asked for its validation cost to
be reworked and measured again. The candidate changes only CPU work, on the
branch `feat/compact-directory-records` (uncommitted, on `main` `4ec3d51`):

- `src/core/dir.c`: case folding through a 256-byte table (name hash and name
  comparison), the 12-byte key comparison as three big-endian words instead of
  `memcmp`, and zero-padding checks in 32-bit words.
- `src/core/crc32.c`: slicing-by-8 for hosts and AROS. The Amiga handler keeps
  `src/amiga/crc32_68k.s`, so this part does not reach the measured handler.

Nothing on disk changes, and no count in the handler should change.

## Host measurement, already taken

`tools/core-workload-profile.sh sync` on Cachy (gcc 16.2.1, callgrind), with
the replica extended by a 1,000-entry listing that runs ExNext and ExAll as the
handler does (positions, 93 ExAll entries per call). Instructions, before and
after the candidate:

| Phase | Before | After |
| --- | ---: | ---: |
| LIST_EXALL_10000 | 26,980,540 | 17,862,079 |
| LIST_EXNEXT_10000 | 33,377,097 | 24,226,291 |
| LIST_CREATE_1000 | 235,357,378 | 183,581,111 |

In LIST_EXALL_10000, `bfs_crc32` falls from 12,920,200 to 5,279,780, and
`dir_entry_ok` plus `bfs_dir_name_hash` from 2,991,430 to 1,630,880 for
`dir_entry_ok`; the hash no longer appears among the 30 largest functions
(below 53,910). `dir_key_compare` appears with 192,232. These are
x86-64 instruction counts; they show the direction and the host effect, not
the 68k effect.

## Emulator run

Exactly two fresh Cachy starts in `deep-compare` mode, BFS first and PFS3
first, with the `BFS_PERF_PROBE` build of the candidate (detail stride 17).
Guest (`eb641e32…`), PFS3 (`bc72fac…`), ROM (`68f9d3a…`), RDB buffers 30 and
the formatter (`a34639e…`) are those of the directory layout pilot of
10 October (`bfs-directory-layout-pilot-2026-10-10`), which is the reference.
No retries, exclusions or extensions; no pooling with earlier runs.

## Retention rule

The candidate is retained if all of the following hold:

1. Both runs pass `emulator-test/verify-bench-results.sh` for `deep-compare`.
2. In every phase, every count field equals that of the reference runs (which
   are identical to each other). Count fields are those ending in `_CALLS`,
   `_READS`, `_WRITES`, `_UPDATES`, `_COMMITS`, `_VIEWS`, `_HITS`, `_MISSES`,
   `_MAPS` or `_ALLOCS`; sample counts, ticks and times are not count fields.
3. No listing phase has a BFS time of at least 1.10 times the reference run of
   the same order in both runs.

Amendment at 08:41 UTC, made after the runs had started and before any of their
results was read: the premise of rule 2 is false. The two reference runs differ in 36
of 1,701 count fields, all in APPEND_1K_256K and APPEND_READ_1280K, where the
delayed-commit timer decides when a commit falls. Rule 2 therefore compares
every count field on which the two reference runs agree; the 36 fields on
which they differ are reported, not compared.

Otherwise the `dir.c` changes are reverted; the slicing-by-8 CRC, which the
handler does not use, stays decided by the host measurement and the tests.
Reported but not deciding: the times and BFS / PFS3 ratios of the listing
phases, and the mean per sampled call of the directory node view, the node
structure validation and the directory binary search. Sampled intervals are
inclusive and nested and are not extrapolated.

The launch script is byte-identical to the one of the earlier pilots: it
refuses to stop an active CI job, stops the runner and Coffin, and restores
both. No CI, commit, push, merge, installation or issue change is part of this
step.
