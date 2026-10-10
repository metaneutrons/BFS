# BFS inode write-back pilot

Inode write-back within a transaction is retained under the rule fixed before
the run. On the emulated 68040, appending 4 KiB packets takes 0.771 of the
previous time (2.12 to 1.59 times PFS3), 1 KiB packets 0.757 (2.49 to 1.84)
and the 8 MiB sequential write 0.796 (0.96 to 0.75). The gain is close to the
bound measured the same day.

## Candidate

`docs/plans/bfs-inode-write-back-v1.md` describes the design: a table of eight
pending inodes on the live inode tree. The first publication of an inode in a
transaction writes the tree; later ones replace the pending copy; every read
returns it; every other write or delete supersedes it; the commit and fsck
write it, and a reload of the committed state drops it. Both handlers are
uninstrumented `make amiga` builds (`-O2 -m68020`):

- `reference`: commit `5136708` plus the big-endian load change, `ea37a89d…`.
- `candidate`: the same plus write-back, `2c3645ba…`.

Before the run the candidate passed `make host-test` with twelve new tests
(seven mutations of the mechanism are each caught), the same tests under
ASAN/UBSAN, the quality gates including a new check against raw access to
inode trees, `make analyze`, the FS-UAE integration suite on Cachy (52 of 52)
and the FUSE qualification on Cachy.

## Run

The [scope](evidence/bfs-inode-write-back-pilot-2026-10-10/PILOT_SCOPE.md) was
fixed before the run: 16 fresh `compare` starts on Cachy, eight rounds of both
handlers, BFS first in odd rounds and PFS3 first in even rounds, handler order
alternating; guest, PFS3, ROM, formatter and RDB Buffers = 30 of the
production baseline pilot. The series ran 11:52:14–12:01:49 UTC on 10 October
2026; runner and Coffin were restored with status 0.

| Rule | Result |
| --- | --- |
| 1. All 16 starts pass the strict verifier | passed |
| 2. APPEND_4K_1M and APPEND_1K_256K median paired ratio ≤ 0.90 | 0.771 and 0.757 |
| 3. Geometric mean of the per-phase median paired ratio ≤ 1.00 | 0.977 |
| 4. No phase with a median paired ratio ≥ 1.10 | none (highest 1.069) |

## Results

| Phase | BFS / PFS3 reference | candidate | candidate / reference (range) |
| --- | ---: | ---: | --- |
| Append 4 KiB × 256 | 2.12 | 1.59 | 0.771 (0.71–1.05) |
| Append 1 KiB × 256 | 2.49 | 1.84 | 0.757 (0.72–1.28) |
| Sequential write 8 MiB | 0.96 | 0.75 | 0.796 (0.65–1.22) |
| Small delete 40 | 1.80 | 1.73 | 0.962 (0.78–1.04) |
| Small create 40 | 1.78 | 1.86 | 1.059 (0.91–1.73) |
| ExAll 400, first pass | 5.50 | 5.69 | 1.069 (0.98–1.42) |
| ExAll 400 (one pass) | 3.06 | 3.26 | 1.054 (0.99–1.49) |

Per Write packet, appending 4 KiB costs 212 µs instead of 273 µs (PFS3
131–135 µs), 1 KiB 174 µs instead of 229 µs (PFS3 91–93 µs). Five phases
have median paired ratios between 1.03 and 1.07: lookup and three ExAll
phases, which publish nothing, and small create, which publishes each inode
once and so writes the tree as before. Every inode read now looks through the
eight slots, which costs a little CPU; the single starts of these
phases scatter more (up to 1.73 and 2.54) than the medians differ from 1. The
full table is in `summary.json`.

## Consequence

The write-back change is retained. Appends remain the largest write-side gap
(1.6–1.8 times PFS3); the rest of their cost is the allocation and mapping of
each new block. A counter of used slots would let reads skip the table when
it is empty.
