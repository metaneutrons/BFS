# Cachy write-path diagnostic evidence

All four fixed starts and all eight schema2 BFS/PFS3 outputs validate. Every
start retains all 23 workload phases. This is a single-candidate attribution
cohort, not a handler comparison or production-speed qualification. Inclusive
probe intervals must not be added, and sampled detail ticks are not extrapolated.
See the [report](../../bfs-write-path-profile-2026-10-09.md).

`results/schedule.tsv` fixes the complete run order. Each run contains raw
outputs, startup/configuration, completion/mount records, actual RDB inspection
and installed runtime receipts before/after execution. The 67 non-handler
asset path/hash mappings are identical; RDB capacity is 30. No licensed guest
assets, ROMs, handlers, executables or HDFs are included. Four Format logs are
losslessly base64-encoded, with decoded original hashes verified.

`run-profile.sh` and `launch-profile.sh` are the executed scripts, with pinned
inputs and stop/restore guards. The private remote disposable workspace is
`/home/fabian/.cache/bfs-performance/write-path-2026-10-09.nH7Mn8`.
Do not rerun against its results: the runner refuses an existing directory.
`runner-console.log` retains all four START/PASS records and restoration status
0. Before/after service records and the fresh restored-service check remain.

To reproduce validation from the repository root:

```sh
python3 docs/qualification/evidence/bfs-write-path-profile-2026-10-09/summarize_profile.py
python3 docs/qualification/evidence/bfs-write-path-profile-2026-10-09/test_profile.py
```

The summary imports generic parsing/metadata checks from the sealed preceding
read/flush helper and the current strict verifier; it does not pool cohorts.
Inventories come from the C protocol headers. Frozen source and test copies
under `protocol` are archival snapshots, not maintained duplicate implementations.
All 228 entries in `source-identities.sha256` pass against this snapshot. An
initial extension-filtered copy omitted eight non-code fixture/definition files;
the failed check is retained and all missing files were recovered from the
unchanged frozen source. Remote pre/post checks and the measurement-time local
228-entry check also pass.

`local/post-run-build-guidance.patch` records the only subsequent local harness
change: missing-artifact messages now suggest the dedicated write targets. It
does not alter successful-run behavior or the frozen executed source. Local
logs record eight host and eight ASAN/UBSAN oracles, 49 split-verifier, 66
existing-verifier and eight summary tests. LeakSanitizer is unavailable on
this Mac; the rejected leak-enabled launch is retained, and sanitizer tests
ran with leak detection disabled. The first compile's conflicting packet ID
is retained; only corrected packet3013 binaries were run. Empty ShellCheck logs
denote successful warning-free checks.

The independent Luna audit confirms exact summary reproduction, all eight
raw protocols and metadata guards. The 514 buffer requests are not measured
heap allocations; the cache can reuse four scratch buffers. No outlier filter,
timing threshold or exclusive-time decomposition is used. The normal BFS-first
4KiB Work commit and slower normal PFS3-first observation are both retained.

Earlier sealed evidence remains separate and unchanged. No filesystem
optimization, CI, commit, push, merge or issue mutation was performed.

Verify all retained files from this directory:

```sh
shasum -a 256 -c SHA256SUMS
```
