# FreeTree allocation goal pilot evidence

Rejected under the predeclared retention rule; active code is restored to the
preceding baseline. All 14 starts, 28 outputs and 23 phases pass correctness.
4KiB growth paired medians are 1.0425 normal and 1.0079 durable candidate/base.
Only one of four target pairs is faster, and durable ExAll-1,000 breaches the
repeated 10% slowdown guard. Diagnostic scratch requests fall 517 to 262 without
establishing a production gain. See the
[report](../../bfs-goal-root-pilot-2026-10-09.md).

`PILOT_SCOPE.md` is fixed before execution; `run-profile.sh` and
`launch-profile.sh` are the executed scripts. `runner-console.log` contains
every START/PASS and restoration status0. The private remote workspace is
`/home/fabian/.cache/bfs-performance/goal-root-2026-10-09.iikmxS`; do not rerun
against its existing results. There are no retries, exclusions or extensions.
Production and diagnostics stay separate from each other and preceding cohorts.

`results` retains all raw protocols and pre/post receipts, completion/mount,
startup, configuration and actual RDB buffers30 records. `summary.json` retains
all fields, both filesystems and every phase. `decision.json` applies the
unchanged pre-run rule. The adapter imports the prior sealed strict verifier;
the decision imports its predeclared policy. No maintained C protocol inventory
or qualification logic is duplicated, and older bundles are unchanged.

`source-identities.sha256` covers the 229-file frozen candidate `protocol`.
It includes all eight new tests and is an archival snapshot, not a maintained
duplicate implementation. Deltas and `local` preimages identify the exact
experiment. Current source intentionally differs after rejection. Both restored
handler identities match their original baseline binaries byte-for-byte.
The fresh restored all-call/write-sidecar host probe passes all eight oracles.

`local` contains all build/test records, independent implementation/test reviews,
source checks and prior-bundle preservation checks. The finalized candidate
passes 586 host and 586 ASAN/UBSAN tests, plus eight probe oracles in each
configuration and six adapter/six shared-policy tests. The early ASAN draft
assertion failures are retained and explained in the report; leak detection is
unsupported here. Format logs preserve original bytes as verified base64 text.
The independent actual-data audit reproduces both JSON files byte-for-byte and
confirms the complete inventory, receipt/configuration checks and rejection.
Licensed assets, executables, ROMs and HDFs are not included. Runner and Coffin
are restored. No CI or Git/issue mutation is performed.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-goal-root-pilot-2026-10-09/summarize_pilot.py
python3 docs/qualification/evidence/bfs-goal-root-pilot-2026-10-09/test_pilot.py
python3 docs/qualification/evidence/bfs-goal-root-pilot-2026-10-09/decide_pilot.py
python3 docs/qualification/evidence/bfs-floor-resident-pilot-2026-10-09/test_decision.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
