# Production status evidence

Twenty-four fresh `compare` starts on Cachy of three uninstrumented handlers:
`base` (commit `5136708`), `current` (commit `b0e062c`, `make amiga`) and
`release040` (commit `b0e062c` with the 68040 release flags). All starts pass
the strict repository verifier. Nothing is retained or reverted. See the
[report](../../bfs-production-status-2026-10-10.md).

`run-profile.sh` was derived from the production baseline pilot's script:
header, handler hashes and names, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared byte for byte with the copies that ran. `runner-console.log` has all
START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/status-2026-10-10.NvWrOR`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 24 starts
and writes `summary.json`. `source-identities.sha256` covers the 327-file
source tree of `b0e062c` that ran (`git archive` without
`docs/qualification`). Licensed assets, executables, ROMs and HDFs are not
included. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-production-status-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
