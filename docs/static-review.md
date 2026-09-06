# Core static-analysis review

Scope: PR #12, reviewed against the 34 added Codacy findings at commit
`5f6fa492280fa3f1bc9d73e6de0d060a61145978`. This document records dispositions,
not milestone acceptance or a claim that all possible defects have been found.

## Corrections

- Replace unbounded snapshot-name length checks with a bounded scan. A
  nonterminated maximum-sized name is rejected by create and lookup tests.
- Share mount/recovery initialization, separating namespace trees, refcounts
  and root validation. Recovery now applies the same inode-number validation.
- Separate pending-block reclamation into batch, shared-reference and range
  operations. Validate pending count against capacity and allocation arithmetic;
  a corrupt-count regression verifies rejection and the recovery latch.
- Separate extent rollback and snapshot reference restoration from their main
  operations; collect utilization counters in a context and use the existing
  child accessor. Split the large-reclamation test's repeated setup.

These address the one Flawfinder string finding and seven Lizard findings
(six function-length findings and the utilization walk's parameter count).
No project-wide metric threshold has been raised.

## Reviewed false positives

Eight bounded copies were independently reported by Flawfinder and Opengrep:

| Location | Bounds evidence |
| --- | --- |
| `btree.c`, two copies in `child_bounds` | `tree_shape_valid` bounds key size by `BFS_MAX_KEY_SIZE`; both destination arrays have that size; parents are validated before descent. |
| `fs.c`, pending-buffer growth | Old count is checked against old capacity; new slots exceed capacity; multiplication is checked before allocation. |
| `namespace.c`, deletion-vector growth | Count equals old capacity; checked doubling allocates the larger destination. |
| `namespace.c`, comment key | The 79-byte limit is checked before copying into an 80-byte destination and adding a terminator. |
| `txn.c`, reclamation batch | Count is checked against source capacity and `SIZE_MAX`; destination allocation and copy use the same size. |
| `test_cache.c`, two BIO transfers | The BIO contract supplies a full block; source/destination rows have exactly that size and the row index is checked. |

Each copy has a local proof comment and call-scoped annotations. The Opengrep
annotation names only `c_buffer_rule-memcpy-CopyMemory`; other rules remain
enabled. Flawfinder's separate `execl` finding in `test_fsck.c` is also local:
the test executes a fixed build-directory binary with constant arguments and
without a shell, intentionally testing the actual fsck command.

Six Cppcheck header-member findings ignore implementation-file accesses:
`last_error`, allocator `error`, free-sink `reserve`, both
`recovery_generation` members and `has_snapshots`. Each declaration names its
consumer and suppresses only `unusedStructMember`. These fields must remain.

Three reported Cppcheck `redundantAssignment` findings in hardware-failure tests
miss reads through the installed BIO callbacks. Resetting the BIO or fault
countdown is required before verification/recovery. Matching restoration sites
are annotated individually; the fault injection is not removed. An equivalent
callback-related restoration in the corruption tests is documented likewise.

No analyzer, rule, source directory or security category has been disabled.
Changes to a guarded operation invalidate its recorded bounds proof and require
review of the annotation. Live Codacy results, local analyzer results and
regression evidence are recorded on the milestone issue, not inferred here.

## Additional fault-path review

- Partial writes previously generated a new checksum over retained corrupt
  bytes. Read and read-modify-write now share CRC validation. The regression
  fails before the fix and covers partial overwrite, truncation, unchanged
  mapping/data and a permitted full-block replacement.
- A persistent device failure could also break namespace rollback without
  latching an error. The pre-fix regression resumes with `recovery_error == 0`;
  the corrected path rejects subsequent mutations/sync and preserves the
  committed comment after remount.
- Failed extent-remap rollback marks ownership uncertain; callers must recover
  rather than free a replacement that may still be referenced. A persistent
  write-failure regression covers failed replacement and restoration inserts.
