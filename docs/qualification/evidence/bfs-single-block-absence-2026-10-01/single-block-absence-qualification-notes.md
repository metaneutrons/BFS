# Single-block absence: root-authored qualification notes

This is a summary of observed tool results, not a captured command transcript.
All paths are relative to the BFS performance worktree unless stated otherwise.

## Frozen candidate and test counter-probes

- Core SHA-256: `b628d72bcc5ba0a8280c801c7ef2c769db9cb0d24523d0ff1eb81055cc06c193`.
- Focused tests SHA-256: `ddf6565742f57ba325488832b467b046eade9c2a5eb1fdd8767d7083b65d3a54`.
- `single-block-absence-red.log`: first old-core run, 21 passes and two
  failures. The original tests included a self-overlapping memory-fixture BIO
  write and an assertion on stash setup that prevented reaching the pop check.
  The original source is preserved as `single-block-absence-tests-red1.c`.
- The corrected fixture updates its root CRC directly in memory. Its pop case
  no longer asserts the setup read count. The corrected source is preserved as
  `single-block-absence-tests-red2.c` and matches the final test source.
- `single-block-absence-red2.log`: corrected tests with the old core, 22
  passes and two failures. The old helper required three root reads for pop
  and two for the below-first stash query; requiring one rejects both paths.
- `single-block-absence-green.log`: candidate core, all 24 focused tests pass.

## Independent M1 review

The test author was Luna worker `allocation_crc_next`. A distinct Luna worker,
`metadata_batch_tests`, read the actual candidate and focused tests without
building or changing them. It found no blocker in the narrow height-one,
single-block proof. It confirmed genuinely caller-owned blocks below, between
and beyond two free extents; exact root/free/reserve accounting; CRC, key-order,
selected-extent and first-read errors; and retained deeper-tree, fault and
snapshot coverage. The root agent separately reviewed the implementation and
corrected fixtures and ran the counter-probes and regressions.

Removing a redundant second allocation/read also removes failures that could
only arise from that unused operation. The first executed read remains fallible.
No assertion is made about unchanged obsolete fault-injection ordinals.

## Completed gates

- `make -j4 host-test BUILD_HOST=build/host-single-block-absence`: exit 0,
  48 suites, 465 passes, zero failures.
- `ASAN_OPTIONS=detect_leaks=0 make -j4 host-test
  BUILD_HOST=build/sanitize-single-block-absence` with warning-strict O1,
  debug info, `-fno-omit-frame-pointer -fsanitize=address,undefined`:
  exit 0, 48 suites, 465 passes, zero failures. Leak detection is disabled;
  this does not claim a leak-detector result.
- `make quality-gates shellcheck actionlint analyze`: exit 0, all 78 quality
  tests pass, ShellCheck/actionlint and clang static analysis pass.
- Two `make -j3 amiga amiga-perf-probe-handler amiga-fs-compare-bench`
  builds with separate `BUILD_AMIGA` directories: both exit 0. Normal handler,
  probe handler and probe guest compare byte-for-byte. Identities are retained.
- `single-block-absence-linux.sh`: exit 0 on Cachy. Twenty conformance tests
  pass, followed by both actual FUSE runs: default options and options 7 with
  disk pressure and interrupted-daemon checks. This is not a complete geometry
  matrix or physical-media qualification.
- Ten fresh-system preparations exit 0. The idle preflight verifies every
  handler, guest, PFS3, ROM and initial image identity, no active FS-UAE/compiler
  process, and inactive (not disabled) `coffin.service` before timing.

No CI, push, PR, merge, external publication or new soak was started.

## Independent normal extraction and completed probe comparison

Luna worker `cache_lookup_hint_review` independently read all sixteen normal
raw TSVs before the primary summary JSON existed. Its six workload means,
raw arrays, four matched deltas per workload, all twenty-four same-run ratios,
seven adverse pairs and twelve threshold failures match the primary extraction.
It explicitly excluded probe results and scheduling/causal precision.

Both candidate probes complete and pass. Comparison to retained CPU-scope
controls changes only B-Tree heap calls and their sample counts in both orders:
create alloc/free 80/80 to 40/40; write 639/639 to 134/134; delete 161/201 to
160/200. Every other non-time field agrees. The controls are not contemporaneous
normal performance controls. The initial diagnostic parser used the wrong
scope name and failed; its exact source/reproduced failure are retained.

All ten completed remote systems are moved recoverably to the dedicated
single-block-absence archive, with twenty TSV hashes identical before/after.
All twenty locally persisted TSVs are byte-identical to the local raw records.
Only copied formatter text is ASCII-CSI normalized with both hashes recorded.
The scoped asset check passes; the separately recorded global check still
fails older unchanged C1-encoded formatter text. No older evidence is rewritten.

## Independent report review

After the raw extraction, the same independent Luna worker reviews the report
against normal and probe records and finds no blocker. It confirms the noisy
mean/median distinction, adverse write pair, all twelve target failures, PFS3
drift, exact heap count changes, unchanged other non-time counts, inclusive
scope limits and the retained core identity. This is review evidence, not an
additional normal measurement or a statistical significance claim.
