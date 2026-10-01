# Sealed preflight geometry review

Independent Luna review found no blocking issue in the production or test
diff against `8b35eec`. Main verified the reviewed source identities and ran
the focused tests on both the exact baseline and candidate implementations.

The mounted-owner gate proves that the working and committed Free-Tree
superblock pointers alias the corresponding owner transaction superblocks
and that their BIO pointers match. No callback or BIO write occurs while the
captured geometry is used. The local planned superblock changes only after
the geometry-dependent checks. The geometry is ephemeral, not persistent
derived state. This relies on the existing serialized, stable mounted-BIO
contract; it does not qualify arbitrary backend mutations during reads.

Per-block backup comparisons retain full-width uint64_t quotients. Old-leaf
range checks retain the previous explicit bfs_blk_t casts. Computing geometry
at entry evaluates the divisions before old-leaf integrity checks; a mutated,
already-invalid mounted BIO with zero block size could fail earlier. Valid
mounted geometry has a nonzero block size.

In the normal m68k assembly, baseline seal_block_reserved contains two
___udivdi3 calls and is called from the pool and input loops. The old-leaf
checks also perform two divisions. Candidate setup computes both quotients
once, using two calls, before the checks and loops. The per-block helper is
inlined into full-width comparisons with no division. Baseline assembly
lines 2757–2789, 3294, 3332, 3409 and 3441; candidate lines 3168–3191 and
3435–3446. These are generated call-site observations, not dynamic counts
or a prediction of elapsed time.

The revised tests retain all four prior backup rejection shapes and the two
current-root rejection cases. Both distinct backup locations are checked for
pending, reserve, inactive pool and old-leaf overlaps. The high-quotient tests
separately exercise both superblocks and the preserved full-width comparison
versus explicit old-leaf cast. Their fixtures abandon the instance without
committing or remounting the deliberately malformed in-memory superblock.
The successful-seal fake device asserts one write but does not trace its
destination; no superblock publication is established by the public function
flow and abandon cleanup, not by a direct write-address oracle.
