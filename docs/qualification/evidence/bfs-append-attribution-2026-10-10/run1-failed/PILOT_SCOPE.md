# Append 4 KiB attribution scope

Fixed before execution, 10 October 2026.

Appending 4 KiB packets took 1.045 and 1.039 of the reference time in the
ExNext read-ahead pilot and its confirmation run (handlers `2c3645ba…` and
`95a26862…`); the host counts no extra work. Fabian asked for a probe
comparison. Between the two handlers lie two steps, which this run separates.

## Handlers

Built on the Mac with m68k-amigaos-gcc 6.5.0b, production with `make amiga`
(`-O2 -m68020`), write probe with `make amiga-write-perf-probe-handler`
(`BFS_PERF_PROBE`, `BFS_PERF_WRITE_DETAIL`, CRC stride 1):

| Handler | Source | Production | Write probe |
| --- | --- | --- | --- |
| `ref` | `6801370` plus the write-back diff as first measured (`bfs-inode-write-back-pilot-2026-10-10/candidate.diff`) | `2c3645ba…` (reproduces the pilot's reference byte for byte) | `26056445…` |
| `mid` | `6ed386b`: write-back with the review corrections and the used-slot count | `53acd50f…` | `8bf20105…` |
| `cand` | `b0e062c`: plus ExNext read-ahead | `95a26862…` | `f4a46c83…` |

`mid` against `ref` isolates the core corrections; `cand` against `mid`
isolates the handler change, which the Write path does not run.

## Emulator run

Exactly 30 fresh Cachy starts, in this order:

- **Part A, production:** 24 starts in `compare` mode with the production
  handlers, eight rounds of all three; rounds 1, 3, 5 and 7 BFS first,
  rounds 2, 4, 6 and 8 PFS3 first; within round r the rotation (r − 1) mod 3
  of `ref, mid, cand`. Guest `fs-compare-production` (`eb641e32…`).
- **Part B, write probe:** 6 starts in `split-write-compare` mode with the
  write-probe handlers, two rounds of all three, round 1 BFS first and round 2
  PFS3 first, rotation as in part A. Guest `fs-compare-write` (`20e751e8…`,
  rebuilt today from the current tree byte for byte).

PFS3 (`bc72fac…`), ROM (`68f9d3a…`), formatter (`a34639e…`) and RDB
Buffers = 30 are those of the earlier pilots of the day. No retries,
exclusions or extensions; no pooling with earlier runs.

## Reading

All 30 starts must pass `emulator-test/verify-bench-results.sh` for their mode;
otherwise the run is reported as failed and not interpreted. Nothing is
retained or reverted.

- **Part A:** for APPEND_4K_1M and every other phase, the medians of the eight
  paired ratios `mid` / `ref`, `cand` / `mid` and `cand` / `ref`. A step whose
  median for APPEND_4K_1M is at least 1.03 is named as the source; if
  `cand` / `ref` is below 1.03, the slowdown is reported as not reproduced in
  this run.
- **Part B:** for APPEND_4K_1M in each start, every count field (calls,
  views, reads, writes, maps, allocations, commits) compared between the
  handlers of the same round, and the sampled ticks per call of the Write
  packet, the core write, the inode read and write, the extent map, the goal
  allocation and the free-space allocation. Probe intervals are inclusive and
  nested and are not added, subtracted or extrapolated.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
