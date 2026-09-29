# BFS free-space-tree quickwins — 2026-09-29

## Result

Two local allocator changes reduce the checked FS-UAE small-file cost, but do
not approach PFS3 parity. The uninstrumented handler was measured on fresh BFS
and PFS3 images with both filesystem orders. Times below are mean microseconds
from two runs per version; the original measurements are documented in the
[deep-profile qualification](bfs-pfs3-deep-profile-2026-09-29.md).

| Checked AmigaDOS phase | BFS before | BFS after | Change | PFS3 after | Remaining BFS/PFS3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create/write/flush/close 40 × 1 KiB files | 3,563,052 | 2,957,648 | −17.0% | 5,600 | 528× |
| Create/write/flush/close one 8 MiB file | 1,275,480.5 | 1,225,561 | −3.9% | 14,079 | 87.0× |
| Delete the 40 small files | 3,439,149 | 2,926,319.5 | −14.9% | 2,880 | 1,016× |

The 400-lock, 40-file read and 8 MiB read phases changed by roughly 1–2% in
these pairs. The first quickwin alone, batched reserve refill, yielded only a
modest uninstrumented BFS-first result: 3,432,172 μs for create, 1,212,153 μs
for the 8 MiB write and 3,176,559 μs for delete. This is one exploratory run,
not a separate two-order performance claim.

## Change and attribution

`bfs_freespace_refill_reserve` now extracts a bounded tail run from the
highest-key free extent with one B-tree mutation. It preserves the reserve's
descending array order, the global-reserve floor and free-block accounting.
`bfs_freespace_return_reserve` now returns adjacent non-emergency blocks in a
range while retaining `4 × tree height + 8` blocks for COW scratch; isolated
and emergency-pool blocks still follow the single-block path. The tree's
commit/publication order and on-disk format are unchanged.

The debug-only deep-compare runs produced identical non-timing counters in
both run orders:

| Phase | Free-tree node writes before | Refill only | Both changes | Combined reduction |
| --- | ---: | ---: | ---: | ---: |
| 40 creates | 4,192 | 3,710 | 3,002 | 28.4% |
| 8 MiB write | 1,121 | 1,110 | 1,086 | 3.1% |
| 40 deletes | 3,896 | 3,396 | 2,839 | 27.1% |

In the final qualified combined runs, create-phase node-read CRC calls fell from 15,466 to
13,186 and delete-phase calls from 14,934 to 12,631. Directory, inode, other
tree and data-write counts did not fall; the improvement is concentrated in
free-tree maintenance. The remaining free-tree traffic still dominates create
and delete. The unchanged 8 MiB data-write count and small write-phase gain
show that these quickwins are not a solution for sequential throughput.

## Correctness and verification

Allocator tests cover repeated refill/return cycles, block uniqueness,
free-tree/reserve separation, accounting and mutations in an allocator-backed
B-tree. A new fault sweep injects failures into batched reserve return, abandons
the working state and checks that a remount of the committed tree has no fsck
errors or leaked blocks. A failed grouped return also bounds restoration by the
fixed reserve-array capacity. Existing crash-cut, pending-free and other fault
tests also pass. During the changed fault schedule, the delete-with-comment
test exposed a pre-existing recovery hole: a compensating directory-tree write
could reset `free_sink_err` after B-tree retirement failed. The namespace paths
now latch that uncertainty before compensation, so sync requires recovery rather
than publishing a partial namespace. Fault sweeps cover delete, rmdir, rename,
mkdir and comment replacement with remount checks of committed state.

The normal and probe m68k handlers build with `make amiga` and
`make amiga-perf-probe-handler`. The host suite, crash injection and hardware
failure simulations pass. `make check` and its static/quality gates were run
for this candidate.

## Evidence and limits

The [raw evidence](evidence/bfs-free-tree-quickwins-2026-09-29/) contains the
refill-only exploratory compare and probe runs, four early combined runs, four
post-capacity-guard runs and four final qualified runs with both filesystem
orders. Each contains the
guest TSVs, completion marker, machine information, FS-UAE configuration and
log. All copied files are listed in `SHA256SUMS`. Every run passed the strict
mode-specific result verifier. The refill-only probe overlapped a host test
build, so its timing is excluded; its deterministic operation counters are
retained for the stepwise comparison. The early combined runs precede the
failure-path capacity guard; the directories named `final-*` precede the
namespace recovery-latch audit. Both sets are excluded from the final timing
means. The qualified normal runs varied from 2,862,321 to 3,052,975 μs for
create and 2,745,968 to 3,106,671 μs for delete. The reported means show a
direction, not a narrow confidence bound.

All images were fresh 256 MiB HDFs under FS-UAE 3.2.35, A1200/68040 at maximum
emulated speed, Kickstart 47.102 and Workbench 47.2. The normal optimized BFS
handler has SHA-256 `11ab0de985ae7f34e0753ae9b687036c0f452e87b0be91ee2f7c148c666d09d2`;
the optimized probe handler has
`ca18d275440c1df2f9115597a6d7d46708c98051ac19d6108490c6782b2d0c3f`.
The PFS3 handler is the same binary as the baseline, SHA-256
`bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.
The refill-only normal/probe handlers were
`0ae58c816e888ea466a417c9f986a78c06206a85f6ae29b86c1cedbb0415ecd2` and
`e29e4cde228a04106d62e3955858093d3bff12f4bd9412133d37b0de826a1317`;
the pre-final combined normal/probe handlers were
`8d330df426a932de6d1ef151a3bb0ccf4a6be1904d669cf6aeeb9fe57eaac0ad` and
`dcc934fd034fa1e74b1b398bce1c4f87630eecf374298bccd6201dbf6d7908c9`.
The post-capacity-guard, pre-namespace-audit normal/probe binaries were
`8a8daf39d733a6a4c8f6bfd42b33ef11787176cf409ca9b6e5d2c9f1a0b75e25` and
`a75ffff87859d88ab73678ae5690d6f8cdb35ed56c94f3bf165225f2816e179d`.
Licensed binaries and HDFs are not committed. One fresh run per order shows
a repeatable local direction, not a confidence interval, hardware throughput
or equivalence of cache and durability policies.

The next optimization needs a larger change: reduce repeated free-tree COW
work across a transaction or coalesce more of its commit/reclaim operations,
with explicit crash and fault qualification. CRC checks must not be removed
without a cache-validation contract. The remaining measured gaps cannot be
described as performance parity.
