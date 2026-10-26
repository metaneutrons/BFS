# ExAll result cache repeat scope

Fixed before execution, 23 October 2026 (CEST; the series runs around 22:30 UTC
on 22 October).

The first main series of 22 October missed two criteria of the plan
(`docs/plans/bfs-exall-result-cache-v1.md`, criteria 3 and 4). The null series
of the same day showed that identical handlers meet both criteria as written
but cross the per-phase limit often, and it left open whether the 40-entry
first-pass miss is a cost of the bound or a noisier window. Fabian ordered a
repeat of the main series. It changes no threshold of the plan and none of the
numbers of the first series or of the null series.

## Handlers and run

The same two handlers as in the first series, uninstrumented 68040 release
handlers from `make release` (`-O2 -m68040`): `reference`, SHA-256
`e2e274bb…`, and `bound`, SHA-256 `be30c603…` (the patch of the first series,
`resultcache-handler.patch`). Their hashes are recorded again in
`results/input-identities.sha256` and compared with those of the first series.
Guest `fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`), ROM
(`68f9d3a…`), formatter (`a34639e…`), RDB Buffers = 30, the source tree of
`b8f3e8a` with its emulator scripts, the run script (header and run names
differ) and the verifier are those of the first series. The launch script is
byte-identical.

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
Odd rounds run `reference` before `bound`, even rounds `bound` before
`reference`. New remote workspace and run names (`exallrepeat-…`). No retries,
exclusions or extensions. The series runs detached on Cachy (`setsid nohup`),
and the launch script stops and restores the runner container and Coffin and
refuses to start while a CI worker is active.

## Evaluation, fixed now

All 16 starts must pass `emulator-test/verify-bench-results.sh` for `compare`;
otherwise the run is reported as failed and not interpreted. `summarize.py`
evaluates three things, each from the medians of the paired ratios `bound`
over `reference` of the same round, exactly as in the first series.

1. **The repeat alone**, by the decision rule of the plan as written, criteria 1
   to 6 with the same limits: gain at 1,000 and 400 entries (`r` ≤ 0.50 and
   estimate ≤ 0.55, `r` ≤ 0.55 and estimate ≤ 0.60), each first-pass ExAll
   phase ≤ 1.05, each of the 16 unchanged phases ≤ 1.05 and their geometric
   mean ≤ 1.02, record ≤ 32 bytes and 1,000 entries below 64 KiB (the memory
   probe of the first series), and valid runs. The reading of a criterion is
   `met` or `not met`; no limit is adjusted for noise.
2. **The pooled data**, the same rule on both main series together: the 16
   paired rounds (eight of the first series, eight of the repeat), medians over
   16 rounds per phase, BFS / PFS3 medians over the 16 starts per handler. It is
   labelled as pooled. It is not an independent test, because the repeat was
   ordered after the first series was known.
3. **The comparison of the two series**, per phase: the median paired ratio of
   each, their difference, whether the repeat's median lies inside the first
   series' range, and the BFS / PFS3 medians of both handlers. The two misses
   of the first series are examined individually. The 40-entry first pass of
   ExAll (`LIST_EXALL_40_ENTRIES_FIRST_PASS`, 1.116) and ExNext 40 × 10 in
   `perf` (`LIST_EXNEXT_400`, 1.053) each count as reproduced if the repeat's
   median paired ratio of that phase exceeds 1.05.

Also reported, for the repeat and for the pooled data, with the method of the
null series: the position effect (second start of a round over the first) and
the effect of the handler (`bound` over `reference`), the bootstrap over the rounds (5,000 draws
with a fixed seed) of the geometric mean and the worst of the 16 unchanged
medians and of the largest first-pass ExAll median, and the round-to-round
scatter, the standard deviation of the logarithm of the per-round geometric
mean of the 16 unchanged phases. For comparison it was 0.114 in the first
series and 0.031 in the null series.

No CI, merge, installation or issue change is part of this step, and nothing is
retained in `src/`. Cachy's runner container and Coffin are stopped for the
series by the launch script and restored afterwards. No watcher process of the
session stays on Cachy, and a single-shot user unit that ends itself writes the
marker `SERIES_DONE`.
