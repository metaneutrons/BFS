# Listing diagnostic evidence

Two fresh `deep-compare` runs of the `main` probe handler (`4ec3d51`), BFS
first and PFS3 first, on Cachy. Both pass the strict repository verifier; all
counts are identical in both runs and to the M5 deep runs of 9 October. See
the [report](../../bfs-listing-diagnostic-2026-10-10.md).

`DIAG_SCOPE.md` was fixed before execution. `run-profile.sh` was derived from
the write-guard pilot's script (inputs, mode `deep-compare`, two runs);
`launch-profile.sh` is byte-identical to it. `runner-console.log` has both
START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator at 23:57:35 UTC. The private remote
workspace is `/home/fabian/.cache/bfs-performance/listing-diag-2026-10-10.xm33KG`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records; format logs are kept as verified base64 text (`encode-format-logs.py`).
`summarize.py` runs the verifier on both runs and on the two M5 reference runs
and writes `summary.json`. `source-identities.sha256` covers the 229-file
source tree that ran. Licensed assets, executables, ROMs and HDFs are not
included. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-listing-diagnostic-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
