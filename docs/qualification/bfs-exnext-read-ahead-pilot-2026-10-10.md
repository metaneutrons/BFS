# BFS ExNext read-ahead pilot

ExNext read-ahead is retained under the rule fixed before the run. On the
emulated 68040, ExNext over 1,000 entries ten times takes 0.745 of the
previous time (2.45 to 1.82 times PFS3), over 400 entries 0.734 (2.61 to
1.89). The same handler carries the corrections from the independent review of
the inode write-back and a count of used pending slots.

## Candidate

Against the retained write-back handler (`2c3645ba…`), the candidate
(`95a26862…`) adds:

- **ExNext read-ahead** in `src/amiga/handler.c`: one scan gathers the
  entries that the next calls return (8 at first, doubling to 32 while each
  batch is used up) and stops on the last one, so the next scan resumes
  without a search. Entries are served while the directory tree is unchanged;
  each entry's inode is read when it is returned. A change to the tree
  discards the batch and halves the next one; ExAll on the lock discards it.
  `tools/core-workload.c` mirrors it.
- **Review corrections** (`src/core/inode.c`, `src/core/btree.c`,
  `src/core/fsck.c`): another write or delete drops the pending copy only
  once the tree holds its result; the commit writes pending copies with a
  headroom check that counts only path nodes of older transactions
  (`bfs_btree_update_path_headroom`); fsck reads the pending copies instead
  of writing them.
- **A count of used slots**, so that inode reads skip an empty table.

Before the run the candidate passed `make host-test` with fifteen write-back
tests (each reverted review correction makes one fail), the same tests under
ASAN/UBSAN, the quality gates, `make analyze`, the FS-UAE integration suite on
Cachy (52 of 52, including ExNext deleting each listed entry and ExAll
alternating with ExNext on one lock) and the FUSE qualification on Cachy.

On the host, callgrind on Cachy counts for the replica, before both
changes and with the candidate (`host-profile`):

| Phase | Before | Candidate | Change |
| --- | ---: | ---: | ---: |
| ExNext 1,000 × 10 | 13,546,919 | 7,471,192 | −44.9 % |
| ExNext 400 | 463,372 | 232,339 | −49.9 % |
| ExAll 1,000 × 10 | 5,952,384 | 6,032,164 | +1.3 % |
| Append 4 KiB × 256 | 4,711,091 | 3,445,784 | −26.9 % |

Between the reference and the candidate, the write phases differ by a few
hundred instructions on the host (Append 4 KiB 3,445,053 before the
corrections, 3,445,784 after).

## Run

The [scope](evidence/bfs-exnext-read-ahead-pilot-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: 16 fresh `compare` starts on Cachy, eight rounds of
both handlers, BFS first in odd rounds and PFS3 first in even rounds, handler
order alternating; guest, PFS3, ROM, formatter and RDB Buffers = 30 of the
production baseline pilot. The series ran 12:28:17–12:37:51 UTC on 10 October
2026; runner and Coffin were restored with status 0.

| Rule | Result |
| --- | --- |
| 1. All 16 starts pass the strict verifier | passed |
| 2. ExNext 1,000 × 10 and 400 × 10 median paired ratio ≤ 0.90 | 0.745 and 0.734 |
| 3. Geometric mean of the per-phase median paired ratio ≤ 1.00 | 0.938 |
| 4. No phase with a median paired ratio ≥ 1.10 | none (highest 1.078) |

## Results

| Phase | BFS / PFS3 reference | candidate | candidate / reference (range) |
| --- | ---: | ---: | --- |
| ExNext 40 × 10 | 2.42 | 1.80 | 0.742 (0.73–0.84) |
| ExNext 400, first pass | 2.88 | 2.43 | 0.818 (0.78–0.92) |
| ExNext 400 × 10 | 2.61 | 1.89 | 0.734 (0.71–0.86) |
| ExNext 1,000, first pass | 2.92 | 2.41 | 0.840 (0.78–0.92) |
| ExNext 1,000 × 10 | 2.45 | 1.82 | 0.745 (0.71–0.82) |
| ExAll 1,000 × 10 | 4.12 | 3.87 | 0.954 (0.71–1.20) |
| Sequential write 8 MiB | 0.72 | 0.78 | 1.078 (0.81–2.01) |
| Small read 40 | 1.24 | 1.33 | 1.063 (0.63–1.10) |
| Append 4 KiB × 256 | 1.58 | 1.68 | 1.045 (1.00–1.33) |
| Small create 40 | 1.75 | 1.81 | 1.042 (0.99–1.26) |

ExNext over 1,000 entries costs 54 µs per entry warm instead of 74 µs (PFS3
30 µs), on the first pass 78 µs instead of 94 µs (PFS3 32 µs).

Five phases outside the listings have median paired ratios from 1.038 to
1.078, and ExAll over 40 entries on the first pass 1.054. Append 4 KiB is at
or above 1 in all eight pairs. The host counts no extra
work in these phases, and two of them (sequential and small reads) run code
that did not change at all. The cause is not established; code layout in the
emulated 68040 and the conditions of this run are candidates. The full table
is in `summary.json`.

## Consequence

The read-ahead stays. ExNext is now 1.8–1.9 times PFS3 when repeated and about
2.4 times on the first pass. Whether the small write-side increase is real
needs a run that compares the two handlers again without other changes.
