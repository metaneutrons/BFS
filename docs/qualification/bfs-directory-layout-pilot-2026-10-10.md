# BFS directory layout pilot

Two fresh `deep-compare` runs of the compact directory layout (branch
`feat/compact-directory-records` on `main` `4ec3d51`, uncommitted) do not meet
the acceptance rule fixed before the run. Directory-tree device reads in
repeated ExAll over 1,000 entries fell from 1,153 to 211, within the bound of
240; directory leaf views fell from 1,171 to 321, above the bound of 244. Every
listing phase is faster than on `main`, and the BFS / PFS3 ratio of every
listing phase fell. The result is "not accepted"; no code was changed for this
step.

## Scope

The [scope](evidence/bfs-directory-layout-pilot-2026-10-10/PILOT_SCOPE.md) was
fixed before the run: the `BFS_PERF_PROBE` build of the branch (detail stride
17, `ba24aad…`), the Linux formatter built on Cachy from the same tree
(`a34639e…`, because the pinned formatter writes the old layout), and the
guest, PFS3, ROM and RDB buffers 30 of the listing diagnostic of 10 October.
BFS first and PFS3 first, no retries. Both runs and both reference runs pass
`emulator-test/verify-bench-results.sh` for `deep-compare`. The series ran
01:38:26–01:39:45 UTC on 10 October 2026; runner and Coffin were restored with
status 0. All counts are identical in both runs, as they are in both runs of
the reference.

Counts are exact. Sampled intervals are every seventeenth call, inclusive and
nested, and include observer overhead; they are reported as means per sampled
call and are not added, subtracted or scaled. The reference is the listing
diagnostic on `main`, not a paired control.

## Acceptance rule

Phase `LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL`, both runs:

| Count | main | Branch | Bound (15 / 72 of main) | Met |
| --- | ---: | ---: | ---: | --- |
| Directory leaf views (`DIR_TREE_LEAF_NODE_VIEWS`) | 1,171 | 321 | 244 | no |
| Directory device reads (node views − resident views) | 1,153 | 211 | 240 | yes |

Leaf views fell to 27% of `main`, device reads to 18%. The bound for leaf
views assumed the plan's ratio of leaf counts for a packed directory; leaf
views also count the re-reads of a leaf across ExAll calls, which do not shrink
with the leaf count. That is an explanation the run does not measure.

## Listing phases

| Phase | main BFS µs (BFS first / PFS3 first) | Branch BFS µs | main BFS / PFS3 | Branch BFS / PFS3 |
| --- | --- | --- | --- | --- |
| ExAll 1,000, first pass | 97,573 / 98,176 | 71,617 / 68,928 | 14.66 / 15.49 | 10.56 / 10.99 |
| ExAll 1,000, ten passes | 898,235 / 931,068 | 737,660 / 689,661 | 18.11 / 18.39 | 14.80 / 13.94 |
| ExAll 400, ten passes | 364,415 / 375,486 | 154,943 / 146,624 | 17.36 / 18.18 | 7.73 / 7.07 |
| ExAll 40, ten passes | 16,769 / 15,360 | 12,735 / 12,863 | 6.55 / 5.86 | 4.97 / 4.37 |
| ExNext 1,000, ten passes | 1,741,816 / 1,736,950 | 1,382,712 / 1,412,217 | 6.29 / 5.85 | 5.00 / 4.39 |
| ExNext 400, ten passes | 655,293 / 684,668 | 407,935 / 417,406 | 5.72 / 5.96 | 3.33 / 3.64 |

Counts for the same phases, identical in both runs of each build:

| Phase | Directory node views (resident) main → branch | Inode-tree node views (resident) main → branch | Device reads main → branch |
| --- | --- | --- | --- |
| ExAll 1,000, ten passes | 2,794 (1,641) → 642 (431) | 1,120 (560) → 1,080 (540) | 1,713 → 751 |
| ExAll 400, ten passes | 1,184 (682) → 280 (280) | 380 (190) → 0 | 692 → 0 |
| ExNext 1,000, ten passes | 6,684 (5,444) → 842 (622) | 1,200 (600) → 1,120 (560) | 1,840 → 780 |
| ExNext 400, ten passes | 2,644 (2,144) → 400 (400) | 360 (180) → 0 | 680 → 0 |

The 400-entry directory and its inodes now stay among the 30 buffers: after
the first pass, its listings read nothing from the device. Over 1,000 entries
the inode tree still misses as before (540 of 1,080 views nonresident).

Mean microseconds per sampled call in repeated ExAll over 1,000 entries (BFS
first):

| Scope | Calls main → branch | main | Branch |
| --- | --- | ---: | ---: |
| Directory node view | 2,794 → 642 | 111.1 | 233.7 |
| Node structure validation | 1,713 → 751 | 34.3 | 84.3 |
| Directory binary search | 504 → 312 | 6.7 | 14.3 |
| ExAll fill (per entry, contains the inode read) | 10,110 → 10,100 | 52.4 | 66.3 |
| Inode read | 10,100 → 10,100 | 48.6 | 51.2 |
| Inode-tree node view | 1,120 → 1,080 | 149.0 | 214.3 |

A directory node view and a node validation cost more per call than on
`main`: a leaf now holds up to 78 records instead of 14, and validation checks
every record, including the name hash of each entry of up to 33 bytes. Total time still fell because the
number of views fell more. The per-entry ExAll fill also costs more; this run
does not attribute that increase.

## Consequence

Under the fixed rule the layout is not accepted. Whether it is retained
despite that is Fabian's decision; the rule is not reinterpreted here. The run
suggests two follow-ups without measuring them: the cost of validating a full
directory leaf (one name hash per head), and the re-views of a leaf across
ExAll calls. Batched inode lookups remain open for the 1,000-entry case.
