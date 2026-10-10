# Namespace and Metadata Conventions

This chapter records the persistent namespace conventions built on the
directory and inode trees. It is normative for v3 readers and writers; platform
path syntax and permission translation remain adapter policy.

## Root and directories

The root inode is always 1. A formatted volume creates exactly this root entry:

```text
directory key: owner = 0, kind = 0, name_hash = hash of "/", ordinal = 0, part = 0
directory value: inode_nr = 1, entry_type = BFS_INODE_DIR, name_len = 1, name = "/"
inode key: 1
inode value: inode_nr = 1, type = BFS_INODE_DIR, link_count = 1
```

Owner 0 holds no other record. Every directory other than the root has exactly
one directory entry and exactly one parent link, and the link names the
directory that holds the entry. Following parent links from any directory
reaches the root. Entries are owned by directories only, and an entry's
`entry_type` equals the type of the inode it names. The root has neither a parent link nor an
entry other than the root record. No entry is named `.` or `..`; neither has a
persisted record. A directory is empty when it owns no entry; its parent link
and its comment do not count.

## Names

A directory-entry name is length-delimited, not NUL-delimited. Its maximum is
255 bytes. An adapter may refuse new names below that maximum; the current
adapters refuse names above 107 bytes, which is what an AmigaDOS
`FileInfoBlock` holds, unless the mount allows long names. Existing names of
any valid length stay readable. The public core rejects empty names and names containing `/` or `:`;
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
international folding operates on single bytes. A case-only rename preserves
the inode and updates the stored original spelling atomically; it does not
create a second case alias.

## Files, links, and comments

Regular files carry their content through their inode's inline extent or
extent tree. A soft link is inode type 2 whose target path is stored as normal
file content, with the inode size giving the target length. It has no separate
target record.

Hard links use more than one normal directory entry for a type-0 file inode and
increment its `link_count`. Inode type 3 is recognized by the core but is not
created by the current hard-link implementation. A writer should not introduce
type 3 without a separately specified compatibility rule.

`link_count == 0` is reserved for a non-directory inode whose final namespace
name was removed while a POSIX adapter retained an open handle. It is not a
directory entry, not a new inode type, and not a separate orphan-list record.
Only the handle-aware core path can read or update it. The last close reclaims
the inode and its extents; writable mount recovery reclaims any such inode
left by a crashed adapter. Readers must neither expose it nor treat it as an
ordinary positive-link inode, but checkers must include its extents in
ownership accounting until recovery runs.

Each inode can have at most one current Amiga file comment of 1 through 79
bytes. It is stored in the comment records the inode owns in the directory
tree (B+tree chapter). A zero-length comment removes them. Comments are
namespace metadata rather than bytes in the inode value.

The inode flag `HAS_COMMENT` is set exactly when the inode owns a comment. A
writer changes the records and the flag in the same committed state. Readers
treat the flag as authoritative: without it they report no comment and do not
search the directory tree, and deleting the inode leaves no comment behind. A
set flag without a comment, a comment without the flag, or a comment whose
inode does not exist is corruption, which a checker reports. Setting a comment
is the exception on the writer side: the current writer looks up the records
itself and replaces a stray comment or clears a stray flag instead of failing.

## Metadata interpretation

`protection` is the raw Amiga protection bitmap. `uid` and `gid` are raw
16-bit Amiga owner fields. BFS v3 does not persist POSIX mode bits, ACLs,
nanosecond timestamps, generic xattrs, or device numbers. It has no separate
Unix orphan-list structure; the constrained zero-link state above is its
complete retained-open-file representation.

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
