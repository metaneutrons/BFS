# ExAll result cache bound evidence

Sixteen fresh `compare` starts on Cachy of two uninstrumented 68040 release
handlers: `reference` (`origin/main` at `b8f3e8a`, byte-identical to the
`v0.3.0` handler) and `bound` (the same tree with `resultcache-handler.patch`,
which serves repeated ExAll passes over an unchanged directory from a fixed
array). See the [plan](../../../plans/bfs-exall-result-cache-v1.md), which
fixed the decision rule before the first build, and the
[report](../../bfs-exall-result-cache-bound-2026-10-22.md). Nothing is
retained in `src/`.

`resultcache-handler.patch` applies to `src/amiga/handler.c` at `b8f3e8a`
(`git apply`). `make release` refuses a dirty tree, so both handlers were
built in one scratch worktree with the same procedure: `reference` from
`b8f3e8a`, `bound` from a throwaway local commit of the patch on top of it.
`memory-probe.sh` compiles the bound's types and `candidate-record.c` for the
68040 and reads their sizes from the compiler (`memory-probe.txt`); run it on a
checkout with the patch applied.

`run-profile.sh` was derived from the script of the listing metadata bound of
10 October: header, handler hashes, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared byte for byte with the copies that ran. `runner-console.log` has all
START/PASS lines, the completion marker and the restoration status;
`services-restored.log` records the runner container and Coffin running, zero CI
worker processes and no emulator. The private remote workspace is
`/home/fabian/.cache/bfs-performance/exallcache-2026-10-22.npPP0U`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts,
writes `summary.json` and prints the figures; `summary.txt` is that output.
`source-identities.sha256` covers the 345 files of the source tree that ran,
which is `b8f3e8a` without `docs/qualification`, a directory the run does not
read. Licensed assets, executables, ROMs and HDFs are not included. No CI or
Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-exall-result-cache-bound-2026-10-22/summarize.py
```

The `null-series` subdirectory holds the control series of `reference` against
`reference`, with its own [README](null-series/README.md). Its files are covered
by the same `SHA256SUMS`.
The `repeat-2026-10-23` subdirectory holds the repeat of this series, with its
own [README](repeat-2026-10-23/README.md), covered by the same `SHA256SUMS`.

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
