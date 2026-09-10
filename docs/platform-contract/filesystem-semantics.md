# Filesystem Semantics

This document defines the host-facing interpretation of BFS v2. `BFS-CAP-*`
identifiers refer to the capability ledger.

## Namespace and inode identity

The FUSE adapter identifies an object by its BFS inode number. The root is
inode `1`. A live mount has one immutable root for its lifetime. A selected
snapshot has the snapshot's immutable root for its lifetime. The adapter must
not create a synthetic snapshot directory, because that could collide with a
real BFS name and would give the namespace a second, undocumented meaning.

BFS directory keys are byte strings of at most 255 bytes and compare using the
on-disk BFS case-folding rule. FUSE lookup is therefore case-insensitive in
the BFS sense. FUSE entry and attribute timeouts are zero in M5 and M6; caching a
case-sensitive Linux dentry would otherwise create aliases that the core does
not distinguish.

### FUSE name encoding

Linux names cannot contain NUL or slash and reserves `.` and `..`. The mapping
must be injective even for malformed or old media that contain such bytes:

* A BFS name is emitted unchanged only when it contains neither NUL nor slash,
  is neither `.` nor `..`, and does not start with the ASCII prefix
  `@bfs-hex-`.
* Every other BFS name is emitted as `@bfs-hex-` followed by two uppercase hex
  digits for every source byte. The prefix itself is escaped by the first rule.
* Lookup decodes only this exact escaped form. A malformed escape is an
  ordinary direct name and is looked up as such.

The conversion preserves all source bytes and has no Unicode normalization.
M6 must use this same mapping for all mutating operations. A new writable name
is rejected with `EINVAL` when its decoded form violates the existing core
name rules or is more than 255 bytes.

## File types, metadata, and xattrs

M5 reports regular files, directories, and symbolic links. A hard link is a
second directory entry to its target inode and is reported with the target's
ordinary file type and `st_nlink` value. An unknown inode type or a malformed
inode is a corruption error, never a guessed file type.

`st_size` is the v2 unsigned 64-bit size. `st_ino` is the unsigned 32-bit BFS
inode number. The adapter must check conversions to the host `off_t`, `ino_t`,
and `blkcnt_t`; an unrepresentable value is `EOVERFLOW`.

v2 timestamps use the Amiga DateStamp epoch: 1978-01-01 00:00:00 UTC, with
days, minutes, and 1/50-second ticks. M5 exposes the stored modification time
rounded down to seconds and exposes the creation time where the FUSE ABI has a
field for it. A zero DateStamp is an unknown timestamp and is reported as zero,
not as the Unix epoch. M6 does not set timestamps through FUSE. Timestamp,
ownership, and generic mode changes return `EOPNOTSUPP`; a successful operation
never claims their persistence.

The Amiga `protection` word is not POSIX mode bits. Read-only mounts publish
conservative modes (`0555` for directories and executable files, `0444` for
other files). An M6 writable mount maps its Read, Write, and Execute deny bits
to the corresponding permission bit for every POSIX owner/group/other class.
Directory write permission additionally requires the Amiga Delete bit, so
create and remove do not bypass it. This is an adapter access policy, not a
round-trippable Unix mode. `st_uid` and `st_gid` are the stored unsigned 16-bit
values. The following xattrs expose the original metadata without inventing a
POSIX interpretation:

| Xattr | Value | Missing result |
| --- | --- | --- |
| `user.bfs.comment` | Stored comment bytes | `ENODATA` |
| `user.bfs.protection` | Eight uppercase hexadecimal ASCII digits | Never missing |
| `user.bfs.uid` | Unsigned decimal ASCII | Never missing |
| `user.bfs.gid` | Unsigned decimal ASCII | Never missing |
| `user.bfs.create_datestamp` | `days:minutes:ticks` decimal ASCII | Never missing |
| `user.bfs.modify_datestamp` | `days:minutes:ticks` decimal ASCII | Never missing |

The FUSE mount uses `default_permissions`, `nodev`, and `nosuid`. On a writable
M6 live mount, only `user.bfs.comment` can be created, replaced, or removed;
empty values, embedded NULs, and values longer than 79 bytes return `EINVAL`.
The other listed
xattrs, ACLs, extended attributes outside this list, ownership changes,
timestamp changes, and permission changes return `EOPNOTSUPP`. Every metadata
mutation on a default or snapshot mount returns `EROFS`.

## Links and snapshots

`readlink` returns the stored non-NUL target bytes unchanged. Linux then applies
its normal symlink resolution rules. A BFS DOS volume name such as `Work:dir`
is not translated into a Linux mount or another BFS volume; cross-volume
resolution is unsupported. A target containing NUL is corrupt for the FUSE
surface and `readlink` returns `EIO`.

M5 and M6 choose a snapshot only with an explicit command-line selector. Selection
by name must reject names whose stored snapshot state is deletion-in-progress;
selection by identifier must do the same. A selected snapshot is always
read-only and its root does not move. A live mount is read-only by default; M6
enables mutation only with `--read-write` and rejects that option together with
either snapshot selector.

## Operation and error contract

The default mount implements `lookup`, `getattr`, `readdir`, `open`, `read`,
`release`, `readlink`, `statfs`, `getxattr`, and `listxattr`; it returns
`EROFS` for every mutation. The `--read-write` live mount additionally supports
regular-file create/mknod, write, atomic `O_APPEND` writes, truncate, mkdir,
rmdir, unlink, non-directory replacement rename, hard links, symbolic links,
comment xattr mutation, `flush`, file sync, and directory sync. `flush` and
`release` only retire descriptor state; `fsync` and `fsyncdir` call the core's
durable transaction barrier. Snapshots remain immutable. `fallocate`, special
files, permission/ownership/timestamp changes, generic xattrs, and nonzero
rename flags return `EOPNOTSUPP` on the writable mount. It returns `ENOSYS`
only for FUSE protocol operations that libfuse can safely suppress; unsupported
visible filesystem operations use a specific errno.

The writable mount uses libfuse `default_permissions`. Linux may therefore
reject an ownership-sensitive operation with `EPERM` or `EACCES` before the
adapter receives it; requests that reach the adapter receive `EOPNOTSUPP` for
the unsupported metadata operations above.

Directory cookies are stable and strictly increasing while the directory is
unchanged. A namespace mutation of that directory invalidates previously
returned cookies; callers must restart enumeration at offset zero. This is the
defined readdir contract for the serialized M6 dispatcher and avoids claiming
a snapshot that the on-disk directory index does not provide.

Core-to-POSIX error mapping is fixed: not found is `ENOENT`, duplicate is
`EEXIST`, invalid input is `EINVAL`, not a directory is `ENOTDIR`, a directory
where a file is required is `EISDIR`, no space is `ENOSPC`, size overflow is
`EOVERFLOW`, permission denial is `EACCES`, corrupted or checksum-invalid
media is `EIO`, and all otherwise unmapped I/O failures are `EIO`. Cancellation
may return `EINTR` only before the request has published a reply.

## Writable-v2 recovery contract

M6 keeps the v2 byte layout frozen. It uses no option bit, padding, new record,
or new inode type. A final-link unlink or a replacement of an open file changes
the displaced non-directory inode's existing `link_count` to zero and removes
its directory name as one mutable namespace state. Existing normal readers
already reject zero-link inodes from namespace operations; the handle-aware
core reader is the sole access path while an adapter has retained the handle.

```text
open inode 42 -> remove final name -> inode 42 has link_count 0 -> last close reclaims
                                  |                                  |
                                  +---- crash ---- writable mount reclaims
```

This marker has bounded recovery behavior: it is never reachable by lookup;
the next writable mount reclaims it before service; a read-only mount does not
modify or reveal it; and offline ownership inspection includes its extents.
The Linux adapter uses it only after it has recorded at least one open handle.
Ordinary Amiga operations continue to use immediate deletion and never create
the marker. M6 qualification must still demonstrate that the pinned baseline
handler accepts the committed post-operation v2 image and that restart recovery
does not publish the unnamed inode.
