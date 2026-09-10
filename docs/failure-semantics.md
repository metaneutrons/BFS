# Filesystem failure semantics

The normative byte layout is [BFS v2 on-disk format](on-disk-format.md). This
document defines operation and recovery behavior, not an alternate layout.

BFS uses copy-on-write metadata and an alternating superblock commit boundary.
Successful file writes update the inode before returning, including short writes.
They are not durable until a successful sync. With ordered data enabled, the
block device must honor flush ordering. Ordinary unshared data blocks may be
overwritten in place: neither COW metadata nor ordered mode makes those data
overwrites atomic after a power failure. Shared snapshot data and checksummed
data use copy-on-write.

Public file operations refresh inode size and extent roots under the filesystem
lock, so multiple handles observe each other's completed writes/truncations
while retaining independent positions. Direct struct fields are cached values.
The ordinary `bfs_fs_delete_file()` operation reclaims the final-link inode and
invalidates old handles. A POSIX adapter that has an open final-link handle uses
`bfs_fs_unlink_open_file()` instead: it removes the namespace entry and commits
the inode with `link_count == 0`. Only handles explicitly marked through
`bfs_file_mark_unlinked()` may then read, write, truncate, sync, or report the
inode with zero links. The adapter reclaims that inode after the last retained
handle closes. No new lookup or normal file open can reach it.

A zero-link inode is an intentional, recoverable v2 state, not a public
namespace object. A writable mount reclaims every such non-directory inode
before exposing the live namespace; a read-only mount preserves it and must
not expose it. Thus a crash after an unlink or replacement rename may discard
the unnamed file's later handle writes, but cannot make it reachable again or
reuse its blocks while it remains recorded. Offline readers and checkers must
retain its extent ownership until a writable recovery pass reclaims it.

Partial writes and truncation validate checksummed old data before retaining any
bytes. A mismatched CRC rejects the operation without generating a new checksum
for corrupt contents. A complete block replacement does not retain old bytes.

Multi-step recovery reloads the latest valid committed roots and invalidates
existing file handles. Reopen handles after an operation reports recovery failure;
an old handle returns an I/O error rather than accessing reclaimed blocks. If
reload itself fails, mutations, sync and file operations reject further use until
the filesystem is abandoned or unmounted and mounted again. An error from a
commit does not prove that no part of the commit reached storage.

A failed namespace rollback latches a recovery error: removing the device fault
does not make the partial namespace committable. Abandon/remount is required.
Failed extent-remap rollback marks block ownership uncertain so the file layer
recovers committed roots rather than freeing a possibly referenced replacement.

Truncation may commit intermediate batches. On failure, already removed ranges
can read as holes, but the inode must not reference reclaimed extent-tree nodes.
Shrinking clears the retained block's tail through the normal snapshot-aware
writer so regrowth cannot expose bytes beyond the truncation point.

File deletion validates its complete extent graph before removing the namespace
entry. Snapshot deletion validates its graph before marking the snapshot as
deleting; it commits each inode's reference updates together with a progress
cursor. Interrupted deletion resumes on mount. Large atomic reclaim units
reserve memory beyond the inline 16,384-entry queue before changing references.
These operations require memory proportional to the largest reclaim unit and
can fail with an allocation error. They do not impose a fixed 64 MiB file limit.

Deferred frees are not a persistent reclamation journal. A crash between root
publication and reclamation can leave allocated, unreachable blocks; offline
`bfsfsck --fix` is needed to rebuild free-space accounting. A read-only `bfsfsck`
run does not commit metadata or resume unfinished snapshot deletion. An image
with unfinished deletion requires a writable recovery pass before it can be
fully checked. Checking an image that is mounted elsewhere is unsupported.

Host fault injection and emulator tests do not establish physical Apollo 68080
hardware qualification, storage-device flush correctness or absence of all bugs.
