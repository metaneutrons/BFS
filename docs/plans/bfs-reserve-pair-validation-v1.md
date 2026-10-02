# Forward-only reserve-pair validation v1

Decision state: rejected after completed M1/M2 qualification; exact production
baseline restored, equivalent added tests retained. The
[qualification report](../qualification/bfs-reserve-pair-validation-performance-2026-10-01.md)
records all adverse pairs and the failed target; this plan owns requirements.
The overall requirement remains at most five times PFS3 in every checked
AmigaDOS workload, without weaker durability, integrity, snapshots or recovery.
No external issue, CI or publication is part of this increment.

## Design and proof

The [retained absence shortcut](../qualification/bfs-single-block-absence-performance-2026-10-01.md)
removes redundant scan workspace. Stock validation still checks each reserve
block against all other reserve blocks on every eligible stash/pop. At reserve
index i it checks protection first, reserve duplicates second, active emergency
membership third. Preserve that exact error precedence.

Compare each reserve pair only at its earlier index: j=i+1..count-1. If the
loop reaches i, every previous k has already succeeded while comparing against
i, so a duplicate with a previous entry is impossible. Future comparisons cover
every remaining duplicate and still precede the next entry's protection check.
A backward-only triangular scan would not preserve this ordering.

The earlier [expanded-stock/hash-set prototype](../qualification/bfs-metadata-stock-performance-2026-10-01.md)
was rejected after mixed normal timings. This pilot does not reinstate its
extra spares, first-request reuse, contiguous-refill policy or transient set.
It keeps the qualified eight-request warmup and present placement unchanged;
only duplicate comparison work within one existing validation is removed.

Reuse the existing private range helper: replace its ignored-index argument
with a first-reserve-index argument. `validate_metadata_stock` passes i+1;
stash and ordinary free pass zero and still check every reserve entry. Retain
the count guards, per-entry protection and full active-emergency checks. Keep
the historical/inactive emergency-slot handling unchanged. No proof is cached
between calls, no arrays or heap allocations are added, and no mutations occur
during validation. The existing serialized/stable-state assumption remains.

For a healthy n-entry full validation, reserve comparisons change from
n(n−1) to n(n−1)/2; at n=128 this is 16256 to 8128. Emergency comparisons do not
change. Invalid early returns can have different comparison counts; no fixed
reduction or runtime forecast is asserted for them. The likely benefit is
unknown and cannot by itself be presented as closing the large create/delete
gap. General range membership must not accidentally skip prefix entries.

## Delivery and acceptance

### M1: Equivalence and focused cases

Dependencies: frozen, separately qualified absence-shortcut baseline.

- Preserve and rerun the independent Luna abstract return-category model:
  reserve lengths 0–5, values 0–2 and all protected/emergency subsets,
  23296 cases. Its bounds/count/arithmetic/concurrency exclusions are explicit.
- Root review checks the actual helper/call sites, including bounded unsigned
  indices, one-block membership equivalence, initial guards and stable state.
- Actual focused cases check mixed-fault precedence: valid X/protected R/X
  returns EXISTS before reaching R; protected X/X returns CORRUPT. Cover healthy
  stock permutations, first/middle/last duplicates, active and inactive
  emergency membership, count boundaries and unchanged stock/root/pool state
  before any failing validation mutation. Retain the existing absence tests.
- Run the final focused suite against both old and new sources: functional
  statuses must agree. This is an equivalence test, not an old-source RED test;
  the model/source proof separately establish the removed comparison work.
- A reviewer distinct from the test author reviews the actual patch and cases.

### M2: Shared-platform gates and normal comparison

Dependencies: M1; frozen candidate identity, no mixed optimizer changes.

- Local normal and actual ASan/UBSan tests, quality/static checks, Linux
  conformance/actual FUSE and reproducible Amiga builds pass. Preserve failures
  and scope limitations as well as successful logs.
- Compare balanced fresh-image normal-handler runs against the retained absence
  shortcut, not the older pre-shortcut baseline. Keep every data/completion
  result, adverse pair and same-run five-times failure. Separate probe timing.
- Check structural writes, publication, fence and allocation policy remain
  unchanged. Retain only useful observed benefit without material regressions;
  otherwise restore the exact baseline and preserve the rejected evidence.
- Completing this pilot does not complete the overall goal unless all six
  workloads satisfy the five-times requirement.

## Rollback and exclusions

Rollback restores the private helper argument/loop and three call sites, then
rebuilds. No format/API migration, ownership cache, LIVE-tag inference, ledger,
in-place COW, writeback, batching, placement/refill, snapshot or fence change.
No CI, push, PR, merge, release, external publication, new soak or hardware/
power/controller qualification. Any larger structural change needs its own
proof, versioned design and qualification, not an extension of this shortcut.
