# ExAll result cache null series evidence

Sixteen fresh `compare` starts on Cachy of one 68040 release handler under two
names, `reference-a` and `reference-b`: the `reference` handler of the
[main series](../README.md) (`e2e274bb…`) copied byte for byte (`inputs.sha256`
and `results/input-identities.sha256` show the three digests equal). The series
shows what the pairing machinery produces when both handlers are the same
bytes. See the addendum to the
[report](../../../bfs-exall-result-cache-bound-2026-10-22.md); the plan's
decision rule and the main series' numbers are untouched.

`run-profile.sh` was derived from the main series' script: header, handler
names and hashes, run names and completion marker differ. `launch-profile.sh`
is byte-identical to it. Both scripts and the scope were compared with the
copies that ran. `runner-console.log` has all START/PASS lines, the completion
marker and the restoration status 0 (12:34:48 UTC). `SERIES_DONE` is the marker a
single-shot user unit wrote when the series process had exited.
`services-restored.log`, written at 14:24 UTC, records the runner container and
Coffin running, zero CI worker processes, no benchmark emulator, and no unit,
harness or `tail` process of the session. The series ran detached on Cachy
(`setsid nohup`), independent of the Mac that started it. The private remote workspace is
`/home/fabian/.cache/bfs-performance/exallnull-2026-10-22.1MCbV7`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts,
writes `summary.json` and prints the figures (`summary.txt`). It takes
`--results`, `--names` and `--out`; `main-series-same-analysis.txt` and `.json`
are its output on the main series' starts with `--names reference bound`, so
that the position effect, the bootstrap and the criteria statistics can be read
for both series in the same form. The source tree is that of the main series
(`source-identities.sha256`, 345 files, checked before and after).

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-exall-result-cache-bound-2026-10-22/null-series/summarize.py
python3 docs/qualification/evidence/bfs-exall-result-cache-bound-2026-10-22/null-series/summarize.py \
    --results docs/qualification/evidence/bfs-exall-result-cache-bound-2026-10-22/results \
    --names reference bound --out /tmp/main-series-same-analysis.json
```

Verify all retained files of the evidence directory, this one included, with
`shasum -a 256 -c SHA256SUMS` in the parent directory.
