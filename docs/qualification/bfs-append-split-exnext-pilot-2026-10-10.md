# BFS append split and ExNext inode batch pilot

Of two changes measured separately under rules fixed before the run, the
ExNext inode batch is retained and the append split is not. ExNext over 1,000
entries ten times takes 0.819 of its previous time (1.85 to 1.56 times PFS3
against the reference), the first pass over 1,000 entries 0.870. The append
split reached its target (ExAll over 1,000 entries, first pass, 0.892) but
small-file creation was 1.101 of the reference, above the bound of 1.10, so
it is reverted with its tests.

## Changes

- **Append split** (`src/core/btree.c`): a key past the end of the last leaf
  splits that leaf at its end, so the left leaf keeps all keys but one. After
  1,000 empty files are created in one directory, their inodes fill 16 leaves
  instead of 30. Reverted; the patch, its test suite and the three test
  adaptations it needed are kept in `reverted/`.
- **ExNext inode batch** (`src/amiga/handler.c`, `src/core/inode.c`): the
  ExNext read-ahead reads the inodes of a gathered batch in one ascending
  batch. An entry uses its copy only while the inode tree's root and
  generation, the recovery generation and a new version counter of the
  pending inode table are unchanged, and reads its inode again otherwise.
  `tools/core-workload.c` mirrors it. The new emulator test `exnextfresh_55`
  changes protection bits and grows a file through an open handle in the
  middle of a listing; it fails when the validity check is removed.

## Run

The [scope](evidence/bfs-append-split-exnext-pilot-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: 24 fresh `compare` starts on Cachy of `ref`
(`2a212c8`, `95a26862…`), `split` (`1456564c…`) and `cand` (split plus batch,
`bd16e902…`), eight rounds with alternating order and rotating handler order;
guest, PFS3, ROM, formatter and RDB Buffers = 30 of the earlier pilots of the
day. All 24 starts pass `emulator-test/verify-bench-results.sh`. The series
ran 15:30:57–15:45:09 UTC on 10 October 2026; runner and Coffin were restored
with status 0.

| Rule | Append split (`split` / `ref`) | ExNext batch (`cand` / `split`) |
| --- | --- | --- |
| Target phase | ExAll 1,000 first pass: 0.892 (≤ 0.97) | ExNext 1,000 × 10: 0.819 (≤ 0.95) |
| Geometric mean ≤ 1.00 | 0.990 | 0.964 |
| No phase ≥ 1.10 | small create 1.101 (0.80–1.51) | none (highest 1.096) |
| Retained | no | yes |

## Results

Median paired ratios:

| Phase | `split` / `ref` | `cand` / `split` | `cand` / `ref` |
| --- | ---: | ---: | ---: |
| ExNext 1,000 × 10 | 0.996 | 0.819 | 0.823 |
| ExNext 1,000, first pass | 0.946 | 0.870 | 0.819 |
| ExNext 400 × 10 | 0.994 | 0.820 | 0.826 |
| ExNext 40, first pass | 0.862 | 0.880 | 0.744 |
| ExAll 1,000, first pass | 0.892 | 1.007 | 0.905 |
| ExAll 1,000 × 10 | 0.999 | 0.994 | 0.998 |
| Small create 40 | 1.101 | 0.954 | 1.000 |
| Append 1 KiB × 256 | 0.951 | 1.096 | 1.012 |

BFS / PFS3 of `cand`: ExNext over 1,000 entries 1.56 ten times and 1.97 on the
first pass; ExAll 3.89 and 5.60. The full table is in `summary.json`. Single
paired ratios scatter widely in the phases near the bound: small create
0.80–1.51 for `split` / `ref`, append 1 KiB 0.86–1.39 for `cand` / `split`.

## Consequence

The ExNext inode batch stays; the handler that stays has not been measured as
such, because the batch was measured on top of the split; it passes the
FS-UAE suite on the AROS ROM. The append split is reverted under the rule,
although it shortened the first ExAll pass by about a tenth; whether its
small-create result is a real cost is open and would need a new scope, for
example a probe comparison of that phase.
