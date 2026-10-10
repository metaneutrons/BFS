# Append 4 KiB attribution evidence

Two runs on Cachy compare three handlers: `ref` (the reference of the ExNext
read-ahead pilot), `mid` (`6ed386b`) and `cand` (`b0e062c`), in production
builds (`compare`) and write-probe builds (`split-write-compare`). See the
[report](../../bfs-append-attribution-2026-10-10.md).

`run1-failed` stopped at its first write-probe start on an
`INODE_READ sidecar counter mismatch`. Under its scope the whole run is failed
and not interpreted; its 24 completed production starts and the failing
start are kept as raw records, and its `summarize.py`, written before the run,
was not applied. Remote workspace: `/home/fabian/.cache/bfs-performance/append-attr-2026-10-10.XtdDJw`.

`run2` repeats it with write probes that count sorted inode reads in the
sidecar (`run2/sorted-sidecar.patch`, applied to each probe source). All 30
starts pass the strict repository verifier. `run2/summarize.py` writes
`run2/summary.json` with the paired ratios of part A and the counters and
sampled times of part B. Remote workspace: `/home/fabian/.cache/bfs-performance/append-attr2-2026-10-10.xibtLq`.

In each run, `run-profile.sh` derives from the production status run's
script; `launch-profile.sh` is byte-identical to the earlier pilots'. Scripts
and scopes were compared byte for byte with the copies that ran.
`runner-console.log` and `services-restored.log` record the series and the
restoration of runner and Coffin (status 1 for run 1, which failed; 0 for
run 2). `results` keeps raw protocols, receipts, configuration, startup and
RDB records and `schedule.tsv`; format logs are kept as verified base64 text.
`source-identities.sha256` covers the 327-file source tree that ran. Licensed
assets, executables, ROMs and HDFs are not included. No CI or Git/issue change
is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-append-attribution-2026-10-10/run2/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
