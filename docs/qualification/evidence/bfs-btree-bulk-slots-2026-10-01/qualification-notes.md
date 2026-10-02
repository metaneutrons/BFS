# Bulk B-tree slots: root-authored observations

This is a primary-authored record of tool results and independent reviews,
not a captured command transcript. Sources/handlers remain frozen for timing.

- Baseline local commit449d66a, exact btree source5095d6dd…, qualified normal
  handler7ec43316…. Candidate btree d9ab6cad…, standard Amiga shim7bc432eb…,
  normal fcc6ed6a…, probe361418d8…. Only four shift helpers and one standard
  declaration change; allocator/CRC/memory assembly remain exact baseline.
- Primary authors the production change. Luna metadata_batch_tests reviews
  all four helpers, calls and ranges without finding a source-equivalence
  blocker. Valid caller indices/capacities bound all suffixes and byte lengths;
  memmove handles both overlap directions and raw child bytes preserve BE32.
  No COW/write/CRC/snapshot/error/rollback policy changes.
- First Amiga compile fails because the local string shim omits memmove.
  The failure log remains. Add only its standard declaration; warnings stay
  errors. Corrected normal/probe builds pass and reproduce byte-for-byte in
  two directories. A third normal link-map build is byte-identical too.
- Actual normal link map selects libm020/libnix.a, not the generic archive.
  The initial generic-archive inference is superseded, not used as proof.
  Selected archive SHA25692d6757f…; selected memmove/bcopy members have hashes
  and disassemblies. Memmove saves D2, calls bcopy, returns original destination.
  Bcopy saves A2/D2, chooses direction, handles alignment and uses eight long
  moves per32-byte iteration plus tails; BFEXTU is 68020-safe. No replacement
  kernel is added. Archive-member binaries remain private generated build data.
- Primary emits old/new assembly with normal O2/m68020 flags, not m68040
  codegen. Luna allocation_crc_next checks actual helper backedges. Old leaf
  insert/remove and internal child-shift loops retain one DIVU per shifted
  item; the compiler combines endpoint capacity calculations within an item,
  not across iterations. Candidate four helper bodies have no shift backedge.
  This is static instruction evidence, not actual workload call counts.

With positive suffix length s, actual emitted helpers have these calls/divides:

| Helper | Old copy calls | Candidate copy calls | Old DIVU | Candidate DIVU |
| --- | --- | --- | --- | --- |
| Leaf insert | 2s+2 memcpy | 2 memmove+2 memcpy | s+1 | 2 |
| Leaf remove | 2s memcpy | 2 memmove | s | 1 |
| Internal insert | s+1 memcpy; direct child moves | 2 memmove+1 memcpy | s+1 | 2 |
| Internal remove | s memcpy; direct child moves | 2 memmove | s | 1 |

Zero suffixes retain no array move: insert writes only the incoming item and
has one layout division; removals have no copy/division. Fixed divisions
remain. Custom memcpy saves11 registers even for tiny copies, while standard
memmove adds a wrapper+bcopy setup. Short-suffix crossover and whole-workload
benefit require normal measurement; no quantitative gain is predicted.

## Existing gates before final additional-test freeze

Both existing normal and actual ASan/UBSan runs pass48 suites/468 cases,
zero failures, exit0. Leak detection is disabled, not qualified. All78 quality
tests, ShellCheck/actionlint and clang analysis pass. The additional independent
test source is not included in those prebuild runs; final coverage is separate.
Eight normal/two probe systems are freshly prepared with unchanged guests,
formatter/PFS3/ROM/geometry. Preparation is not performance acceptance.

No CI, push, PR, merge, release, external publication, new soak or physical
hardware/power/controller qualification. The five-times target remains unmet.

## Final additional tests and corrected fixture

The first independent test source b6375097…b7041ce3e… uses unsupported512-byte
geometry in three fixtures. Old-source and full candidate normal/ASan attempts
fail those three setup assertions; only the4KiB max-key case passes. Exact
first source and failed logs are retained, not gates. The worker's first hash
message has a transcription typo; primary shasum output is authoritative.

Correction changes only small geometry to shared BFS_MIN_BLOCK_SIZE1024 and
adds a31/5 odd-width comparator/shape for the1600-entry multilevel case.
All original oracles remain. Final source cf0333a3…40e607ea registers four
cases; the exact same source passes4/4 on old and new btree sources. Full final
normal and actual ASan/UBSan each pass49 suites/472 cases, zero failures/exit0.
No earlier failed log is overwritten. Leak detection remains explicitly off.

The separate Luna reviewer confirms final source/ranges/oracles and finds no
remaining blocker. At1KiB, leaf27/internal28 permits at most29*27=783 keys in
a height-two tree, so1600 requires height>=3. At4KiB, max512/3 capacities7/7
likewise force240 keys beyond height two. First/middle/last/zero-suffix leaf
shifts are explicit; internal slot indices are not instrumented. The bitwise
full-block CRC, scans/values, recursive child/separator bounds and old tree
readability are checked. The old tree is a standalone transaction-age view,
not a mounted snapshot. Faults cover initial leaf insert reads and full writes
rejected before reaching backing storage, not partial writes/delete faults.
Intermediate deletion states are not independently scanned; existing complete
fault/graph/snapshot/low-space regressions remain unchanged and pass.

Linux conformance20 plus two actual FUSE runs pass on unchanged production
source. Those earlier copy identities contain the first new test source, which
is not executed by those gates. The corrected exact test is then copied alone
to that private source directory and independently built/run on Linux:4/4
PASS, matching final source hash. No production change occurs between gates.

## Normal decision and exact restoration

All eight normal systems finish and sixteen raw schema1 TSVs pass. A separate
Luna worker extracts raw inputs before the primary JSON exists; all six means,
matched deltas, same-run ratios and counts agree. Candidate write increases
5.9999% in the mean and in all four matched pairs. Create+2.4407%, lookup+1.7366%,
small read+0.9977%, read−2.9866%, delete−4.0909%;14/24pairs adverse. Create/write/
delete fail5× in all four runs each:12/24failures. PFS3 also drifts; no causal
explanation or statistical precision is claimed. Candidate is rejected.

Two separate candidate probe systems pass and have zero structural or other
non-time counter differences against retained absence-shortcut controls in
both orders. Controls are not contemporaneous normal timing evidence.

Only the four helpers and added shim prototype are restored via exact patch.
Btree5095d6dd… and shim8f402e80… equal baseline. Rebuilt normal7ec43316… is
byte-identical to measured retained baseline. New equivalent tests remain;
restored host and actual ASan/UBSan logs are separate gates, not candidate
timings. All ten completed remote SYS directories are moved recoverably to
/home/fabian/.cache/bfs-performance/btree-bulk-slots-2026-10-01/;20before/after
TSV hashes are identical. No older system or raw result is removed.

Final restored normal and actual ASan/UBSan each pass49 suites/472 cases,
zero failures/exit0. A distinct Luna reviewer then checks final report/plan
against raw JSON, actual source and logs: no actionable blocker. Numeric
means, pairs, ratios, drift and14/24adverse counts agree; rejected outcome and
geometry/leak/static-analysis limitations are explicit. Reviewer makes no edits.

All20persisted raw TSVs compare byte-for-byte with private originals. The
new evidence scope passes the unchanged text-asset audit; actual global
audit still exits1 on older unchanged C1 formatter text. Only new copied
formatter output is normalized to ASCII CSI with original/output hashes.
Whitespace attributes exempt only exact saved-baseline trailing whitespace
and required diff-context/terminal blank lines; neither source bytes nor
patch bytes are rewritten to hide the inherited formatting.
