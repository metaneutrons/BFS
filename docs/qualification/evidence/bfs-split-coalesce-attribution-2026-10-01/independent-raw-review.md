# Independent raw extraction: split/coalesce paths

Method: read only the two `bfs.deep-compare.tsv` and two
`pfs3.deep-compare.tsv` files, each run's `complete.txt`,
`info-after-format.txt`, and `bench.fs-uae`. I did not read the consumer,
root-generated JSON, primary summary, or run logs. No timing values are used in
this extraction.

## Input identity and structural checks

| Run order | File | SHA-256 |
| --- | --- | --- |
| BFS first | `bfs.deep-compare.tsv` | `f237d5c68deae7537870227d3c9ee1d12330bd14fb5e9a9b07c804440fc2739f` |
| BFS first | `pfs3.deep-compare.tsv` | `64027726481b46aa84a06228f4705645634622d5486fd1170b6441803b4b9ec2` |
| PFS3 first | `bfs.deep-compare.tsv` | `90749bc2003d55fcdee538c6f5977ad09ce2c4d32e190dc47999e73cefbf439b` |
| PFS3 first | `pfs3.deep-compare.tsv` | `ceec6ce16106508320b74e51c83bf6ea7f56c96ec18d125de520119df06a002e` |

Both BFS TSVs declare schema 12 / `DH1:`, end in `PASS\t1`, contain 1,488
two-column rows with unique names and numeric values, and have exactly 140
matrix rows in each of six phases (840 total). Both PFS3 TSVs have the schema-12
header, `DH2:`, exactly six elapsed rows, and terminal `PASS\t1`. The
`complete.txt` marker is `BFS-PFS3-DEEP-COMPARE-COMPLETE`; its SHA-256 is
`81c4760af5af82566811dbd3329c626358563def824ca64e89d58a929f356091` in both
orders. `info-after-format.txt` has SHA-256
`3356e7ef51185adef95574fdd8d1328be1d5f7d8d44602d6d67ac7ca538475cb` in both
orders; each inventory reports `BFSTest` and `PFSTest` mounted.

The two `bench.fs-uae` configurations use an A1200 model and 68040 CPU under
FS-UAE. These are emulator runs, not physical-hardware evidence. Config hashes
are `d017996af3777fda1f9362a85eea00ce31eac8ccc1de8f63ad61f11d4a783048`
(BFS-first) and `ceb6223e533ac70b57759d47cd8295d1cd5089e014c58f4e17f0659c1c7d970b`
(PFS3-first), because each points at its own run image. Path matrices are byte
for byte identical across orders; the four TSV hashes above identify the exact
raw inputs.

## Path counts

Only `SMALL_CREATE_40` has nonzero path cells, in both orders:

| Kind / role / shape | Attempt | Initial write call | Right select | Right read call | Right rewrite call |
| --- | ---: | ---: | ---: | ---: | ---: |
| Leaf / DIR_TREE / ROOT | 1 | 1 | 0 | 0 | 0 |
| Leaf / DIR_TREE / DEEP | 3 | 3 | 2 | 2 | 2 |

All other split cells are zero, including every internal split cell. Every
coalesce cell is zero. Per-phase totals:

| Phase | Split attempts | Initial-write calls | Right select/read/rewrite | Coalesce attempts/completions |
| --- | ---: | ---: | ---: | ---: |
| `SMALL_CREATE_40` | 4 | 4 | 2 / 2 / 2 | 0 / 0 |
| `LOOKUP_400` | 0 | 0 | 0 / 0 / 0 | 0 / 0 |
| `SMALL_READ_40` | 0 | 0 | 0 / 0 / 0 | 0 / 0 |
| `SEQ_WRITE_8M` | 0 | 0 | 0 / 0 / 0 | 0 / 0 |
| `SEQ_READ_8M` | 0 | 0 | 0 / 0 / 0 | 0 / 0 |
| `SMALL_DELETE_40` | 0 | 0 | 0 / 0 / 0 | 0 / 0 |

The six initial-write and right-rewrite events are counted at their respective
`node_write` call sites; they do not establish successful physical I/O. The
zero coalesce matrix means these particular workload phases had no recorded
two-edit free-space merge; it does not establish that the code path is
unreachable. No speedup, timing, or causal claim follows from these counters.
