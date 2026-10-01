# Inode stamp normal measurement summary

All values below come from schema-1 TSVs. Eight fresh normal images ran serially
in the listed order after two separate schema-10 probe runs. Four observations
per revision are balanced by filesystem order. All sixteen normal filesystem
outputs passed the strict verifier. No sample is trimmed.

## Chronological observations

Elapsed microseconds; each cell is BFS / PFS3.

| Run | Create 40 | Lookup 400 | Small read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline-bfs-first | 277695 / 16320 | 104960 / 66496 | 46655 / 13952 | 385158 / 34175 | 592765 / 293119 | 196542 / 11777 |
| candidate-pfs3-first | 187839 / 16895 | 105151 / 66880 | 47743 / 14848 | 374461 / 35648 | 502397 / 275121 | 169215 / 8000 |
| baseline-pfs3-first | 267904 / 21055 | 135934 / 148287 | 52738 / 26816 | 376063 / 67827 | 543037 / 279175 | 189311 / 8384 |
| candidate-bfs-first | 254653 / 15104 | 139326 / 69566 | 57665 / 22642 | 361086 / 51137 | 504382 / 259966 | 166719 / 8128 |
| repeat-candidate-bfs-first | 249791 / 18815 | 124225 / 74353 | 46528 / 14080 | 389310 / 36864 | 549168 / 261502 | 202302 / 8000 |
| repeat-baseline-pfs3-first | 322493 / 16833 | 105023 / 66176 | 49856 / 14145 | 382653 / 33985 | 486652 / 315069 | 171199 / 9023 |
| repeat-candidate-pfs3-first | 192190 / 17089 | 117376 / 66623 | 45054 / 15296 | 371966 / 34431 | 529532 / 269310 | 161471 / 8191 |
| repeat-baseline-bfs-first | 230207 / 17151 | 105663 / 65408 | 47359 / 14284 | 498109 / 34495 | 546109 / 267262 | 174144 / 7806 |

## Means and order/repeat-matched comparisons

Negative changes mean less candidate elapsed time. Ratios of means are not
the means of individual ratios.

| Workload | Baseline BFS | Candidate BFS | Candidate change | Candidate PFS3 | Ratio of candidate means | Adverse matched cases | Candidate ratios above 5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Create 40 | 274574.75 | 221118.25 | -19.47% | 16975.75 | 13.026 | 1/4 | 4/4 |
| Lookup 400 | 112895 | 121519.50 | +7.64% | 69355.50 | 1.752 | 3/4 | 0/4 |
| Small read 40 | 49152 | 49247.50 | +0.19% | 16716.50 | 2.946 | 1/4 | 0/4 |
| Write 8 MiB | 410495.75 | 374205.75 | -8.84% | 39520 | 9.469 | 0/4 | 4/4 |
| Read 8 MiB | 542140.75 | 521369.75 | -3.83% | 266474.75 | 1.957 | 2/4 | 0/4 |
| Delete 40 | 182799 | 174926.75 | -4.31% | 8079.75 | 21.650 | 1/4 | 4/4 |

There are 8/24 adverse BFS matched changes. All four candidate create, write and delete
observations exceed five times their same-run PFS3 time; lookup and both read
phases do not. The goal remains unachieved. Matching on filesystem order and
repeat is descriptive; it does not remove uncontrolled host scheduling or
establish statistical/causal precision. PFS3 variation is not attributed to
the BFS source change.

## All matched changes and same-run candidate ratios

Each row compares one candidate to the baseline with the same order/repeat.

| Workload / candidate label | Baseline BFS | Candidate BFS | Change | Candidate PFS3 | Same-run ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| Create 40 / candidate-pfs3-first | 267904 | 187839 | -29.89% | 16895 | 11.118023 |
| Create 40 / candidate-bfs-first | 277695 | 254653 | -8.30% | 15104 | 16.859971 |
| Create 40 / repeat-candidate-bfs-first | 230207 | 249791 | +8.51% | 18815 | 13.276163 |
| Create 40 / repeat-candidate-pfs3-first | 322493 | 192190 | -40.40% | 17089 | 11.246416 |
| Lookup 400 / candidate-pfs3-first | 135934 | 105151 | -22.65% | 66880 | 1.572234 |
| Lookup 400 / candidate-bfs-first | 104960 | 139326 | +32.74% | 69566 | 2.002789 |
| Lookup 400 / repeat-candidate-bfs-first | 105663 | 124225 | +17.57% | 74353 | 1.670746 |
| Lookup 400 / repeat-candidate-pfs3-first | 105023 | 117376 | +11.76% | 66623 | 1.761794 |
| Small read 40 / candidate-pfs3-first | 52738 | 47743 | -9.47% | 14848 | 3.215450 |
| Small read 40 / candidate-bfs-first | 46655 | 57665 | +23.60% | 22642 | 2.546816 |
| Small read 40 / repeat-candidate-bfs-first | 47359 | 46528 | -1.75% | 14080 | 3.304545 |
| Small read 40 / repeat-candidate-pfs3-first | 49856 | 45054 | -9.63% | 15296 | 2.945476 |
| Write 8 MiB / candidate-pfs3-first | 376063 | 374461 | -0.43% | 35648 | 10.504404 |
| Write 8 MiB / candidate-bfs-first | 385158 | 361086 | -6.25% | 51137 | 7.061149 |
| Write 8 MiB / repeat-candidate-bfs-first | 498109 | 389310 | -21.84% | 36864 | 10.560710 |
| Write 8 MiB / repeat-candidate-pfs3-first | 382653 | 371966 | -2.79% | 34431 | 10.803230 |
| Read 8 MiB / candidate-pfs3-first | 543037 | 502397 | -7.48% | 275121 | 1.826095 |
| Read 8 MiB / candidate-bfs-first | 592765 | 504382 | -14.91% | 259966 | 1.940184 |
| Read 8 MiB / repeat-candidate-bfs-first | 546109 | 549168 | +0.56% | 261502 | 2.100053 |
| Read 8 MiB / repeat-candidate-pfs3-first | 486652 | 529532 | +8.81% | 269310 | 1.966255 |
| Delete 40 / candidate-pfs3-first | 189311 | 169215 | -10.62% | 8000 | 21.151875 |
| Delete 40 / candidate-bfs-first | 196542 | 166719 | -15.17% | 8128 | 20.511688 |
| Delete 40 / repeat-candidate-bfs-first | 174144 | 202302 | +16.17% | 8000 | 25.287750 |
| Delete 40 / repeat-candidate-pfs3-first | 171199 | 161471 | -5.68% | 8191 | 19.713222 |

| Workload | Candidate ratio range |
| --- | ---: |
| Create 40 | 11.118023–16.859971 |
| Lookup 400 | 1.572234–2.002789 |
| Small read 40 | 2.546816–3.304545 |
| Write 8 MiB | 7.061149–10.803230 |
| Read 8 MiB | 1.826095–2.100053 |
| Delete 40 | 19.713222–25.287750 |

The slower repeat-candidate-BFS-first create/delete and all three slower
matched lookups are retained. The high baseline write (498109 µs), baseline
create (322493 µs), baseline PFS3 lookup (148287 µs) and write (67827 µs) also
remain. No instrumented elapsed measurement is used for normal-run acceptance.
