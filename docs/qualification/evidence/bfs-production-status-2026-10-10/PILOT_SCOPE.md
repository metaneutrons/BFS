# Production status scope

Fixed before execution, 10 October 2026.

Fabian asked to measure after commit and push. This run measures the pushed
state (`b0e062c` on `feat/compact-directory-records`) against the state of
the morning (`5136708`), in one series so that the comparison is paired, and
the 68040 release build as users now get it.

## Handlers

Uninstrumented production handlers built on the Mac with m68k-amigaos-gcc
6.5.0b:

- `base`: commit `5136708`, `make amiga` (`-O2 -m68020`), SHA-256 `c6326fc1…`
  (the `base` of the production baseline pilot).
- `current`: commit `b0e062c`, built from `git archive`, `make amiga`,
  SHA-256 `95a26862…`, byte-identical to the candidate of the ExNext
  read-ahead pilot.
- `release040`: commit `b0e062c` with the release flags for the 68040
  (`-O2 -m68040` since `6801370`), SHA-256 `fdbed91e…`.

## Emulator run

Exactly 24 fresh Cachy starts in `compare` mode: eight rounds, each with all
three handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3
first. Within round r the handlers run in the rotation (r − 1) mod 3 of
`base, current, release040`. Guest `fs-compare-production` (`eb641e32…`), PFS3
(`bc72fac…`), ROM (`68f9d3a…`), formatter (`a34639e…`) and RDB Buffers = 30
are those of the production baseline pilot. No retries, exclusions or
extensions; no pooling with earlier runs.

## Reported

Nothing is retained or reverted. All 24 starts must pass
`emulator-test/verify-bench-results.sh` for `compare`; otherwise the run is
reported as failed and not interpreted. Reported for every phase: the
BFS / PFS3 ratio of each handler (median of eight starts and per order), and
the medians of the paired ratios `current` / `base` and `release040` / `base`
with their geometric means over all phases.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
