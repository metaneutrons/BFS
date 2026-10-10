# Directory layout pilot scope

Fixed before execution, 10 October 2026.

Purpose: check the diagnostic acceptance of
`docs/plans/bfs-directory-layout-v1.md` ("Validation and acceptance") for the
compact directory layout, implemented on branch
`feat/compact-directory-records` on top of `main` (`4ec3d51`), uncommitted.

Exactly two fresh Cachy starts in `deep-compare` mode, BFS first and PFS3
first. The handler is the `BFS_PERF_PROBE` build of the branch
(`make amiga-perf-probe-handler`, detail stride 17). Guest (`fs-compare-bench`
`eb641e32…`), PFS3 (`bc72fac…`), ROM (`68f9d3a…`) and RDB buffers 30 are those
of the listing diagnostic of 10 October
(`bfs-listing-diagnostic-2026-10-10`). The formatter is the Linux `bfs` built
on Cachy from the same source tree, because the pinned formatter `f785d49…`
writes the old directory layout; its SHA-256 is recorded with the other inputs
before the first run. No retries, exclusions or extensions; the runs are not
pooled with earlier ones. Every run must pass
`emulator-test/verify-bench-results.sh` for `deep-compare`.

## Acceptance rule

For phase `LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL`, in each of the two runs,
against the listing diagnostic on `main` (whose counts are identical in both
of its runs):

- `DIR_TREE_LEAF_NODE_VIEWS` ≤ 244, that is 1,171 × 15 / 72; and
- directory-tree device reads, `DIR_TREE_NODE_VIEWS` −
  `DIR_TREE_RESIDENT_VIEWS`, ≤ 240, that is 1,153 × 15 / 72.

15 / 72 is the plan's ratio of leaves for a packed 1,000-entry directory at
4 KiB blocks (the upper end of 11 to 15, against 72). The plan speaks of
measured leaf counts; the bench does not record the leaf count of its
directory, so this fixed ratio replaces it.

The result is "accepted" only if both runs meet both bounds, otherwise "not
accepted", with the numbers. Reported but not criteria: the times and BFS /
PFS3 ratios of every listing phase, inode-tree node views and resident views,
total device reads, and the ExNext phases. No code changes as part of this
step.

The launch script is byte-identical to the one of the listing diagnostic: it
refuses to stop an active CI job, stops the runner and Coffin, and restores
both. No CI, commit, push, merge, installation or issue change is part of this
step.
