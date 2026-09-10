# Cross-Platform Filesystem Contract

Status: accepted for Milestone M2.

This contract defines the boundaries for the portable BFS work. It is a
binding design input for M3 through M6, not an implementation plan. The
on-disk format remains BFS v2 as specified in [the format specification]
(on-disk-format.md). No v2 padding, reserved field, option bit, or legacy
field may be repurposed to satisfy a host-platform API.

The design has five participants:

| Participant | Responsibility | Must not own |
| --- | --- | --- |
| `src/core` / `libbfs` | Format validation, namespace, file, transaction, and snapshot semantics | Host descriptors, FUSE requests, OS packet structures |
| `src/host` | POSIX storage transport and explicit device safety policy | Format decisions or test-only fault injection |
| `src/fuse` | Linux FUSE protocol adaptation and request lifetime | Direct block I/O or duplicated filesystem logic |
| `src/amiga` | AmigaDOS packet adaptation and Amiga device transport | Host POSIX or FUSE policy |
| `src/aros` and `src/morphos` | Native platform adapters when their target contracts are available | Changes to v2 meaning |

The build must produce `libbfs` from `src/core` without `tests/` or a platform
adapter. Test backends remain test-only. Production POSIX I/O is a separate
library or executable-local module in `src/host`.

The remaining documents are normative:

* [Filesystem semantics](platform-contract/filesystem-semantics.md) defines
  names, metadata, operation results, snapshots, and the v2 write and recovery
  contract.
* [Lifecycle and storage](platform-contract/lifecycle-and-storage.md) defines
  public-core ownership, mount safety, I/O, locking, and cache policy.
* [Capabilities](platform-contract/capabilities.md) assigns stable capability
  identifiers and required test evidence.
* [Target matrix](platform-contract/targets.md) pins the supported execution
  environments and records external blockers.

## Acceptance rule

M3 through M6 use the documented v2 zero-link recovery contract for final-link
open-unlink and replacement rename. It adds no field, option, or format
version: a non-directory inode with `link_count == 0` is retained only for an
open POSIX handle and reclaimed on writable mount recovery. M6 acceptance still
requires pinned-baseline Amiga interoperability and interrupted-state evidence.
A portability adapter must fail closed when a required capability is absent; it
must not silently emulate a different durability model.
