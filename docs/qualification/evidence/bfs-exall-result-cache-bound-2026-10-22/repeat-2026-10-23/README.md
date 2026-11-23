# ExAll result cache repeat evidence

Sixteen fresh `compare` starts on Cachy of the two handlers of the
[first main series](../README.md), `reference` (`e2e274bb…`) and `bound`
(`be30c603…`), repeated on 22/23 October 2026 with the same inputs, harness,
schedule and verifier. See the section "Repeat of the main series, 23 October
2026" of the [report](../../bfs-exall-result-cache-bound-2026-10-22.md). The
plan's thresholds and the numbers of the first series and of the null series are
untouched.

`PILOT_SCOPE.md` fixed the evaluation before the first start: the repeat alone
by the decision rule as written, the pooled data of both main series, and the
per-phase comparison with the first series. `run-profile.sh` was derived from the
first series' script: header, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared with the copies that ran. `results/input-identities.sha256` records the
hashes again; they equal those of the first series. The source tree is that of
the first series (`source-identities.sha256`, 345 files, checked before and
after).

The series ran detached on Cachy (`setsid nohup`), independent of the Mac that
started it. `runner-console.log` has all START/PASS lines, the completion marker
and the restoration status. `SERIES_DONE` is the marker a single-shot user unit
wrote when the series process had exited. A first unit watched the wrapper
process instead of the launch script, fired early and wrote a false marker
within seconds of the start. The false marker was deleted and a second unit was
started on the launch script's own PID before the first start had finished;
the series itself was not touched. `services-restored.log` records the runner
container and Coffin running, zero CI worker processes, no benchmark emulator
and no unit, harness or `tail` process of the session after the series. The
private remote workspace is `/home/fabian/.cache/bfs-performance/exallrepeat-2026-10-23.PuwzVz`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts
of the repeat and of the first series, writes `summary.json` and prints the
figures (`summary.txt`). `memory-probe.txt` of the parent directory supplies
criterion 5.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-exall-result-cache-bound-2026-10-22/repeat-2026-10-23/summarize.py
```

Verify all retained files of the evidence directory, this one included, with
`shasum -a 256 -c SHA256SUMS` in the parent directory.
