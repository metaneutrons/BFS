# FreeTree non-split counter extraction

Run `node build/free-tree-non-split-counter-extract.mjs [repo-root] [output-path]`; the root defaults to the working directory, and JSON goes to stdout unless an output path is supplied. The script reads only the four raw schema-12 TSVs below, checks their pinned SHA-256 values and `PASS=1`, and extracts every per-phase counter and tick field. It does not read a summary JSON.

| Order | BFS SHA-256 | PFS3 SHA-256 |
| --- | --- | --- |
| BFS first | `f237d5c68deae7537870227d3c9ee1d12330bd14fb5e9a9b07c804440fc2739f` | `64027726481b46aa84a06228f4705645634622d5486fd1170b6441803b4b9ec2` |
| PFS3 first | `90749bc2003d55fcdee538c6f5977ad09ce2c4d32e190dc47999e73cefbf439b` | `ceec6ce16106508320b74e51c83bf6ea7f56c96ec18d125de520119df06a002e` |

Every non-time field agrees across orders: BFS has 1,326 fields, comprising 1,320 phase counters and six metadata fields; PFS3 has three metadata fields and no probe counters. PFS3 has phase elapsed times only. The raw inputs were produced by the frozen ABI-13/schema-12 producer at `build/split-coalesce-attribution-source/`: `build/split-coalesce-attribution-source/src/amiga/perf_probe.h:12-13` defines both versions and `build/split-coalesce-attribution-source/tools/fs-compare-bench.c:520-521` emits the schema macro. Its probe declares the split/coalescence matrices at `build/split-coalesce-attribution-source/src/amiga/perf_probe.h:39-45,153-156`; `build/split-coalesce-attribution-source/src/amiga/perf_paths.h:8-33` records tree role, root/deep shape, and events; `build/split-coalesce-attribution-source/tools/fs-compare-bench.c:213-230,338` emits those rows. The three frozen source files were verified byte-identical to their lossless copies under `docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01/diagnostic-source/` by decoded SHA-256. The version difference from the current source tree is deliberate.

## Phase reconciliation

The node-write attempt counters and BIO write calls are separate fields. Rows aggregate the phase workload; a node-write count is not a file-row count.

| Phase | Workload API calls | FreeTree node writes: total = allocation body + refill + return + pending reclaim + other | `NODE_WRITES` / `BIO_WRITES` | Freespace allocs / iface allocs | TXN / sealed / publications |
| --- | --- | --- | --- | --- | --- |
| Create 40 | create 40; file-write 40; sync 40 | `353 = 233 + 40 + 0 + 0 + 80` | 548 / 628 | 233 / 193 | 40 / 40 / 40 |
| Lookup 400 | packet 800 | `0 = 0 + 0 + 0 + 0 + 0` | 0 / 0 | 0 / 0 | 0 / 0 / 0 |
| Read 40 | packet 160; data-read 40 | `0 = 0 + 0 + 0 + 0 + 0` | 0 / 0 | 0 / 0 | 0 / 0 / 0 |
| Sequential write 8 MiB | create 1; file-write 128; sync 1 | `142 = 137 + 1 + 0 + 0 + 4` | 401 / 2,450 | 137 / 259 | 1 / 1 / 1 |
| Sequential read 8 MiB | packet 131; data-read 2,048 | `0 = 0 + 0 + 0 + 0 + 0` | 0 / 0 | 0 / 0 | 0 / 0 / 0 |
| Delete 40 | delete 40; sync 40 | `197 = 116 + 40 + 0 + 0 + 41` | 313 / 353 | 116 / 116 | 40 / 40 / 40 |

For every phase, the five FreeTree buckets sum to `FREE_TREE_NODE_WRITES`, and the per-tree node-write counters sum to `NODE_WRITES`. Across the mutation phases: `692 = 486 allocation-body + 81 refill + 0 return + 0 pending-reclaim + 125 other`. The other three phases have zero FreeTree writes. `DATA_WRITES` is separately 40 on create and 2,048 on sequential write.

Across mutation phases, `TXN_COMMITS`, `SEALED_COMMITS`, `SEALED_METADATA_FENCES`, and `SUPERBLOCK_PUBLICATIONS` are each 81. Reserve-return calls/runs/blocks/batches/skips and node writes are zero; post-publish reclaim passes and maximum passes per commit are zero. The direct B-tree heap counters total 334 malloc and 374 free calls. The JSON retains all per-phase values for these counters, interface allocations/frees, CRC, and all CPU scopes.

## Attribution limits

`IFACE_ALLOC_CALLS` counts top-level requests outside recursive `in_alloc`; those requests can call `FREESPACE_ALLOCS`, so the counters overlap. The source has metadata-spare take/stash and `alloc_partial_root_leaf` paths, but the raw schema has no spare hit/miss/fallback or root-leaf partial-withdrawal count. It also does not subdivide the 125 `FREE_TREE_OTHER_NODE_WRITES` by mutation path.

`ALLOCATION_BODY` is not a full request-cost scope. The one-block tail path enters it around `alloc_tail_from_highest`; on the first-fit path, the scan and optional wrap-around selection at `src/core/alloc.c:565-600` precede phase entry at `src/core/alloc.c:614`. Its bucket does not attribute first-fit scan/read cost. A future cost scope should enclose the allocator request before selection and preserve reserve-refill attribution when nested; a scan/selection count would separate that work from mutation writes.

The only nonzero split/coalescence raw fields are directory-tree leaf events during create: one root attempt/initial write, three deep attempts/initial writes, and two deep right-select/read/write events. FreeTree split and coalescence fields are all zero.

CPU scopes use sample stride 1 and record one sample per call, but their times are inclusive and may nest. CRC uses stride 64. Neither scope ticks nor other sampled ticks are added to each other or to phase elapsed microseconds. The stored probe timings support no ≤5× acceptance or latency claim.
