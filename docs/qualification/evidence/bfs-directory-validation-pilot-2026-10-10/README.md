# Directory validation pilot evidence

Host profiles and two fresh `deep-compare` runs of the candidate probe handler
(`feat/compact-directory-records`, uncommitted, on `main` `4ec3d51`), BFS
first and PFS3 first, on Cachy. Both runs pass the strict repository verifier.
Under the rule in `PILOT_SCOPE.md`, including its recorded amendment, the
candidate is retained. See the
[report](../../bfs-directory-validation-pilot-2026-10-10.md).

`host-profile` holds the output of `tools/core-workload-profile.sh sync 30`
on Cachy before (`baseline-sync.tsv`) and after (`candidate-sync.tsv`) the
candidate, with the replica already extended by the 1,000-entry listing.

`run-profile.sh` was derived from the directory layout pilot's script: header,
handler hash, run names and completion marker differ. `launch-profile.sh` is
byte-identical to it. `runner-console.log` has both START/PASS lines, the
completion marker and the restoration status 0; `services-restored.log`
records the runner container and Coffin running, zero CI worker processes and
no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/dirvalid-pilot-2026-10-10.U56ZG2`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records; format logs are kept as verified base64 text (`encode-format-logs.py`).
`summarize.py` runs the verifier on both runs and on the two reference runs and
writes `summary.json` with the retention evaluation. `source-identities.sha256`
covers the 340-file source tree that ran. Licensed assets, executables, ROMs
and HDFs are not included. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-directory-validation-pilot-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
