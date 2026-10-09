# Cachy split read/flush diagnostic evidence

All twelve fixed starts and all 24 BFS/PFS3 outputs validate. This is bounded
attribution with two M5/sparse pairs and one M5/M5 pair per mode, not statistical
equivalence or production-speed qualification. Read, byte verification, work
and explicit volume flush remain separate. Inclusive nested probe timers must
not be added. See the [report](../../bfs-read-flush-cachy-2026-10-09.md).

`results/schedule.tsv` defines the exact twelve-run inventory. Each directory
retains all 23 phases, raw outputs, completion/mount records, startup sequence,
configuration, actual RDB inspection and installed runtime receipts before
and after execution. The 67 non-handler asset path/hash mappings are identical
across runs. The summary validates the complete inventory before calculating
ratios; zero denominators remain null ratios with raw values preserved.
Format logs are losslessly base64-encoded and decoded hashes are verified.
No licensed guest assets, ROMs, handlers, executables or HDFs are included.

`run-profile.sh` and `launch-profile.sh` are the executed scripts, with pinned
input hashes and stop/restore guards. The remote disposable workspace is
`/home/fabian/.cache/bfs-performance/read-flush-2026-10-09.3T1zRo`.
Do not rerun scripts against these results: they refuse an existing result
directory. `runner-console.log` contains all twelve START/PASS records and
restoration status 0. Before/after and fresh restored-service checks are
retained. `preflight-docker-top.log` is the rejected preflight before services
were stopped or runs launched. The report records the subsequent correction
and the corrected summary receipt-order assumption without altering raw data.

To reproduce validation from the matching repository protocol:

```sh
python3 docs/qualification/evidence/bfs-read-flush-cachy-2026-10-09/summarize_profile.py
python3 docs/qualification/evidence/bfs-read-flush-cachy-2026-10-09/test_profile.py
```

The summary reuses `tools/verify-split-bench.py`, which derives inventories
from the C protocol headers. Frozen copies of those sources and their tests
are under `protocol`; they are archival snapshots, not a second maintained
implementation. Source receipts identify 223 source/critical harness files
and the post-run check verifies them. `local-source-check.log` records one
known local/export mismatch: the local host-probe test includes added CRC
oracles absent from the earlier export. Both test versions and their diff
are retained; all 222 other sources/harness entries match and the test is
not used to build either measured binary. The summaries and tests contain no
performance thresholds or outlier filter. Luna independently verified every
raw output and exact regeneration of the stored summary; the root agent
checked table values and all 1,840 paired I/O/CRC counter comparisons.

Local logs record 21 summary, 26 split and 66 existing verifier tests passing.
An empty `shellcheck.log` records a successful warning-free run; both runner
scripts also passed shell syntax checks. Earlier sealed cohorts remain
separate and unchanged. This follow-up does not modify the filesystem core,
production inputs, integrity checks or durability guarantees. No CI, commit,
push, merge or issue mutation was performed.

Verify the retained files from this directory with:

```sh
shasum -a 256 -c SHA256SUMS
```
