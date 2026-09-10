# M7 Linux Qualification

This document defines the Linux evidence required by M7. It supplements the
versioned acceptance criteria in `docs/plans/cross-platform-development.md`; it
does not claim native AROS, MorphOS, Apollo hardware, or preemptive FUSE
callback support.

## Profiles

`tests/qualification/linux-m7-matrix.json` is the versioned source of truth.
Its `fast` profile runs on every pull request through
`make linux-qualification-fast` and records `build/linux-qualification/fast.json`.
The record identifies the source commit, matrix digest, Linux/Python runtime,
dispatcher, concurrency profile, every command, elapsed time, exit status, and
hashes plus bounded tails of captured output. The tails retain a failure reason
without allowing an unbounded test log to become CI evidence.

The fast profile runs direct-core and conformance tests, then mounts each of the
seven legal BFS block sizes (1, 2, 4, 8, 16, 32, and 64 KiB). Every mounted
case creates the fixture through the shared core, compares it with the
independent oracle, exercises read-only and read/write operation paths,
hard-links, snapshots, 255-byte names, maximum comments, 2 and 4 GiB sparse
file offsets, fragmented allocation, disk-full recovery, abrupt daemon
termination, and both individual and dual-superblock rejection paths. The
direct-core portion includes the bounded corruption, fault-injection, crash,
durability, stress, and seed-corpus tests. Sparse offsets qualify representation
behavior only; they do not claim that a real maximum-capacity medium was tested.

The selected dispatcher is `fuse_session_loop`: libfuse invokes callbacks
serially. The mounted workload starts multiple client processes for atomic
append, so clients may queue requests. This remains a serialized callback
profile. Parallel callback dispatch, ThreadSanitizer, and in-flight unmount
coordination are explicitly deferred to issue #54.

## Soak Approval And Evidence

M7 requires one 72-hour Linux FUSE soak on an identified Linux host. It is not
part of pull-request CI: GitHub-hosted jobs cannot reliably reserve a suitable
72-hour FUSE host, and a shortened run must not be presented as the target
soak. Before starting it, record in issue #29:

1. Host identity, kernel, libfuse version, CPU/RAM, backing-storage type, and
   available capacity.
2. The source commit, matrix digest, planned start/end timestamps, operator,
   and a link to the explicit approval comment.
3. Limits from the matrix: four client processes, 60-second cycles, a
   10-second operation deadline, 256 MiB daemon RSS, and 128 open descriptors.

The soak runner records each cycle's operation totals, deadline status, daemon
RSS and descriptor count, clean unmount, oracle comparison, checker result, and
recovery/remount result. Any stall, crash, integrity mismatch, resource-limit
breach, or missed cleanup is a failure requiring a retained reproduction and
investigation. Only a record whose completed duration is at least 259200 seconds
can satisfy M7-A4; shorter runs are preflight evidence only.

On the approved host, build the current commit and run:

```sh
make linux-qualification-soak \
  APPROVAL_REFERENCE='https://github.com/metaneutrons/BFS/issues/29#issuecomment-...' \
  OUTPUT=/var/tmp/bfs-m7-soak-<commit>
```

For a non-qualifying installation preflight, add
`SOAK_ARGS='--preflight --duration-seconds 300'`. The runner refuses that
duration without `--preflight`, and marks its result as not qualified.

## Gate Integrity

`tests/qualification/test_linux_qualification.py` rejects a matrix that omits a
legal block size or weakens the 72-hour target. The runner refuses to overwrite
an evidence record, treats missing `/dev/fuse` as a failure, checks that the
serialized dispatcher remains selected, and preserves records when an invoked
criterion fails. The required `CI Success` aggregate continues to include the
Linux FUSE job, so this addition does not weaken branch protection.
