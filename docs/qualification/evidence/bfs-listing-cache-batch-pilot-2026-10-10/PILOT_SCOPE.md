# Listing cache and inode batch pilot scope

Fixed before execution, 10 October 2026.

Fabian asked to attack the two remaining levers of large listings on the
Amiga: inode reads and device reads. The candidate, on branch
`feat/compact-directory-records` (uncommitted, on `main` `4ec3d51`), adds to
the retained directory validation state:

- A floor for the handler's node cache: at least 64 nodes, but the floor never
  exceeds 256 KiB of nodes (`bfs_cache_mount_slots`); a larger Mountlist
  `Buffers` value still applies, and if the memory is missing the handler
  falls back to `Buffers`. Fabian chose this option on 10 October.
- Inode reads in ascending order (`bfs_btree_sorted_search`,
  `bfs_inode_read_sorted`): neighbouring inodes share one leaf view.
- Batched EXAMINE_ALL: entries are gathered in directory order, as many as
  their smallest size lets fit (up to 64 per batch), their inodes are read in
  one ascending batch, and the entries are written in order. Without the
  batch memory the handler lists entry by entry as before.

Nothing on disk changes.

## Host measurement, already taken

`tools/core-workload-profile.sh sync` on Cachy (callgrind), the replica
mirroring the handler (its slots for Buffers = 30, batched ExAll). x86-64
instructions:

| Phase | Validation state, 30 slots | 64 slots | 64 slots and batches |
| --- | ---: | ---: | ---: |
| ExAll 1,000 × 10 | 17,862,079 | 6,803,495 | 5,941,964 |
| ExNext 1,000 × 10 | 24,226,291 | 13,420,459 | 13,464,801 |

Every other phase stays within 1.5% of the 64-slot column.

## Emulator run

Exactly two fresh Cachy starts in `deep-compare` mode, BFS first and PFS3
first, with the `BFS_PERF_PROBE` build of the candidate (detail stride 17).
Guest (`eb641e32…`), PFS3 (`bc72fac…`), ROM (`68f9d3a…`), RDB buffers 30 and
the formatter (`a34639e…`) are those of the directory validation pilot of
10 October (`bfs-directory-validation-pilot-2026-10-10`), which is the
reference. The Mountlist keeps Buffers = 30; the handler raises it. No
retries, exclusions or extensions; no pooling with earlier runs.

## Retention rule

The candidate is retained if all of the following hold:

1. Both runs pass `emulator-test/verify-bench-results.sh` for `deep-compare`.
2. Phase LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL, each run: directory and
   inode device reads together (node views minus resident views of both
   trees) are at most 75, a tenth of the reference's 751.
3. The same phase, each run: BFS time at most 0.85 times the reference run of
   the same order.
4. No phase of the workload has a BFS time of at least 1.10 times the
   reference run of the same order in both runs.

Otherwise the candidate's changes are reverted. Reported but not deciding:
the times and BFS / PFS3 ratios of every phase, the device reads of every
listing phase, and the mean per sampled call of the inode read, inode search,
directory node view and ExAll fill. Sampled intervals are inclusive and nested
and are not extrapolated. The run cannot separate the cache floor from the
batches; the host measurement attributes them.

The launch script is byte-identical to the one of the earlier pilots: it
refuses to stop an active CI job, stops the runner and Coffin, and restores
both. No CI, commit, push, merge, installation or issue change is part of this
step.
