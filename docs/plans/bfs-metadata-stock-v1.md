# MS: bounded pre-publication metadata stock

Status: measured and rejected as the default. The
[measurement report](../qualification/bfs-metadata-stock-performance-2026-10-01.md)
records the mixed timing result and complete candidate patch. The retained
default is the locally tested
[sealed-settlement checkpoint](../qualification/bfs-sealed-settlement-performance-2026-10-01.md).
The goal remains ≤5× PFS3 in every checked AmigaDOS phase, with unchanged
guarantees. No performance forecast is accepted in place of measurements.

The SF probe still records 282 allocation-body Free-Tree writes while creating
40 files and 116 while deleting them. Its current metadata-reuse policy starts
after eight requests in each live transaction and can use only surplus above
the complete recursive reserve floor. The next experiment targets those
top-level COW allocations, not data blocks or publication barriers.

## Scope and ownership

Only mounted, writable, recovery-clean, no-snapshot/no-refcount/no-legacy-extent,
height-one contexts may use the new policy. Require exact owner, BIO, allocator,
working/committed superblock and live-ID wiring. Format and standalone allocators
retain the current behavior. Snapshot creation/deletion transitions are excluded
even when an option bit is clear. Keep SF's separate exact unpublished-root
check at commit; stock availability is not a substitute for that proof.

Preserve `reserve_refill_target` as the full recursive floor: **20 blocks at
height one**. Never spend its prefix for top-level metadata. Try a validated
ordinary suffix first. Only if stock is at/below that floor, acquire a bounded
additional 20 blocks, capped by the existing 128-entry array, using the existing
refill/withdrawal implementation. The first trial's highest-tail selection
created 204 refill writes for 40 creates, so the revised expanded-target refill
first looks for one complete contiguous run, sharing the same tail-withdrawal
mutation. Only an error-free missing run may use the original highest-tail
selection; scan/read/validation errors propagate. Ordinary floor refills retain
the original selection. Do not continuously refill to the expanded
target after each pop. Keep the current global-reserve and surplus limits,
recursion guard, rollback/error behavior and roving placement preservation.
If safe surplus cannot be obtained, use the ordinary allocation path.

Existing metadata stock validation must additionally protect both recorded
backup-SB blocks, all working and committed tree roots and the legacy extent
root. Check duplicates, pool provenance and free-tree absence before reuse.
Pool-origin suffix entries never become ordinary metadata stock. An unpublished
node can be stashed only through the established B-tree retirement/abort
contract, after the caller has relinquished it; no active/committed node may
circulate through stock. Stock changes reserve ownership, not free accounting.

The trial also tests equivalent validation-cost reductions. Invariant geometry
is computed once per stock validation rather than once per entry. An ephemeral
exact open-addressed set checks every stock duplicate and active-pool alias,
instead of repeated pairwise range scans. Its 256 slots exceed the maximum 128
stock entries, so even adversarial collisions terminate. No validation result
is cached across mutations; selected-block free-tree absence and all root,
backup, range and historical-pool restrictions remain mandatory.

Within this narrower context, allow proven suffix reuse/stash from the first
metadata request. Outside it, retain the qualified eight-request UR policy.
An explicit settlement-active guard disables **the new policy** throughout
`txn_commit_working`, including its initial reserve return and every legacy
pending-reclaim pass. Restore the guard on every exit. Do not alter the existing
UR fallback, 256-pass convergence bound or recovery latch. Mount recovery and
reload must preserve exact ownership wiring; initialization resets volatile
stock policy state.

## Required proof and measurements

- Natural mounted first-request witnesses: batched withdrawal, multiple
  top-level draws without additional Free-Tree writes, full 20-block recursive
  floor retained, exact accounting and no alias with live/old roots or SBs.
- Low-space, full/historical pool, duplicate/free/root/backup overlap, read-only,
  recovery, snapshot/refcount/extent transitions and deeper-tree boundaries.
- Read, allocation, partial-write and pending-headroom cuts around refill and
  COW; preserve committed roots and abort ownership, never fall back after an
  ambiguous mutation. Normal and buffered-persistence fixtures must still
  recover complete old/new state with strict data/fsck oracles.
- Explicit legacy-settlement witnesses and repeated create/write/overwrite/
  delete commits, tiny pending caps, snapshots and low-space campaigns. The
  earlier surplus allocator's convergence failure is not waived by SF.
- Focused/full normal and sanitizer gates, independent implementation/test
  review, normal/probe builds, fresh normal-handler images in both orders and
  unchanged strict verifiers. Retain adverse samples. Roll back an ineffective
  or unsafe experiment; no format migration, dirty cache or reduced flushes.

No CI, push, PR, merge or hardware qualification is included. Bulk payload I/O
is a separate possible increment; this plan does not change the BIO contract.
