# Append 4 KiB attribution scope, run 2

Fixed before execution, 10 October 2026.

Run 1 (`../run1-failed`) stopped at its first write-probe start: the split
verifier found `INODE_READ sidecar counter mismatch` in LIST_EXALL_400. The
write probe counted single inode reads but not the sorted reads of the
batched ExAll, while the main counter counts both. Under run 1's scope the
whole run is failed and not interpreted, including its 24 completed
production starts.

The sorted reads now count in the write sidecar (`src/core/inode.c`, only
under `BFS_PERF_WRITE_DETAIL`), the host oracle checks it, and it runs in
`make host-test`. Production handlers do not change. This run repeats run 1
with the corrected write probes, and each part is valid on its own.

## Handlers

As in run 1; production handlers unchanged, write probes rebuilt with the
correction applied to each source:

| Handler | Production | Write probe |
| --- | --- | --- |
| `ref` (`6801370` plus the first write-back diff) | `2c3645ba…` | `d69ff1fd…` |
| `mid` (`6ed386b`) | `53acd50f…` | `39e5cf90…` |
| `cand` (`b0e062c`) | `95a26862…` | `b991b0a2…` |

## Emulator run

Exactly 30 fresh Cachy starts in the order of run 1: part A, 24 `compare`
starts of the production handlers in eight rounds; then part B, 6
`split-write-compare` starts of the write probes in two rounds. Orders,
rotation, guests (`eb641e32…`, `20e751e8…`), PFS3, ROM, formatter and RDB
Buffers = 30 as in run 1. No retries, exclusions or extensions; no pooling
with run 1 or earlier runs.

## Reading

Part A is interpreted if its 24 starts pass
`emulator-test/verify-bench-results.sh` for `compare`, part B if its 6 starts
pass it for `split-write-compare`; a part that fails is reported as failed
and not interpreted. If a start fails, the series stops there, as before.
Nothing is retained or reverted.

- **Part A:** for APPEND_4K_1M and every other phase, the medians of the eight
  paired ratios `mid` / `ref`, `cand` / `mid` and `cand` / `ref`. A step whose
  median for APPEND_4K_1M is at least 1.03 is named as the source; if
  `cand` / `ref` is below 1.03, the slowdown is reported as not reproduced in
  this run.
- **Part B:** for APPEND_4K_1M in each start, every count field compared
  between the handlers of the same round, and the sampled ticks per call of
  the write-path scopes. Probe intervals are inclusive and nested and are not
  added, subtracted or extrapolated.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
