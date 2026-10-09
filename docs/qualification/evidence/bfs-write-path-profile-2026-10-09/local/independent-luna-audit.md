# Independent Luna audit

The read-only audit reproduced `summary.json` byte-for-byte from
`summarize_profile.py` and passed the strict verifier for all four runs/eight
schema2 BFS/PFS3 tables. Exact schedule, runtime pre/post identities, all 67
asset mappings, one RDB partition with 30 buffers, emulator configuration and
startup mode/order/Stack were checked. No files were changed or builds run by
the evidence auditor.

Durable 4KiB Work has 515/257/256/256 inode-read/inode-write/extent-map/goal
calls, zero device reads, 256 device/data writes, zero node writes/commits/CRCs.
Its two flushes have zero reads, five writes, three updates, four node writes,
one commit and four write CRCs (182/140 ticks). Sidecar samples equal calls;
all flush sidecars are zero. At 709,379Hz the Work sidecar intervals are
18.306/16.977ms, 11.612/9.862ms, 3.855/3.120ms and 15.605/14.641ms.

Normal BFS-first 4KiB retains two device reads, 261 writes, three updates,
four node writes/write CRCs and one commit inside Work. Normal PFS3-first
retains 205.660ms Work with no such metadata activity. Neither was filtered.
All four 1KiB Work intervals have 64 data/device reads, 256 data/device writes,
64 maps/goals, zero node writes/commits/CRCs. Their durable flushes each have
two reads, five writes, three updates, four node writes, one commit and four
write CRCs (317/182 ticks). No read CRC occurs in these growth intervals.

The 514 normal PFS3-first buffer calls measure buffer requests, not malloc.
`BTREE_MALLOC_CALLS=0` instruments direct malloc in `btree.c` only, excluding
`bfs_bio_alloc_buffer`'s callback/helper path. The handler's cache callback
can reuse four scratch slots and falls back to malloc for other sizes or
occupied slots. The prior 258 inode-search plus 256 allocation-buffer split
is a code-audit inference, not a measured per-category or heap count.

A separate Luna integration review checked optional wrappers, packet/sidecar
guards, guest/reset/flush handling, parser inventories and negative tests.
It identified stale missing-artifact build advice; the root corrected only
that local error-path wording after measurements and retained its diff.
No performance or production-qualification conclusion follows from this
single-candidate four-start diagnostic cohort.
