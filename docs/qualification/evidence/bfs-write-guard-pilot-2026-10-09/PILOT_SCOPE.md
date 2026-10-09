# Write protection check pilot scope

Fixed before execution, 9 October 2026 at 21:41 UTC.

The candidate removes the Amiga handler's separate protection read from
ACTION_WRITE. `bfs_file_write_checked` tests FIBF_WRITE against the inode that
the core refreshes under the same write lock, before any data, allocation,
sampling or inode change, and returns the new `BFS_ERR_PROTECTED`. Nothing is
cached between packets. There is no on-disk change. The expected effect is one
logical inode read fewer per Write packet: 259 instead of 515 for 256 writes.

The design is that of the goal-root pilot. It has two diagnostics (durable
split write, write sidecar, all-call CRC, detail stride 17), two production
pairs per normal/durable mode with reversed filesystem and handler order, and
one identical-baseline control pair per mode. That is exactly 14 fresh Cachy
starts, all 23 workloads and both filesystems. Guests, PFS3, ROM, formatter,
format v3 and actual RDB buffers 30 are those of the goal-root pilot. Both base
handlers are byte-identical to its base handlers. There are no retries,
exclusions, extensions or pooling with preceding evidence.

Fabian agreed to run this pilot. The retention rule below was proposed to him
before execution, in place of the earlier rule, because the identical-baseline
controls of the goal-root pilot ranged from 0.81 to 1.03 for 4KiB growth, which
is wider than the expected effect.

Retention requires all of the following:

1. Every strict correctness and identity check of the shared verifier passes.
2. Diagnostic, APPEND_4K_1M work: WORK_PACKET_WRITE_CALLS is 256 for base and
   candidate. WORK_INODE_READ_CALLS and WORK_WRITE_DETAIL_INODE_READ_CALLS are
   each exactly 256 lower for the candidate. WORK_CORE_FILE_WRITE_CALLS and
   WORK_WRITE_DETAIL_INODE_WRITE_CALLS are unchanged.
3. Diagnostic, every phase, work and volume flush: no count field is higher for
   the candidate than for the base. Count fields are those ending in `_CALLS`,
   `_READS`, `_WRITES`, `_UPDATES`, `_COMMITS`, `_VIEWS`, `_HITS`, `_MISSES`,
   `_MAPS` or `_ALLOCS`. Sample counts, ticks and times are not count fields.
4. Production: no phase has a BFS `US` ratio of at least 1.10 in both pairs of
   either mode.

The paired 4KiB growth medians, the number of pairs that are not slower, the
controls and the PFS3 calibrators are reported. They do not decide. Probe
times are inclusive and nested; sampled ticks are not extrapolated.

If retention fails, only this candidate's changes are restored: the five
source files and the new test, recorded as deltas in this bundle. The evidence
is kept. There is no second attempt in this step. The preceding sparse-CRC
read-regression question and the <=5x PFS3 objective remain open. No CI,
commit, push, merge, installation or issue change is part of this step.
