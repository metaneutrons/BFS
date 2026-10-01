# Complete normal metadata-batch measurements

All sixteen normal TSVs are retained. Pairing matches filesystem order and repeat,
not physical simultaneity; the runner script records the balanced chronological
sequence. Positive change means more candidate BFS time. No observation is removed.
Probe elapsed time is excluded. Raw elapsed values are integer microseconds.

| Phase | Order/repeat pair | Baseline BFS | Candidate BFS | BFS change | Baseline PFS3 | Candidate PFS3 | Same-run candidate ratio | Above 5 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Create 40 | baseline-bfs-first | 199805 | 323391 | 61.853% | 16960 | 16448 | 19.661418 | yes |
| Create 40 | baseline-pfs3-first | 189951 | 286334 | 50.741% | 16063 | 16127 | 17.754945 | yes |
| Create 40 | repeat-baseline-pfs3-first | 190719 | 295742 | 55.067% | 16768 | 15355 | 19.260306 | yes |
| Create 40 | repeat-baseline-bfs-first | 206270 | 285951 | 38.629% | 17280 | 17473 | 16.365306 | yes |
| Lookup 400 | baseline-bfs-first | 113394 | 167871 | 48.042% | 66814 | 65216 | 2.574077 | no |
| Lookup 400 | baseline-pfs3-first | 105919 | 119231 | 12.568% | 72448 | 65278 | 1.826511 | no |
| Lookup 400 | repeat-baseline-pfs3-first | 105984 | 103742 | -2.115% | 66815 | 67008 | 1.548203 | no |
| Lookup 400 | repeat-baseline-bfs-first | 106176 | 102078 | -3.860% | 66431 | 65663 | 1.554574 | no |
| Small read 40 | baseline-bfs-first | 50433 | 46337 | -8.122% | 13824 | 14335 | 3.232438 | no |
| Small read 40 | baseline-pfs3-first | 47423 | 60480 | 27.533% | 15168 | 15168 | 3.987342 | no |
| Small read 40 | repeat-baseline-pfs3-first | 47680 | 44160 | -7.383% | 14719 | 13951 | 3.165364 | no |
| Small read 40 | repeat-baseline-bfs-first | 42497 | 46144 | 8.582% | 14337 | 14529 | 3.175993 | no |
| Write 8 MiB | baseline-bfs-first | 395198 | 454014 | 14.883% | 33856 | 34688 | 13.088503 | yes |
| Write 8 MiB | baseline-pfs3-first | 366664 | 484734 | 32.201% | 36031 | 34303 | 14.130951 | yes |
| Write 8 MiB | repeat-baseline-pfs3-first | 378238 | 517308 | 36.768% | 34880 | 34560 | 14.968403 | yes |
| Write 8 MiB | repeat-baseline-bfs-first | 371709 | 449214 | 20.851% | 34496 | 34880 | 12.878842 | yes |
| Read 8 MiB | baseline-bfs-first | 481151 | 475199 | -1.237% | 255998 | 271101 | 1.752849 | no |
| Read 8 MiB | baseline-pfs3-first | 485949 | 487932 | 0.408% | 260159 | 262397 | 1.859518 | no |
| Read 8 MiB | repeat-baseline-pfs3-first | 508477 | 514430 | 1.171% | 259518 | 255104 | 2.016550 | no |
| Read 8 MiB | repeat-baseline-bfs-first | 520062 | 490174 | -5.747% | 256064 | 256255 | 1.912837 | no |
| Delete 40 | baseline-bfs-first | 165374 | 232766 | 40.751% | 7808 | 7872 | 29.568852 | yes |
| Delete 40 | baseline-pfs3-first | 165822 | 226368 | 36.513% | 7808 | 7935 | 28.527788 | yes |
| Delete 40 | repeat-baseline-pfs3-first | 165054 | 229247 | 38.892% | 8065 | 7936 | 28.886971 | yes |
| Delete 40 | repeat-baseline-bfs-first | 213439 | 299390 | 40.270% | 7935 | 7937 | 37.720801 | yes |

Eighteen of 24 matched BFS changes are adverse. Every create, write and delete
candidate observation exceeds five times its same-run PFS3 value (12 of 24
threshold failures). Independent Luna extraction agrees with all means, raw
matched deltas, adverse counts and ratios in metadata-batch-comparison.json.
