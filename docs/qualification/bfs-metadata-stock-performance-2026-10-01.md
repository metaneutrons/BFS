# Metadata-stock experiment: measured, not adopted

The ≤5× PFS3 goal remains **unachieved**. The expanded pre-publication stock
prototype is not enabled in the default source. The retained implementation is
the previously qualified sealed-settlement checkpoint, `6aadfac`.

## Decision from normal measurements

Eight fresh-image normal runs compared that checkpoint with the final stock
prototype: four runs per version, two in each filesystem order. All sixteen
filesystem outputs passed the unchanged strict schema-1 verifier. Both versions
used the same guest tool, formatter, PFS3 handler, ROM, options=0, partition
geometry and isolated Cachy FS-UAE 68040 configuration. No CI was run.

Times below are four-run means in µs; ratios divide candidate BFS and candidate
PFS3 means. Every sample, including adverse samples, remains in the
[raw measurement summary](evidence/bfs-metadata-stock-2026-10-01/measurement-summary.md).

| Workload | Baseline BFS | Stock BFS | Change | Stock / PFS3 | Candidate runs >5× |
|---|---:|---:|---:|---:|---:|
| Create 40 | 345230.50 | 312433.25 | −9.500% | 18.183× | 4/4 |
| Lookup 400 | 132943.50 | 126591.00 | −4.778% | 1.846× | 0/4 |
| Small read 40 | 57988.25 | 59712.00 | +2.973% | 4.101× | 0/4 |
| Write 8 MiB | 489389.50 | 469405.75 | −4.083% | 12.019× | 4/4 |
| Read 8 MiB | 586780.25 | 574541.75 | −2.086% | 1.866× | 0/4 |
| Delete 40 | 214367.00 | 228462.75 | +6.576% | 26.892× | 4/4 |

Delete is slower in three of four order-matched comparisons; write is slower
in one. The mean create improvement includes a high 436926 µs baseline sample.
These four observations per version establish neither statistical precision
nor causal attribution. The modest mixed timing result does not justify
enabling the extra allocation policy and ownership machinery as the default.
Fewer Free-Tree writes are not a substitute for the elapsed-time goal.

## Experiment and diagnostic observations

The narrow mounted policy kept the full recursive reserve floor of 20 blocks,
seeded an additional 20 ordinary spares, and reused an ordinary suffix from the
first request. It excluded snapshot/refcount/legacy/deeper-tree transitions and
all commit settlement. No publication fence, flush, integrity check, snapshot
contract or workload was removed.

Four exploratory schema-9 probe runs passed the strict data/completion/counter
verifier. They are not normal-handler acceptance measurements; only the final
variant underwent the complete functional gates below. Tuples are create /
write / delete Free-Tree node writes, followed by refill node writes.

| Variant | Free-Tree nodes | Refill nodes | Probe elapsed µs: create / write / delete |
|---|---|---|---|
| Expanded stock, highest-tail refill | 284 / 135 / 123 | 204 / 6 / 83 | 413310 / 622333 / 264319 |
| Prefer one contiguous refill run | 120 / 130 / 80 | 40 / 1 / 40 | 409726 / 648572 / 255807 |
| Hoist invariant geometry | 120 / 130 / 80 | 40 / 1 / 40 | 365950 / 640929 / 253373 |
| Exact ephemeral duplicate/pool set | 120 / 130 / 80 | 40 / 1 / 40 | 287231 / 518654 / 218111 |

The qualified SF probe recorded 451 / 141 / 197 Free-Tree nodes. The final MS
probe therefore reduced this traffic sharply without a corresponding normal
elapsed-time improvement. Generated 68k assembly demonstrates two
`___udivdi3` calls inside the earlier per-entry stock predicate, moved outside
the loop by the geometry hoist. The transient set retains exact duplicate and
active-pool alias rejection, including adversarial wraparound collisions; it
caches no validation across mutations.

The available probe measures device-call time and sampled CRC time, but not
heap allocations or allocator/namespace CPU time. Most of the elapsed-time
gap is still unlocalized. Individual exploratory runs do not prove the timing
contribution of each source change. The next experiment must add bounded CPU
attribution before choosing another structural change; bulk payload I/O alone
cannot be assumed to account for the entire gap.

## Functional proof and retained inputs

The final candidate passed **44 suites / 433 tests** in normal and
ASan/UBSan builds, plus 64 quality tests, ShellCheck and static analysis. macOS
does not support this LeakSanitizer setup; `detect_leaks=0` was explicit.
The existing buffered-persistence, strong graph/data/fsck, natural convergence,
snapshot, low-space and tiny-pending-capacity oracles remained in place.

Eight added stock tests cover first-request behavior at 1 KiB and 4 KiB,
accounting/roving/full-floor preservation, write-free suffix draws, real spare
retirement, fragmented refill, actual-versus-committed roots, both backup and
free/pool aliases, hash wraparound, unsupported reads, partial-write ownership
conservation, and settlement guards on success and error. The reserve-prefix
oracle permits inactive pool-origin scratch; selected ordinary metadata still
excludes all 32 historical pool slots. Test-construction failures and the
initial prototype's compatibility regressions were resolved without weakening
the established production test oracles.

The [candidate patch](evidence/bfs-metadata-stock-2026-10-01/candidate.patch)
preserves the complete final experimental source and added test file against
`6aadfac`. Apply it with `git apply --unidiff-zero` only to that clean base.
Its replayability was checked before restoring the default source. The
[identity log](evidence/bfs-metadata-stock-2026-10-01/input-identities.log),
build/test logs, raw outputs and `SHA256SUMS` bind the final normal handler
`08419a05…`, probe `bebef048…`, guest tool `afc18697…`, verifier and inputs.
Earlier exploratory variants have their own handler identities in runner logs;
they are retained as diagnostic history, not a reproducible release candidate.
No licensed ROM, Workbench assets, disk images or handler binaries are committed.

Real power cuts, controller cache/flush lies and particular hardware media
remain separate device-bound qualification; this experiment establishes none
of those claims. No push, PR, merge or release was performed.
