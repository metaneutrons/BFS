# Namespace and Metadata Conventions

This chapter records the persistent namespace conventions built on the
directory and inode trees. It is normative for v2 readers and writers; platform
path syntax and permission translation remain adapter policy.

## Root and directories

The root inode is always 1. A formatted volume creates exactly this root entry:

```text
directory key: parent_id = 0, name = "/"
directory value: inode_nr = 1, entry_type = BFS_INODE_DIR
inode key: 1
inode value: inode_nr = 1, type = BFS_INODE_DIR, link_count = 1
```

Every non-root directory has one internal `..` directory entry whose
`parent_id` is the directory's inode and whose value identifies its parent.
Root has no required `..` entry. `.` has no persisted entry. Public namespace
operations reject `.` and `..`, but a reader must recognize the internal `..`
record to walk parent relationships and to determine whether a directory is
empty.

## Names

A directory-key name is length-delimited, not NUL-delimited. Its maximum is
255 bytes. The public core rejects empty names and names containing `/` or `:`;
it also reserves `.` and `..`. The formatting path applies the same `/` and `:`
restriction to the volume label.

Names are stored in their original byte case but compared with the byte folding
defined in the B+tree chapter. `name_len` is authoritative even if a stored
name contains a zero byte. Current low-level core calls do not reject such a
byte, but AmigaDOS path interfaces cannot reliably express it. Portable tools
and future adapters should reject it at their public boundary until M2 defines
an explicit escaping policy; they must not truncate a name at it while reading
raw metadata.

The format has no UTF-8 marker or Unicode normalization. The documented
international folding operates on single bytes. A case-only rename is a no-op
in the current core because the old and new names compare equal; it preserves
the existing original spelling.

## Files, links, and comments

Regular files carry their content through the per-inode extent tree. A soft
link is inode type 2 whose target path is stored as normal file content, with
the inode size giving the target length. It has no separate target record.

Hard links use more than one normal directory entry for a type-0 file inode and
increment its `link_count`. Inode type 3 is recognized by the core but is not
created by the current hard-link implementation. A writer should not introduce
type 3 without a separately specified compatibility rule.

Each inode can have at most one current Amiga file comment. A comment is stored
as a hidden directory-tree entry:

```text
parent_id = inode_nr | 0x80000000
name      = comment bytes, 1 through 79 bytes
value     = inode_nr, entry_type = 0
```

No public inode number sets bit 31, so this parent ID is distinct from a real
directory. A zero-length comment removes the hidden entry. A reader should not
display this internal parent as a filesystem directory. Comments are namespace
metadata rather than bytes in the inode value.

## Metadata interpretation

`protection` is the raw Amiga protection bitmap. `uid` and `gid` are raw
16-bit Amiga owner fields. BFS v2 does not persist POSIX mode bits, ACLs,
nanosecond timestamps, xattrs, device numbers, or a Unix orphan list.

Creation and modification timestamps consist of the three 16-bit fields used
by Amiga `DateStamp`: days, minutes, and ticks. The core preserves these
fields and does not validate their calendar range. The Amiga handler populates
them on creation and updates the modification fields through its packet API.
Their interpretation outside that API must be specified by the target adapter.

## Allocation and ownership conventions

The free-space tree is authoritative for reusable blocks only after the
selected committed superblock has published its root. Current transaction COW
blocks that replace old metadata are held in memory until the committed root
switch makes the older blocks unreachable. The free tree must exclude:

- physical blocks below `ceil(4096 / block_size)`;
- the whole block containing backup slot B;
- blocks listed in the superblock emergency pool; and
- transient allocator reserve blocks while they are checked out.

The allocator's reserve and pending-free queue are runtime state, not a
persistent journal. A checker must reconstruct ownership from the selected
roots instead of treating a free-tree discrepancy after interrupted reclaim as
an alternate committed namespace.
