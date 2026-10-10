# BFS append 4 KiB attribution

The slowdown of appending 4 KiB packets seen in the ExNext read-ahead pilot
(1.045) and its confirmation run (1.039) is not reproduced and has no cause in
the code. In 24 new production starts the pushed handler takes 0.999 of the
reference time. The write probe counts the same work for all three handlers,
apart from a delayed commit that the timer places inside the phase in some
starts and not in others.

## Handlers

| Handler | Source | Production | Write probe |
| --- | --- | --- | --- |
| `ref` | `6801370` plus the write-back diff as first measured; reproduces the reference of the ExNext pilot byte for byte | `2c3645ba…` | `d69ff1fd…` |
| `mid` | `6ed386b`: review corrections and used-slot count, no read-ahead | `53acd50f…` | `39e5cf90…` |
| `cand` | `b0e062c`: plus ExNext read-ahead | `95a26862…` | `b991b0a2…` |

## Run 1, failed

The [first scope](evidence/bfs-append-attribution-2026-10-10/run1-failed/PILOT_SCOPE.md)
asked for 24 production and 6 write-probe starts, all of which had to pass the
strict verifier. The first write-probe start stopped with
`INODE_READ sidecar counter mismatch` in LIST_EXALL_400: since the batched
ExAll, the main probe counted sorted inode reads but the write sidecar did
not. Under that scope the run is failed and not interpreted, including its 24
completed production starts, whose raw records are kept.

The sorted reads now count in the sidecar (`src/core/inode.c`, only under
`BFS_PERF_WRITE_DETAIL`). The host oracle `tests/test_perf_probe.c` checks it
and expects one inode tree write per transaction for the 256 growth writes
since the inode write-back, where it still expected 256. Its write-probe
build now runs in `make host-test`, which it did not before, so neither gap
had been seen. Production handlers do not change.

## Run 2

The [second scope](evidence/bfs-append-attribution-2026-10-10/run2/PILOT_SCOPE.md)
repeats run 1 with the corrected write probes; each part is valid on its own.
All 30 starts pass `emulator-test/verify-bench-results.sh`. The series ran
14:22:26–14:40:36 UTC on 10 October 2026; runner and Coffin were restored with
status 0.

### Part A, production (24 starts)

| Phase | `mid` / `ref` | `cand` / `mid` | `cand` / `ref` |
| --- | ---: | ---: | ---: |
| Append 4 KiB × 256 | 1.019 (0.95–1.23) | 1.004 (0.82–1.04) | 0.999 (0.98–1.09) |
| Append 1 KiB × 256 | 1.001 | 1.024 | 1.023 |
| Sequential write 8 MiB | 0.975 | 1.027 | 1.010 |
| Small create 40 | 1.055 | 0.980 | 1.002 |
| ExNext 1,000 × 10 | 0.983 | 0.739 | 0.725 |
| ExNext 400 × 10 | 0.986 | 0.755 | 0.726 |

No step reaches 1.03 for appending 4 KiB, and `cand` / `ref` is below 1.03:
under the scope the slowdown is not reproduced in this run. Geometric means
over all phases: `mid` / `ref` 0.978, `cand` / `mid` 0.934, `cand` / `ref`
0.900.

### Part B, write probe (6 starts)

For APPEND_4K_1M, every count field is the same for the three handlers of a
round except those of one delayed commit, which falls inside the phase in one
start per round: `mid` in round 1, `ref` in round 2. That start has one
transaction commit, one seal, four node writes, three more inode-tree writes
and one more free-space allocation. Outside that commit, each start makes 256
free-space allocations, 256 extent mappings, 259 inode reads and 2 inode tree
writes.

Sampled microseconds per call (inclusive, nested, not extrapolated):

| Scope | Round 1: ref / mid / cand | Round 2: ref / mid / cand |
| --- | --- | --- |
| Write packet | 269.1 / 256.8 / 259.8 | 273.9 / 257.0 / 264.4 |
| Core file write | 252.6 / 241.3 / 242.8 | 258.6 / 243.2 / 250.6 |
| Goal-directed allocation | 61.1 / 57.2 / 58.8 | 56.8 / 55.5 / 56.6 |
| Extent mapping | 14.8 / 16.5 / 15.8 | 15.4 / 13.6 / 14.6 |
| Inode read | 8.8 / 10.4 / 10.2 | 11.8 / 12.6 / 9.2 |

The candidate does no extra work per packet, and its Write packets are not
slower than the reference's in either round.

## Consequence

Across the three runs that compared the two handlers, appending 4 KiB took
1.045, 1.039 and 0.999 of the reference. The probe finds the same work in
all three handlers; whether a delayed commit lands inside the phase changes
its work and varies from start to start. The investigation is closed without
a code change to the handlers; the write probe's accounting and its oracle
are corrected.
