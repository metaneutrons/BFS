# BFS listing metadata bound

A handler that lists without reading any inode bounds what listing fields in
directory entries can gain (`docs/plans/bfs-listing-metadata-v1.md`). On the
emulated 68040, ExAll over 1,000 entries ten times falls to 0.59 of the
reference, from 4.00 to 2.32 times PFS3; ExNext over 1,000 entries ten times
to 0.78, from 2.63 to 1.97 times PFS3. Phases without listings stay within
5 %. Even without any inode read, no listing phase reaches PFS3.

## Prototype

Both handlers are uninstrumented production builds (`-O2 -m68020`) of commit
`5136708` with the big-endian load change retained earlier the same day.

- `reference` (`ea37a89d…`): that tree as is.
- `bound` (`cc02b398…`): the same tree with `noinode-handler.patch`. The
  batched ExAll takes a zeroed inode for every entry and ExNext fills its
  FileInfoBlock from a zeroed inode; size, protection and date are wrong. The
  guest checks only names, types and counts.

The bound omits what the real change adds: 59 instead of 78 records per
4 KiB directory leaf, copying the fields from the record, and keeping the copy
current. It overstates the gain.

On the host, `tools/core-workload.c` with the same change
(`host-profile/core-workload-noinode.patch`, callgrind on Cachy, gcc 16.2.1)
needs 68 % fewer instructions for ExAll over 1,000 entries ten times
(5,952,384 to 1,898,032) and 38 % fewer for ExNext (13,546,919 to 8,381,840);
lookups are unchanged (758,283).

## Run

The [scope](evidence/bfs-listing-metadata-bound-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: 16 fresh `compare` starts on Cachy, eight rounds of
both handlers, BFS first in odd rounds and PFS3 first in even rounds, handler
order alternating; guest, PFS3, ROM, formatter and RDB Buffers = 30 of the
production baseline pilot. All 16 starts pass
`emulator-test/verify-bench-results.sh`. The series ran 10:16:22–10:25:54 UTC
on 10 October 2026; runner and Coffin were restored with status 0. Nothing is
retained or reverted.

## Results

BFS / PFS3, median of eight starts, and the median of the eight paired ratios
`bound` over `reference`:

| Phase | reference | bound | bound / reference |
| --- | ---: | ---: | ---: |
| ExAll 40 × 10 | 3.37 | 2.14 | 0.631 |
| ExAll 400, first pass | 5.67 | 3.36 | 0.614 |
| ExAll 400 × 10 | 3.85 | 2.36 | 0.642 |
| ExAll 1,000, first pass | 5.78 | 3.21 | 0.557 |
| ExAll 1,000 × 10 | 4.00 | 2.32 | 0.594 |
| ExNext 40 × 10 | 2.43 | 1.91 | 0.817 |
| ExNext 400, first pass | 2.98 | 2.14 | 0.725 |
| ExNext 400 × 10 | 2.51 | 2.03 | 0.777 |
| ExNext 1,000, first pass | 3.14 | 2.14 | 0.745 |
| ExNext 1,000 × 10 | 2.63 | 1.97 | 0.781 |

Per entry, ExAll over 1,000 entries costs 20.3 µs warm and 39.6 µs on the
first pass with inode reads, 11.8 and 22.3 µs without; PFS3 needs 5.0 and
6.8 µs. ExNext costs 73.1 µs per entry warm with inode reads and 57.0 µs
without; PFS3 needs 28.2 µs. The nine phases without listings have median
paired ratios from 0.958 to 1.049. Single paired ratios scatter widely in some
phases (ExAll 1,000 first pass 0.43–0.93); every listing median lies below
0.83. The full table is in `summary.json`.

## Consequence

Listing fields in directory entries can remove about 40 % of ExAll and about
20–25 % of ExNext on the 68k, less after the costs the bound omits. What
remains is the directory scan and the fill per entry (ExAll about 12 µs
against PFS3's 5 µs) and, for ExNext, the cost of each call resuming the scan.
The decision on the format belongs to Fabian.
