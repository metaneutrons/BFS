# BFS cache lookup hint normal measurements

All sixteen schema-1 TSVs were independently checked for the exact header,
expected drive, ordered six integer metrics and PASS 1. Microseconds below
include every observation. The chronological order is the table order.
Candidate normal handler c832040b…; baseline c4d4241a…; unchanged guest 1fccf207….

## BFS raw observations

| Case | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hint-normal-baseline-bfs-first | 223359 | 101888 | 45631 | 382013 | 484989 | 247744 |
| hint-normal-candidate-pfs3-first | 226111 | 121471 | 42175 | 390206 | 595963 | 198527 |
| hint-normal-baseline-pfs3-first | 294143 | 106944 | 49473 | 407165 | 547708 | 200434 |
| hint-normal-candidate-bfs-first | 250495 | 130623 | 50369 | 419006 | 558462 | 169153 |
| hint-normal-repeat-candidate-bfs-first | 243071 | 102271 | 44351 | 422398 | 498877 | 196608 |
| hint-normal-repeat-baseline-pfs3-first | 333118 | 105984 | 47103 | 395645 | 501566 | 275070 |
| hint-normal-repeat-candidate-pfs3-first | 421372 | 103166 | 47231 | 384957 | 495549 | 213017 |
| hint-normal-repeat-baseline-bfs-first | 279424 | 127166 | 67136 | 462590 | 532477 | 171775 |

## PFS3 raw observations

| Case | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hint-normal-baseline-bfs-first | 18112 | 66496 | 15232 | 42943 | 278588 | 8064 |
| hint-normal-candidate-pfs3-first | 17664 | 66369 | 13824 | 33791 | 291582 | 7936 |
| hint-normal-baseline-pfs3-first | 16256 | 74880 | 14145 | 34496 | 256575 | 8064 |
| hint-normal-candidate-bfs-first | 18753 | 80639 | 14912 | 37311 | 355839 | 10367 |
| hint-normal-repeat-candidate-bfs-first | 16320 | 66688 | 14271 | 38218 | 276991 | 12288 |
| hint-normal-repeat-baseline-pfs3-first | 17152 | 66497 | 14720 | 36352 | 264126 | 7809 |
| hint-normal-repeat-candidate-pfs3-first | 18688 | 72307 | 13888 | 52033 | 318207 | 14655 |
| hint-normal-repeat-baseline-bfs-first | 26945 | 69441 | 15104 | 39603 | 350910 | 7935 |

## Means and change

| Workload | Baseline BFS | Candidate BFS | Change percent | Baseline PFS3 | Candidate PFS3 | Ratio of candidate means |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 282511.00 | 285262.25 | 0.97 | 19616.25 | 17856.25 | 15.975 |
| Lookup 400 | 110495.50 | 114382.75 | 3.52 | 69328.50 | 71500.75 | 1.600 |
| Small read 40 | 52335.75 | 46031.50 | -12.05 | 14800.25 | 14223.75 | 3.236 |
| Write 8 MiB | 411853.25 | 404141.75 | -1.87 | 38348.50 | 40338.25 | 10.019 |
| Read 8 MiB | 516685.00 | 537212.75 | 3.97 | 287549.75 | 310654.75 | 1.729 |
| Delete 40 | 223755.75 | 194326.25 | -13.15 | 7968.00 | 11311.50 | 17.180 |

## Matched BFS change

Candidate minus baseline in microseconds, then percent of the corresponding
baseline. Positive values mean slower. Pairs match order and repeat, not a
controlled physical-host scheduling condition.

| Workload | First BFS first | First PFS3 first | Repeat BFS first | Repeat PFS3 first |
| --- | ---: | ---: | ---: | ---: |
| Create 40 | 27136 (12.15%) | -68032 (-23.13%) | -36353 (-13.01%) | 88254 (26.49%) |
| Lookup 400 | 28735 (28.20%) | 14527 (13.58%) | -24895 (-19.58%) | -2818 (-2.66%) |
| Small read 40 | 4738 (10.38%) | -7298 (-14.75%) | -22785 (-33.94%) | 128 (0.27%) |
| Write 8 MiB | 36993 (9.68%) | -16959 (-4.17%) | -40192 (-8.69%) | -10688 (-2.70%) |
| Read 8 MiB | 73473 (15.15%) | 48255 (8.81%) | -33600 (-6.31%) | -6017 (-1.20%) |
| Delete 40 | -78591 (-31.72%) | -1907 (-0.95%) | 24833 (14.46%) | -62053 (-22.56%) |

## Candidate same run ratios

| Workload | First PFS3 first | First BFS first | Repeat BFS first | Repeat PFS3 first | Range | Count above five |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| Create 40 | 12.801 | 13.358 | 14.894 | 22.548 | 12.801–22.548 | 4/4 |
| Lookup 400 | 1.830 | 1.620 | 1.534 | 1.427 | 1.427–1.830 | 0/4 |
| Small read 40 | 3.051 | 3.378 | 3.108 | 3.401 | 3.051–3.401 | 0/4 |
| Write 8 MiB | 11.548 | 11.230 | 11.052 | 7.398 | 7.398–11.548 | 4/4 |
| Read 8 MiB | 2.044 | 1.569 | 1.801 | 1.557 | 1.557–2.044 | 0/4 |
| Delete 40 | 25.016 | 16.316 | 16.000 | 14.535 | 14.535–25.016 | 4/4 |

## Adverse observations and limitations

Ten of 24 matched BFS changes are positive: create, lookup, small read and
large read each have two; large write and delete each have one. All are retained.
The repeat PFS3-first candidate create is 421372 µs. Neither that observation
nor baseline create 333118 µs or baseline small read 67136 µs was excluded.
Candidate PFS3 write 52033 µs, read 355839 µs and delete 14655 µs also remain.
Twelve candidate ratios exceed five: every create, write and delete. No other
candidate ratio exceeds five.

Four fresh observations per revision are descriptive, not statistical precision
or proof of a causal regression. Revisions/order are interleaved, but other VM
and physical-host scheduling effects are uncontrolled. Smaller source-level
search counts do not establish faster normal-handler elapsed time. Independent
Luna extraction and primary calculations agree. The adoption decision is rejection
for insufficient consistent benefit, not a general claim that a hint cannot help.
