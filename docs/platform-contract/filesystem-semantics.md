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
the BFS sense. FUSE entry and attribute timeouts are zero in M5; caching a
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
not as the Unix epoch. M6 may set a timestamp only if it can be represented
exactly as a valid DateStamp; otherwise it returns `EOVERFLOW`.

The Amiga `protection` word is not POSIX mode bits. M5 publishes conservative
read-only modes (`0555` for directories and executable files, `0444` for other
files) and does not claim POSIX permission enforcement. `st_uid` and `st_gid`
are the stored unsigned 16-bit values. The following read-only xattrs expose
the original metadata without inventing a POSIX interpretation:

| Xattr | Value | Missing result |
| --- | --- | --- |
| `user.bfs.comment` | Stored comment bytes | `ENODATA` |
| `user.bfs.protection` | Eight uppercase hexadecimal ASCII digits | Never missing |
| `user.bfs.uid` | Unsigned decimal ASCII | Never missing |
| `user.bfs.gid` | Unsigned decimal ASCII | Never missing |
| `user.bfs.create_datestamp` | `days:minutes:ticks` decimal ASCII | Never missing |
| `user.bfs.modify_datestamp` | `days:minutes:ticks` decimal ASCII | Never missing |

The FUSE mount uses `default_permissions`, `nodev`, and `nosuid`. M5 exposes no
write or security xattrs. `setxattr`, `removexattr`, ACLs, extended attributes
outside this list, ownership changes, and permission changes return `EROFS`.

## Links and snapshots

`readlink` returns the stored non-NUL target bytes unchanged. Linux then applies
its normal symlink resolution rules. A BFS DOS volume name such as `Work:dir`
is not translated into a Linux mount or another BFS volume; cross-volume
resolution is unsupported. A target containing NUL is corrupt for the FUSE
surface and `readlink` returns `EIO`.

M5 chooses a snapshot only with an explicit command-line selector. Selection
by name must reject names whose stored snapshot state is deletion-in-progress;
selection by identifier must do the same. A selected snapshot is always
read-only and its root does not move. Live mounts are read-only in M5 as well.

## Operation and error contract

M5 implements only `lookup`, `getattr`, `readdir`, `open`, `read`, `release`,
`readlink`, `statfs`, `getxattr`, and `listxattr`. It returns `EROFS` for all
mutations, including create, mkdir, unlink, rmdir, rename, link, symlink,
write, truncate, setattr, fallocate, and every xattr mutation. It returns
`ENOSYS` only for FUSE protocol operations that libfuse can safely suppress;
unsupported visible filesystem operations use a specific errno.

Core-to-POSIX error mapping is fixed: not found is `ENOENT`, duplicate is
`EEXIST`, invalid input is `EINVAL`, not a directory is `ENOTDIR`, a directory
where a file is required is `EISDIR`, no space is `ENOSPC`, size overflow is
`EOVERFLOW`, permission denial is `EACCES`, corrupted or checksum-invalid
media is `EIO`, and all otherwise unmapped I/O failures are `EIO`. Cancellation
may return `EINTR` only before the request has published a reply.

## Writable-v2 gate

The present v2 core does not provide the semantics required for a POSIX
writable mount. This is a deliberate M2 decision, not a deferred bug.

For an open file with a single link, current deletion removes the directory
entry, deletes the inode, and queues its extents for reclamation in one
operation. A still-open FUSE file handle would reference reclaimed state:

```text
open inode 42 -> unlink last name -> inode 42 deleted -> commit/reclaim
      |                                                          |
      +-------------------- later read/write -------------------+
                              unsafe
```

Retaining a zero-link inode would change the v2 recovery contract. An older
driver has no durable orphan list or cleanup rule and can leak or expose that
inode after a restart. Reusing a reserved field or option bit would also violate
the frozen v2 format.

Current rename inserts the destination entry first and rejects an existing
destination. POSIX replacement rename needs one atomic namespace transition:

```text
required: old/name + new/name -> new/name refers to old inode; old target gone
v2 today: insert new/name -> EEXIST, or remove target first -> crash window
```

Removing the target first creates a crash state in which neither the old nor
new name is the required durable result. Inserting first cannot replace. A
sequence of core calls cannot repair this with a transaction wrapper because
the needed primitive and recovery state do not exist.

M6 must not start or be accepted until an on-disk-format decision defines an
orphan/recovery record, replacement-rename transaction semantics, legacy
driver behaviour, upgrade and downgrade policy, and crash tests for every
publication point. A v3 format is the expected route. M5 is unaffected because
it opens only read-only state and never mutates the namespace.
