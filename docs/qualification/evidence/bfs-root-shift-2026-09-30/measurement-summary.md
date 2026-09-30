# AR qualification measurements: root-shift candidate

Baseline is UR commit `60946e3`; candidate removes one COW free-tree update per partial run. Timing units are microseconds (µs). Each tuple below is ordered `create40, lookup400, small-read40, seq-write8M, seq-read8M, delete40`.

Normal results contain 16 run directories / 32 workload TSVs, all with `PASS=1`. Blocks: a = `shift-cachy` first; b = `shift-cachy-repeat`; c = `shift2-cachy` first; d = `shift2-cachy-repeat`. The first batch is all `shift-cachy` runs (a+b); the repeat batch is all `shift2-cachy` runs (c+d). Each batch has eight run directories, four per variant. The `-repeat` directory suffix is included within its batch. `BFS-first` and `PFS3-first` refer to the order the filesystems were benchmarked within each run.

## Raw normal timings

| Block | Run directory | Variant | Order | BFS µs (six workloads) | PFS3 µs (six workloads) |
|---|---|---|---|---|---|
| a | `shift-cachy-baseline-bfs-first` | baseline | BFS-first | 503869, 147712, 58943, 508785, 663485, 400446 | 16766, 68863, 16191, 37632, 293311, 8319 |
| a | `shift-cachy-candidate-bfs-first` | candidate | BFS-first | 489213, 123775, 61632, 500926, 647676, 494526 | 16257, 86527, 13953, 36096, 307389, 8255 |
| a | `shift-cachy-baseline-pfs3-first` | baseline | PFS3-first | 438589, 122495, 58113, 494973, 833980, 436604 | 27519, 67646, 16897, 66305, 318398, 8191 |
| a | `shift-cachy-candidate-pfs3-first` | candidate | PFS3-first | 460734, 142590, 68543, 534716, 696060, 457404 | 16129, 66176, 13696, 35903, 320445, 12545 |
| b | `shift-cachy-repeat-baseline-bfs-first` | baseline | BFS-first | 473406, 135359, 55231, 528894, 569851, 405630 | 17280, 65663, 16320, 51263, 271806, 8255 |
| b | `shift-cachy-repeat-candidate-bfs-first` | candidate | BFS-first | 488446, 123711, 56895, 502205, 607996, 407486 | 15168, 67263, 13120, 33983, 250879, 7935 |
| b | `shift-cachy-repeat-baseline-pfs3-first` | baseline | PFS3-first | 449789, 124991, 59455, 591100, 656253, 474110 | 17855, 66753, 15937, 35776, 270655, 8513 |
| b | `shift-cachy-repeat-candidate-pfs3-first` | candidate | PFS3-first | 452989, 134080, 58752, 494334, 603325, 446205 | 16835, 66305, 14912, 38463, 255614, 8448 |
| c | `shift2-cachy-baseline-bfs-first` | baseline | BFS-first | 478974, 135103, 75903, 521149, 688956, 457789 | 16255, 66305, 14337, 39552, 265598, 7871 |
| c | `shift2-cachy-candidate-bfs-first` | candidate | BFS-first | 438206, 126976, 53631, 488701, 627708, 405617 | 15104, 65919, 13313, 40768, 315965, 7872 |
| c | `shift2-cachy-baseline-pfs3-first` | baseline | PFS3-first | 553532, 132991, 66880, 568174, 620220, 414270 | 17088, 65600, 14834, 42177, 261439, 10304 |
| c | `shift2-cachy-candidate-pfs3-first` | candidate | PFS3-first | 428734, 168639, 81216, 515454, 569020, 390232 | 16257, 66368, 13568, 40320, 258879, 7808 |
| d | `shift2-cachy-repeat-baseline-bfs-first` | baseline | BFS-first | 561852, 122878, 59071, 634813, 620925, 413054 | 16769, 78527, 15808, 70078, 305085, 7835 |
| d | `shift2-cachy-repeat-candidate-bfs-first` | candidate | BFS-first | 450941, 125822, 56448, 522622, 563901, 529406 | 20991, 115905, 15360, 38016, 259006, 7937 |
| d | `shift2-cachy-repeat-baseline-pfs3-first` | baseline | PFS3-first | 455997, 133823, 74815, 655611, 601982, 425022 | 16512, 66559, 14657, 38206, 263359, 7806 |
| d | `shift2-cachy-repeat-candidate-pfs3-first` | candidate | PFS3-first | 450492, 138688, 57088, 513853, 618108, 405629 | 17152, 88126, 22145, 56511, 257215, 7745 |

## Mean timings and unpaired mean deltas

Each cell is `baseline / candidate / Δ / %Δ` in µs, where Δ = candidate − baseline and %Δ = (candidate mean ÷ baseline mean − 1) × 100. First batch and repeat batch each average four run directories per variant; combined averages eight.

| Filesystem | Workload | First batch B / C / Δ / %Δ | Repeat batch B / C / Δ / %Δ | Combined B / C / Δ / %Δ |
|---|---|---:|---:|---:|
| BFS | create | 466413.250 / 472845.500 / +6432.250 / +1.379% | 512588.750 / 442093.250 / -70495.500 / -13.753% | 489501.000 / 457469.375 / -32031.625 / -6.544% |
| BFS | lookup | 132639.250 / 131039.000 / -1600.250 / -1.206% | 131198.750 / 140031.250 / +8832.500 / +6.732% | 131919.000 / 135535.125 / +3616.125 / +2.741% |
| BFS | small-read | 57935.500 / 61455.500 / +3520.000 / +6.076% | 69167.250 / 62095.750 / -7071.500 / -10.224% | 63551.375 / 61775.625 / -1775.750 / -2.794% |
| BFS | seq-write | 530938.000 / 508045.250 / -22892.750 / -4.312% | 594936.750 / 510157.500 / -84779.250 / -14.250% | 562937.375 / 509101.375 / -53836.000 / -9.563% |
| BFS | seq-read | 680892.250 / 638764.250 / -42128.000 / -6.187% | 633020.750 / 594684.250 / -38336.500 / -6.056% | 656956.500 / 616724.250 / -40232.250 / -6.124% |
| BFS | delete | 429197.500 / 451405.250 / +22207.750 / +5.174% | 427533.750 / 432721.000 / +5187.250 / +1.213% | 428365.625 / 442063.125 / +13697.500 / +3.198% |
| PFS3 | create | 19855.000 / 16097.250 / -3757.750 / -18.926% | 16656.000 / 17376.000 / +720.000 / +4.323% | 18255.500 / 16736.625 / -1518.875 / -8.320% |
| PFS3 | lookup | 67231.250 / 71567.750 / +4336.500 / +6.450% | 69247.750 / 84079.500 / +14831.750 / +21.418% | 68239.500 / 77823.625 / +9584.125 / +14.045% |
| PFS3 | small-read | 16336.250 / 13920.250 / -2416.000 / -14.789% | 14909.000 / 16096.500 / +1187.500 / +7.965% | 15622.625 / 15008.375 / -614.250 / -3.932% |
| PFS3 | seq-write | 47744.000 / 36111.250 / -11632.750 / -24.365% | 47503.250 / 43903.750 / -3599.500 / -7.577% | 47623.625 / 40007.500 / -7616.125 / -15.992% |
| PFS3 | seq-read | 288542.500 / 283581.750 / -4960.750 / -1.719% | 273870.250 / 272766.250 / -1104.000 / -0.403% | 281206.375 / 278174.000 / -3032.375 / -1.078% |
| PFS3 | delete | 8319.500 / 9295.750 / +976.250 / +11.734% | 8454.000 / 7840.500 / -613.500 / -7.257% | 8386.750 / 8568.125 / +181.375 / +2.163% |

## Matched candidate − baseline deltas

Each value is paired within the same block and order (µs). Columns are `a BFS-first`, `a PFS3-first`, `b BFS-first`, `b PFS3-first`, `c BFS-first`, `c PFS3-first`, `d BFS-first`, `d PFS3-first`. Negative means lower candidate time.

### BFS

| Workload | a B | a P | b B | b P | c B | c P | d B | d P |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| create | -14656 | +22145 | +15040 | +3200 | -40768 | -124798 | -110911 | -5505 |
| lookup | -23937 | +20095 | -11648 | +9089 | -8127 | +35648 | +2944 | +4865 |
| small-read | +2689 | +10430 | +1664 | -703 | -22272 | +14336 | -2623 | -17727 |
| seq-write | -7859 | +39743 | -26689 | -96766 | -32448 | -52720 | -112191 | -141758 |
| seq-read | -15809 | -137920 | +38145 | -52928 | -61248 | -51200 | -57024 | +16126 |
| delete | +94080 | +20800 | +1856 | -27905 | -52172 | -24038 | +116352 | -19393 |

### PFS3

| Workload | a B | a P | b B | b P | c B | c P | d B | d P |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| create | -509 | -11390 | -2112 | -1020 | -1151 | -831 | +4222 | +640 |
| lookup | +17664 | -1470 | +1600 | -448 | -386 | +768 | +37378 | +21567 |
| small-read | -2238 | -3201 | -3200 | -1025 | -1024 | -1266 | -448 | +7488 |
| seq-write | -1536 | -30402 | -17280 | +2687 | +1216 | -1857 | -32062 | +18305 |
| seq-read | +14078 | +2047 | -20927 | -15041 | +50367 | -2560 | -46079 | -6144 |
| delete | -64 | +4354 | -320 | -65 | +1 | -2496 | +102 | -61 |

## Candidate BFS / PFS3 ratios

For each candidate run, ratio = BFS time ÷ PFS3 time for the same workload and run. `Mean ratios` is the arithmetic mean of the eight per-run ratios; `ratio of means` is also shown. Range is the min–max of the eight run ratios. A >5× count is per-run, not inferred from the ratio of means. Run IDs use block plus B (BFS-first) or P (PFS3-first).

| Workload | Ratio of means | Mean ratios | Per-run min–max | >5× / 8 and all runs over threshold |
|---|---:|---:|---:|---|
| create | 27.333× | 27.613× | 21.483–32.202× | 8/8: aB=30.092×, aP=28.566×, bB=32.202×, bP=26.908×, cB=29.013×, cP=26.372×, dB=21.483×, dP=26.265× |
| lookup | 1.742× | 1.822× | 1.086–2.541× | 0/8: — |
| small-read | 4.116× | 4.246× | 2.578–5.986× | 2/8: aP=5.005×, cP=5.986× |
| seq-write | 12.725× | 13.002× | 9.093–14.893× | 8/8: aB=13.878×, aP=14.893×, bB=14.778×, bP=12.852×, cB=11.987×, cP=12.784×, dB=13.747×, dP=9.093× |
| seq-read | 2.217× | 2.228× | 1.987–2.423× | 0/8: — |
| delete | 51.594× | 52.640× | 36.461–66.701× | 8/8: aB=59.906×, aP=36.461×, bB=51.353×, bP=52.818×, cB=51.527×, cP=49.978×, dB=66.701×, dP=52.373× |

## Deep instrumentation

Three deep runs, all included, with create40, seq-write8M, and delete40 rows. Timings are BFS/PFS3 µs; the remaining counters are BFS. “Tree nodes” order is directory / inode / other; “free-tree parts” order is allocation-body / reserve-refill / reserve-return / post-publish pending-reclaim / other node writes.

| Deep run | Operation | BFS / PFS3 µs | BIO writes / updates | Data writes / extent maps | Node writes | Tree nodes (dir / inode / other) | Free-tree node writes | Free-tree parts | Txn commits | Superblock publications |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|---:|
| `shift-cachy-deep` | create40 | 470526 / 25666 | 1022 / 160 | 40 / 40 | 862 | 75 / 123 / 40 | 624 | 276 / 40 / 80 / 145 / 83 | 40 | 120 |
| `shift-cachy-deep` | seq-write8M | 544700 / 41728 | 2483 / 4 | 2048 / 128 | 432 | 2 / 156 / 128 | 146 | 136 / 1 / 2 / 4 / 3 | 1 | 3 |
| `shift-cachy-deep` | delete40 | 427326 / 18240 | 725 / 160 | 0 / 0 | 605 | 76 / 40 / 0 | 489 | 116 / 40 / 80 / 252 / 1 | 40 | 120 |
| `shift2-cachy-baseline-deep` | create40 | 492861 / 18112 | 1034 / 160 | 40 / 40 | 874 | 75 / 127 / 40 | 632 | 280 / 40 / 80 / 145 / 87 | 40 | 120 |
| `shift2-cachy-baseline-deep` | seq-write8M | 654466 / 38591 | 2618 / 4 | 2048 / 128 | 567 | 2 / 163 / 128 | 274 | 264 / 1 / 2 / 4 / 3 | 1 | 3 |
| `shift2-cachy-baseline-deep` | delete40 | 401086 / 7937 | 726 / 160 | 0 / 0 | 606 | 76 / 40 / 0 | 490 | 116 / 40 / 80 / 253 / 1 | 40 | 120 |
| `shift2-cachy-candidate-deep-pfs3-first` | create40 | 533182 / 17153 | 1042 / 160 | 40 / 40 | 882 | 75 / 130 / 40 | 637 | 283 / 40 / 80 / 145 / 89 | 40 | 120 |
| `shift2-cachy-candidate-deep-pfs3-first` | seq-write8M | 524414 / 43328 | 2483 / 4 | 2048 / 128 | 432 | 2 / 156 / 128 | 146 | 136 / 1 / 2 / 4 / 3 | 1 | 3 |
| `shift2-cachy-candidate-deep-pfs3-first` | delete40 | 399231 / 7872 | 726 / 160 | 0 / 0 | 606 | 76 / 40 / 0 | 490 | 116 / 40 / 80 / 253 / 1 | 40 | 120 |

## Reproduction

Read the key/value pairs from the 16 normal run directories’ `system/Results/{bfs,pfs3}.tsv` and the three deep directories’ `*.deep-compare.tsv`. First batch groups all `shift-cachy` runs; repeat batch groups all `shift2-cachy` runs. Compute means as `sum(values) / count`; percent deltas use `(candidate mean / baseline mean - 1) * 100`; paired deltas use the same block and order (`candidate - baseline`); candidate ratios use BFS/PFS3 from the same run; per-run threshold counts use `ratio > 5`. The six-value tuples follow the key order stated above. This was read-only extraction; no guest benchmark or verifier was rerun.

## Interpretation limits

This is descriptive data from eight candidate and eight baseline normal runs on one host; it does not establish statistical significance or cross-host variance. The candidate small-read ratio of means is 4.116×, but two of eight matched per-run ratios exceed 5×: a-P (5.005×) and c-P (5.986×). Those outliers remain in all means and extrema above. Create40, seq-write8M, and delete40 exceed 5× in all eight candidate runs; the other three workload counts are shown in the table. The strict verifier result supplied for this qualification is 19/19 PASS.
