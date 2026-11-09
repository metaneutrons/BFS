# ExAll result cache null series scope

Fixed before execution, 22 October 2026.

The [report](../../../bfs-exall-result-cache-bound-2026-10-22.md) found the
first-listing and no-regression criteria of the plan
(`docs/plans/bfs-exall-result-cache-v1.md`, criteria 3 and 4) narrowly missed
and said that a series of `reference` against itself would show how much bias
this pairing has on Cachy. Fabian ordered that series. It measures what the
pairing machinery produces when both handlers are the same bytes, so that the
limits of 1.05 per phase and 1.02 for the geometric mean can be judged against
the host's own bias and spread. It does not change the decision rule or the
numbers of the main series.

## Handlers

One uninstrumented 68040 release handler under two names, so that the harness
treats them as two handlers: `reference-a` and `reference-b`, both the
`reference` handler of the main series (`origin/main` at `b8f3e8a`, `make
release`, `-O2 -m68040`, SHA-256 `e2e274bb…`, byte-identical to the `v0.3.0`
handler). The copies are made from the one file and their SHA-256 are recorded
and compared before the series (`inputs.sha256`).

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
names. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
Odd rounds run `reference-a` before `reference-b`, even rounds `reference-b`
before `reference-a`. Guest `fs-compare-production` (`eb641e32…`), PFS3
(`bc72fac…`), ROM (`68f9d3a…`), formatter (`a34639e…`), RDB Buffers = 30, the
source tree of `b8f3e8a` with its emulator scripts and the verifier are those
of the main series; the guest, PFS3, ROM and formatter come from the same
files on Cachy. The run script differs from the main series' only in the
header, the handler names and hashes, the run names and the completion marker.
The launch script is byte-identical. No retries, exclusions or extensions; no
pooling with other runs.

## Reported

All 16 starts must pass `emulator-test/verify-bench-results.sh` for `compare`;
otherwise the run is reported as failed and not interpreted. For all 23
phases: the median BFS / PFS3 ratio of each name, the median of the eight
paired ratios `reference-b` over `reference-a` in the same round, and their
range. Evaluated with the logic of the main series' `summarize.py` on these
ratios, all of which belong to unchanged code:

- the worst phase and the geometric mean over the 16 phases the bound does not
  change (the nine without listings and the seven ExNext phases);
- the three first-pass ExAll phases, each against 1.05, and every phase
  against 1.05;
- whether the series itself meets criteria 3 and 4 as written.

The position effect, second start of a round over first start, is estimated
from the alternation. With `b` over `a` as `r_odd` in odd rounds (`b` second)
and `r_even` in even rounds (`b` first), the position effect is
`sqrt(r_odd / r_even)` and the effect of the name `sqrt(r_odd · r_even)`. They
are computed per phase from the medians of the four rounds of each kind, as the
rule uses medians, and combined as the geometric mean over the 16 phases and
over all 23; the same from the means of the logarithms of the per-round
geometric means is kept for comparison. Both starts of a round share the
filesystem order, so it cancels in the paired ratio. The sampling spread of the
statistics of criteria 3 and 4 (the geometric mean and the worst of the 16
medians, the largest first-pass ExAll median) and of the two effects is
estimated by a bootstrap over the rounds, 5,000 draws of four odd and four even
rounds, with a fixed seed. The same analysis is run on the paired ratios of
the main series, with `reference` and `bound` as the two names, so that both
can be read side by side; that changes none of its numbers.

No CI, merge, installation or issue change is part of this step, and nothing
is retained in `src/`. Cachy's runner container and Coffin are stopped for the
series by the launch script and restored afterwards, and no watcher process of
the session is left behind.
