# Performance fixture inventory before push

This is the primary agent's transcription of the read-only Luna inventory and
independent packaging review, not a new runtime qualification. The checkpoint
before packaging is `3cbe7f1`. The push includes its full branch history.

## Retained diagnostic fixtures

The split and coalescence archive contains 150 versioned files; its manifest
validates. Ignored local fixture sources, prior revisions, probe stubs, fake
headers, replay scripts and associated source/build/run logs have byte-identical
tracked counterparts. The source packages are under
`docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01/fixtures/`.
The key files are `fixtures.c.b64`, `probe_stubs.c.b64` and `replay.sh`.

Ignored consumer sources, tests and logs also match the tracked archive copies.
The main tracked helpers are `consumer/consume.py`, `consumer/test_consumer.py`,
`counter-equivalence.mjs`, `summary.mjs` and `replay.mjs` in the same archive.
The private diagnostic source is reconstructible from Git checkpoint `bdb3ae0`
and the tracked private patch/source packages. The active benchmark verifier
tests are tracked in `tests/quality/test_bench_verifier.py`.

The historical `fixtures/replay.sh` assumes its original build-directory layout
and the reconstructed `build/split-coalesce-attribution-source` tree. It is not
a directly executable clean-checkout compile runner in its archive location.
The source bytes and reconstruction provenance are retained. The separate
top-level `replay.mjs` works without that build tree and has been rerun here.
Generated executables, dSYM directories and Python caches are not fixtures that
must be committed; the repository intentionally retains source and text evidence.

## Active performance test families

The reviewer checked tracked status for these 18 source files in the push range:

- `tests/amiga/crc32_probe.c`, `crc32_abi_probe.s`,
  `memory_copy_compare_probe.c` and `memory_functions_probe.c`.
- `tests/test_btree_bulk_slots.c`, `test_btree_scratch_lifetime.c`,
  `test_btree_validation_cache.c`, `test_btree_noop.c` and `test_btree_buffers.c`.
- `tests/test_alloc_root_shift.c` and `test_alloc_metadata_reuse.c`.
- `tests/test_txn_sealed_convergence.c`, `test_txn_sealed_persistence.c` and
  `test_txn_sealed_preflight.c`.
- `tests/test_inode_stamp.c` and `test_inode_stamp_faults.c`.
- `tests/test_cache_buffer_faults.c` and `test_cache_resident_paths.c`.

These paths are versioned; no needed fixture source in this checked inventory
exists only in an ignored build tree. This inventory does not assert that every
historic discarded experiment is a supported runnable fixture.

## Packaging review

The independent review passes the raw-byte and ASCII-view checks for 68 logs,
all nine preserved original manifests, current manifest entries and the scoped
source/tooling check. Its independent tracked diff found only the expected
formatter views and manifests changed. The reviewer requested clearer clean
input and evidence-fixture scope guards, and a retained inventory document.
The primary agent added those guards and this inventory, then repeated the
read-only verification. No production source, TSV, fixture or test changes
are part of the packaging repair.

The focused final review passes the updated clean-tracked-input guard, exact
nine-manifest set and membership, full tracked-diff allowlist, inventory wording
and read-only verifier. Its remaining wording caveat is resolved in the README:
unrelated untracked files are outside the cleanliness check; intended output
collisions are rejected.
