# Static-analysis review

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

## Amiga integration review

PR #13 introduced 25 Codacy findings at `c722e8c`. Each is reviewed separately;
the quality threshold remains unchanged.

- Split path-directory traversal from BSTR decoding and result copying. During
  review, a failed parent lookup was found to fall back to the root silently.
  Only an explicit traversal above the root now stays at root; other lookup
  errors propagate. The guest `path_45` group exercises relative parent paths,
  volume-prefix reset, and missing/non-directory intermediates. It does not
  claim to inject device I/O faults into DOS path traversal.
- Move the ExAll match-pattern buffer into the owner of its control object, so
  the control never retains the address of an expired local array.
- The Python runner accepts a nonempty argv list, resolves the executable and
  calls it without a shell. Its command is an operator-controlled local CLI
  argument, never guest input. Call-scoped Bandit/Opengrep annotations record
  that intentional trust boundary. The aggregate test invokes the fixed script
  using `/bin/bash`; no command string is constructed.
- `amiga_bio.removable` is used by the device change-state check; its header-only
  unused-member finding is annotated individually.
- Eight newly reported handler copies have explicit bounds proofs: allocated
  BSTR length plus two bytes; 256-byte name destinations with uint8 lengths;
  bounded volume names; two FIB quadwords checked with a static assertion; and
  caller-owned MorphOS ABI quadwords. AmigaOS has no process memory isolation:
  packet callers must supply valid storage of the documented ABI size. The
  handler rejects absent and misaligned quadword pointers, but cannot prove
  that arbitrary caller addresses designate allocated memory.

All copy annotations apply only to Flawfinder's individual call and Opengrep's
`c_buffer_rule-memcpy-CopyMemory` rule. Cleanup functions in the optional emulator
launchers are invoked by their adjacent EXIT traps; local ShellCheck annotations
cover the old and new analyzer names for this indirect-call false positive.
