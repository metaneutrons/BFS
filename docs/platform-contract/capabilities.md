# Capability Ledger

Every platform-facing implementation and test report uses these identifiers.
An adapter must declare an unavailable capability explicitly. M3 adds a
machine-readable manifest and a test that fails when an implemented surface has
no ledger entry. The current implementation declarations are in
[`capabilities.json`](capabilities.json).

| Identifier | Meaning | Owner/source surface | Current status | Required evidence |
| --- | --- | --- | --- | --- |
| `BFS-CAP-CORE-RO-MOUNT` | Core can mount and close without any write-side recovery or commit | `include/bfs_fs.h`, `src/core/` | Required | Zero-write probe |
| `BFS-CAP-CORE-RW-MOUNT` | Core mutable mount and commit lifecycle | `include/bfs_fs.h`, Amiga adapter | Existing Amiga only | Commit/recovery tests |
| `BFS-CAP-POSIX-RANGE-IO` | Checked 64-bit POSIX image or partition-range I/O | `src/host/` | M3 prerequisite | Short I/O and overflow tests |
| `BFS-CAP-POSIX-RO-GUARD` | Read-only descriptor and transport reject every write/sync | `src/host/` | M3 prerequisite | Counter assertions |
| `BFS-CAP-FUSE-LOOKUP` | Inode lookup and attributes | `src/fuse/` | Required | Mounted fixture test |
| `BFS-CAP-FUSE-DIRECTORY` | Directory enumeration with reversible name mapping | `src/fuse/` | Required | Mounted fixture test |
| `BFS-CAP-FUSE-READ` | Open/read/release regular files | `src/fuse/` | Required | Mounted image comparison |
| `BFS-CAP-FUSE-SYMLINK` | Read-only symbolic-link exposure | `src/fuse/` | Required | Mounted fixture test |
| `BFS-CAP-FUSE-XATTR-RO` | Read-only BFS metadata xattrs | `src/fuse/` | Required | Mounted fixture test |
| `BFS-CAP-FUSE-STATFS` | Filesystem capacity reporting | `src/fuse/` | Required | Oracle comparison |
| `BFS-CAP-FUSE-SNAPSHOT` | Explicit, immutable snapshot-root selection | `src/fuse/`, `src/core/` | Required when snapshots exist | Deletion-state fixture |
| `BFS-CAP-FUSE-MUTATION` | Opt-in POSIX writable namespace and data operations | `src/fuse/`, `src/core/` | M6 implementation | Mounted writable fixture and remount test |
| `BFS-CAP-POSIX-OPEN-UNLINK` | Open file survives unlink and restart safely | `src/core/`, `src/fuse/` | M6 implementation | Handle-lifetime and recovery test |
| `BFS-CAP-POSIX-RENAME-REPLACE` | Atomic replacement rename with crash safety | `src/core/`, `src/fuse/` | M6 implementation | Replacement and crash/recovery test |
| `BFS-CAP-AROS-HANDLER` | Native AROS filesystem adapter | `src/aros/` | Deferred | Target boot and packet tests |
| `BFS-CAP-MORPHOS-HANDLER` | Native MorphOS filesystem adapter | `src/morphos/` | Blocked externally | SDK/runtime access and packet tests |

The default FUSE mount remains read-only. `BFS-CAP-FUSE-MUTATION` is available
only with the explicit `--read-write` selection, never for a snapshot mount.
The capability test must verify that every advertised operation has a matching
mount test and that every mutation on a default or snapshot mount returns
`EROFS`.
