# ExAll result cache bound scope

Fixed before execution, 22 October 2026.

Fabian asked for an upper-bound experiment before deciding whether BFS should
cache the result of a finished ExAll listing in RAM
(`docs/plans/bfs-exall-result-cache-v1.md`, committed as `7e7468f` with its
decision rule before any build). This run measures the bound on the 68040: a
handler that serves the scan and the inode reads of a repeated ExAll pass over
an unchanged directory from a fixed array.

## Handlers

Uninstrumented release handlers built on the Mac with m68k-amigaos-gcc 6.5.0b
and the release flags for the 68040 (`make release`, `-O2 -m68040`,
`build/release/bfshandler.040`), both from one scratch worktree and one build
procedure:

- `reference`: `origin/main` at `b8f3e8a`, SHA-256 `e2e274bb…`. The same
  bytes as the `v0.3.0` handler of the
  [v0.3.0 report](../../bfs-v0.3.0-performance-2026-10-12.md); the toolchain
  rebuilt `v0.3.0` to `e2e274bb…` before this build. Between `v0.3.0` and
  `b8f3e8a` only MorphOS and string-cast changes touch `handler.c`.
- `bound`: `b8f3e8a` plus `resultcache-handler.patch` (SHA-256 `87d8335c…`),
  committed locally as a throwaway because `make release` requires a clean
  tree; SHA-256 `be30c603…`. It adds a handler-level counter raised by every
  packet that can change the volume, and one 1,024-entry array. The batched
  ExAll serves gather and inode reads from the array while the directory
  matches and the counter is unchanged, and records a complete unfiltered pass
  that reads inodes. Fill, pattern match and the lock's resume point are the
  existing code. The array survives `UnLock`. It is a measuring device and
  never a release artifact; nothing of it is kept in `src/`.

The bound omits invalidation by tree stamps, allocation and capping of the
memory, the cost of a real fill, and the lock-owned lifetime of the candidate,
and so overstates the gain.

## Checks before the run

Not part of the measurement. The dev build (`make amiga`, `-m68020`) of the
bound passed the on-Amiga tests `exall_44`, `exalldel_49`, `exallpack_50`,
`exnext_37`, `exnextdel_48` and `exnextfresh_55` in FS-UAE on the Mac, and a
build with deliberately truncated cached names failed `exallpack_50`, which
shows that the test reaches the cached path. The guest used here checks names,
types and counts of the listings, not sizes or dates.

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
Odd rounds run `reference` before `bound`, even rounds `bound` before
`reference`. Guest `fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`),
ROM (`68f9d3a…`), formatter (`a34639e…`) and RDB Buffers = 30 are those of the
v0.3.0 series, taken from its workspace on Cachy and checked against the
digests. The emulator scripts and the guest source are those of `b8f3e8a`;
`emulator-test` and `tools/fs-compare-bench.c` are unchanged since `v0.3.0`.
No retries, exclusions or extensions; no pooling with earlier runs.

## Reported

All 16 starts must pass `emulator-test/verify-bench-results.sh` for `compare`;
otherwise the run is reported as failed and not interpreted. Reported for
every phase: the BFS / PFS3 ratio of each start and its median per handler,
and the median of the eight paired ratios `bound` over `reference` in the same
round. The decision rule of the plan is evaluated by `summarize.py` on those
medians: criteria 1 to 4 and 6 on the timings, criterion 5 on
`memory-probe.txt`. The geometric mean of the sixteen phases the bound does not
change, the per-entry cost of the ExAll phases and the lock-owned estimate
`0.1 · f + 0.9 · r` are derived from the same starts.

The launch script is byte-identical to the one of the earlier pilots. No CI,
merge, installation or issue change is part of this step, and nothing is
retained in `src/`. Cachy's runner container and Coffin are stopped for the
series by the launch script and restored afterwards.
