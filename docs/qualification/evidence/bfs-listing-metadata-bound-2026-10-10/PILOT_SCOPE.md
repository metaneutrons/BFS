# Listing metadata bound scope

Fixed before execution, 10 October 2026.

Fabian asked for a draft and a prototype before deciding whether directory
entries should carry the listing fields (size, protection, date) while format
v3 is open (`docs/plans/bfs-listing-metadata-v1.md`). This run measures the
upper bound of that change on the 68k: a handler that lists without reading
inodes at all.

## Handlers

Both are uninstrumented production handlers (`make amiga`, `-O2 -m68020`),
built on the Mac from the same tree: commit `5136708` plus the big-endian load
change retained in the production baseline pilot of the same day.

- `reference`: that tree, SHA-256 `ea37a89d…` (the retained candidate of the
  production baseline pilot).
- `bound`: the same tree with `noinode-handler.patch`: the batched ExAll takes
  a zeroed inode for every entry instead of reading it, and ExNext fills its
  FileInfoBlock from a zeroed inode. Size, protection and date are therefore
  wrong; names, types and counts, which the guest checks, are not affected.
  SHA-256 `cc02b398…`. It is a measuring device and never a release artifact.

The bound omits the costs the real change would add: wider directory records
(59 instead of 78 per 4 KiB leaf), copying the fields from the record, and the
extra record update on attribute changes. It therefore overstates the gain.

## Emulator run

Exactly 16 fresh Cachy starts in `compare` mode: eight rounds, each with both
handlers. Rounds 1, 3, 5 and 7 run BFS first, rounds 2, 4, 6 and 8 PFS3 first.
Odd rounds start with `reference`, even rounds with `bound`. Guest
`fs-compare-production` (`eb641e32…`), PFS3 (`bc72fac…`), ROM (`68f9d3a…`),
formatter (`a34639e…`) and RDB Buffers = 30 are those of the production
baseline pilot. No retries, exclusions or extensions; no pooling with earlier
runs.

## Reported

Nothing is retained or reverted; the decision on the format is Fabian's.
Reported for every phase: the BFS / PFS3 ratio of each start and its median
per handler, and the median of the eight paired ratios `bound` over
`reference` in the same round. All 16 starts must pass
`emulator-test/verify-bench-results.sh` for `compare`; otherwise the run is
reported as failed and not interpreted.

The launch script is byte-identical to the one of the earlier pilots. No CI,
commit, push, merge, installation or issue change is part of this step.
