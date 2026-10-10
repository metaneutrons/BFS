# Write phase confirmation evidence

Sixteen fresh `compare` starts on Cachy of the ExNext read-ahead pilot's two
handlers, with the handler order reversed. All starts pass the strict
repository verifier. See the
[report](../../bfs-write-phase-confirmation-2026-10-10.md).

`run-profile.sh` was derived from the ExNext read-ahead pilot's script:
header, handler order, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared byte for byte with the copies that ran. `runner-console.log` has all
START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/confirm-2026-10-10.TmkILS`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts
and writes `summary.json` with the reading fixed in the scope.
`source-identities.sha256` covers the 345-file source tree that ran.
Licensed assets, executables, ROMs and HDFs are not included. No CI or
Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-write-phase-confirmation-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
