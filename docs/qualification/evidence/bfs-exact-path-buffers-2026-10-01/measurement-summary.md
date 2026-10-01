# BFS exact path buffer normal measurements

Date: 2026-10-01

## Scope and validation

This summary covers eight normal fresh runs in the chronological order below. Original inputs were `build/benchmark/<case>/system/Results/{bfs,pfs3}.tsv`; all sixteen are persisted in the eight neighboring case directories as `{bfs,pfs3}.tsv`. The baseline source is `2c6fa11`; candidate handler `c4d4241…` changes insert/delete allocation from `height+1` blocks to `height` blocks. The normal pool-retained handler is `6ecdf122…`; all runs used guest `1fccf207…` and `FS_COMPARE_BENCH` schema `1`.

Each file was checked for the exact `FS_COMPARE_BENCH\t1` header, the expected drive (`DH1:` for BFS, `DH2:` for PFS3), exactly these six metric names in order, integer values, and a final `PASS\t1`. All sixteen files passed. Values are elapsed microseconds; lower is faster. No observations were trimmed.

All eight runs were performed serially after the final functional gates. There was no ten-minute gap between a first run and the remaining runs. This is a small descriptive sample and does not establish statistical or causal precision.

## Raw observations in chronological order

### BFS (DH1:), microseconds

| Case | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
|---|---:|---:|---:|---:|---:|---:|
| `path-normal-baseline-bfs-first` | 262975 | 106687 | 57600 | 401661 | 512382 | 208446 |
| `path-normal-candidate-pfs3-first` | 227711 | 150976 | 86783 | 414204 | 492669 | 175679 |
| `path-normal-baseline-pfs3-first` | 260224 | 145087 | 47359 | 506684 | 583550 | 191742 |
| `path-normal-candidate-bfs-first` | 238973 | 109309 | 46207 | 400765 | 515517 | 171135 |
| `path-normal-repeat-candidate-bfs-first` | 249407 | 105215 | 46591 | 406662 | 511612 | 180288 |
| `path-normal-repeat-baseline-pfs3-first` | 277950 | 129856 | 98111 | 594237 | 580669 | 182399 |
| `path-normal-repeat-candidate-pfs3-first` | 229887 | 107006 | 44545 | 416318 | 515197 | 168383 |
| `path-normal-repeat-baseline-bfs-first` | 248319 | 105471 | 51905 | 456573 | 562046 | 287999 |

### PFS3 (DH2:), microseconds

| Case | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
|---|---:|---:|---:|---:|---:|---:|
| `path-normal-baseline-bfs-first` | 16960 | 66625 | 16384 | 40895 | 295562 | 7936 |
| `path-normal-candidate-pfs3-first` | 16652 | 67006 | 14017 | 33600 | 270591 | 8256 |
| `path-normal-baseline-pfs3-first` | 17215 | 65791 | 14528 | 34240 | 262142 | 7936 |
| `path-normal-candidate-bfs-first` | 17024 | 69057 | 15168 | 37760 | 328318 | 15169 |
| `path-normal-repeat-candidate-bfs-first` | 17153 | 81983 | 14400 | 35328 | 261439 | 7937 |
| `path-normal-repeat-baseline-pfs3-first` | 17151 | 65856 | 14464 | 35583 | 267007 | 9664 |
| `path-normal-repeat-candidate-pfs3-first` | 16895 | 69183 | 17087 | 39424 | 288893 | 8192 |
| `path-normal-repeat-baseline-bfs-first` | 17152 | 67008 | 14528 | 36288 | 276990 | 7807 |

## Four-run means and unpaired changes

Each mean uses the four runs for that revision. Change is `(candidate mean / baseline mean - 1) × 100%`; negative values mean lower candidate elapsed time.

### BFS

| Metric | Baseline mean (µs) | Candidate mean (µs) | Change |
|---|---:|---:|---:|
| Create 40 | 262367.00 | 236494.50 | −9.86% |
| Lookup 400 | 121775.25 | 118126.50 | −3.00% |
| Small read 40 | 63743.75 | 56031.50 | −12.10% |
| Write 8 MiB | 489788.75 | 409487.25 | −16.40% |
| Read 8 MiB | 559661.75 | 508748.75 | −9.10% |
| Delete 40 | 217646.50 | 173871.25 | −20.11% |

### PFS3

| Metric | Baseline mean (µs) | Candidate mean (µs) | Change |
|---|---:|---:|---:|
| Create 40 | 17119.50 | 16931.00 | −1.10% |
| Lookup 400 | 66320.00 | 71807.25 | +8.27% |
| Small read 40 | 14976.00 | 15168.00 | +1.28% |
| Write 8 MiB | 36751.50 | 36528.00 | −0.61% |
| Read 8 MiB | 275425.25 | 287310.25 | +4.32% |
| Delete 40 | 8335.75 | 9888.50 | +18.63% |

## Matched BFS changes by order and repeat

Each cell gives candidate minus baseline in microseconds and percentage change for the corresponding order-matched pair. Negative values mean lower candidate elapsed time.

| BFS metric | First BFS-first | First PFS3-first | Repeat BFS-first | Repeat PFS3-first |
|---|---:|---:|---:|---:|
| Create 40 | −24002 (−9.13%) | −32513 (−12.49%) | +1088 (+0.44%) | −48063 (−17.29%) |
| Lookup 400 | +2622 (+2.46%) | +5889 (+4.06%) | −256 (−0.24%) | −22850 (−17.60%) |
| Small read 40 | −11393 (−19.78%) | +39424 (+83.25%) | −5314 (−10.24%) | −53566 (−54.60%) |
| Write 8 MiB | −896 (−0.22%) | −92480 (−18.25%) | −49911 (−10.93%) | −177919 (−29.94%) |
| Read 8 MiB | +3135 (+0.61%) | −90881 (−15.57%) | −50434 (−8.97%) | −65472 (−11.28%) |
| Delete 40 | −37311 (−17.90%) | −16063 (−8.38%) | −107711 (−37.40%) | −14016 (−7.68%) |

## Candidate same-run BFS/PFS3 ratios

Each ratio is BFS elapsed microseconds divided by PFS3 elapsed microseconds from the same candidate run. The range is across the four candidate runs. Ratio of means is candidate BFS mean divided by candidate PFS3 mean for that metric. Counts use the strict test `ratio > 5`.

| Metric | First PFS3-first | First BFS-first | Repeat BFS-first | Repeat PFS3-first | Range | Ratio of means | Count >5 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Create 40 | 13.675 | 14.037 | 14.540 | 13.607 | 13.607–14.540 | 13.968 | 4/4 |
| Lookup 400 | 2.253 | 1.583 | 1.283 | 1.547 | 1.283–2.253 | 1.645 | 0/4 |
| Small read 40 | 6.191 | 3.046 | 3.235 | 2.607 | 2.607–6.191 | 3.694 | 1/4 |
| Write 8 MiB | 12.328 | 10.613 | 11.511 | 10.560 | 10.560–12.328 | 11.210 | 4/4 |
| Read 8 MiB | 1.821 | 1.570 | 1.957 | 1.783 | 1.570–1.957 | 1.771 | 0/4 |
| Delete 40 | 21.279 | 11.282 | 22.715 | 20.555 | 11.282–22.715 | 17.583 | 4/4 |

All thirteen candidate same-run ratios strictly above 5 are retained below. The first candidate PFS3-first small-read BFS observation is 86783 µs; its same-run BFS/PFS3 ratio is 6.191 and remains included.

- Create 40: first PFS3-first 13.675; first BFS-first 14.037; repeat BFS-first 14.540; repeat PFS3-first 13.607.
- Small read 40: first PFS3-first 6.191.
- Write 8 MiB: first PFS3-first 12.328; first BFS-first 10.613; repeat BFS-first 11.511; repeat PFS3-first 10.560.
- Delete 40: first PFS3-first 21.279; first BFS-first 11.282; repeat BFS-first 22.715; repeat PFS3-first 20.555.

## Adverse matched samples retained

The table lists every order-and-repeat matched sample in which candidate elapsed time exceeded its corresponding baseline sample, for both filesystems. All are included in the means and tables above.

| Filesystem | Order | Metric | Baseline (µs) | Candidate (µs) | Change |
|---|---|---|---:|---:|---:|
| BFS | First BFS-first | Lookup 400 | 106687 | 109309 | +2622 (+2.46%) |
| BFS | First BFS-first | Read 8 MiB | 512382 | 515517 | +3135 (+0.61%) |
| BFS | First PFS3-first | Lookup 400 | 145087 | 150976 | +5889 (+4.06%) |
| BFS | First PFS3-first | Small read 40 | 47359 | 86783 | +39424 (+83.25%) |
| BFS | Repeat BFS-first | Create 40 | 248319 | 249407 | +1088 (+0.44%) |
| PFS3 | First BFS-first | Create 40 | 16960 | 17024 | +64 (+0.38%) |
| PFS3 | First BFS-first | Lookup 400 | 66625 | 69057 | +2432 (+3.65%) |
| PFS3 | First BFS-first | Read 8 MiB | 295562 | 328318 | +32756 (+11.08%) |
| PFS3 | First BFS-first | Delete 40 | 7936 | 15169 | +7233 (+91.14%) |
| PFS3 | First PFS3-first | Lookup 400 | 65791 | 67006 | +1215 (+1.85%) |
| PFS3 | First PFS3-first | Read 8 MiB | 262142 | 270591 | +8449 (+3.22%) |
| PFS3 | First PFS3-first | Delete 40 | 7936 | 8256 | +320 (+4.03%) |
| PFS3 | Repeat BFS-first | Create 40 | 17152 | 17153 | +1 (+0.01%) |
| PFS3 | Repeat BFS-first | Lookup 400 | 67008 | 81983 | +14975 (+22.35%) |
| PFS3 | Repeat BFS-first | Delete 40 | 7807 | 7937 | +130 (+1.67%) |
| PFS3 | Repeat PFS3-first | Lookup 400 | 65856 | 69183 | +3327 (+5.05%) |
| PFS3 | Repeat PFS3-first | Small read 40 | 14464 | 17087 | +2623 (+18.13%) |
| PFS3 | Repeat PFS3-first | Write 8 MiB | 35583 | 39424 | +3841 (+10.79%) |
| PFS3 | Repeat PFS3-first | Read 8 MiB | 267007 | 288893 | +21886 (+8.20%) |

## Reproducing the arithmetic from persisted inputs

No guest or benchmark rerun is needed. Read the neighboring `<case>/{bfs,pfs3}.tsv` files for each exact case name in the chronological tables above. After validating the schema, drive, metric order, integer values, and `PASS 1`, the tables above follow directly from these rules:

1. For each filesystem and metric, take the arithmetic mean across the four baseline cases and the four candidate cases; calculate unpaired change as `(candidate mean / baseline mean - 1) × 100%`.
2. Pair BFS observations by order and repetition: first BFS-first, first PFS3-first, repeat BFS-first, repeat PFS3-first. Calculate matched differences as `candidate - baseline` and percentages as `(candidate / baseline - 1) × 100%`. Use the same four pairs to identify every adverse sample for BFS and PFS3.
3. For candidate same-run ratios, divide each candidate case's BFS value by its PFS3 value for each metric. Calculate the range from the four ratios, the ratio of four-run means, and the count using strict `> 5`.
