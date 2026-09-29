# Verified B-tree node cache — 2026-09-29

## Result

The Amiga handler's existing write-through block cache now remembers whether
the **resident bytes** of a B-tree node have passed CRC and structural
validation. A subsequent cache hit still checks the node magic, header bounds,
key ordering and child pointers, but need not recalculate the block CRC.
Writes, failed writes, eviction and explicit invalidation clear the verified
state. A raw BIO without this optional capability continues to calculate the
CRC on every node read. The on-disk format and transaction protocol are
unchanged.

Two fresh, data-checked, uninstrumented FS-UAE A1200/68040 runs in opposite
BFS/PFS3 orders measured the cache-only implementation. The baseline is the
single post-fix normal-handler run in
[`bfs-free-tree-leaf-batch-2026-09-29.md`](bfs-free-tree-leaf-batch-2026-09-29.md).

| Phase | Baseline BFS (µs) | Cached BFS mean (µs) | Change | PFS3 mean (µs) | BFS/PFS3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 40 × 1 KiB create | 2,796,721 | 1,431,448 | −48.8% | 5,632 | 254× |
| 400 lookups | 280,447 | 48,769 | −82.6% | 23,904.5 | 2.0× |
| 40 small reads | 86,912 | 23,712.5 | −72.7% | 4,704 | 5.0× |
| 8 MiB sequential write | 1,180,856 | 701,212 | −40.6% | 14,399.5 | 48.7× |
| 8 MiB sequential read | 586,941 | 259,453.5 | −55.8% | 105,087.5 | 2.5× |
| 40 small-file deletes | 2,503,858 | 1,261,465.5 | −49.6% | 2,784 | 453× |

The baseline has only one post-fix timing run, and the candidate has two.
These are not confidence intervals or hardware throughput. Nevertheless, the
large differences repeat with reversed run order and match the mechanism in
the independent instrumented run. PFS3 remains much faster for writes and
small-file mutation.

## Mechanism and integrity boundary

The instrumented, PFS3-first deep comparison measured the following actual
node-read CRC calculations. The baseline deep run is the post-fix run in the
leaf-batch qualification. The free-tree write counts were identical between
the two versions: 3,029 for create, 1,089 for the 8 MiB write, and 2,660 for
delete. This optimization reduces repeated validation work, not COW writes.

| Phase | Baseline CRC calls | Cached CRC calls |
| --- | ---: | ---: |
| 40 creates | 13,102 | 3,263 |
| 400 lookups | 1,600 | 6 |
| 40 small reads | 480 | 42 |
| 8 MiB write | 5,010 | 1,473 |
| 8 MiB read | 2,311 | 4 |
| 40 deletes | 11,688 | 2,851 |

The optimization trusts the cache's in-memory bytes until that slot changes.
It does **not** detect a mutation of a validated cache slot by code that
bypasses the cache operations. Nor does a hot cache re-read the underlying
medium to discover an out-of-band change; the existing block cache already
had that coherence boundary. A cache invalidate, eviction or cache recreation
makes the next node read validate its CRC again. The Amiga handler invalidates
its cache on reformat, media-change notification and teardown. This is not a
qualification of unexpected media replacement, controller cache/flush lies,
or physical power loss.

## Verification and evidence

Focused host tests cover the verified-state lifecycle across cache hit,
successful/failed write, eviction and invalidation. A B-tree test corrupts a
cached block through the cache, confirms detection on its next read, then
corrupts backing storage after eviction and confirms detection on reload.
`test_cache`, `test_btree` and `test_pending_frees` pass. The full local
`make check` suite passed. Both Amiga handlers build with `-Werror`. The
normal handler is SHA-256
`d8bec87f23a69abaef75c41e140f2d2665b3a2840a1a196656f6924ac77badf0`;
the instrumented handler is
`7991685a1e088ef8cd43ffcb78e86c01f9082a76d1ad266401620cc05a66bd64`.

The [raw evidence](evidence/bfs-verified-node-cache-2026-09-29/) contains the
three checked guest result pairs, completion markers, machine information,
FS-UAE configurations and logs. Only trailing whitespace was removed from
the copied logs to satisfy the repository whitespace gate. `SHA256SUMS`
authenticates all 18 evidence files. ROM, licensed PFS3 handler and HDF images
are not committed. The Amiga
integration suite, sanitizers and remote CI have not run on this candidate;
those gates remain before merge. No new remote CI run was started during this
measurement phase.

The next performance target is the unchanged free-tree COW write volume,
especially 3,029 node writes for 40 creates and 2,660 for 40 deletes.
