# ExNext read-ahead pilot evidence

Sixteen fresh `compare` starts on Cachy of two uninstrumented handlers:
`reference` (the retained inode write-back handler) and `candidate` (plus
ExNext read-ahead, the review corrections of the write-back and a count of
used pending slots). All starts pass the strict repository verifier. Under
the rule in `PILOT_SCOPE.md` the read-ahead is retained. See the
[report](../../bfs-exnext-read-ahead-pilot-2026-10-10.md).

`candidate.diff` is the uncommitted change to `src`, `include` and the
replica against commit `5136708` as the candidate was built (it also holds the
big-endian loads and the write-back of the earlier pilots). `host-profile`
holds `tools/core-workload-profile.sh sync 30` on Cachy before both changes
(`before-sync.tsv`) and for the candidate (`candidate-sync.tsv`).

`run-profile.sh` was derived from the inode write-back pilot's script: header,
handler hashes, run names and completion marker differ. `launch-profile.sh`
is byte-identical to it. Both scripts and the scope were compared byte for
byte with the copies that ran. `runner-console.log` has all START/PASS lines,
the completion marker and the restoration status 0; `services-restored.log`
records the runner container and Coffin running, zero CI worker processes and
no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/exnext-2026-10-10.8MRoga`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts
and writes `summary.json` with the retention evaluation.
`source-identities.sha256` covers the 345-file source tree that ran.
Licensed assets, executables, ROMs and HDFs are not included. No CI or
Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-exnext-read-ahead-pilot-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
