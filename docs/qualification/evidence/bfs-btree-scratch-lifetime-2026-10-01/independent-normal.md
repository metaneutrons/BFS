# Scratch-lifetime normal benchmark: independent raw extraction

Independent calculation from the 16 normal `bfs.tsv`/`pfs3.tsv` files. The
primary normal-summary script and JSON were not read or used. The run log lists
all eight normal labels as verified by `verify-bench-results.sh`; I also
independently checked every file's schema, drive, exact six-metric sequence,
positive integer values, `PASS\t1` terminator, completion marker, and mounted
volume inventory.

The metric order below is `SMALL_CREATE_40_US`, `LOOKUP_400_US`,
`SMALL_READ_40_US`, `SEQ_WRITE_8M_US`, `SEQ_READ_8M_US`,
`SMALL_DELETE_40_US`. Times are microseconds. Baseline/candidate means use four
fresh runs per filesystem and revision. Deltas are `(candidate / baseline -
1) * 100`, matched by run/order. Ratios are same-run candidate BFS/PFS3.

## Means

| Workload | Baseline BFS | Candidate BFS | BFS Δ | Baseline PFS3 | Candidate PFS3 | PFS3 Δ |
|---|---:|---:|---:|---:|---:|---:|
| SMALL_CREATE_40_US | 204335.50 | 186431.00 | -8.762% | 16943.75 | 17202.50 | +1.527% |
| LOOKUP_400_US | 114223.00 | 102958.75 | -9.862% | 66991.50 | 78207.75 | +16.743% |
| SMALL_READ_40_US | 48192.00 | 45311.75 | -5.977% | 14272.25 | 14128.25 | -1.009% |
| SEQ_WRITE_8M_US | 329953.25 | 346865.75 | +5.126% | 35712.00 | 39656.50 | +11.045% |
| SEQ_READ_8M_US | 522173.25 | 564281.75 | +8.064% | 270654.00 | 269438.50 | -0.449% |
| SMALL_DELETE_40_US | 188836.75 | 164252.00 | -13.019% | 8223.75 | 7968.50 | -3.104% |

## Matched candidate-minus-baseline deltas

Columns are in the required order: `bfs-first`, `pfs3-first`,
`repeat-bfs-first`, `repeat-pfs3-first`.

| BFS workload | bfs-first | pfs3-first | repeat-bfs-first | repeat-pfs3-first |
|---|---:|---:|---:|---:|
| SMALL_CREATE_40_US | -14.559% | -8.647% | -3.183% | -6.724% |
| LOOKUP_400_US | -12.917% | -19.016% | -1.251% | -4.339% |
| SMALL_READ_40_US | -5.239% | -19.472% | +9.141% | -5.149% |
| SEQ_WRITE_8M_US | +5.284% | +15.776% | -0.359% | -0.486% |
| SEQ_READ_8M_US | +13.548% | +13.642% | -2.695% | +8.062% |
| SMALL_DELETE_40_US | -4.304% | -13.371% | +0.589% | -28.802% |

| PFS3 workload | bfs-first | pfs3-first | repeat-bfs-first | repeat-pfs3-first |
|---|---:|---:|---:|---:|
| SMALL_CREATE_40_US | -3.799% | +7.118% | +5.456% | -2.091% |
| LOOKUP_400_US | -0.931% | +63.274% | +1.974% | +3.563% |
| SMALL_READ_40_US | +2.315% | +7.895% | -3.721% | -10.300% |
| SEQ_WRITE_8M_US | -0.037% | +52.378% | +1.242% | -10.112% |
| SEQ_READ_8M_US | -8.614% | +4.378% | +1.646% | +0.966% |
| SMALL_DELETE_40_US | -3.852% | +1.563% | +1.640% | -11.008% |

Positive delta means slower candidate time. Across the 24 matched pairs per
filesystem, BFS has 7 adverse pairs, 6 over +5%; PFS3 has 14 adverse pairs, 5
over +5%. Overall: 21/48 adverse, 11/48 over +5%.

## Same-run candidate BFS/PFS3 ratios

Columns again follow `bfs-first`, `pfs3-first`, `repeat-bfs-first`,
`repeat-pfs3-first`. Ratios above 5 fail the stated per-workload goal.

| Workload | bfs-first | pfs3-first | repeat-bfs-first | repeat-pfs3-first |
|---|---:|---:|---:|---:|
| SMALL_CREATE_40_US | 13.032 | 9.716 | 11.152 | 9.726 |
| LOOKUP_400_US | 1.495 | 0.948 | 1.605 | 1.436 |
| SMALL_READ_40_US | 3.027 | 2.841 | 3.691 | 3.349 |
| SEQ_WRITE_8M_US | 9.948 | 6.991 | 9.040 | 10.105 |
| SEQ_READ_8M_US | 2.217 | 2.084 | 2.032 | 2.049 |
| SMALL_DELETE_40_US | 20.256 | 20.344 | 20.829 | 21.047 |

There are 12 individual five-times failures out of 24 candidate
run/workload pairs: all four runs each for create, sequential write, and
delete. The other three workloads have no ratio over five.

## Raw TSV SHA-256 identities

Every path is under
`build/benchmark/btree-scratch-lifetime-<label>/system/Results/`.

| Label | `bfs.tsv` | `pfs3.tsv` |
|---|---|---|
| normal-baseline-bfs-first | `c4535d9cdb921f7ec739e468e08c0cf0a934ef5b230f96a615e0500f4ff7a8cf` | `81909d44944f3ef0a9ce6a3099c792f59a7e05a328fd61120bd238d26dd0e0b0` |
| normal-candidate-pfs3-first | `3dc78c1db00c024b7e672ed797920a19bf7d6bcaf8a9786a2e91982dbb5b63c9` | `3602b5d6c39a3d94e5a6d60ace5146afabd37054b7b8f58e9982dba0bcc859a6` |
| normal-baseline-pfs3-first | `a297f826bdad1a1d4a39b8a9fbcf6a6ee0183fd69422c5342ad6ab44d86c133d` | `74fb45fce2c336109615ed88f7cc7f55de5d5e421cc917d17af0f44300bf848b` |
| normal-candidate-bfs-first | `f841de8d24cbdece6be57fa292f41e3ab34941d36ba30792c5aeff6f9881577d` | `b5b1b7c69fb8a2a3fba80ea416b5a4ce6a814c6bc9fe3974fb2a2f35a91f56b4` |
| normal-repeat-candidate-bfs-first | `19f07c8ba230ee8fc0778155255050751ac5e3d092b909bede3046cf8acfb5fc` | `52b0a6c4b700dcec0ba12dd45d7709496795f6ff2293248239d4451eee2e16eb` |
| normal-repeat-baseline-pfs3-first | `1466652fc4d40824614f5c405a69f2a0f58532152be080e49e7f02259fc31f1d` | `c268adaf37995d7a36f8caa1e618b2b2a640344d4ab588dfb2c082ef2a760b53` |
| normal-repeat-candidate-pfs3-first | `6da6476a4cf8671169a34b785f302e0a3ed844936559bcdcb22d22d9ddf1dc07` | `013a05b38874effa2984256ef8075f82acef3fa7d25f26f4526225be2f374e60` |
| normal-repeat-baseline-bfs-first | `7ce2683f123561fb76501c8cfa30f9f41913e6d1f9dda081a30548b042bbee1a` | `f425c25f083f00b514861d8e8a755f560d08601cd279d49d9a760c07c22c4799` |

## Scope

This is an independent parse of the completed normal TSVs and their
completion/inventory records. It does not combine or interpret the separately
instrumented probe runs, and does not attribute timing deltas causally to the
scratch-lifetime change. The timing evidence includes 12/24 candidate
five-times failures, so the stated goal is not met by these normal runs.
