# Filesystem failure semantics

The normative byte layout is [BFS v3 on-disk format](on-disk-format.md). This
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

A zero-link inode is an intentional, recoverable state, not a public
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

The AmigaOS handler commits delayed changes from a timer: within about one
second of a change, and before it replies to a flush, inhibit, write
protection, check, snapshot, format or shutdown packet. A successful AmigaDOS
`Close` is therefore not yet durable; a crash before the next commit loses the
changes since the last one and recovers the previous committed state. The
Mountlist option `COMMIT=SYNC` commits at every close and standalone metadata
operation instead. A failed timed commit is retried about once per second and
reported by the next flush; if the core latched a recovery error, later
mutating packets fail as well.

A writable mount synchronizes the selected readable state before exposing
allocation or running mount-time recovery. This also covers a newer valid
superblock left only in volatile device storage by a failed publication flush.
A failed mount barrier leaves the filesystem unmounted. Read-only inspection
does not perform this synchronization or make a durability assertion.

A failed namespace rollback latches a recovery error: removing the device fault
does not make the partial namespace committable. Abandon/remount is required.

A B-tree node that the live transaction allocated, and that no committed
superblock or snapshot can reference, is rewritten in place instead of being
copied again. Such rewrites are published only after every allocation and
every other fallible step of the mutation succeeded, so an earlier failure
leaves the node unchanged. A device write failure while publishing them makes
the transaction's graph uncertain: it is latched like an ownership-uncertain
reclamation and the caller recovers the latest committed roots. Committed
nodes are never rewritten in place; whether a node is owned is decided from an
in-memory record of the transaction's own allocations, not from the node's
on-disk transaction id. With the Amiga handler's node cache, owned nodes are
not written at every change: the cache keeps them dirty, at most half of its
slots, and the commit writes every dirty node before its metadata fence and
superblock. A dirty node may be written earlier to make room, which is safe
because no committed state references it. A crash before the commit loses
only the uncommitted transaction; a failed write of a dirty node fails the
commit or the operation that needed the room, and the node stays dirty until
recovery discards the transaction.
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

Eligible no-snapshot, single-leaf commits settle deferred frees in a private
replacement leaf before publication. Allocation stays frozen across a successful
metadata fence, the superblock write and its successful flush. Thus a failed
publication flush may persist only the SB without exposing a missing COW graph.
Errors keep the mounted owner recovery-latched; no retired block is reused before
successful publication. Other shapes retain the established post-publication
reclamation path and its leak-repair behavior.

Deferred frees are not a persistent reclamation journal. A crash between root
publication and reclamation can leave allocated, unreachable blocks; offline
`bfs check IMAGE --repair` is needed to rebuild free-space accounting. A
read-only `bfs check` run does not commit metadata or resume unfinished snapshot deletion. An image
with unfinished deletion requires a writable recovery pass before it can be
fully checked. Checking an image that is mounted elsewhere is unsupported.

Host fault injection and emulator tests do not establish physical Apollo 68080
hardware qualification, storage-device flush correctness or absence of all bugs.
The bounded execution and evidence design for broader fault/corruption work is
in [fault and corruption qualification](plans/fault-corruption-qualification.md).
The executable direct-image campaign and its verification procedure are in
[fault and corruption qualification](qualification/fault-corruption.md).
Neither changes these failure semantics or makes a physical-storage claim.
