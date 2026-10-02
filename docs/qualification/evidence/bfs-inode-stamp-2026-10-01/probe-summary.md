# Inode stamp probe attribution

Two schema-10 candidate probes use the unchanged ABI-11 guest. Baseline
probes are the retained post-buffer checkpoint, not new paired normal timings.
Counts below are exact observed baseline → candidate values, in filesystem order.
All four candidate probe filesystem outputs passed strict data verification.

## bfs-first

| Metric | Create 40 | Write 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: |
| NODE_WRITES | 690 → 548 | 421 → 401 | 353 → 313 |
| INODE_TREE_NODE_WRITES | 128 → 80 | 150 → 129 | 40 → 40 |
| FREE_TREE_NODE_WRITES | 447 → 353 | 141 → 142 | 237 → 197 |
| FREE_TREE_ALLOCATION_BODY_NODE_WRITES | 281 → 233 | 136 → 137 | 116 → 116 |
| FREE_TREE_RESERVE_REFILL_NODE_WRITES | 40 → 40 | 1 → 1 | 80 → 40 |
| FREE_TREE_OTHER_NODE_WRITES | 126 → 80 | 4 → 4 | 41 → 41 |
| BIO_WRITES | 770 → 628 | 2470 → 2450 | 393 → 353 |
| BIO_UPDATES | 120 → 120 | 3 → 3 | 120 → 120 |
| TXN_COMMITS | 40 → 40 | 1 → 1 | 40 → 40 |
| SEALED_COMMITS | 40 → 40 | 1 → 1 | 40 → 40 |
| SEALED_METADATA_FENCES | 40 → 40 | 1 → 1 | 40 → 40 |
| SUPERBLOCK_PUBLICATIONS | 40 → 40 | 1 → 1 | 40 → 40 |
| BTREE_MALLOC_CALLS | 128 → 80 | 680 → 639 | 161 → 161 |
| BTREE_FREE_CALLS | 128 → 80 | 680 → 639 | 201 → 201 |

## pfs3-first

| Metric | Create 40 | Write 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: |
| NODE_WRITES | 691 → 548 | 422 → 401 | 314 → 313 |
| INODE_TREE_NODE_WRITES | 128 → 80 | 151 → 129 | 40 → 40 |
| FREE_TREE_NODE_WRITES | 448 → 353 | 141 → 142 | 198 → 197 |
| FREE_TREE_ALLOCATION_BODY_NODE_WRITES | 281 → 233 | 136 → 137 | 116 → 116 |
| FREE_TREE_RESERVE_REFILL_NODE_WRITES | 40 → 40 | 1 → 1 | 41 → 40 |
| FREE_TREE_OTHER_NODE_WRITES | 127 → 80 | 4 → 4 | 41 → 41 |
| BIO_WRITES | 771 → 628 | 2471 → 2450 | 354 → 353 |
| BIO_UPDATES | 120 → 120 | 3 → 3 | 120 → 120 |
| TXN_COMMITS | 40 → 40 | 1 → 1 | 40 → 40 |
| SEALED_COMMITS | 40 → 40 | 1 → 1 | 40 → 40 |
| SEALED_METADATA_FENCES | 40 → 40 | 1 → 1 | 40 → 40 |
| SUPERBLOCK_PUBLICATIONS | 40 → 40 | 1 → 1 | 40 → 40 |
| BTREE_MALLOC_CALLS | 128 → 80 | 682 → 639 | 161 → 161 |
| BTREE_FREE_CALLS | 128 → 80 | 682 → 639 | 201 → 201 |

Directory/refcount/other node counts remain create 75/0/40, write 2/0/128,
delete 76/0/0. Data writes remain 40/2048/0; extent maps remain 40/128/0.
Reserve-return and post-publication Free-Tree node writes remain zero. Delete
inode writes are unchanged; its aggregate reduction is entirely reserve refill
and differs by order. Inode writes removed on create are 48 in both probes;
write removes 21/22, not 128, because existing identical-update elision had
already skipped many legacy same-tick mutations.

Independent Luna extraction and primary count calculations agree. These are
instrumented observations, not elapsed-time acceptance or real-device barrier
proof. Gross allocator/device/heap scopes overlap and cannot be summed or
subtracted into a residual CPU estimate. CRC-stride sampling cannot establish
an unbiased exact total. No such extrapolation is used here.

Baseline source TSVs are retained in
`../bfs-after-buffer-profile-2026-10-01/after-buffer-probe-{bfs-first,pfs3-first}/bfs.deep-compare.tsv`.
Candidate TSVs are nested under each probe's `system/Results` in this evidence.
