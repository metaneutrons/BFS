# Validated identical-update elision — 2026-09-30

## Result and acceptance target

The target remains ≤5× PFS3 in every checked AmigaDOS phase with unchanged
durability, integrity, snapshots and recovery. It is **not achieved**. Against
`8c19502` on the same Cachy KVM guest, the normal candidate's mean 8 MiB write
time drops by 20.6%. Creation drops by 7.3%; deletion by 12.4%, although the
two-sample evidence and variable VM/PFS3 timings do not establish precise gains.

Fresh images, normal handlers, sequential interleaved runs in both orders:

| Handler / order | Create 40 | Lookup 400 | Read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline, BFS first | 503,101 | 165,951 | 68,479 | 816,252 | 684,476 | 454,014 |
| Candidate, PFS3 first | 450,871 | 133,247 | 61,888 | 581,820 | 731,836 | 389,758 |
| Baseline, PFS3 first | 500,032 | 133,696 | 63,103 | 691,196 | 722,621 | 475,582 |
| Candidate, BFS first | 478,973 | 137,150 | 65,279 | 615,216 | 748,731 | 424,702 |
| Baseline mean | 501,566.5 | 149,823.5 | 65,791 | 753,724 | 703,548.5 | 464,798 |
| Candidate mean | 464,922 | 135,198.5 | 63,583.5 | 598,518 | 740,283.5 | 407,230 |
| PFS3 mean in candidate runs | 16,481 | 66,432.5 | 14,656 | 37,088 | 279,101.5 | 16,384 |
| Candidate / PFS3 mean | 28.2× | 2.04× | 4.34× | 16.1× | 2.65× | 24.9× |

Times are microseconds. PFS3 deletion is 10,112 versus 22,656 µs in the two
candidate runs; the candidate's per-run delete ratios are **38.5× and 18.7×**.
The mean ratio must not hide this variability. Creation is 28.9× and 27.6×;
writing 16.2× and 16.1×. All three mutating phases fail acceptance in each run.
Sequential-read mean is 5.2% above baseline; do not claim a read optimization.
Two runs per handler do not provide a confidence interval, and absolute times
must not be compared with Mac-hosted runs or treated as hardware qualification.

## Mechanism and preserved guarantees

`bfs_btree_update` retains its argument, shape and pending-headroom preflight.
It descends through CRC/structure/level-validated nodes and requires the key
to exist. Only then does it compare all existing value bytes with `new_val`.
Equality frees the private buffers and returns success without COW, allocation,
deallocation or deferred retirement. Existing blocks keep their real transaction
tags and bytes. A different value follows the unchanged COW path.

This is shared core logic, not an Amiga-only metadata shortcut. It avoids
re-emitting identical timestamp/archive values within one DateStamp tick and
other genuinely unchanged values. It does not suppress an Amiga dirty flag,
notification, inode-field change, filesystem sync or transaction publication.
Reads, corruption errors, missing keys and original headroom errors still occur
before the equality decision. Snapshot roots and data-checksum behavior are
unchanged. No in-place metadata write, dirty cache or format change.

## Diagnostic operation counts

The final candidate diagnostic passed the strict v8 verifier. Compared with
the preceding CRC candidate's diagnostic, it emitted:

| Phase | Baseline / candidate all node writes | Baseline / candidate Free-Tree writes | Baseline / candidate inode writes |
| --- | ---: | ---: | ---: |
| Create 40 | 967 / 874 | 692 / 633 | 160 / 126 |
| Write 8 MiB | 1,423 / 1,148 | 1,035 / 852 | 258 / 166 |
| Delete 40 | 632 / 605 | 516 / 489 | 40 / 40 |

The write phase removes 92 inode emissions and their allocator/reclamation
work. It still writes 2,048 data blocks and 128 extent nodes. Its 852 Free-Tree
writes comprise 552 allocation-body, one refill, two reserve-return, five
post-publication and 292 other writes. There are still three superblock
publications for the large write and 120 each for create/delete. Lookup and
both read phases emit zero metadata writes. Timestamp-dependent counts vary
with elapsed time; they are not a fixed predicted saving on all hosts. The
different allocation history also changes later Free-Tree reclamation work.
Diagnostic elapsed times are not normal-handler performance evidence.

## Verification and provenance

`make check`, a separate complete normal host suite, and the complete ASan/UBSan
suite passed. All eight focused tests passed; the unmodified implementation
first failed exactly the three intended no-op cases. Tests cover leaf/internal
roots, unchanged bytes/tags, no allocation/deallocation/deferred frees/writes,
changed-value COW, a preserved write fault consumed by the next changed update,
read failure, CRC-valid bad ordering, missing keys and unchanged zero-headroom
preflight. Existing crash cuts, snapshots, corruption, low-space, pending storms,
remount, checker and handler tests passed. Independent Luna review checked the
actual source and tests separately from their authors.

The benchmark builder now accepts `BFS_BENCH_FORMATTER_FILE`; its default is
unchanged. This pins one formatter during concurrent local builds. All four
normal runs and the diagnostic used the same copied formatter and fresh equal
256 MiB images. The machine, ROM, PFS3 and comparison tool match the preceding
[CRC report](bfs-crc-zero-performance-2026-09-30.md). Cachy is a KVM guest with
12 vCPUs and a guest-reported i5-1235U model; its host scheduler and other VM
workloads were not controlled. No competing compiler/emulator ran in the guest.
`coffin.service` remains stopped; it was not disabled or deleted.

SHA-256 identities:

- Baseline normal: `220735ac6a66daef704f43cf1d024262af8ab5f4077001d91c7b4f306372c777`.
- Candidate normal: `5d944b17e206e22badc72685d7bdf3b77fcecc6f3b32ee112326df06d4c1d120`.
- Candidate probe: `80163b39f76c168b2bafaa3ef4c81baba6d18bff50250da57674cb15121546b7`.
- Fixed formatter: `0f87a0638791a4a2371083339ed014d4d6659dc85a2270bd993f9df1ddf13cc6`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

[Raw evidence](evidence/bfs-noop-update-2026-09-30/) contains accepted normal
runs, the diagnostic, full passing host/sanitizer logs and focused tests.
SHA256SUMS covers every raw file; log copies have trailing whitespace removed.
No licensed binary, ROM or HDF is committed. No remote CI, power-cut, physical
media or controller-lie qualification ran.
