# Forward-only reserve validation: root-authored notes

This summarizes observed tools and independent reviews; it is not a captured
command transcript. All source/compiler inputs remain frozen during timing.

- Qualified baseline: local commit 47a29c3, core b628d72b…, normal handler
  7ec43316…, probe 8bed35de…. Candidate core 6161a109…, normal fa264b32…,
  probe e4da08b9…. Final focused tests cfd43be9…; exact hashes are retained.
- The primary implements only the private helper/call-site change. Luna worker
  allocation_crc_next authors only three new registered test functions.
  Existing oracles are unchanged. Worker metadata_batch_tests independently
  reviews the actual core, confirms all three callers and forward-only proof,
  bounded indices and preserved protection/duplicate/active-emergency order.
- The independent abstract model is preserved verbatim apart from root-added
  scope comments and rerun successfully: 23296 return-category cases match.
  It excludes real count/bounds/arithmetic and concurrent mutation; actual
  source and focused tests address the former, stable-state contract the latter.
- Exact same final focused tests compile with a baseline-source override and
  with the candidate: both 27/27 PASS. No old-source RED failure is expected
  from an equivalent status change. This does not measure runtime comparisons.
- Count0/1 fixtures deliberately truncate active reserve prefixes and are not
  publishable/free-space-accounting witnesses. Reversed ordinary stock and
  full128 stock use owned blocks; the full-stock case allocates each additional
  block before recording it and pops the suffix through iface.alloc. Last
  validation index passes begin128. Six malformed-stock configurations verify
  exact EXISTS/CORRUPT order and unchanged root bytes/ID, total_free, complete
  reserve bytes/count, working SB/emergency pool and write count.
- Normal and actual ASan/UBSan runs each pass 48 suites/468 cases, exit0.
  Leak detection is explicitly disabled, not qualified. All78 quality tests,
  ShellCheck/actionlint and clang static analysis pass. Twenty Linux conformance
  tests plus both actual FUSE runs (default; options7 pressure/interruption)
  pass. No full geometry/hardware/power/controller qualification is asserted.
- Two distinct Amiga build directories reproduce normal/probe handlers and
  guest byte-for-byte. The normal guest1fcc… and probe guest5126… are unchanged.
  Ten fresh preparations and idle preflight pass before timing: input hashes,
  ROM, no remote emulator/compiler and inactive, not disabled, coffin.service.

No CI, push, PR, merge, release, external publication, new soak or real hardware
qualification is performed. The prior expanded-stock/hash-set prototype is
not reinstated; warmup8, placement, stock size and all-slot emergency behavior
remain unchanged.

## Independent final-test review

The distinct Luna reviewer finds no blocker in final tests cfd43be9…. It
confirms all three registered additions, genuinely allocated full-stock blocks,
the final suffix entry127, preserved bytes/accounting and six error cases.
Synthetic0/1-prefix accounting limitations are explicit. The new cases do not
directly inject reserve_count>128 or emergency_count>32 through this stock path;
those production guards are unchanged and separately inspected. This is not a
claim of new direct over-cap test coverage.

## Measured rejection and exact restoration

Independent Luna raw extraction precedes the primary JSON and agrees on all
means, matched deltas,24 ratios,11 adverse pairs and12 threshold failures.
Delete's +16.4961% mean includes four adverse matched pairs; create has three.
Write's −15.2710% mean includes one adverse pair. Underlying compiler/layout/
scheduling contributions are not isolated. The candidate is rejected.

Both diagnostic runs pass; every non-time field agrees with the retained
single-block-absence controls in both orders. Probe wall scopes overlap and
are not normal acceptance data. All ten completed systems are recoverably
archived with20 before/after TSV hashes equal. All20 persisted raw TSVs match
local records exactly. Only copied formatter CSI text is ASCII-normalized.

Core is restored byte-for-byte to b628d72b…, normal handler rebuilt to exact
7ec43316…. Equivalent new tests cfd43be9… remain. Full restored normal and
actual ASan/UBSan suites both finish exit0 with48 suites/468 passes/zero errors.
The rejected patch, baseline and candidate identities remain as evidence.

## Independent report review

The distinct Luna reviewer rechecks all raw normal records against the report:
six means, changes, adverse pairs, matched deltas, PFS3 drift and same-run
ranges agree. Both probe orders have valid schema/drive/PASS metadata and no
non-time counter differences. Core/test identities and restoration statements
agree. No blocker is found; rejection is explicitly observational rather than
an isolated causal or statistically precise conclusion.
