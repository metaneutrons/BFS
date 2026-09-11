# Commit, Recovery, and Checker Boundaries

BFS v2 publishes metadata through copy-on-write trees and an alternating
superblock boundary. This chapter defines the resulting observable committed
states and the limits of that design.

## Commit sequence

The mounted transaction starts from the selected committed superblock and works
on a next transaction ID. Mutating a B+tree writes new nodes, changes in-memory
tree roots, and retains older nodes until publication. A normal commit:

1. Requests a device sync first when `BFS_OPT_DATA_ORDERED` is set.
2. Returns unused allocator-reserve blocks to the free-space tree.
3. Copies current directory, inode, free-space, refcount, snapshot, and inode
   allocation state into the working superblock.
4. Writes and syncs the older superblock slot with that working state.
5. Reclaims deferred old blocks into the free-space tree, using refcounts if
   snapshots are active, and publishes the updated free-space root in later
   superblock commits.
6. Requests a final device sync.

`txn_id` consequently identifies a published metadata state, not a count of
user-visible operations. A single user operation may make intermediate commits
to keep the deferred-free queue bounded.

File data is written before the inode/extent metadata that makes a new block
reachable. With ordered data enabled, the pre-publication barrier requests that
data reach stable storage first. The guarantee depends on the block backend
implementing `sync` as a real ordering and durability barrier.

## Permitted power-loss outcomes

After a crash, recovery selects the greatest compatible, CRC-valid superblock
as specified in the superblock chapter. Any metadata blocks reachable from that
root define the selected committed state. A valid older copy remains usable if
a newer slot write was torn or otherwise invalid.

Copy-on-write metadata prevents the selected root from requiring a partially
rewritten old tree. It does not make all data updates atomic: unshared live
data can be overwritten in place. Snapshot-shared data and data written with
checksums use copy-on-write data blocks. Ordered mode does not change the
atomicity of an in-place overwrite.

A crash after root publication and before deferred reclamation can leave
allocated but unreachable blocks. That is space leakage, not an alternate
namespace. `bfs check IMAGE --repair` rebuilds free-space accounting. The
deferred-free queue itself is never written as a recovery journal.

## Snapshot deletion state machine

Snapshot creation first commits a clean live state, records the captured
directory and inode roots, increments graph references, inserts the snapshot
record, and commits it. Deletion first validates the graph, then converts the
record name to `.deleting_<id>` and sets its cursor to zero in a committed
transaction. Each subsequent committed unit advances the cursor only after the
corresponding inode's references have been reclaimed. The final unit removes
the record and updates snapshot/refcount roots together.

Mount scans for deletion-prefixed records and resumes them before returning a
mounted filesystem. It can therefore write and cannot serve as a strict
read-only mount lifecycle. A raw read-only checker must not resume deletion;
an image with a deletion record needs a writable recovery pass before it can be
fully cleaned.

## Failure and recovery limits

An I/O failure during commit does not prove that no bytes reached media. The
core latches a recovery error after a failed publication or reclaim operation;
the caller must abandon or remount rather than commit the uncertain in-memory
state. Reloading starts again from the selected committed superblock and
invalidates outstanding file handles.

The format gives no authenticity protection, RAID semantics, write-cache power
loss protection, concurrent-writer protocol, or guarantee that a host `fsync`
implementation reaches physical media. An image mounted for writing by more
than one external driver is unsupported.

## Checker responsibilities

A read-only checker may inspect the selected compatible superblock, all
reachable B+tree nodes, keys, record constraints, and extent ranges. It MUST
not choose an alternative state from a lower transaction ID merely because a
newer valid copy looks unfamiliar. An intact unsupported copy requires a
version-aware tool and must not be modified by a v2 repair pass.

A repairing checker reconstructs allocation ownership from the selected roots,
reserves fixed regions and the emergency pool, and then rebuilds free-space
accounting. It should report, rather than guess through, conflicting ownership,
malformed record ordering, invalid reference counts, and incomplete snapshot
deletion. The current source does not define an on-disk repair journal; a repair
tool needs its own interruption-safe protocol before it claims crash-safe
repair.
