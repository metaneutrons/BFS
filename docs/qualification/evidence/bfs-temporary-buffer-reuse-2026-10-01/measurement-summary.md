# BFS temporary buffer reuse normal measurements

Date: 2026-10-01

## Scope and validation

This summary covers the eight normal fresh runs in the order shown below, using all sixteen files at `build/benchmark/<case>/system/Results/{bfs,pfs3}.tsv`. The baseline was `f3b593ad…` (from `86e9d80`); the candidate was `6ecdf122…`, with the shared BIO four-slot scratch-buffer recycler. All runs used normal guest `1fccf207…` and reported `FS_COMPARE_BENCH` schema `1` and `PASS 1`.

All sixteen files were checked for the exact schema header, expected drive (`DH1:` for BFS and `DH2:` for PFS3), exactly the six metric names below in this order, integer values, and final `PASS\t1`. Every file passed these checks. Values below are elapsed microseconds, so lower values are faster. No observations were trimmed.

The first baseline BFS-first run was taken about ten minutes before the following seven runs while the final functional gates were being completed. The next seven were run serially. This is a small descriptive sample; no claim of statistical or causal precision is made.

## Raw observations in chronological order

### BFS (DH1:), microseconds

| Run | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
|---|---:|---:|---:|---:|---:|---:|
| Baseline, first BFS-first | 299390 | 125183 | 56576 | 470014 | 566525 | 215742 |
| Candidate, first PFS3-first | 235467 | 103424 | 46080 | 400062 | 514493 | 229824 |
| Candidate, first BFS-first | 262782 | 112128 | 46653 | 398655 | 516157 | 189502 |
| Baseline, first PFS3-first | 328318 | 126274 | 60800 | 501820 | 566269 | 235198 |
| Baseline, repeat PFS3-first | 294781 | 125119 | 73793 | 489085 | 596477 | 241984 |
| Candidate, repeat BFS-first | 265854 | 110463 | 46143 | 451197 | 529470 | 191294 |
| Candidate, repeat PFS3-first | 309836 | 105023 | 48832 | 486524 | 494141 | 182271 |
| Baseline, repeat BFS-first | 286718 | 124608 | 56512 | 464766 | 572477 | 254783 |

### PFS3 (DH2:), microseconds

| Run | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
|---|---:|---:|---:|---:|---:|---:|
| Baseline, first BFS-first | 16448 | 66239 | 14400 | 37760 | 258687 | 7808 |
| Candidate, first PFS3-first | 15936 | 66496 | 14400 | 36467 | 364413 | 7871 |
| Candidate, first BFS-first | 16063 | 66686 | 13440 | 34623 | 280061 | 7922 |
| Baseline, first PFS3-first | 30593 | 95347 | 19968 | 36915 | 307263 | 15707 |
| Baseline, repeat PFS3-first | 16768 | 65729 | 14145 | 35905 | 274367 | 8192 |
| Candidate, repeat BFS-first | 16766 | 74431 | 14080 | 41345 | 258110 | 8192 |
| Candidate, repeat PFS3-first | 16255 | 65471 | 14145 | 35840 | 256767 | 8128 |
| Baseline, repeat BFS-first | 15872 | 70017 | 15424 | 77695 | 301823 | 8128 |

## Four-run means and unpaired change

Each baseline mean and candidate mean uses its four runs. Change is `(candidate mean / baseline mean - 1) × 100%`; a negative percentage means lower elapsed time for the candidate.

### BFS

| Metric | Baseline mean (µs) | Candidate mean (µs) | Change |
|---|---:|---:|---:|
| Create 40 | 302301.75 | 268484.75 | −11.19% |
| Lookup 400 | 125296.00 | 107759.50 | −14.00% |
| Small read 40 | 61920.25 | 46927.00 | −24.21% |
| Write 8 MiB | 481421.25 | 434109.50 | −9.83% |
| Read 8 MiB | 575437.00 | 513565.25 | −10.75% |
| Delete 40 | 236926.75 | 198222.75 | −16.34% |

### PFS3

| Metric | Baseline mean (µs) | Candidate mean (µs) | Change |
|---|---:|---:|---:|
| Create 40 | 19920.25 | 16255.00 | −18.40% |
| Lookup 400 | 74333.00 | 68271.00 | −8.16% |
| Small read 40 | 15984.25 | 14016.25 | −12.31% |
| Write 8 MiB | 47068.75 | 37068.75 | −21.25% |
| Read 8 MiB | 285535.00 | 289837.75 | +1.51% |
| Delete 40 | 9958.75 | 8028.25 | −19.39% |

## Matched BFS changes by order and repeat

Each cell gives candidate minus baseline in microseconds and the percentage change for that order-matched pair. Negative values mean lower candidate elapsed time.

| BFS metric | First BFS-first | First PFS3-first | Repeat BFS-first | Repeat PFS3-first |
|---|---:|---:|---:|---:|
| Create 40 | −36608 (−12.23%) | −92851 (−28.28%) | −20864 (−7.28%) | +15055 (+5.11%) |
| Lookup 400 | −13055 (−10.43%) | −22850 (−18.10%) | −14145 (−11.35%) | −20096 (−16.06%) |
| Small read 40 | −9923 (−17.54%) | −14720 (−24.21%) | −10369 (−18.35%) | −24961 (−33.83%) |
| Write 8 MiB | −71359 (−15.18%) | −101758 (−20.28%) | −13569 (−2.92%) | −2561 (−0.52%) |
| Read 8 MiB | −50368 (−8.89%) | −51776 (−9.14%) | −43007 (−7.51%) | −102336 (−17.16%) |
| Delete 40 | −26240 (−12.16%) | −5374 (−2.28%) | −63489 (−24.92%) | −59713 (−24.68%) |

## Candidate same-run BFS/PFS3 ratios

Ratios are elapsed BFS microseconds divided by elapsed PFS3 microseconds from the same candidate run. The range is across the four candidate runs. Ratio of means is the four-run BFS mean divided by the four-run PFS3 mean for that metric. The count is the number of the four same-run ratios greater than 5.

| Metric | First PFS3-first | First BFS-first | Repeat BFS-first | Repeat PFS3-first | Range | Ratio of means | Count >5 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Create 40 | 14.776 | 16.359 | 15.857 | 19.061 | 14.776–19.061 | 16.517 | 4/4 |
| Lookup 400 | 1.555 | 1.681 | 1.484 | 1.604 | 1.484–1.681 | 1.578 | 0/4 |
| Small read 40 | 3.200 | 3.471 | 3.277 | 3.452 | 3.200–3.471 | 3.348 | 0/4 |
| Write 8 MiB | 10.971 | 11.514 | 10.913 | 13.575 | 10.913–13.575 | 11.711 | 4/4 |
| Read 8 MiB | 1.412 | 1.843 | 2.051 | 1.924 | 1.412–2.051 | 1.772 | 0/4 |
| Delete 40 | 29.199 | 23.921 | 23.351 | 22.425 | 22.425–29.199 | 24.691 | 4/4 |

All twelve ratios above 5 are retained here, grouped by metric:

- Create 40: first PFS3-first 14.776; first BFS-first 16.359; repeat BFS-first 15.857; repeat PFS3-first 19.061.
- Write 8 MiB: first PFS3-first 10.971; first BFS-first 11.514; repeat BFS-first 10.913; repeat PFS3-first 13.575.
- Delete 40: first PFS3-first 29.199; first BFS-first 23.921; repeat BFS-first 23.351; repeat PFS3-first 22.425.

## Adverse matched samples retained

These are all order-and-repeat matched metric samples where candidate elapsed time exceeded its corresponding baseline sample. They remain included in every mean and table above.

| Filesystem | Order | Metric | Baseline (µs) | Candidate (µs) | Change |
|---|---|---|---:|---:|---:|
| BFS | Repeat PFS3-first | Create 40 | 294781 | 309836 | +15055 (+5.11%) |
| PFS3 | First BFS-first | Lookup 400 | 66239 | 66686 | +447 (+0.67%) |
| PFS3 | First BFS-first | Read 8 MiB | 258687 | 280061 | +21374 (+8.26%) |
| PFS3 | First BFS-first | Delete 40 | 7808 | 7922 | +114 (+1.46%) |
| PFS3 | First PFS3-first | Read 8 MiB | 307263 | 364413 | +57150 (+18.60%) |
| PFS3 | Repeat BFS-first | Create 40 | 15872 | 16766 | +894 (+5.63%) |
| PFS3 | Repeat BFS-first | Lookup 400 | 70017 | 74431 | +4414 (+6.30%) |
| PFS3 | Repeat BFS-first | Delete 40 | 8128 | 8192 | +64 (+0.79%) |

The unpaired PFS3 read-8-MiB mean is also higher for the candidate (+1.51%).

## Reproduction from persisted inputs

The eight neighboring `buffer-normal-*` directories preserve the original
case names and each contains `{bfs,pfs3}.tsv` directly. The table labels map to
the version, optional `repeat` component and filesystem-order suffix in those
names. Read the six named metric rows without removing any run. Compute means
as sum divided by four, percentages as `(candidate / baseline - 1) × 100`,
matched changes within the same repeat/order, and ratios from the same
candidate case. Count strict ratios greater than five; rounding is only for
display. These normal measurements exclude all probe outputs.
