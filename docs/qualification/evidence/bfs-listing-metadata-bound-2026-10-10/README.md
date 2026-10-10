# Listing metadata bound evidence

Sixteen fresh `compare` starts on Cachy of two uninstrumented handlers:
`reference` (commit `5136708` with the retained big-endian load change) and
`bound` (the same tree with `noinode-handler.patch`, which lists without
reading inodes). All starts pass the strict repository verifier. Nothing is
retained or reverted. See the
[report](../../bfs-listing-metadata-bound-2026-10-10.md).

`host-profile` holds `tools/core-workload-profile.sh sync 30` on Cachy for
the replica as committed plus the big-endian load change
(`proto-entry-inode-sync.tsv`) and for the replica with
`core-workload-noinode.patch` (`proto-entry-noinode-sync.tsv`).

`run-profile.sh` was derived from the production baseline pilot's script:
header, handler hashes, schedule, run names and completion marker differ.
`launch-profile.sh` is byte-identical to it. Both scripts and the scope were
compared byte for byte with the copies that ran. `runner-console.log` has all
START/PASS lines, the completion marker and the restoration status 0;
`services-restored.log` records the runner container and Coffin running, zero
CI worker processes and no emulator. The private remote workspace is `/home/fabian/.cache/bfs-performance/metabound-2026-10-10.kPl0jg`.

`results` keeps the raw protocols, receipts, configuration, startup and RDB
records and `schedule.tsv`; format logs are kept as verified base64 text
(`encode-format-logs.py`). `summarize.py` runs the verifier on all 16 starts
and writes `summary.json`. `source-identities.sha256` covers the 342-file
source tree that ran. Licensed assets, executables, ROMs and HDFs are not
included. No CI or Git/issue change is made.

From the repository root:

```sh
python3 docs/qualification/evidence/bfs-listing-metadata-bound-2026-10-10/summarize.py
```

Verify all retained files from this directory with `shasum -a 256 -c SHA256SUMS`.
