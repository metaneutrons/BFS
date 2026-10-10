# Append split and ExNext inode batch pilot evidence

Twenty-four fresh `compare` starts on Cachy of three uninstrumented handlers:
`ref` (commit `2a212c8`), `split` (plus the append split) and `cand` (plus
the ExNext inode batch). All starts pass the strict repository verifier.
Under the rule in `PILOT_SCOPE.md` the ExNext inode batch is retained and the
append split is not. See the
[report](../../bfs-append-split-exnext-pilot-2026-10-10.md).

`reverted` keeps the append split as it was measured: the core change and
the three test adaptations it needed (`append-split-and-test-adaptations.patch`)
and its test suite (`test_btree_append_split.c`).

`run-profile.sh` was derived from the production status run's script:
header, handler hashes and names, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared byte for byte with the copies that ran. `summarize.py` was derived
from the production status run's as well; its first version still selected
the phases from a handler called `base` and stopped with a KeyError before
writing anything; the selection now uses `ref`. `runner-console.log` has all
START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/splitbatch-2026-10-10.USYRpP`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `source-identities.sha256` covers the 347-file
source tree that ran. Licensed assets, executables, ROMs and HDFs are not
included. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-append-split-exnext-pilot-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
