# Production baseline and big-endian load pilot scope

Fixed before execution, 10 October 2026.

Fabian asked for three things in this order: the BFS / PFS3 ratios of the
uninstrumented handler (every recent pilot measured the `BFS_PERF_PROBE`
build, whose observer cost inflates BFS), and at the same time a cheap,
broad CPU change on the 68k. One series answers both and adds the release
build, which is what users install.

## Handlers

All three are uninstrumented production handlers, built on the Mac with
m68k-amigaos-gcc 6.5.0b:

- `base`: commit `5136708` (`feat/compact-directory-records`, pushed),
  `make amiga` (`-O2 -m68020`), SHA-256 `c6326fc1…`.
- `candidate`: `base` plus one change. `bfs_load_be16/32/64` and
  `bfs_store_be16/32/64` (`include/bfs_types.h`) read and write through
  aliasing word types on m68k instead of `memcpy`, which this compiler copies
  through a stack slot and loads again; `all_zero` in `src/core/dir.c` uses
  the loader. Hosts and AROS keep `memcpy`. `dir_key_compare` falls from 132
  to 54 bytes of code and needs no stack slot; the handler shrinks from
  172,656 to 171,240 bytes. Same flags as `base`, SHA-256 `ea37a89d…`.
- `release040`: commit `5136708` built with the flags of `make release` for
  the 68040 (`-Os -m68040`), SHA-256 `25c68982…`. It differs from `base` in
  optimization level and CPU flag.

Nothing on disk changes. The candidate passed `make host-test` (unchanged
host code) and the FS-UAE integration suite on Cachy, 52 of 52, before this
scope was fixed.

## Emulator run

Exactly 24 fresh Cachy starts in `compare` mode (the production guest without
probe counters): eight rounds, each with all three handlers. Rounds 1, 3, 5
and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first. Within round r the
handlers run in the rotation (r − 1) mod 3 of `base, candidate, release040`.
Guest `fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`), ROM
(`68f9d3a…`), formatter (`a34639e…`) and RDB Buffers = 30 are those of the
listing cache and inode batch pilot; the handler raises the cache to 64
nodes as before. No retries, exclusions or extensions; no pooling with
earlier runs.

## Reported, not deciding

- For each handler and phase: the BFS / PFS3 ratio of every start, its median
  over the eight starts, and the medians of the four BFS-first and the four
  PFS3-first starts. These are the production ratios Fabian asked for.
- `release040` over `base`: per phase, the median of the eight ratios of the
  two handlers' BFS times in the same round. Whether releases should be built
  differently is Fabian's decision and not part of this step.

## Retention rule for the candidate

For each phase, the eight paired ratios are the candidate's BFS time over the
base's BFS time in the same round. The candidate is retained if all of the
following hold:

1. All 24 starts pass `emulator-test/verify-bench-results.sh` for `compare`.
2. The geometric mean over all phases of the per-phase median paired ratio is
   at most 1.02.
3. No phase has a median paired ratio of at least 1.10.

The change removes instructions and keeps behaviour, so the rule asks for no
evidence of harm rather than for a proven gain; a geometric mean below 1.00
is reported as a gain only together with its spread. Otherwise the change in
`include/bfs_types.h` and `src/core/dir.c` is reverted.

The launch script is byte-identical to the one of the earlier pilots: it
refuses to stop an active CI job, stops the runner and Coffin, and restores
both. No CI, commit, push, merge, installation or issue change is part of this
step.
