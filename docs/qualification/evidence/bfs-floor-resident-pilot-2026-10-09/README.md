# Resident root floor pilot evidence

The candidate is rejected under the predeclared retention rule and its active
code is restored to baseline. All 14 starts and 28 BFS/PFS3 outputs pass. The
4KiB growth medians improve approximately 0.5% normal and 5.0% durable, but
repeated deletion/ExNext slowdowns breach the guard. See the
[report](../../bfs-floor-resident-pilot-2026-10-09.md).

`PILOT_SCOPE.md` was fixed before execution. `run-profile.sh` and
`launch-profile.sh` are the executed runner/stop/restore scripts. The exact
schedule includes two diagnostics, eight paired production starts and four
same-baseline controls. No retries, exclusions or extensions were used.
`runner-console.log` retains every START/PASS and restoration status0.
The private remote workspace is
`/home/fabian/.cache/bfs-performance/floor-resident-2026-10-09.mJFQvl`;
do not rerun against its existing results.

Every raw output and all 23 phases remain under `results`, with completion,
mount, startup, configuration, RDB and installed pre/post identity records.
Production and diagnostic guests are pinned separately; 67 asset mappings
match within each cohort, with only the guest differing across cohorts.
`summary.json` preserves all fields and comparisons without pooling or filtering.
`decision.json` applies the exact pre-run rule, not production qualification.
The identical-handler controls and PFS3 calibrators are not noise bounds.
All probe timers remain inclusive; sampled ticks are not extrapolated.

`source-identities.sha256` covers 229 frozen source/critical harness files,
all checked pre/post and in `protocol`. This is an archival snapshot of the
rejected candidate, not a maintained second implementation. It preserves the
six added floor tests and the added probe oracle even though they were removed
from the active checkout on restoration. `core-delta.patch` and
`local/btree-before.c` identify the exact experiment. Restored handler hashes
and build/test logs are under `local`; the production and sidecar binaries
are byte-identical to baseline, and a fresh restored host probe passes its eight
baseline oracles. Current checkout source is intentionally not
identical to the archived candidate after the rejection.

All 584 host and 584 ASAN/UBSAN tests pass for the candidate, plus nine all-call
probe oracles in each configuration and six summary/six decision tests.
Leak detection is disabled on this unsupported Mac runtime. Luna independently
verified both byte-exact JSON regenerations, all actual outputs/metadata,
source checks and the negative retention decision. The report records the
pre-measurement diagnostic correction, source-export setup correction and
summary-guard/unfinished-control rejection; no runtime data was repaired.

Format logs are losslessly base64-encoded and decoded hashes verified. No
licensed guest assets, ROMs, executables or HDFs are included. Previous sealed
evidence remains unchanged; runner and Coffin are restored. No CI or Git/issue
mutation was performed.

From the repository root, reproduce validation with:

```sh
python3 docs/qualification/evidence/bfs-floor-resident-pilot-2026-10-09/summarize_pilot.py
python3 docs/qualification/evidence/bfs-floor-resident-pilot-2026-10-09/test_pilot.py
python3 docs/qualification/evidence/bfs-floor-resident-pilot-2026-10-09/decide_pilot.py
python3 docs/qualification/evidence/bfs-floor-resident-pilot-2026-10-09/test_decision.py
```

The helpers reuse sealed generic protocol parsers and the strict repository
verifiers; they contain no maintained duplicate C protocol inventory.
Verify retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
