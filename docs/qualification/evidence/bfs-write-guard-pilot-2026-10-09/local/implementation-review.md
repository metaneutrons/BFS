# Independent implementation review

The reviewer was a separate agent with a fresh context. It did not write the
change. It reviewed the isolated patch before the fixes below and judged
"accept with changes".

## Findings and resolution

1. Medium, tests. A mutation that writes the caller's bytes in place into the
   existing block before reporting the denial passed all eight tests. The
   denied write in the first test lay past EOF, and the fifth test read the
   content back only after an allowed write of the same bytes. Resolved: the
   first test also denies an overwrite inside the file; the fifth test reads
   bytes 0..3 through a second handle directly after the denial;
   `recovery_error` is part of the compared state. The mutation now fails two
   tests.
2. Low, contract. With a deny mask, `len > INT32_MAX` returned
   `BFS_ERR_PROTECTED`, although the header said argument errors take
   precedence. Resolved: the length check moved before the lock for every
   write, and the header names the file-size limit and resource errors as the
   ones reported only for permitted writes. A new test covers the precedence.
3. Low, hardening. The check read `seed.inode` without testing `seed.valid`.
   Resolved: a nonzero mask without a valid seed denies.
4. Information. The handler path has no host test. The guest test
   `protectio_40` covers it and passes on the AROS ROM (52/52).
5. Information. The patch depends on the uncommitted working baseline
   (`file_inode_seed_t` is not in `07216b7`).

## Behavioral differences confirmed by the reviewer

On a reloaded volume or with `recovery_error` set, a write to a protected file
reports the handle or recovery error instead of `ERROR_WRITE_PROTECTED`. A
denied write refreshes the handle's cached size and extents like any other
refresh, keeping the offset. Unlinked handles are unreachable in the handler.
`BFS_ERR_PROTECTED` can only come from `bfs_file_write_checked` with a nonzero
mask, whose only caller is ACTION_WRITE.

## Mutation results after the fixes

The reviewer's seven mutations, ported to the final code where their search
text changed, each fail at least one test: a separate pre-refresh read (A2,
1 failure), the check before the refresh (B2, 6), no check for `len == 0`
(C2, 1), a linked-only read (D2, 2), the check after the write (E2, 7), no
check on the handle-rebuild path (F, 1) and an in-place write before the
denial (G, 2). The scripts are in `build/write-guard-2026-10-09/reviewer/mut/`
of the working tree.
