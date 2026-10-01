# BFS inode timestamp coalescing

Decision state: design for a bounded follow-on experiment, not implemented or
accepted. The cache lookup experiment is still being measured. The goal remains
at most five times PFS3 elapsed time in every checked AmigaDOS workload with
unchanged durability, integrity, snapshot and recovery guarantees. This design
does not predict its speedup or solve the remaining delete workload by itself.

## Redundant metadata mutations

A successful nonempty Amiga ACTION_WRITE reads an inode for protection, refreshes
the file handle, reads and writes its final size and extent root in file.c, then
reads and writes it again in MarkFileChanged for modification time and the
archive bit. These are four inode reads and two inode writes in the normal
caller/core path; stale-handle reopening and intermediate commits can add more.
The 8 MiB benchmark makes 128 DOS writes of 64 KiB, not 2048 DOS writes.

Creating an Amiga file similarly inserts an initially zero-timestamp inode and
then updates its creation and modification dates with another COW mutation.
Linux FUSE currently uses the zero-timestamp create and ordinary write APIs.
Its behavior must not change merely because Amiga supplies timestamp metadata.

## Common core API boundaries

Use a portable three-field host-order timestamp and a non-failing provider
callback. The provider may sample a platform clock but must not reenter BFS,
mutate its BIO/cache or perform filesystem operations. It fully initializes
the timestamp. No inode number, type, links, size, extent root, UID/GID or cached
permission value is supplied by the caller.

A create-with-stamp entry point samples once after parent validation and inode
number reservation, immediately before the first inode insert. Apply that
stamp to creation and modification fields in the zeroed new inode. Keep the
original create API as a NULL-provider wrapper with unchanged behavior.

A write-with-stamp entry point carries the provider/context and a protection
bit-clear mask only for this operation, not in a persistent file handle. Thread
them into finalization. After positive progress, the existing final inode
update reads the fresh inode and overlays its size, extent root, modification
stamp and the requested clear mask in the same COW write. Intermediate commits
retain their existing inode updates and do not sample the provider. Zero-byte
operations and errors without positive progress do not sample or apply it.
Existing write/unlocked/append APIs use NULL and a zero mask.

One common inode helper encodes the stamp fields. Amiga DateStamp sampling and
the archive-bit choice remain frontend policy. Amiga protection checking stays
fresh and unchanged; it is not folded into a cached inode. Dirty/notification
bookkeeping remains in the handler. Linux callers keep the legacy wrappers.

Sampling moves from after a redundant core mutation to immediately before the
necessary inode publication. Creation therefore records creation-start rather
than the later second update; final write records completion immediately before
publication, not after the first inode write. This timing difference must be
documented and checked on Amiga. The on-disk date representation and 16-bit
conversion remain unchanged.

## Ownership and failure behavior

No old-root-reachable node is overwritten. Keep every CRC, structural check,
COW allocation, reclamation guard, commit point, fence and flush. The existing
finalization path must reject or recover an uncertain transaction before
applying the optional metadata. An inode-update failure follows its current
reload-committed path and returns an error, never success with uncertain
metadata. Partial positive writes receive a final stamp only on successful
inode publication. Snapshot and read-only handles reject mutation before the
provider runs. The callback adds no allocation or fallible post-write step.

The eliminated second timestamp COW has no independent failure point in the
new path. This is not permission to weaken the data/graph/recovery oracles or
omit testing failures of the remaining combined publication.

## Qualification and decision

Add focused tests for exact date encoding; NULL-wrapper equivalence; one
provider call on create and positive/partial writes; no calls on zero progress,
invalid/read-only/snapshot requests; field-limited updates of a freshly changed
inode; remount and snapshot preservation; and COW/inode-publication failure
recovery. Check open-unlinked inode writes if the new API supports them. Retain
all existing fault tests, including intermediate commit and recovery-generation
cases. A source-count reduction is not evidence of elapsed-time improvement.

Independently review implementation and test oracles. Run complete normal and
ASan/UBSan host suites, local quality/static checks, normal/probe m68k builds
and actual Linux FUSE tests. Then compare four fresh normal runs per revision,
both filesystem orders, identical guest/formatter/PFS3/geometry. Persist every
raw observation, adverse matched case, ratio above five and input identity.
Accept or reject from that evidence; the overall goal stays open until every
checked workload meets its threshold. No CI, GitHub issue, push, PR, merge,
release or real-device qualification is part of this local increment.
