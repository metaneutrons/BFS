# BFS directory validation pilot

The cheaper validation of the compact directory layout is retained under the
rule fixed before the run. On the host it removes a third of the instructions
of large listings; in the emulated Amiga handler every compared count is
unchanged and no listing phase became slower in both runs, while the total
times stay within the run-to-run spread.

## Candidate

The candidate changes CPU work only, on branch
`feat/compact-directory-records` (uncommitted, on `main` `4ec3d51`):

- `src/core/dir.c`: case folding through a 256-byte table in the name hash and
  in name comparisons, the 12-byte key comparison as three big-endian words
  instead of `memcmp` (a library call with a byte loop on the Amiga), and the
  zero-padding checks in 32-bit words.
- `src/core/crc32.c`: slicing-by-8 for hosts and AROS, with a block, offset and
  split test against a bitwise reference. The Amiga handler keeps its
  assembly CRC, so this part is judged by the host measurement only.

Nothing on disk changes.

## Host measurement

`tools/core-workload-profile.sh sync` on Cachy (gcc 16.2.1, callgrind). The
replica now also creates a 1,000-entry directory and lists it ten times with
ExNext and with ExAll as the handler does (positions, 93 ExAll entries per
call). x86-64 instructions:

| Phase | Before | After | Change |
| --- | ---: | ---: | ---: |
| ExAll 1,000 × 10 | 26,980,540 | 17,862,079 | −33.8% |
| ExNext 1,000 × 10 | 33,377,097 | 24,226,291 | −27.4% |
| Create 1,000 | 235,357,378 | 183,581,111 | −22.0% |
| Create 40 | 6,899,341 | 5,944,566 | −13.8% |
| Delete 40 | 6,134,569 | 5,161,730 | −15.9% |
| Lookup 400 | 875,104 | 755,985 | −13.6% |

In ExAll 1,000 × 10, `bfs_crc32` falls from 12.9 to 5.3 million instructions
and `dir_entry_ok` with the name hash from 3.0 to 1.6 million. The profiles are
in [`host-profile`](evidence/bfs-directory-validation-pilot-2026-10-10/host-profile).
These counts show the host effect and the direction, not the 68k effect.

## Emulator run

The [scope](evidence/bfs-directory-validation-pilot-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: two fresh `deep-compare` starts of the candidate's
`BFS_PERF_PROBE` handler (`a070349…`), BFS first and PFS3 first, with guest,
PFS3, ROM, buffers and formatter of the directory layout pilot, which is the
reference. Both runs pass `emulator-test/verify-bench-results.sh`. The series
ran 08:40:42–08:42:00 UTC on 10 October 2026; runner and Coffin were restored
with status 0.

Rule 2 as first written required every count field to equal the reference.
Its premise was false: the two reference runs already differ in 36 of 1,701
count fields, all in two append phases whose commits the delayed-commit timer
places. The rule was amended at 08:41 UTC, after the runs had started and
before any result was read, to compare the fields on which the reference runs
agree. The amendment is recorded in the scope.

| Rule | Result |
| --- | --- |
| 1. Strict verifier, both runs | passed |
| 2. Count fields equal to the reference (1,665 compared) | all equal; the 36 timer-dependent fields lie within the reference pair's range in both runs |
| 3. No listing phase ≥ 1.10 × the reference in both runs | none |

Listing times, candidate over reference of the same order:

| Phase | BFS first | PFS3 first |
| --- | ---: | ---: |
| ExAll 1,000, first pass | 1.150 | 1.021 |
| ExAll 1,000 × 10 | 0.947 | 1.009 |
| ExAll 400 × 10 | 0.929 | 1.018 |
| ExAll 40 × 10 | 1.030 | 0.995 |
| ExNext 1,000, first pass | 0.993 | 0.924 |
| ExNext 1,000 × 10 | 1.037 | 0.961 |
| ExNext 400 × 10 | 0.995 | 0.960 |

The ratios scatter on both sides of 1; the 68k effect is not separable from
the spread with two runs. Sampled means in ExAll 1,000 × 10 (candidate /
reference, BFS first and PFS3 first): directory binary search 5.3 / 14.3 and
11.8 / 14.2 µs, node structure validation 77.4 / 84.3 and 79.4 / 83.9 µs.
These samples are few, inclusive and nested; they are not extrapolated.

## Consequence

The validation rework is retained. On the Amiga, the cost of large listings
remains dominated by inode reads and device reads, which this step does not
change.
