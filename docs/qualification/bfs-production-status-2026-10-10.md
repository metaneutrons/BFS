# BFS production status after the 10 October levers

The pushed state (`b0e062c`) takes 0.875 of the time of the morning's state
(`5136708`) in the geometric mean over all workload phases, measured in one
paired series; the 68040 release build as users now get it takes 0.852.
Appends and sequential writes take about three quarters of their former
time, ExNext about 0.71–0.74 when repeated. ExAll changes little.

## Handlers

Uninstrumented production handlers, built on the Mac with m68k-amigaos-gcc
6.5.0b:

- `base`: commit `5136708`, `make amiga` (`-O2 -m68020`), `c6326fc1…`.
- `current`: commit `b0e062c` from `git archive`, `make amiga`, `95a26862…`
  (byte-identical to the ExNext read-ahead pilot's candidate).
- `release040`: commit `b0e062c` with the release flags for the 68040
  (`-O2 -m68040`), `fdbed91e…`.

Between them lie the big-endian loads (`57ebab8`), the release flags
(`6801370`), inode write-back (`6ed386b`) and ExNext read-ahead (`b0e062c`).

## Run

The [scope](evidence/bfs-production-status-2026-10-10/PILOT_SCOPE.md) was
fixed before the run: 24 fresh `compare` starts on Cachy, eight rounds of all
three handlers, BFS first in odd rounds and PFS3 first in even rounds, handler
order rotating per round; guest, PFS3, ROM, formatter and RDB Buffers = 30 of
the production baseline pilot. All 24 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 13:09:23–13:23:42 UTC
on 10 October 2026; runner and Coffin were restored with status 0. Nothing is
retained or reverted.

## Results

BFS / PFS3, median of eight starts, and the median paired ratio over `base`:

| Phase | base | current | release040 | current / base | release040 / base |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lookup 400 | 0.94 | 0.90 | 0.88 | 0.962 | 0.958 |
| Sequential write 8 MiB | 0.83 | 0.75 | 0.74 | 0.768 | 0.760 |
| Sequential read 8 MiB | 1.07 | 1.00 | 1.05 | 0.985 | 0.974 |
| Append read 1,280 KiB | 1.10 | 1.07 | 1.05 | 0.994 | 0.990 |
| Small read 40 | 1.36 | 1.25 | 1.26 | 1.047 | 0.976 |
| Small create 40 | 1.87 | 1.77 | 1.74 | 0.936 | 0.905 |
| Small delete 40 | 1.84 | 1.73 | 1.77 | 0.948 | 0.943 |
| Append 4 KiB × 256 | 2.07 | 1.60 | 1.66 | 0.760 | 0.761 |
| Append 1 KiB × 256 | 2.50 | 1.81 | 1.83 | 0.751 | 0.728 |
| ExNext 400 × 10 | 2.58 | 1.89 | 1.87 | 0.710 | 0.691 |
| ExNext 1,000, first pass | 2.94 | 2.47 | 2.36 | 0.824 | 0.776 |
| ExNext 1,000 × 10 | 2.57 | 1.86 | 1.84 | 0.736 | 0.708 |
| ExAll 400 × 10 | 3.91 | 3.74 | 3.78 | 0.978 | 0.947 |
| ExAll 1,000, first pass | 6.45 | 6.07 | 6.09 | 0.970 | 0.936 |
| ExAll 1,000 × 10 | 3.99 | 3.85 | 3.71 | 0.962 | 0.916 |

Geometric mean of the paired medians: 0.875 for `current`, 0.852 for
`release040`. Small read is the only phase with a median above 1 (1.047 for
`current`, single pairs 0.87–1.10; 0.976 for `release040`). The full table,
including the 40-entry listings, is in `summary.json`.

## Consequence

After the day's work, BFS is within 0.75–1.07 times PFS3 for lookups and
large transfers, 1.25 times for small reads, about 1.75 times for small
create and delete, 1.6–1.8 times for appends, 1.8–1.9 times for repeated and
2.4–2.5 times for first-pass ExNext, and 2.8–6.1 times for ExAll. ExAll is the
largest remaining gap, followed by the first ExNext pass and small-file
creation and deletion.
