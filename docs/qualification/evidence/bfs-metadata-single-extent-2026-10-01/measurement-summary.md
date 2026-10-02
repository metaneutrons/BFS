# Complete normal single-extent metadata measurements

All sixteen normal TSVs are retained. Pairing matches filesystem order and repeat,
not physical simultaneity; the runner script records the balanced chronological
sequence. Positive change means more candidate BFS time. No observation is removed.
Probe elapsed time is excluded. Raw elapsed values are integer microseconds.

| Phase | Order/repeat pair | Baseline BFS | Candidate BFS | BFS change | Baseline PFS3 | Candidate PFS3 | Same-run candidate ratio | Above 5 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Create 40 | baseline-bfs-first | 228351 | 260794 | 14.208% | 15937 | 17108 | 15.243979 | yes |
| Create 40 | baseline-pfs3-first | 187517 | 276350 | 47.373% | 16768 | 15360 | 17.991536 | yes |
| Create 40 | repeat-baseline-pfs3-first | 190016 | 311935 | 64.162% | 16960 | 18560 | 16.806843 | yes |
| Create 40 | repeat-baseline-bfs-first | 190590 | 273150 | 43.318% | 18111 | 18112 | 15.081162 | yes |
| Lookup 400 | baseline-bfs-first | 105664 | 107200 | 1.454% | 68287 | 65076 | 1.647305 | no |
| Lookup 400 | baseline-pfs3-first | 110079 | 104062 | -5.466% | 66048 | 66368 | 1.567954 | no |
| Lookup 400 | repeat-baseline-pfs3-first | 107071 | 105728 | -1.254% | 66431 | 66496 | 1.589990 | no |
| Lookup 400 | repeat-baseline-bfs-first | 103999 | 104448 | 0.432% | 66880 | 70528 | 1.480944 | no |
| Small read 40 | baseline-bfs-first | 44351 | 45951 | 3.608% | 13825 | 14399 | 3.191263 | no |
| Small read 40 | baseline-pfs3-first | 61825 | 44607 | -27.850% | 13570 | 13619 | 3.275351 | no |
| Small read 40 | repeat-baseline-pfs3-first | 48193 | 46912 | -2.658% | 14336 | 15040 | 3.119149 | no |
| Small read 40 | repeat-baseline-bfs-first | 48511 | 46591 | -3.958% | 15295 | 19135 | 2.434858 | no |
| Write 8 MiB | baseline-bfs-first | 381631 | 523262 | 37.112% | 35391 | 34496 | 15.168773 | yes |
| Write 8 MiB | baseline-pfs3-first | 361342 | 469437 | 29.915% | 38719 | 35263 | 13.312452 | yes |
| Write 8 MiB | repeat-baseline-pfs3-first | 361150 | 489277 | 35.478% | 34879 | 36543 | 13.389076 | yes |
| Write 8 MiB | repeat-baseline-bfs-first | 427903 | 508862 | 18.920% | 35968 | 67698 | 7.516647 | yes |
| Read 8 MiB | baseline-bfs-first | 594094 | 497469 | -16.264% | 287358 | 256766 | 1.937441 | no |
| Read 8 MiB | baseline-pfs3-first | 485949 | 481789 | -0.856% | 255934 | 265279 | 1.816160 | no |
| Read 8 MiB | repeat-baseline-pfs3-first | 494077 | 517118 | 4.663% | 257215 | 261438 | 1.977976 | no |
| Read 8 MiB | repeat-baseline-bfs-first | 488380 | 534334 | 9.409% | 356669 | 262335 | 2.036838 | no |
| Delete 40 | baseline-bfs-first | 197312 | 240767 | 22.023% | 8050 | 7937 | 30.334761 | yes |
| Delete 40 | baseline-pfs3-first | 167296 | 233790 | 39.746% | 7936 | 7935 | 29.463138 | yes |
| Delete 40 | repeat-baseline-pfs3-first | 189504 | 231102 | 21.951% | 7936 | 7745 | 29.838864 | yes |
| Delete 40 | repeat-baseline-bfs-first | 165694 | 227326 | 37.196% | 11187 | 7806 | 29.121957 | yes |

Seventeen of 24 matched BFS changes are adverse. Every create, write and delete
candidate observation exceeds five times its same-run PFS3 value (12 of 24
threshold failures). Independent Luna extraction agrees with all means, raw
matched deltas, adverse counts and ratios in metadata-single-extent-comparison.json.
