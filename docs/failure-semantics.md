# Filesystem failure semantics

BFS uses copy-on-write metadata and an alternating superblock commit boundary.
Successful file writes update the inode before returning, including short writes.
They are not durable until a successful sync. With ordered data enabled, the
block device must honor flush ordering. Ordinary unshared data blocks may be
overwritten in place: neither COW metadata nor ordered mode makes those data
overwrites atomic after a power failure. Shared snapshot data and checksummed
data use copy-on-write.

Multi-step recovery reloads the latest valid committed roots and invalidates
existing file handles. Reopen handles after an operation reports recovery failure;
an old handle returns an I/O error rather than accessing reclaimed blocks. If
reload itself fails, mutations, sync and file operations reject further use until
the filesystem is abandoned or unmounted and mounted again. An error from a
commit does not prove that no part of the commit reached storage.

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
