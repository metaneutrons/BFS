# Listing diagnostic scope

Fixed before execution, 10 October 2026.

Purpose: attribute the cost of large ExAll and ExNext listings on current
`main` (`4ec3d51`), so that the next step can be chosen between batched inode
lookups and the directory-tree layout change of
`docs/plans/bfs-directory-layout-v1.md`. This is a diagnostic, not a
performance comparison: there is no candidate and no retention decision.

Exactly two fresh Cachy starts in `deep-compare` mode, BFS first and PFS3
first. The handler is the `BFS_PERF_PROBE` build of `main`
(`make amiga-perf-probe-handler`, detail stride 17). The guest, PFS3, ROM,
formatter, format v3 and actual RDB buffers 30 are those of the M5 deep
diagnostics of 9 October (`bfs-scan-admission-performance-2026-10-09`) and of
the later pilots; the guest is the pinned `fs-compare-bench` `eb641e32…`. No
retries, exclusions or extensions; the runs are not pooled with earlier ones.

Every run must pass `emulator-test/verify-bench-results.sh` for
`deep-compare`. Counts are exact; sampled ticks are every seventeenth call,
inclusive and nested, and are reported as means per sampled call only. They
are not added, subtracted or scaled into exclusive time. The M5 deep runs serve
as a reference for counts and sampled means, not as a paired control.

The launch script is the unchanged one of the earlier pilots: it refuses to
stop an active CI job, stops the runner and Coffin, and restores both.
No CI, commit, push, merge, installation or issue change is part of this step.
