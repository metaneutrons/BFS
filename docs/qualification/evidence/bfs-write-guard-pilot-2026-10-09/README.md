# Write protection check pilot evidence

Retained locally under the predeclared rule. All 14 starts, 28 outputs and 23
phases pass correctness. The diagnostic saves exactly one inode read per Write
packet (4KiB growth 515 to 259) and no count field rises. No phase is at least
10% slower in both pairs of a mode. Paired 4KiB growth medians are 0.8477
normal and 0.9884 durable; the identical-baseline controls are 1.1189 and
1.2367, so these ratios establish no speedup. See the
[report](../../bfs-write-guard-pilot-2026-10-09.md).

`PILOT_SCOPE.md` was fixed before execution; `run-profile.sh` and
`launch-profile.sh` are the executed scripts. `launch-profile.sh` is
byte-identical to the goal-root pilot's; `run-profile.sh` differs only in the
two candidate handler hashes, the run-name prefix and the completion marker. `runner-console.log` contains every START/PASS line and the
restoration status 0. The private remote workspace is
`/home/fabian/.cache/bfs-performance/write-guard-2026-10-09.qG6xKe`; do not rerun
against its existing results. There are no retries, exclusions or extensions.

`results` retains all raw protocols, receipts, completion, mount, startup,
configuration and actual RDB records. `summary.json` comes from
`summarize_pilot.py`, which reuses the strict verifier of the floor-resident
pilot with this pilot's schedule and candidate pins. `decision.json` comes from
`decide_pilot.py`, which implements the scope's rule; `test_decision.py` tests
that rule on synthetic summaries. These three scripts were written while the
series ran, after the scope was fixed and before any result was fetched.
`decide_pilot.py` treats a missing `US` ratio as not slower and takes the phase
list from the first pair; neither case occurs in this data (no missing ratio,
23 phases in every pair). An independent audit regenerated both JSON files
byte-for-byte and confirmed the rule, the inventory and the report's figures;
its wording corrections are applied to the report. Format logs are kept as verified base64 text
(`encode-format-logs.py`).

`write-guard-delta.patch` is the exact candidate change against the working
baseline; `local/preimage` holds the baseline versions of the five source
files. `source-identities.sha256` covers the 229-file source tree that was run.
`local` also holds the final host, sanitizer, m68k build and AROS integration
logs, the base and candidate rebuild logs with `handler-identities.sha256`, the
independent review record, a description of `services-restored.log` and the
checks that the 55 earlier evidence bundles are unchanged. Licensed assets, executables, ROMs and HDFs are not
included. Runner and Coffin are restored. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-write-guard-pilot-2026-10-09/summarize_pilot.py
python3 docs/qualification/evidence/bfs-write-guard-pilot-2026-10-09/decide_pilot.py
python3 docs/qualification/evidence/bfs-write-guard-pilot-2026-10-09/test_decision.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
