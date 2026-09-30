# CRC nibble table qualification measurements

Baseline: source AR `5172505` / `dc9d3dc`, using the old 4 KiB matrix. Candidate: 16 KiB CRC table. The eight normal runs are grouped as `a` = `nibble-cachy-*` and `b` = `nibble-cachy-repeat-*`; each group has baseline/candidate × BFS-first/PFS3-first. Timings are µs. Tuples are ordered `create40, lookup400, small-read40, seq-write8M, seq-read8M, delete40`.

The strict verifier result supplied for these ten directories is 10/10 PASS. The saved normal TSVs also have `PASS=1`.

## Raw normal timings

| Batch | Run directory | Variant | Order | BFS µs (six workloads) | PFS3 µs (six workloads) |
|---|---|---|---|---|---|
| a | `nibble-cachy-baseline-bfs-first` | baseline | BFS-first | 519996, 179760, 61760, 645500, 743485, 471806 | 17152, 75903, 22016, 47167, 301373, 12928 |
| a | `nibble-cachy-baseline-pfs3-first` | baseline | PFS3-first | 491449, 149908, 57151, 515069, 596605, 390717 | 19711, 94783, 20544, 42558, 284030, 8191 |
| a | `nibble-cachy-candidate-bfs-first` | candidate | BFS-first | 408510, 122865, 57215, 517564, 556925, 364542 | 17025, 66305, 14400, 38783, 252990, 7872 |
| a | `nibble-cachy-candidate-pfs3-first` | candidate | PFS3-first | 414461, 125119, 60095, 556348, 575293, 369727 | 17343, 66176, 14720, 40832, 257279, 7937 |
| b | `nibble-cachy-repeat-baseline-bfs-first` | baseline | BFS-first | 518014, 124927, 69375, 573500, 612743, 445438 | 17724, 66610, 18945, 59328, 274047, 8000 |
| b | `nibble-cachy-repeat-baseline-pfs3-first` | baseline | PFS3-first | 440956, 122494, 58036, 472701, 556286, 431101 | 16959, 65023, 14143, 36608, 265664, 7743 |
| b | `nibble-cachy-repeat-candidate-bfs-first` | candidate | BFS-first | 497277, 126528, 59007, 507261, 625980, 407421 | 17025, 91776, 30080, 77234, 273918, 8320 |
| b | `nibble-cachy-repeat-candidate-pfs3-first` | candidate | PFS3-first | 448894, 125374, 58945, 485885, 640123, 411261 | 17665, 67840, 24511, 93311, 361789, 8255 |

## Means and candidate-versus-baseline deltas

Each cell is `baseline mean / candidate mean / Δ / %Δ` in µs, where Δ = candidate − baseline and %Δ = (candidate mean ÷ baseline mean − 1) × 100. Batch a and batch b each average two runs per variant; combined averages four.

| Filesystem | Workload | a: baseline / candidate / Δ / %Δ | b: baseline / candidate / Δ / %Δ | Combined: baseline / candidate / Δ / %Δ |
|---|---|---:|---:|---:|
| BFS | create | 505722.500 / 411485.500 / -94237.000 / -18.634% | 479485.000 / 473085.500 / -6399.500 / -1.335% | 492603.750 / 442285.500 / -50318.250 / -10.215% |
| BFS | lookup | 164834.000 / 123992.000 / -40842.000 / -24.778% | 123710.500 / 125951.000 / +2240.500 / +1.811% | 144272.250 / 124971.500 / -19300.750 / -13.378% |
| BFS | small-read | 59455.500 / 58655.000 / -800.500 / -1.346% | 63705.500 / 58976.000 / -4729.500 / -7.424% | 61580.500 / 58815.500 / -2765.000 / -4.490% |
| BFS | seq-write | 580284.500 / 536956.000 / -43328.500 / -7.467% | 523100.500 / 496573.000 / -26527.500 / -5.071% | 551692.500 / 516764.500 / -34928.000 / -6.331% |
| BFS | seq-read | 670045.000 / 566109.000 / -103936.000 / -15.512% | 584514.500 / 633051.500 / +48537.000 / +8.304% | 627279.750 / 599580.250 / -27699.500 / -4.416% |
| BFS | delete | 431261.500 / 367134.500 / -64127.000 / -14.870% | 438269.500 / 409341.000 / -28928.500 / -6.601% | 434765.500 / 388237.750 / -46527.750 / -10.702% |
| PFS3 | create | 18431.500 / 17184.000 / -1247.500 / -6.768% | 17341.500 / 17345.000 / +3.500 / +0.020% | 17886.500 / 17264.500 / -622.000 / -3.477% |
| PFS3 | lookup | 85343.000 / 66240.500 / -19102.500 / -22.383% | 65816.500 / 79808.000 / +13991.500 / +21.258% | 75579.750 / 73024.250 / -2555.500 / -3.381% |
| PFS3 | small-read | 21280.000 / 14560.000 / -6720.000 / -31.579% | 16544.000 / 27295.500 / +10751.500 / +64.987% | 18912.000 / 20927.750 / +2015.750 / +10.659% |
| PFS3 | seq-write | 44862.500 / 39807.500 / -5055.000 / -11.268% | 47968.000 / 85272.500 / +37304.500 / +77.770% | 46415.250 / 62540.000 / +16124.750 / +34.740% |
| PFS3 | seq-read | 292701.500 / 255134.500 / -37567.000 / -12.835% | 269855.500 / 317853.500 / +47998.000 / +17.787% | 281278.500 / 286494.000 / +5215.500 / +1.854% |
| PFS3 | delete | 10559.500 / 7904.500 / -2655.000 / -25.143% | 7871.500 / 8287.500 / +416.000 / +5.285% | 9215.500 / 8096.000 / -1119.500 / -12.148% |

## Order-matched candidate − baseline deltas

Values are µs. Pairing is within the same batch and order: a BFS-first, a PFS3-first, b BFS-first, b PFS3-first.

### BFS

| Workload | a BFS-first | a PFS3-first | b BFS-first | b PFS3-first |
|---|---:|---:|---:|---:|
| create | -111486 | -76988 | -20737 | +7938 |
| lookup | -56895 | -24789 | +1601 | +2880 |
| small-read | -4545 | +2944 | -10368 | +909 |
| seq-write | -127936 | +41279 | -66239 | +13184 |
| seq-read | -186560 | -21312 | +13237 | +83837 |
| delete | -107264 | -20990 | -38017 | -19840 |

### PFS3

| Workload | a BFS-first | a PFS3-first | b BFS-first | b PFS3-first |
|---|---:|---:|---:|---:|
| create | -127 | -2368 | -699 | +706 |
| lookup | -9598 | -28607 | +25166 | +2817 |
| small-read | -7616 | -5824 | +11135 | +10368 |
| seq-write | -8384 | -1726 | +17906 | +56703 |
| seq-read | -48383 | -26751 | -129 | +96125 |
| delete | -5056 | -254 | +320 | +512 |

## Candidate BFS / PFS3 ratios

For each candidate run, ratio = BFS time ÷ PFS3 time for the same workload and order. The table gives ratio of combined means, arithmetic mean of four per-run ratios, min–max, and the number of per-run ratios above 5×. All runs over 5× are listed so outliers remain visible.

| Workload | Ratio of means | Mean per-run ratio | Per-run min–max | >5× / 4 and runs over threshold |
|---|---:|---:|---:|---|
| create | 25.618× | 25.628× | 23.898–29.209× | 4/4: aB=23.995×, aP=23.898×, bB=29.209×, bP=25.411× |
| lookup | 1.711× | 1.743× | 1.379–1.891× | 0/4: — |
| small-read | 2.810× | 3.106× | 1.962–4.083× | 0/4: — |
| seq-write | 8.263× | 9.686× | 5.207–13.625× | 4/4: aB=13.345×, aP=13.625×, bB=6.568×, bP=5.207× |
| seq-read | 2.093× | 2.123× | 1.769–2.285× | 0/4: — |
| delete | 47.954× | 47.920× | 46.309–49.820× | 4/4: aB=46.309×, aP=46.583×, bB=48.969×, bP=49.820× |

## Deep instrumentation totals and tree writes

Each row is one deep run/workload. Times are BFS/PFS3 µs; counters are BFS. BIO and data counters are read / write / update and read / write respectively. Tree-node counts are directory / inode / refcount / other. Free-tree parts are allocation-body / reserve-refill / reserve-return / post-publish pending-reclaim / other node writes.

| Deep run | Operation | BFS / PFS3 µs | BIO R / W / U | Data R / W | Node writes | Txn commits | Freespace allocs | Extent maps | Tree nodes (dir / inode / refcount / other) | FT node writes | FT parts | Publications |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---:|---|---:|
| baseline-deep | create40 | 448764 / 22541 | 1 / 1025 / 160 | 0 / 40 | 865 | 40 | 277 | 40 | 75 / 124 / 0 / 40 | 626 | 277 / 40 / 80 / 145 / 84 | 120 |
| baseline-deep | seq-write8M | 509575 / 39168 | 3 / 2483 / 4 | 0 / 2048 | 432 | 1 | 136 | 128 | 2 / 155 / 0 / 128 | 147 | 136 / 1 / 2 / 5 / 3 | 3 |
| baseline-deep | delete40 | 403518 / 13057 | 49 / 724 / 160 | 0 / 0 | 604 | 40 | 116 | 0 | 76 / 40 / 0 / 0 | 488 | 116 / 40 / 80 / 251 / 1 | 120 |
| candidate-deep | create40 | 545982 / 17088 | 1 / 1035 / 160 | 0 / 40 | 875 | 40 | 281 | 40 | 75 / 128 / 0 / 40 | 632 | 281 / 40 / 80 / 144 / 87 | 120 |
| candidate-deep | seq-write8M | 559805 / 36914 | 3 / 2484 / 4 | 0 / 2048 | 433 | 1 | 136 | 128 | 2 / 157 / 0 / 128 | 146 | 136 / 1 / 2 / 4 / 3 | 3 |
| candidate-deep | delete40 | 538493 / 8768 | 49 / 726 / 160 | 0 / 0 | 606 | 40 | 116 | 0 | 76 / 40 / 0 / 0 | 490 | 116 / 40 / 80 / 253 / 1 | 120 |

## Deep CRC-write sampling

Mean sampled ticks per sampled CRC write call = `NODE_CRC_WRITE_SAMPLE_TICKS / NODE_CRC_WRITE_SAMPLES`. `CLOCK_HZ` is the reported frequency. Samples are taken at stride 64 from selected calls; this mean is not an unbiased population estimate and must not be multiplied by all calls to estimate total CRC time. `CLOCK_PAIR_TICKS` is shown separately because timer-pair overhead differs.

| Deep run | Operation | CRC write calls | CRC write samples | Sample ticks | Mean ticks / sampled call | CLOCK_HZ | CLOCK_PAIR_TICKS |
|---|---|---:|---:|---:|---:|---:|---:|
| baseline-deep | create40 | 865 | 13 | 730 | 56.154 | 709379 | 0 |
| baseline-deep | seq-write8M | 432 | 6 | 886 | 147.667 | 709379 | 1 |
| baseline-deep | delete40 | 604 | 9 | 435 | 48.333 | 709379 | 1 |
| candidate-deep | create40 | 875 | 13 | 832 | 64.000 | 709379 | 2 |
| candidate-deep | seq-write8M | 433 | 6 | 649 | 108.167 | 709379 | 1 |
| candidate-deep | delete40 | 606 | 9 | 592 | 65.778 | 709379 | 1 |

## Interpretation cautions

The PFS3 seq-write candidate runs in batch b measured 77,234 and 93,311 µs, versus 59,328 and 36,608 µs for the corresponding baseline runs. These slow candidate PFS3 observations raise the candidate PFS3 seq-write mean and lower the observed BFS/PFS3 ratio; they remain included above.

Read and lookup paths are unchanged by the CRC table change, so their apparent timing improvements are confounded by host/cache/history variation and cannot be attributed to this change. The BFS candidate deep elapsed times are slower than baseline for create40, seq-write8M, and delete40, but these instrumented observations are not normal-run timing evidence. The runs do not guarantee invariant free-tree or inode counts: timestamps and history can change those counters. Only the CRC table change is intended. Results are descriptive and do not establish statistical significance or cross-host variance.

## Reproduction

Read key/value pairs from the eight normal directories’ `system/Results/{bfs,pfs3}.tsv` and two deep directories’ `*.deep-compare.tsv`. Compute means as `sum(values) / count`; `%Δ = (candidate mean / baseline mean - 1) * 100`; paired deltas match batch and order; candidate ratios use BFS/PFS3 from the same candidate run; threshold counts use `ratio > 5`. For CRC sampling, `mean sampled ticks/call = SAMPLE_TICKS / SAMPLES`. No guest, CI, or full-suite work was rerun.
