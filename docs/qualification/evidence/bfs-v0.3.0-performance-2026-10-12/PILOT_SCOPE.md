# v0.3.0 performance scope

Fixed before execution, 12 October 2026.

Fabian asked for a new performance measurement after the v0.3.0 release. The
preliminary README figures come from the 68040 release build of the
pre-release commit `b0e062c` ([production status of 10 October](../../bfs-production-status-2026-10-10.md)).
This run measures the 68040 handler of v0.3.0 against that handler in one
paired series and leaves every other input unchanged.

## Handlers

Uninstrumented release handlers built on the Mac with m68k-amigaos-gcc
6.5.0b and the release flags for the 68040 (`-O2 -m68040`):

- `previous`: commit `b0e062c`, SHA-256 `fdbed91e…`, the `release040` input
  of the 10 October series. Rebuilt today from `b0e062c` with `make release`
  to the same bytes, so the toolchain has not changed since.
- `v030`: tag `v0.3.0` (`336a532d`), `make release`,
  `build/release/bfshandler.040`, SHA-256 `e2e274bb…`. Byte identity with
  the handler inside the signed release archive is not checked.

Between them lie the batched inode reads for ExNext (`fba03cb`), the
directory-walk refactoring for the Codacy limits (`470b427`) and the DOS
string conversions of #95; no on-disk format change.

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3
first. Odd rounds run `previous` before `v030`, even rounds `v030` before
`previous`. Guest `fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`),
ROM (`68f9d3a…`), formatter (`a34639e…`) and RDB Buffers = 30 are those of the
10 October series. The emulator scripts come from `v0.3.0`; `run-bench.sh`,
`build-bench-image.sh`, `bench-config.sh`, `verify-bench-results.sh` and the
guest source are unchanged since `b0e062c`. No retries, exclusions or
extensions; no pooling with earlier runs.

## Reported

All 16 starts must pass `emulator-test/verify-bench-results.sh` for
`compare`; otherwise the run is reported as failed and not interpreted.
Reported for every phase: the BFS / PFS3 ratio of each handler (median of
eight starts and per order), and the median of the paired ratio
`v030` / `previous` with its geometric mean over all phases.

If the run passes, the README's preliminary figures are replaced by the
`v030` medians. The launch script is byte-identical to the one of the earlier
pilots. No CI, merge, installation or issue change is part of this step.
