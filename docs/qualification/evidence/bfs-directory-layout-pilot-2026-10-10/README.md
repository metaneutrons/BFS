# Directory layout pilot evidence

Two fresh `deep-compare` runs of the branch probe handler
(`feat/compact-directory-records`, uncommitted, on `main` `4ec3d51`), BFS first
and PFS3 first, on Cachy. Both pass the strict repository verifier; all counts
are identical in both runs. Under the rule fixed in `PILOT_SCOPE.md` the
result is "not accepted". See the
[report](../../bfs-directory-layout-pilot-2026-10-10.md).

`PILOT_SCOPE.md` was fixed before execution. `run-profile.sh` was derived from
the listing diagnostic's script: header, the branch formatter in the
workspace, the two input hashes, the run names and the completion marker
differ. `launch-profile.sh` is byte-identical to it. `runner-console.log` has
both START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator at 01:40:00 UTC. `formatter-build.log` is
the Cachy build of the formatter. The private remote workspace is
`/home/fabian/.cache/bfs-performance/dirlayout-pilot-2026-10-10.esz8SD`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records; format logs are kept as verified base64 text (`encode-format-logs.py`).
`summarize.py` runs the verifier on both runs and on the two runs of the
listing diagnostic, and writes `summary.json` with the acceptance evaluation.
`source-identities.sha256` covers the 340-file source tree that ran. Licensed
assets, executables, ROMs and HDFs are not included. No CI or Git/issue change
is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-directory-layout-pilot-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
