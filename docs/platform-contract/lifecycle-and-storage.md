# Lifecycle and Storage Contract

## Public-core lifecycle

M3 must make the following lifecycle explicit in the public core API:

```text
open transport -> select geometry -> mount(read-only or read-write)
-> use one filesystem instance -> close according to mount mode -> close transport
```

The existing mutable `bfs_fs_mount()` is not a read-only mount: it resumes
interrupted snapshot deletion. M3 must add a mode-bearing mount API and a
non-writing close path. A read-only mount must validate the selected committed
superblock and all accessed metadata, but must never resume deletion, commit a
transaction, flush a cache, change the backup-superblock slot, or call a
transport write callback. Closing a read-only mount must release memory only.

The legacy mutable API remains available to the Amiga handler. It must not be
used by `bfs mount` in M5. The implementation may share parsing and validation
internals, but mode checks belong in `libbfs`, not only in a FUSE callback.

A filesystem instance has one owner. No caller may use it after close starts,
after mount failure, or after `bfs_fs_abandon()`. A core recovery error
invalidates all outstanding core file handles. FUSE request state must retain a
mount reference until its reply or cancellation cleanup has completed; unmount
then waits for the request count and handle count to reach zero.

## Concurrency, cache, and FUSE policy

The current core has one filesystem scratch block and several read paths take a
write lock. M5 and M6 therefore use libfuse's single-threaded loop. This is an
explicit correctness boundary, not a performance promise. Multiple client
processes may queue requests, but no two callbacks execute concurrently. M6
uses the core's lock-held append operation for atomic end-of-file placement in
that serialized profile. A multithreaded dispatcher requires the separate
per-request scratch ownership, lock ordering, and threaded-stress qualification
defined by Issue #54.

M5 uses foreground mode by default, `default_permissions`, `nodev`, `nosuid`,
zero entry and attribute cache timeouts, and no writeback cache. `allow_other`,
background daemon operation, kernel cache, and writeback cache are out of
scope. Mount-option parsing must reject unsafe or unknown policy-changing
options instead of silently accepting them.

## POSIX transport requirements

`src/host` provides the production POSIX block transport. It is distinct from
`tests/block_device_emu.c`, which remains a fault-injection test backend. The
production transport must satisfy all of the following:

* Use checked 64-bit byte offsets for `pread` and `pwrite`; reject arithmetic
  overflow and short transfer as an I/O error after retrying `EINTR`.
* Open a regular image or an explicitly selected partition range. The range has
  a checked byte offset and length, and all core block accesses stay within it.
* Determine file geometry with `fstat`; for block devices, use platform
  geometry APIs. Never infer a partition from a pathname suffix.
* Reject writable raw devices unless the caller selected an explicit write mode,
  the device is not mounted by the host, and an exclusive advisory lock was
  acquired. M5 never requests this mode.
* For read-only mounts, open with `O_RDONLY`, install a transport whose write
  callback fails, and instrument it so M3/M5 tests can prove a zero write and
  zero sync count. A read-only regular-image mount is safe only when the image
  itself does not change during the test or use session.
* `sync` uses `fsync` for mutable regular files and a documented equivalent for
  block devices. A failed sync is a failed commit. The transport must preserve
  the core's ordered-data and superblock publication ordering.
* Close releases the descriptor exactly once. It must be idempotent after a
  failed mount and must report no stale descriptor as a successful mount.

The transport accepts a requested block size only after the core's validated
superblock probe has selected it. It must not truncate a 64-bit range before
`bfs_bio_set_geometry()` checks the v2 32-bit block-count maximum.

## Representation and boundary checks

The disk is big-endian. Every multi-byte field crosses the core boundary via
the existing endian helpers; no adapter may cast packed on-disk records to host
native metadata. Adapters must copy packed fields before accessing them if the
target can require aligned accesses.

All byte counts and offsets use checked `uint64_t` arithmetic until an API
requires a narrower type. File reads are split into requests no larger than
both the FUSE request length and the core's `uint32_t` read length. Conversion
of a FUSE offset, POSIX `off_t`, device size, directory cookie, inode, or block
count fails with `EOVERFLOW` rather than wrapping.

## Durability boundary

The core transaction commit is the only filesystem durability boundary. A host
adapter must never issue an independent metadata flush that reorders data and
superblock publication. M3 fault probes inject `EINTR`, short I/O, read error,
write error, and sync error at the POSIX transport boundary. M4 replays their
resulting images through an independent format oracle and records the outcome.
