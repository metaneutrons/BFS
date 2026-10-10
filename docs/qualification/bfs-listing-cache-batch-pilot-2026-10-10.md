# BFS listing cache and inode batch pilot

A node-cache floor of 64 blocks and batched inode reads in ExAll are retained
under the rule fixed before the run. Repeated ExAll over 1,000 entries now
reads nothing from the device and takes 40–44% of its previous time; its
BFS / PFS3 ratio falls from 14.1 and 13.9 to 5.5 and 5.9. No phase of the
workload became slower in both runs.

## Candidate

On branch `feat/compact-directory-records` (uncommitted, on `main`
`4ec3d51`), on top of the retained directory validation state:

- `bfs_cache_mount_slots`: the handler keeps at least 64 nodes, but the floor
  never exceeds 256 KiB of nodes; a larger Mountlist `Buffers` value applies,
  and without the memory the handler falls back to `Buffers`. Fabian chose
  this option on 10 October.
- `bfs_btree_sorted_search` and `bfs_inode_read_sorted`: inodes read in
  ascending order share one view of their leaf, found through the leaf hints
  or one descent. A test compares every result with single reads at 1 and
  4 KiB, with and without the cache and its hints.
- Batched EXAMINE_ALL: entries are gathered in directory order, as many as
  their smallest size lets fit and at most 64, their inodes are read in one
  ascending batch, and the entries are written in order. Without the batch
  memory the handler lists entry by entry as before.

The host replica (`tools/core-workload.c`) mirrors the handler: its slots for
Buffers = 30 and the batched ExAll. Nothing on disk changes.

## Host measurement

`tools/core-workload-profile.sh sync` on Cachy, x86-64 instructions:

| Phase | Validation state, 30 slots | 64 slots | 64 slots and batches |
| --- | ---: | ---: | ---: |
| ExAll 1,000 × 10 | 17,862,079 | 6,803,495 | 5,941,964 |
| ExNext 1,000 × 10 | 24,226,291 | 13,420,459 | 13,464,801 |
| Lookup 400 | 755,115 | 749,480 | 755,880 |

The first version of the sorted search shared a hint helper with the single
search, which the compiler then stopped inlining: single searches became 1–6%
more expensive. The helpers are now inlined by force, and the dir-tree-only
hint is not shared; every phase without batches stays within 1.5% of the
64-slot column. The profiles are in
[`host-profile`](evidence/bfs-listing-cache-batch-pilot-2026-10-10/host-profile).

## Emulator run

The [scope](evidence/bfs-listing-cache-batch-pilot-2026-10-10/PILOT_SCOPE.md)
was fixed before the run: two fresh `deep-compare` starts of the candidate's
`BFS_PERF_PROBE` handler (`d370e97…`), BFS first and PFS3 first, against the
directory validation pilot as reference, with its guest, PFS3, ROM, formatter
and Mountlist Buffers = 30. Both runs pass
`emulator-test/verify-bench-results.sh`. The series ran 09:07:55–09:09:12 UTC
on 10 October 2026; runner and Coffin were restored with status 0. Before the
start, the run script still carried the previous handler's hash; it was
corrected before the launch, and the input check confirms the candidate.

| Rule | BFS first | PFS3 first |
| --- | --- | --- |
| 1. Strict verifier | passed | passed |
| 2. ExAll 1,000 × 10 device reads ≤ 75 (reference 751) | 0 | 0 |
| 3. ExAll 1,000 × 10 time ≤ 0.85 × reference | 0.403 | 0.440 |
| 4. No phase ≥ 1.10 × reference in both runs | none | |

Listing phases, BFS / PFS3, candidate (reference), BFS first and PFS3 first:

| Phase | BFS first | PFS3 first | Device reads, dir + inode |
| --- | --- | --- | --- |
| ExAll 1,000, first pass | 7.60 (12.37) | 11.54 (11.00) | 20 + 29 (21 + 56) |
| ExAll 1,000 × 10 | 5.55 (14.12) | 5.87 (13.94) | 0 (211 + 540) |
| ExAll 400 × 10 | 5.46 (7.14) | 5.43 (7.41) | 0 (0) |
| ExAll 40 × 10 | 4.82 (5.25) | 3.27 (5.13) | 0 (0) |
| ExNext 1,000, first pass | 3.84 (4.17) | 3.82 (4.46) | 20 + 31 (20 + 58) |
| ExNext 1,000 × 10 | 3.53 (5.02) | 3.62 (4.66) | 0 (220 + 560) |
| ExNext 400 × 10 | 3.15 (3.63) | 4.03 (3.59) | 0 (0) |

The other phases change by −19% to +5.4% in single runs, none by 10% in both;
the full table is in `summary.json`. Mean per sampled call in ExAll 1,000 × 10
(BFS first, candidate / reference): inode read 20.4 / 50.5 µs, inode search
5.7 / 35.8 µs, ExAll fill 7.6 / 63.5 µs (the fill no longer contains the
inode read), directory node view 10.9 / 189.6 µs. These samples are
inclusive and nested; they are not extrapolated.

## Consequence

The candidate is retained. The cache floor removes the device reads of
directories up to about 1,000 entries; the batches lower the cost per inode
read. ExAll over 400 and 1,000 entries is now 5.4–5.9 times PFS3, close to
but still above the five-times goal. Larger directories still exceed the
cache, and ExNext reads its inodes one at a time.
