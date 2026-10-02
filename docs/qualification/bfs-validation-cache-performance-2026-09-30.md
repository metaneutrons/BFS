# Resident node validation cache — 2026-09-30

## Result and acceptance target

The target remains ≤5× PFS3 in every checked AmigaDOS workload without
weakening durability, integrity, snapshots or recovery. It is **not achieved**.
Compared with `e2002d9`, four normal candidate runs and four baseline runs
on fresh images show mean reductions of 21.4% in lookups, 17.2% in small reads
and 18.1% in sequential reads. The write mean changes by only −1.5%; no robust
write gain is established. Create/delete means change by −4.9%/−11.0%, but
variance and four samples per version do not establish precise gains.

Times below are microseconds. A and B are sequential interleaved batches;
both handler versions run in both filesystem orders in each batch.

| Normal run | Create 40 | Lookup 400 | Read 40 | Write 8 MiB | Read 8 MiB | Delete 40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A: baseline-bfs-first | 455293 | 180734 | 78080 | 602877 | 772539 | 496892 |
| A: candidate-pfs3-first | 433981 | 122930 | 56511 | 774587 | 613563 | 381503 |
| A: baseline-pfs3-first | 489149 | 136192 | 74239 | 595708 | 699462 | 489329 |
| A: candidate-bfs-first | 478456 | 124031 | 62002 | 558782 | 680061 | 417853 |
| B: candidate-bfs-first | 459774 | 127743 | 55424 | 567486 | 606588 | 407485 |
| B: baseline-pfs3-first | 526526 | 167295 | 61696 | 700541 | 823098 | 417712 |
| B: candidate-pfs3-first | 445629 | 120575 | 57023 | 570237 | 563454 | 394430 |
| B: baseline-bfs-first | 440764 | 145664 | 64768 | 609405 | 713469 | 396157 |
| baseline bfs mean | 477933 | 157471.25 | 69695.75 | 627132.75 | 752142 | 450022.5 |
| candidate bfs mean | 454460 | 123819.75 | 57740 | 617773 | 615916.5 | 400317.75 |
| candidate pfs3 mean | 17088.5 | 75007.25 | 14243.5 | 36716.5 | 264654.5 | 7926.25 |
| Candidate / PFS3 mean | 26.59× | 1.65× | 4.05× | 16.83× | 2.33× | 50.51× |

All three mutating phases fail 5× in **every** candidate run. Their per-run
ranges are 25.05–28.00× for create, 14.10–22.71× for write and 47.78–53.08×
for delete. All three read/lookup phases pass 5× in each candidate run.
The 774,587 µs candidate write is retained, not removed as an outlier.
PFS3 deletes here take 7,770–8,255 µs; their lower reference time makes ratios
larger than the preceding report's mean with a 22,656 µs PFS3 deletion sample.
Do not interpret that cross-report ratio change as a BFS regression.

Cachy is the same KVM guest and FS-UAE configuration as the
[previous report](bfs-noop-update-performance-2026-09-30.md). Host scheduling
and other VM workloads are not controlled. No guest compiler or competing
emulator ran during measurements. These results are software/emulator
evidence, not hardware parity or a confidence interval.

## Mechanism and proof boundary

A cache entry may remember a completed node-local CRC/header/key-order/child
pointer validation for its exact resident bytes. The token copies comparator
function identity, key size, value size, block size and block count; matching
does not depend on an ops object's address or struct padding. The generic
B-tree opts in through `cache_key_order`; its default false preserves full
structural checks for arbitrary/stateful comparators. The seven production
comparators are fixed, side-effect-free functions of stored bytes.

The first validation remains complete. Every write, including a successful
trusted node write, clears structural validation. Trusted writes may retain
only their already computed CRC: transitional count-zero nodes must not
become structurally trusted. Failed writes, read-miss/node-write eviction,
invalidation and cache reinitialization clear the state. Cached validation
never replaces per-traversal expected-level or parent-bound checks. Value-
specific inode/extent/refcount checks and data CRCs remain outside this cache.

The existing cache is not thread-safe: reads also mutate LRU and buffers.
Whole read/validate/mark operations must be serialized and nonreentrant;
individual BIO-call locks or shared reader locks are insufficient. This is
now explicit in the headers. Production cache instances are confined to the
single-task Amiga handler; Linux production uses raw BIO and takes the normal
full-validation fallback. Media changes invalidate, and format/remount
reinitialize. Buffer resizing requires destroy/reinit. There is no format,
write-through, allocation, flush, snapshot or publication change.

## Diagnostic counts

The strict v8 deep-comparison verifier passed. Create/write/delete emit
875/1,129/602 node writes, including 633/839/486 Free-Tree writes.
The large write still writes 2,048 data blocks and 128 extent nodes; its
Free-Tree attribution is 546 allocation-body, one refill, two reserve-return,
four post-publication and 286 other writes. It has 160 inode writes. There
are still 120/3/120 superblock publications for create/write/delete.
Small count differences from the prior report depend on timestamp ticks and
allocation history; structural caching does not itself elide metadata writes.
Diagnostic elapsed times are not normal-handler performance evidence.

## Verification and provenance

A full `make check`, a separate complete normal host suite and the complete
ASan/UBSan suite passed. Fourteen focused tests pass normally and under
ASan/UBSan. They cover scan reduction with a comparator counter, stable
versus stateful semantics, copied token fields and mutable ops objects,
CRC-valid bad ordering, successful ordinary/trusted and failed writes,
read and node-write eviction, media invalidation followed by tree reread,
count-zero trusted writes, and warmed-child level/parent-bound errors.
Primary review corrected a false-positive unsigned-underflow assertion in the
initial test; the retained count assertion checks the incremental second-read
cost. Independent Luna review checked production code and tests separately
from their authors. Both normal and probe m68k builds passed.

All eight normal runs and the diagnostic passed mount, completion-marker and
data-verification checks on fresh equal 256 MiB images with a fixed formatter.
The normal/probe hashes are identical after a final rebuild.

SHA-256 identities:

- Baseline normal: `5d944b17e206e22badc72685d7bdf3b77fcecc6f3b32ee112326df06d4c1d120`.
- Candidate normal: `ea683c9362a0bcc159124bbd40517563ddaa9b061cb2e35843562541abc1c207`.
- Candidate probe: `463ffbfe0d1a82395cdf0d18ec5e534598aaeef30e38fe50c3598e528c3d615b`.
- Fixed formatter: `0f87a0638791a4a2371083339ed014d4d6659dc85a2270bd993f9df1ddf13cc6`.
- Compare tool: `9d27f056499ebf049449a663bf7a42a6d55a1060147f80640c2ab46ce45072da`.
- PFS3: `bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7`.

[Raw evidence](evidence/bfs-validation-cache-2026-09-30/) contains the eight
normal runs, diagnostic, passing full-suite and focused-test logs, and SHA256SUMS.
Only text evidence is committed; copied log trailing whitespace is normalized.
No ROM, HDF or licensed binary is committed. No remote CI or physical
power-cut/controller/media qualification ran. `coffin.service` remains stopped
for further measurement; it was not disabled or deleted.
