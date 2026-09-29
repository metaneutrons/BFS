# Node-write retention and 68k CRC experiment — 2026-09-29

## Result

The Amiga handler now retains newly written B-tree nodes in its existing
write-through LRU cache. The B-tree engine passes only completed nodes through
an optional node-write BIO operation, after installing their full-block CRC.
Ordinary data writes do not populate the cache. A failed write invalidates an
existing slot; eviction, explicit invalidation and cache recreation discard
the CRC-valid state. Structural fields are still validated on every B-tree
read. Other BIO implementations retain their original write behavior. The
on-disk format and transaction protocol are unchanged.

The 68020+ CRC implementation was independently changed to slicing-by-four.
Host and emulated 68040 tests compare it to a bitwise reference over short
lengths, all four alignment offsets, three initial values and a full 4 KiB
node. The additional tables occupy 4 KiB total instead of the original 1 KiB.

Two fresh, checked, uninstrumented FS-UAE A1200/68040 runs with reversed
BFS/PFS3 order measured the final source. An earlier combined-candidate pair
also passed before a cache-code cleanup and showed the same direction. The
baseline is the verified-node-cache candidate in
[`bfs-verified-node-cache-2026-09-29.md`](bfs-verified-node-cache-2026-09-29.md).

| Phase | Baseline BFS mean (µs) | Combined BFS mean (µs) | Change | PFS3 mean (µs) | BFS/PFS3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 40 × 1 KiB create | 1,431,448 | 953,594.5 | −33.4% | 5,855.5 | 163× |
| 400 lookups | 48,769 | 49,696 | +1.9% | 24,767.5 | 2.0× |
| 40 small reads | 23,712.5 | 23,961.5 | +1.1% | 4,769 | 5.0× |
| 8 MiB sequential write | 701,212 | 485,885 | −30.7% | 14,271 | 34× |
| 8 MiB sequential read | 259,453.5 | 263,838.5 | +1.7% | 110,496.5 | 2.4× |
| 40 small-file deletes | 1,261,465.5 | 872,347 | −30.8% | 3,073 | 284× |

The small read and lookup differences are within the scale of observed
emulator variation; they are not evidence of a useful regression or gain.
The large write-phase improvements repeat in both orders and align with the
instrumented counters. The earlier combined pair measured 998,617.5 µs for
create, 451,259.5 µs for the 8 MiB write, and 908,923.5 µs for delete.
The differences between these pairs caution against interpreting the exact
percentages as stable speedups. This is still far from PFS3 parity, especially for
small-file mutation. These measurements are emulator workload results, not
hardware throughput or confidence intervals.

## Mechanism and isolated CRC check

A checked CRC-only source snapshot, with the original cache behavior, produced
two normal runs. Against the preceding baseline its mean 8 MiB write time
fell from 701,212 to 641,339.5 µs (−8.5%); create changed by −1.1% and
delete by −4.5%. This smaller CRC-specific signal is susceptible to emulator
variance. It does not establish the CRC routine's standalone speedup. A later
original-CRC/write-retention control ran while macOS system services and an
unrelated AROS build were busy; its PFS3 timings rose substantially, so its
wall times are excluded from the comparison.

The checked deep comparison between CRC-only and CRC-plus-retention gives a
more direct mechanism check. A second deep run on the final source reproduced
the after-counts exactly:

| BFS phase | CRC reads before → after | BIO reads before → after | B-tree writes |
| --- | ---: | ---: | ---: |
| 40 creates | 3,263 → 32 | 303 → 82 | 3,304 unchanged |
| 8 MiB write | 1,473 → 1 | 28 → 3 | 1,477 unchanged |
| 40 deletes | 2,851 → 111 | 424 → 163 | 2,776 unchanged |

Free-tree writes also stayed at 3,029, 1,089 and 2,660 respectively. The
cache reduces repeated reads and CRC work; it does not reduce COW writes.
General free-tree multi-edit is a separate structural optimization described
in [`bfs-free-tree-multiedit-v1.md`](../plans/bfs-free-tree-multiedit-v1.md).

## Integrity boundary and verification

A hot cache trusts its own node bytes after a successful device write. It
cannot discover out-of-band media changes until invalidation, eviction or
cache recreation. A B-tree test explicitly corrupts the backing device under
a hot slot, then confirms that invalidation causes the next read to reject the
bad CRC. This is the same coherence boundary as the existing block cache,
now also applying to newly written nodes. Controller cache/flush lies, real
power cuts and specific hardware media remain outside this software
qualification.

The full local `make check` and `make sanitize` suites passed after the final
cache cleanup. The normal and probe Amiga handlers build with `-Werror`.
Their SHA-256 values are respectively
`d7568afad5654f3260c89f16c22194bd5699dc25a8b2dce1d6c1a4e762e1fccf`
and `cb044526873f1679704ea989f7ad23d7d0bf7a34d27628dd893b05b2f2cd3f80`.
The guest CRC suite passed under `vamos -C 68040`. No remote CI run was
started during this performance work. The checked guest benchmark and its
strict verifier confirm actual data contents rather than timing alone.

The same final normal handler and licensed test assets were copied by matching
SHA-256 to a separate CachyOS x86_64 host with FS-UAE 3.2.35. Four fresh
`xvfb-run` guest comparisons passed: two with the slicing CRC and two with a
control handler built from the same final source except for the original CRC
assembly from commit `464a523`. The control handler SHA-256 is
`6a21071cd5a60209a98f4e62433865d1ba9a7ba36023e2e85271642e9ccb0a60`.
An unrelated long-running FS-UAE process consumed CPU on Cachy, and the PFS3
8 MiB write control ranged from 41,025 to 81,023 µs. Therefore the Cachy
wall times validate portability and data integrity, but are not included in
the speedup table or treated as clean CRC-attribution evidence. FS-UAE was
already installed there; no package installation was necessary. During these
runs the benchmark script's redundant background timer was removed because
it left orphaned `sleep` processes after successful remote runs. The patched
script passed ShellCheck and a subsequent Cachy run left no timeout process.

The [raw evidence](evidence/bfs-node-write-retention-2026-09-29/) contains the
CRC-only, pre-cleanup combined, and final-source normal run pairs plus both
combined deep runs and four Cachy comparisons. It includes guest outputs,
completion markers, machine information, FS-UAE configurations and
logs. `SHA256SUMS` verifies all 72 evidence files. Licensed ROM, Workbench
files, PFS3 handler and HDFs are excluded.
