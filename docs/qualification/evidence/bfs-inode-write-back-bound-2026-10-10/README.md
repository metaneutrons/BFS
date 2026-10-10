# Inode write-back bound evidence

Sixteen fresh `compare` starts on Cachy of two uninstrumented handlers:
`reference` (commit `5136708` with the retained big-endian load change) and
`bound` (the same tree with `defer-inode.patch`, which writes a handle's inode
only at ACTION_END). All starts pass the strict repository verifier. Nothing
is retained or reverted. See the
[report](../../bfs-inode-write-back-bound-2026-10-10.md).

`run-profile.sh` was derived from the listing metadata bound's script: header,
bound handler hash, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared byte for byte with the copies that ran. `runner-console.log` has all
START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/wbbound-2026-10-10.2MoC3f`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts
and writes `summary.json`. `source-identities.sha256` covers the 343-file
source tree that ran. Licensed assets, executables, ROMs and HDFs are not
included. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-inode-write-back-bound-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
