# Single-block Free-Tree absence proof v1

Decision state: bounded local pilot within the authorized performance goal.
External issue tracking is not created by this increment. The qualification
report will own its observed outcome; this plan defines acceptance requirements.

## Outcome and boundaries

Keep the overall requirement: at most five times PFS3 in every checked AmigaDOS
workload, with unchanged durability, integrity, snapshots and recovery. The
[CPU-scope evidence](../qualification/bfs-handler-cpu-scopes-2026-10-01.md)
records about 61 ms of inclusive top-level interface-free work across 255 large
write callbacks. It does not attribute that time exclusively to validation.

Mounted metadata spare stash/pop validates stock and absence from the Free-Tree.
The absence helper first searches for the greatest key at or below the requested
start, validates that selected extent and rejects a containing predecessor.
It then independently scans from the start to find an overlapping starting key.
No intervening mutation occurs. For a one-block integer interval in a validated
height-one leaf, the second search cannot establish a distinct overlap.

This pilot changes only that duplicate search. It does not cache an absence
proof across calls, change stock membership/ownership, allocate a new ledger,
infer ownership from LIVE tags, overwrite any live node, alter placement or
refill policy, batch commits or weaken fences. The 48-byte memcpy pilot must
be separately resolved before normal workload comparison of this increment.

## Design and compatibility

After the existing successful or NOTFOUND floor result and selected-extent
checks, return BFS_OK only for count=1, height=1 and a nonempty root. A strictly
ordered integer-key leaf has no other key in [start,start+1): an exact key was
already selected and rejected; a strictly greater key starts at or after the
exclusive end. A NOTFOUND floor result means every key is greater than start.

The floor traversal already checks CRC, node shape, expected leaf level and
strict key order under the existing serialized BIO/cache contract. Restrict to
height one because the later scan in deeper trees additionally validates
ancestor-derived bounds; this plan does not claim the floor search replaces
those checks. General ranges and deeper trees retain the scan. First-read
failures remain failures; a removed second read cannot produce an injected
failure that depended solely on that unnecessary callback. Fault qualification
must cover the new executed paths, not preserve obsolete read ordinals.

The format, APIs, common-core Linux/Amiga behavior and cache ownership contract
remain unchanged. No proof is retained after any mutation. Concurrent media
replacement without cache invalidation and callback reentrancy remain outside
the existing cache contract, not new exclusions created by this pilot.

## Delivery and acceptance

### M1: Proof and focused implementation

Dependencies: unchanged qualified production baseline; independent review.

- M1-A1: Focused old-source counter-probe demonstrates the duplicate read and
  new-source positive tests require exactly one validated root read for stash
  and pop, with exact root/free/reserve accounting unchanged apart from the
  intended one-entry transfer.
- M1-A2: Test below-first, gap, after-last and overlap boundaries; malformed
  CRC/order/selected extent and first-read errors reject without mutation.
  Existing stock/reserve/emergency/root/snapshot/rollback tests remain passing.
- M1-A3: Restriction to height-one single-block queries is independently
  reviewed; general range and deeper-tree behavior remain qualified.

### M2: Normal performance and shared-platform qualification

Dependencies: M1; a source identity separate from the memcpy experiment.

- M2-A1: Local normal and actual ASan/UBSan regressions, quality/static gates,
  Linux conformance plus actual FUSE and reproducible Amiga builds pass for
  the frozen production candidate. Persist identities and failures.
- M2-A2: Balanced fresh-image normal-handler comparisons preserve every checked
  data/EOF/completion result, adverse pair and threshold failure. Diagnostic
  read/counter changes explain the removed callback without changing write,
  publication or fence policy; do not pool probe times into normal elapsed.
- M2-A3: Retain only demonstrated useful normal benefit without material
  regressions. Reject and restore an unhelpful candidate. Completing this
  pilot does not complete the overall goal unless all six workloads satisfy
  the five-times requirement.

## Rollback, risks and cost

Rollback is the exact helper guard removal and rebuild, without volume migration.
The main correctness risk is accidentally applying the argument to deeper trees
or larger intervals; keep an explicit narrow guard and negative coverage.
The timing benefit is unknown. Focused tests precede the expensive platform
matrix. This plan includes no CI, push, PR, merge, release, external publication
or hardware/power/controller qualification.
