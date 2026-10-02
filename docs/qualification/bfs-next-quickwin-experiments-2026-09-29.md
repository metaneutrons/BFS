# BFS follow-up quickwin experiments — 2026-09-29

## Decision

Neither follow-up candidate is suitable for the performance branch. Both source
prototypes and their candidate-specific tests were removed; the branch retains
the previously qualified free-tree quickwins unchanged. These measurements are
exploratory, not a claim about hardware throughput or PFS3 parity.

## Transaction-local node recycling

Luna identified the repeated free-tree COW path as the dominant small-file
cost. An aggressive prototype reused retired current-transaction B-tree nodes
from the allocator reserve. It passed the new in-memory ownership test and the
existing crash-injection tests, but `test_pending_frees` and existing hardware
failure tests exposed a real `BFS_ERR_AGAIN` during post-publication reclaim.
The prototype was rejected without an emulator performance claim.

A narrower prototype only delayed those retirements in the reserve until
commit. Its single [deep-compare run](evidence/bfs-next-quickwin-experiments-2026-09-29/stage-only-deep/bfs.deep-compare.tsv)
passed the guest verifier and reduced free-tree node writes from the qualified
baseline by 3,002 → 2,749 for 40 creates, 1,086 → 1,066 for the 8 MiB write,
and 2,839 → 2,821 for 40 deletes. `test_pending_frees` passed, but five
existing hardware-failure tests lost their required post-mutation fault
coverage under the changed I/O schedule. The candidate did not establish a
safe, meaningful improvement and was also removed.

## Optional contiguous data writes

Luna then implemented an optional all-or-error BIO range write for fresh,
contiguous append runs of at most 64 KiB, with the original one-block path as
fallback. The core maps no extent if a range write fails, even when the device
may have written a prefix. New cache-coherence and partial-write/remount/fsck
tests passed; `make host-test`, `make amiga`, and the probe-handler build passed.

The [deep-compare run](evidence/bfs-next-quickwin-experiments-2026-09-29/bulk-deep/bfs.deep-compare.tsv)
verified the intended mechanism: 8 MiB payload write calls fell from 2,048
to 128. The free-tree node-write count stayed at 1,086. Two checked normal
runs in [BFS-first](evidence/bfs-next-quickwin-experiments-2026-09-29/bulk-normal-bfs-first/bfs.tsv)
and [PFS3-first](evidence/bfs-next-quickwin-experiments-2026-09-29/bulk-normal-pfs3-first/bfs.tsv)
order measured 1,251,896 and 1,243,065 µs for the BFS 8 MiB write
(mean 1,247,480.5 µs). The prior qualified mean was 1,225,561 µs: this
candidate was about 1.8% slower, within the scale of emulator run variation
but certainly not evidence of improvement. The 40-file free-tree counts and
timings showed no relevant gain.

An independent Luna review found an additional hardware qualification gap:
the temporary 64 KiB buffer came from generic `malloc`, while Amiga DosEnvec
also specifies `de_BufMemType`. Checking `de_Mask` and `de_MaxTransfer` alone
does not establish that the buffer's memory class is appropriate for direct
device I/O. Because the FS-UAE target did not improve and this hardware
contract remained unproven, the entire range-write prototype was removed.
The experimental normal and probe handlers had SHA-256 values
`aeeb7799c8b044a2d0717543cc64ffe1455170df23b72670d906af1b66baab6f`
and `080914ac468faefa417dcd9f09e98e7a31663d248f37bdce6fbd7f938d013aca`.
They are not release artifacts. After removal, both local handler binaries were
rebuilt and matched the qualified baseline hashes in the preceding report.

## Evidence and next target

The [evidence directory](evidence/bfs-next-quickwin-experiments-2026-09-29/)
retains four checked FS-UAE runs (guest results, completion marker, machine
information and log), with checksums in `SHA256SUMS`. The prior baseline and
its conditions are in the [qualified free-tree report](bfs-free-tree-quickwins-2026-09-29.md).
The rejected source prototypes are not preserved as production code, so these
runs document a negative design decision rather than a reproducible release.

The next meaningful optimization is transaction-scoped free-tree mutation
batching or a different free-space index. It needs explicit ownership,
post-publication reclaim convergence, write-failure and crash-cut invariants
before performance qualification. Repeating reserve or BIO-call tuning without
that design is unlikely to close the measured gap.
