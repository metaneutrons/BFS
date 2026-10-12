# v0.3.0 performance evidence

Sixteen fresh `compare` starts on Cachy of two uninstrumented 68040 release
handlers: `previous` (commit `b0e062c`) and `v030` (tag `v0.3.0`). All starts
pass the strict repository verifier. See the
[report](../../bfs-v0.3.0-performance-2026-10-12.md).

`run-profile.sh` was derived from the production status script of 10 October:
header, handler hashes and names, the two-handler schedule, run names and
completion marker differ. `launch-profile.sh` is byte-identical to it. Both
scripts and the scope were compared byte for byte with the copies that ran.
`runner-console.log` has all START/PASS lines, the completion marker and the
restoration status 0; `services-restored.log` records the runner container and
Coffin running, zero CI worker processes and no benchmark emulator. The
private remote workspace is
`/home/fabian/.cache/bfs-performance/v030-2026-10-12.9xUESy`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts
and writes `summary.json`.

The workspace held the full `git archive` of `v0.3.0` (18,302 files). The
retained `source-identities.sha256` and `results/source-check.*.log` omit the
17,966 files under `docs/qualification`, which the run does not read, and keep
the 336 others; `results/source-check-full.txt` records the SHA-256 and the
all-OK result of the full lists before and after the run. Licensed assets,
executables, ROMs and HDFs are not included. No CI or Git/issue change is
made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-v0.3.0-performance-2026-10-12/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
