# Sealed preflight geometry measurements

All sixteen normal filesystem outputs pass schema 1 and the unchanged strict
completion and data verifier. Times are microseconds. Four fresh runs per
version balance both filesystem orders; no sample is removed or rerun.
The primary calculation and independent Luna extraction agree on all means,
matched deltas, same-run ratio ranges and threshold counts. Fourteen of the
twenty-four matched BFS pairs are adverse.

| Workload | Baseline BFS mean | Candidate BFS mean | Candidate PFS3 mean | BFS change percent | Ratio of means | Same-run ratio range | Candidate runs over five times |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SMALL_CREATE_40_US | 249358.5 | 238830.25 | 18623.5 | -4.222134 | 12.824133 | 11.878606–14.173017 | 4/4 |
| LOOKUP_400_US | 118046.5 | 106431.75 | 81273.25 | -9.839131 | 1.309554 | 1.119395–1.588108 | 0/4 |
| SMALL_READ_40_US | 56976 | 50399.5 | 14800 | -11.542579 | 3.405372 | 3.239590–3.681614 | 0/4 |
| SEQ_WRITE_8M_US | 395104 | 434957.25 | 42831.5 | 10.086775 | 10.155079 | 6.623609–13.720235 | 4/4 |
| SEQ_READ_8M_US | 550316.75 | 559580.75 | 314350.25 | 1.683394 | 1.780119 | 1.427764–2.399761 | 0/4 |
| SMALL_DELETE_40_US | 178846.75 | 196974.5 | 9423.75 | 10.135912 | 20.901923 | 15.260700–25.176986 | 4/4 |

## Every timing observation

Tuple order: create40, lookup400, smallread40, write8MiB, read8MiB, delete40.
Run labels identify the source artifacts and chronological order.

| Run | BFS tuple | PFS3 tuple |
| --- | --- | --- |
| baseline-bfs-first | 246718, 122111, 46080, 390398, 526909, 169599 | 18496, 74175, 14784, 67326, 278461, 8000 |
| candidate-pfs3-first | 235839, 105088, 45824, 480318, 616060, 179710 | 16640, 73972, 14145, 35008, 431486, 11776 |
| baseline-pfs3-first | 226687, 104831, 46272, 401085, 498877, 174014 | 17792, 67775, 15744, 35264, 250879, 7744 |
| candidate-bfs-first | 228222, 106816, 54014, 406524, 496317, 212670 | 19007, 95423, 15615, 61375, 308479, 8447 |
| repeat-candidate-bfs-first | 234302, 106111, 49216, 391998, 500861, 174399 | 17215, 66816, 15168, 36671, 256958, 8129 |
| repeat-baseline-pfs3-first | 278655, 141117, 79487, 414525, 683581, 198207 | 17600, 67583, 14655, 35711, 290055, 8193 |
| repeat-candidate-pfs3-first | 256958, 107712, 52544, 460989, 625085, 221119 | 21632, 88882, 14272, 38272, 260478, 9343 |
| repeat-baseline-bfs-first | 245374, 104127, 56065, 374408, 491900, 173567 | 14655, 67583, 12544, 33395, 256575, 8127 |

## Same-order candidate minus baseline

Positive values are adverse. Pairs are first BFS-first, first PFS3-first,
repeat BFS-first and repeat PFS3-first. These are descriptive comparisons,
not randomized causal estimates or confidence intervals.

| Workload | First BFS-first | First PFS3-first | Repeat BFS-first | Repeat PFS3-first |
| --- | ---: | ---: | ---: | ---: |
| SMALL_CREATE_40_US | -18496 | 9152 | -11072 | -21697 |
| LOOKUP_400_US | -15295 | 257 | 1984 | -33405 |
| SMALL_READ_40_US | 7934 | -448 | -6849 | -26943 |
| SEQ_WRITE_8M_US | 16126 | 79233 | 17590 | 46464 |
| SEQ_READ_8M_US | -30592 | 117183 | 8961 | -58496 |
| SMALL_DELETE_40_US | 43071 | 5696 | 832 | 22912 |

Write and delete are adverse in all four matched pairs. Other adverse BFS
pairs are first PFS3-first create and lookup, repeat BFS-first lookup and
large read, first BFS-first small read, and first PFS3-first large read.
Unchanged read/lookup paths also vary, limiting attribution. Every candidate
create, write and delete sample remains over five times its same-run PFS3
reference; all candidate lookup and read samples are below that threshold.
