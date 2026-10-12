# BFS v0.3.0 performance against PFS3

The 68040 release handler of v0.3.0 takes 0.968 of the time of the pre-release
handler `b0e062c` in the geometric mean over all workload phases, measured in
one paired series. ExNext listings take 0.82–0.93 of their former time; the
other phases are unchanged within the run-to-run spread. Against PFS3, v0.3.0
needs 0.71–1.26 times the time for lookups, large transfers and small reads,
1.6–1.8 times for appends and small-file creation and deletion, 1.5–2.1 times
for ExNext and 2.9–6.2 times for ExAll.

## Handlers

Uninstrumented release handlers, built on the Mac with m68k-amigaos-gcc
6.5.0b and the release flags for the 68040 (`-O2 -m68040`):

- `previous`: commit `b0e062c`, `fdbed91e…`, the `release040` handler of the
  [production status of 10 October](bfs-production-status-2026-10-10.md),
  whose figures the README showed. Rebuilt on 12 October from `b0e062c` to the
  same bytes, so the toolchain has not changed since.
- `v030`: tag `v0.3.0` (`336a532d`), `make release`,
  `build/release/bfshandler.040`, `e2e274bb…`. Byte identity with the handler
  inside the signed release archive was not checked.

Between them lie the batched inode reads for ExNext (`fba03cb`), the
directory-walk refactoring for the Codacy limits and the DOS string
conversions of #95. The on-disk format is the same.

## Run

The [scope](evidence/bfs-v0.3.0-performance-2026-10-12/PILOT_SCOPE.md) was
fixed before the run: 16 fresh `compare` starts on Cachy, eight rounds of both
handlers, BFS first in odd rounds and PFS3 first in even rounds, `previous`
first in odd rounds and `v030` first in even rounds; guest, PFS3, ROM,
formatter and RDB Buffers = 30 of the 10 October series. All 16 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 18:27:01–18:36:28 UTC
on 12 October 2026; runner and Coffin were restored with status 0.

## Results

BFS / PFS3, median of eight starts, and the median paired ratio
`v030` / `previous` with its range over the eight rounds:

| Phase | previous | v030 | v030 / previous | range |
| --- | ---: | ---: | ---: | ---: |
| Lookup 400 | 0.89 | 0.89 | 1.004 | 0.99–1.53 |
| Sequential write 8 MiB | 0.74 | 0.71 | 1.008 | 0.61–1.07 |
| Sequential read 8 MiB | 0.99 | 1.01 | 1.019 | 0.95–1.28 |
| Append read 1,280 KiB | 1.07 | 1.05 | 0.982 | 0.79–1.03 |
| Small read 40 | 1.23 | 1.26 | 1.005 | 0.64–1.15 |
| Small create 40 | 1.83 | 1.78 | 1.034 | 0.92–1.05 |
| Small delete 40 | 1.73 | 1.72 | 1.007 | 0.67–1.46 |
| Append 4 KiB × 256 | 1.64 | 1.59 | 0.987 | 0.92–1.18 |
| Append 1 KiB × 256 | 1.87 | 1.83 | 1.008 | 0.87–1.05 |
| ExNext 400 × 10 | 1.88 | 1.52 | 0.819 | 0.79–0.85 |
| ExNext 1,000, first pass | 2.26 | 2.07 | 0.881 | 0.44–1.00 |
| ExNext 1,000 × 10 | 1.84 | 1.56 | 0.836 | 0.77–0.86 |
| ExAll 400 × 10 | 3.63 | 3.55 | 0.982 | 0.87–1.16 |
| ExAll 1,000, first pass | 5.39 | 6.24 | 1.042 | 0.95–1.07 |
| ExAll 1,000 × 10 | 3.79 | 3.93 | 1.020 | 0.99–1.17 |

Geometric mean of the paired medians over all 23 phases: 0.968. Geometric
mean of the BFS / PFS3 medians: 2.026 for `previous`, 1.958 for `v030`. The
full table, including the 40-entry listings, is in
[`summary.json`](evidence/bfs-v0.3.0-performance-2026-10-12/summary.json).

The paired ratio compares the BFS times of both handlers in the same round
and measures the change between them. The BFS / PFS3 medians also carry the
spread of PFS3's own times, so they can differ between the handlers by more
than the paired ratio: for the first ExAll pass over 1,000 entries 5.39 and
6.24 at a paired ratio of 1.042.

The `previous` handler reproduces the 10 October medians within 5 % in 19 of
23 phases. The first ExAll passes over 40, 400 and 1,000 entries are 7–13 %
lower today and ExAll 400 × 10 is 4 % lower; these phases spread most between
starts on this virtual host.

## Consequence

v0.3.0 needs 16–18 % less time than the pre-release handler for repeated
ExNext listings of 400 and 1,000 entries and 7–12 % less for the other ExNext
phases; it changes nothing else measurably. The README's
preliminary figures now show the `v030` medians. ExAll remains the largest
gap to PFS3, followed by the first ExNext pass and small-file creation and
deletion.
