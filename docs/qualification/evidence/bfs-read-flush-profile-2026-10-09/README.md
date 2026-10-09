# BFS read and flush local diagnostic evidence

`normal-m5` and `durable-sparse` are complete, strictly validated local Mac
protocol checks, not a paired performance cohort. `failed-stack-guest` is the
preceding rejected emulator-reset attempt; do not use it for speed claims.
All retained outputs are text. Licensed guest assets and binaries are omitted;
installed-asset and runtime receipts retain their SHA256 identities.

See [the report](../../bfs-read-flush-profile-2026-10-09.md) for the diagnostic
scope, input identities, table values, timer limitations and pending Cachy work.
`summary.json` includes every phase from both accepted runs. Node/device and
packet intervals are inclusive and must not be added. Zero ticks do not imply
zero cost. The original Format logs are losslessly base64-encoded, with the
decoded byte digests checked by `encode-format-logs.py`.

Run `python3 summarize-local.py` from this bundle to validate and regenerate
the summary against the current matching repository protocol. Frozen source
snapshots are under `protocol`; source receipts identify the matching revision.
`record-local.sh` collected receipts after the local runs. Do not execute it
in a sealed bundle: it recreates files from ignored local build directories.

`run-profile.sh` defines the separate, pinned, twelve-start Cachy inventory.
It has not run. It requires quiet system services, a stopped Docker runner and
no emulator. Pausing/restoring the Docker runner needs Fabian's authorization.
The prepared remote workspace is recorded in the report. Its fixture/schema
files must match the final source before starting.

Verify retained-file integrity with `shasum -a 256 -c SHA256SUMS` in this
directory. Earlier evidence bundles are separate and unchanged.
