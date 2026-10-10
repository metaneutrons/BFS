# BFS listing diagnostic on main

Two fresh `deep-compare` runs of the current `main` (`4ec3d51`) attribute the
cost of large listings. Inode reads and directory node views remain the two
large scopes. The leaf-range hints answer 94% of the inode lookups without a
descent, but each read still costs about 50 microseconds per sampled call, and
the remaining misses read evicted inode leaves from the device. Directory node
views became cheaper with the sparse-key CRC and are unchanged in number. This
is a diagnostic: there is no candidate and no retention decision.

## Scope

The [scope](evidence/bfs-listing-diagnostic-2026-10-10/DIAG_SCOPE.md) was fixed
before the run: the `BFS_PERF_PROBE` build of `main` (detail stride 17), the
pinned guest `eb641e32…`, PFS3, ROM, formatter, format v3 and RDB buffers 30 of
the 9 October M5 deep diagnostics, BFS first and PFS3 first. Both runs pass
`emulator-test/verify-bench-results.sh` for `deep-compare`. The series ran
23:55:00–23:56:20 UTC on 9 October 2026; runner and Coffin were restored with
status 0.

Counts are exact. Sampled intervals are every seventeenth call, inclusive and
nested, and include observer overhead; they are reported as means per sampled
call and are not added, subtracted or scaled. The M5 deep runs of 9 October are
a reference, not a paired control.

## Repeated ExAll over 1,000 entries (ten passes)

Every count is identical in both runs and identical to M5: 10,100 inode reads
with 100 exact-key and 9,440 leaf-range hint hits; 1,120 inode-tree node views,
560 of them nonresident; 2,794 directory node views, 1,153 of them nonresident;
1,713 device reads with CRC checks.

| Run | BFS microseconds | PFS3 microseconds | BFS / PFS3 |
| --- | ---: | ---: | ---: |
| main, BFS first | 898,235 | 49,601 | 18.11 |
| main, PFS3 first | 931,068 | 50,625 | 18.39 |
| M5, BFS first | 1,069,433 | 49,792 | 21.48 |
| M5, PFS3 first | 1,073,338 | 71,359 | 15.04 |

Mean microseconds per sampled call, main runs, with M5 for reference:

| Scope | Calls | main | M5 |
| --- | ---: | --- | --- |
| ExAll fill (per entry, contains the inode read) | 10,110 | 52.4 / 53.5 | 55.1 / 56.5 |
| Inode read | 10,100 | 48.6 / 53.2 | 52.4 / 55.2 |
| Inode search | 10,100 | 33.9 / 36.8 | 36.9 / 39.9 |
| Inode validation | 10,100 | 2.5 / 3.8 | 3.6 / 3.3 |
| Inode-tree node view | 1,120 | 149.0 / 159.2 | 158.4 / 176.7 |
| Directory node view | 2,794 | 111.1 / 120.5 | 160.1 / 177.9 |
| Node structure validation | 1,713 | 34.3 / 29.8 | 22.6 / 30.5 |
| Cache peek | 14,575 | 4.2 / 4.8 | 5.4 / 4.5 |

The directory node view is about 30% cheaper than in M5, consistent with the
sparse-key CRC on directory leaves; the number of views and reads is the same.
The inode read is unchanged within the spread. Its nested node views occur only
on the 560 lookups that miss the hints (two views each). Half of those views
are device reads of inode leaves that are no longer among the 30 buffers;
displacement by the directory leaves is the likely cause, but this run does not
measure it. The hinted lookups carry the rest of the inode-read cost.

## Other listing phases

| Phase | main BFS / PFS3 | M5 BFS / PFS3 |
| --- | --- | --- |
| ExAll 1,000, first pass | 14.66 / 15.49 | 17.07 / 15.14 |
| ExNext 1,000, ten passes | 6.29 / 5.85 | 6.74 / 5.83 |
| ExAll 40, ten passes | 6.55 / 5.86 | 5.97 / 7.20 |

Repeated ExNext over 1,000 entries makes 6,684 directory node views and 1,840
device reads for 10,010 inode reads. All other counts and the remaining phases
are in the [summary](evidence/bfs-listing-diagnostic-2026-10-10/summary.json).

## Consequence for the next step

Both levers remain relevant. The directory-tree layout of
`docs/plans/bfs-directory-layout-v1.md` reduces directory node views and reads,
and with fewer directory leaves in the 30 buffers, inode leaves are evicted less
often, which addresses the hint misses as well. Batched, sorted inode lookups
would mainly reduce the per-lookup cost of the hinted path; they do not change
the directory side. These are expectations from the attribution, not measured
effects.
