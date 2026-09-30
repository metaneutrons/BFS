# BFS metadata-stock normal measurements

Baseline is `6aadfac` (normal label `f3b`); candidate is `08419a05` (expanded stock metadata, contiguous placement, geometry hoist, exact transient set). All runs used the same guest benchmark tool (`afc…`, schema 1). The eight listed normal run directories are eight unique inputs; their 16 `system/Results/{bfs,pfs3}.tsv` files have `FS_COMPARE_BENCH=1` and `PASS=1`. No deep/exploratory variant is included in these normal timing statistics. Timing units are microseconds (µs); each tuple is ordered `create40, lookup400, small-read40, seq-write8M, seq-read8M, delete40`.

## Raw normal timings

Block `a` is the first batch; `b` is the repeat batch. Order names the filesystem run first within the benchmark directory. No samples are trimmed.

| Block | Run directory | Variant | Order | BFS µs (six workloads) | PFS3 µs (six workloads) |
|---|---|---|---|---|---|
| a | `ms-normal-baseline-bfs-first` | baseline | BFS-first | 301695, 125247, 53649, 481022, 592380, 253631 | 39872, 148479, 29375, 42880, 264191, 7808 |
| a | `ms-normal-candidate-pfs3-first` | candidate | PFS3-first | 341055, 123646, 59392, 475774, 575934, 237759 | 15680, 73280, 14719, 37183, 399551, 7743 |
| a | `ms-normal-baseline-pfs3-first` | baseline | PFS3-first | 326207, 124671, 57023, 486206, 578172, 204991 | 17280, 66355, 14592, 38783, 259135, 7935 |
| a | `ms-normal-candidate-bfs-first` | candidate | BFS-first | 277310, 122559, 56320, 432446, 584637, 216702 | 17664, 66241, 14272, 38528, 287293, 8065 |
| b | `ms-normal-repeat-candidate-bfs-first` | candidate | BFS-first | 353417, 137343, 65983, 440190, 574782, 222015 | 17600, 67071, 15296, 40704, 288191, 10368 |
| b | `ms-normal-repeat-baseline-pfs3-first` | baseline | PFS3-first | 316094, 157825, 63169, 508157, 597692, 197374 | 17217, 65919, 15104, 39871, 259392, 8063 |
| b | `ms-normal-repeat-candidate-pfs3-first` | candidate | PFS3-first | 277951, 122816, 57153, 529213, 562814, 237375 | 17788, 67648, 13952, 39808, 256575, 7806 |
| b | `ms-normal-repeat-baseline-bfs-first` | baseline | BFS-first | 436926, 124031, 58112, 482173, 578877, 201472 | 16576, 66176, 14400, 39552, 294654, 8192 |

The candidate PFS3-first seq-read sample `399551 µs` (block a) is retained; it is markedly slower than the other three candidate PFS3 seq-read samples (`256575–288191 µs`). The raw table also retains all high BFS samples, including baseline create `436926 µs` and candidate seq-write `529213 µs`.

## BFS means and unpaired deltas

Each cell is `baseline mean / candidate mean / Δ / %Δ` in µs, with Δ = candidate − baseline and %Δ = Δ ÷ baseline mean × 100. Each variant has four normal runs.

| Workload | Baseline / candidate / Δ / %Δ |
|---|---:|
| create40 | 345230.50 / 312433.25 / -32797.25 / -9.500% |
| lookup400 | 132943.50 / 126591.00 / -6352.50 / -4.778% |
| small-read40 | 57988.25 / 59712.00 / +1723.75 / +2.973% |
| seq-write8M | 489389.50 / 469405.75 / -19983.75 / -4.083% |
| seq-read8M | 586780.25 / 574541.75 / -12238.50 / -2.086% |
| delete40 | 214367.00 / 228462.75 / +14095.75 / +6.576% |

## Matched BFS deltas

Candidate − baseline, paired within batch and order (µs). Columns are `a BFS-first`, `a PFS3-first`, `b BFS-first`, `b PFS3-first`.

| Workload | a B | a P | b B | b P |
|---|---:|---:|---:|---:|
| create40 | -24385 | +14848 | -83509 | -38143 |
| lookup400 | -2688 | -1025 | +13312 | -35009 |
| small-read40 | +2671 | +2369 | +7871 | -6016 |
| seq-write8M | -48576 | -10432 | -41983 | +21056 |
| seq-read8M | -7743 | -2238 | -4095 | -34878 |
| delete40 | -36929 | +32768 | +20543 | +40001 |

## Candidate BFS / PFS3 ratios

Same-run ratio is BFS ÷ PFS3 for each of the four candidate runs. “Ratio of means” uses the candidate BFS and PFS3 means; “mean ratios” averages the four same-run ratios. The >5× count is per run. Run labels are block plus `B` (BFS-first) or `P` (PFS3-first).

| Workload | Candidate mean BFS / PFS3 (µs) | Ratio of means | Mean ratios | Per-run min–max | >5× / 4 and all runs over threshold |
|---|---:|---:|---:|---:|---|
| create40 | 312433.25 / 17183.00 | 18.183× | 18.289× | 15.626–21.751× | 4/4: aB=15.699×, aP=21.751×, bB=20.081×, bP=15.626× |
| lookup400 | 126591.00 / 68560.00 | 1.846× | 1.850× | 1.687–2.048× | 0/4: — |
| small-read40 | 59712.00 / 14559.75 | 4.101× | 4.098× | 3.946–4.314× | 0/4: — |
| seq-write8M | 469405.75 / 39055.75 | 12.019× | 12.032× | 10.814–13.294× | 4/4: aB=11.224×, aP=12.795×, bB=10.814×, bP=13.294× |
| seq-read8M | 574541.75 / 307902.50 | 1.866× | 1.916× | 1.441–2.194× | 0/4: — |
| delete40 | 228462.75 / 8495.50 | 26.892× | 27.350× | 21.413–30.706× | 4/4: aB=26.869×, aP=30.706×, bB=21.413×, bP=30.409× |

Create40, seq-write8M, and delete40 exceed 5× in every candidate run. The ≤5× goal is not achieved; the main qualification report rejects this candidate as the default. The read-side timings and ratios are descriptive observations only; they do not establish statistical precision or causal attribution.

## Reproduction

The persisted raw inputs are the eight subdirectories beside this summary;
their names are the table's run-directory names without the `ms-normal-`
prefix, and each contains `{bfs,pfs3}.tsv` directly. Read the six named key/value
rows from those files. Means are `sum / 4`; percentages use
`(candidate mean / baseline mean − 1) × 100`; matched deltas use the same batch
and order; ratios use BFS/PFS3 from the same candidate run, with threshold
counts using strict `> 5`. Deep exploratory variants are excluded. The summary
was a read-only extraction; the primary agent separately ran the unchanged
strict verifier against all completed inputs.
