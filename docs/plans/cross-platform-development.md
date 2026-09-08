# BFS format specification and cross-platform development

Epic: https://github.com/metaneutrons/BFS/issues/35
Decision state: Accepted by the repository owner on 2026-09-08. Publication and releases remain subject to their independent acceptance gates.

## Outcome and baseline

Document the complete v2 disk format in `docs/on-disk-format.md`, expose the
existing filesystem core through a read/write Linux libfuse driver, deliver a
standalone conformance program, and develop native read/write AROS and MorphOS
handlers. The practical outcome is repeatable core and mounted-filesystem
testing without requiring an Amiga emulator for every iteration.

Baseline inspected on 2026-09-08: main commit
`431ead6159e5d4217f029ac2b6dd02a51db7d8a2`, release v0.1.3, format v2.
This is a source and tracking survey, not a new qualification run.

- `src/core/` is already built for host tests and the Amiga handler. There is
  no Linux filesystem mount adapter or explicit native AROS/MorphOS build.
- `tests/block_device_emu.c` is a test backend, not a production storage
  adapter: it uses shared stdio positions and its sync is `fflush`, not a
  stable-storage barrier. Production FUSE must not use it unchanged.
- `bfs_fs_mount()` resumes snapshot deletion and can write. Normal unmount
  commits. A genuinely read-only mount lifecycle is missing.
- Deleted files do not retain POSIX open-handle access. Existing rename is
  not an implementation of POSIX atomic replacement. These are requirements
  gaps for the new adapter, not evidence that the existing Amiga contract fails.
- `docs/format-compatibility.md` and `docs/failure-semantics.md` describe
  important contracts, but not the complete format. Several header comments
  contradict the implementation.
- `BFS_AMIGA` currently implies big-endian data, and the handler contains
  m68k register bindings and assembly startup. A new little-endian DOS target
  cannot simply reuse that compilation configuration.
- Host tests and the Amiga FULL46 suite already exist. They remain in use;
  this initiative adds interfaces and independent observations, not a replacement.

Existing tracking is retained:
[Apollo hardware #27](https://github.com/metaneutrons/BFS/issues/27),
[fault/corruption qualification #28](https://github.com/metaneutrons/BFS/issues/28),
[soak testing #29](https://github.com/metaneutrons/BFS/issues/29), and
[README PR #30](https://github.com/metaneutrons/BFS/pull/30).
Link relevant evidence to these records without duplicating or automatically
closing them. Historical `HANDOFF.md` statements are not current acceptance
evidence. Reconcile overlapping README work during delivery.

## Scope and architectural decisions

### One production core, separate platform adapters

Keep the existing C implementation and compile it into an internal `libbfs`
build target for host consumers. This does not promise a stable external
library ABI. Build the same core sources into Amiga, AROS and MorphOS handlers.
No copied filesystem core, no FUSE dependency in the core, and no m68k assembly
or Amiga runtime requirement in the Linux build.

Proposed boundaries, adjusted only to actual needs during review:

| Area | Responsibility |
| --- | --- |
| `src/core/`, `include/` | Format codecs, trees, files, transactions and platform-neutral lifecycle APIs |
| `src/host/` | Production POSIX image/block-device I/O and storage ownership |
| `src/fuse/` | libfuse 3 low-level inode-oriented adapter and Linux policy |
| `src/amiga/` | Existing classic handler, device adapter and m68k optimization |
| `src/aros/`, `src/morphos/` | Native SDK/ABI, startup, DOS/device integration and build glue |
| `tests/conformance/` | Shared scenario catalog, independent expected results and replay protocol |
| `tests/format/` | Independent byte fixtures and format inspection oracle |

Share demonstrably identical DOS-packet logic through focused helpers when
the first native port needs them. Do not copy `handler.c` into each target or
undertake a speculative rewrite of all platform interfaces. Preserve the
existing Amiga behavior through explicit policies where Linux differs.

Choose libfuse's low-level interface for explicit inode, lookup and handle
lifetime control. It is an adapter to the shared core, not an independently
implemented filesystem. Independence is provided by a separate test oracle
that neither includes BFS layout headers nor links its codecs/tree engine.
It inspects committed images and produces comparable namespace, metadata and
content manifests. It is not a second writer or an automatic repair tool.

### Compatibility and feature contract

Freeze format v2. No field-width changes, private interpretation of padding,
new option bits, silent migration, or platform-specific disk variants belong
in this initiative. Historical valid v2 images remain readable; newly written
volumes must remain interoperable with the recorded v0.1.3 baseline under its
documented supported operations. Older releases are not newly qualified.

M2 must resolve how Linux open-unlink and replacing-rename semantics work
without leaving persistent states that the existing Amiga driver cannot read
or safely recover. In-memory orphan handling alone is not a crash-recovery
design. Do not invent hidden persistent objects that older readers misinterpret.
If required semantics cannot be achieved under the v2 contract, stop acceptance
of the writable milestone and obtain an explicit revised decision. Do not
silently weaken semantics or retrofit a new format under version 2.

The Linux contract is a filesystem with documented Amiga naming and metadata
limits, not a claim of complete POSIX conformance. M2 defines each operation,
mapping, rejection and observable persistence property. At minimum address:

- Case-insensitive Amiga name ordering, original-case preservation, case-only
  rename, Latin-1/UTF-8 round trips, byte-length expansion, forbidden names,
  encoding failures and collision handling. Never truncate or replace bytes
  lossily. Define an unambiguous escape/rejection policy for names that cannot
  be represented directly in Linux directory components.
- UID/GID widths, protection-bit mapping, mount ownership/masks, execute bits,
  special-mode rejection, comments as explicitly named xattrs, and the limits
  of stored timestamps, their epoch, time-zone convention and resolution.
  Never report successful persistence of unsupported metadata.
- Symlink target syntax across DOS volume paths and POSIX paths, absolute and
  relative targets, hard-link identities, reserved/internal entries, sparse
  files, extent/address limits and checked signed `off_t` conversion.
- Open/create/exclusive/truncate/append, short I/O, concurrent handles,
  unlink-open lifetime, directory cursors, replacement rename and errors.
- Mount, remount, recovery, read-only refusal, unmount, abort, `flush`,
  `fsync`, `fdatasync`, directory sync, error latching and durability limits.

Use one capability catalog with stable IDs across core, FUSE and DOS surfaces.
Each target declares source-linked capabilities and reasoned differences in a
ledger. A test detects undeclared drift. The ledger describes full builds and
does not prove that conditional compilation enabled every capability.

Initial FUSE policy: foreground-testable, single-user, serialized requests,
`default_permissions`, `nodev`, `nosuid`, conservative caches, no writeback
cache, and no `allow_other`. Reject unsupported options rather than silently
enabling unqualified behavior. Cache invalidation must account for case aliases
and renamed/open inodes. Later concurrency/cache optimization is separate work.
Images or partitions must never have concurrent external writers.

Native ports use native DOS and device APIs, not FUSE. Proposed first AROS
target is non-SMP x86_64 to exercise pointer width and little-endian behavior;
M2 must confirm a reproducible SDK, ABI and runtime before this is accepted.
MorphOS targets native PowerPC with a pinned SDK and named OS minimum.
AROS ARM/Raspberry Pi, SMP, additional ABIs, and other CPUs require subsequent
target-specific qualification. Existing AROS ROM use in FS-UAE is m68k guest
integration evidence, not native x86_64 or MorphOS qualification.

### Non-goals

No Linux kernel module, macFUSE delivery, new on-disk version, 64-bit disk-address
migration, AMMX, network filesystem, full Unix ACL/device-node support, or
unqualified performance claims. Snapshot reading and interoperability are in
scope; new Linux snapshot-management commands are not. Unsupported optional
syscalls such as advanced `fallocate` modes or rename exchange must have
explicit negative tests and must not be falsely advertised.

## Tracking and delivery model

This versioned plan owns scope, dependencies and normative acceptance criteria.
One epic indexes ten ordinary milestone issues. Those issues own execution
stage, next action, blockers, PRs and evidence; they link here rather than copy
the acceptance checklists. Native GitHub Milestones/Projects are optional.
Issue links are established below. Update their execution records as work proceeds;
do not duplicate this plan's normative acceptance criteria in the issues.

Partial PRs use non-closing issue references. Use functionally grouped
Conventional Commits and focused PRs: specification, core behavior, host I/O,
tests, FUSE, individual native adapters, and delivery. Coupled code and regression
tests stay together. No omnibus platform PR or bypass of required CI.

Dependencies form the following acyclic delivery order:

```text
M1 specification + M2 contract/design
                  |
                  v
M3 host core/I/O -> M4 conformance harness -> M5 read-only FUSE
                                                    |
                                                    v
                                        M6 read/write FUSE
                                                    |
                                                    v
                                        M7 Linux qualification
                                             /             \
                                     M8 AROS            M9 MorphOS
                                             \             /
                                          M10 release qualification
```

M2 may investigate against a draft of M1, but cannot be accepted before M1.
Platform SDK/runtime investigations can start during M2. Port implementation
follows M7 to avoid stabilizing three new adapters simultaneously.

## Delivery and acceptance

### M1: Complete v2 format specification

Execution: https://github.com/metaneutrons/BFS/issues/36
Dependencies: None.

- M1-A1: `docs/on-disk-format.md` normatively defines the v2 recognition
  envelope, both superblock locations, selection/veto rules, geometry,
  absolute block addressing, reserved regions and all supported block sizes.
  Every stored field has offset, width, endianness, range, sentinel and meaning.
- M1-A2: Specify every tree's keys/values, comparator and capacity calculation,
  separate fixed-capacity key/value arrays, internal children, CRC algorithms
  and parameters, extent/checksum encoding, inode/link/comment representation,
  names and special entries, free-space/reserve/refcount ownership, snapshots
  and the persistent deletion cursor. Distinguish reserved, legacy and active
  fields and distinguish representation limits from qualified operational limits.
- M1-A3: Define valid committed states, ordered publication, barriers, permitted
  post-crash outcomes, recovery and fsck responsibilities, version refusal and
  future-format boundaries. Explain current limitations without presenting
  unimplemented safeguards as existing guarantees.
- M1-A4: Correct conflicting source/README comments and connect compatibility
  and failure-semantics documents without competing normative definitions.
  Add hand-reviewed byte fixtures for every record/tree-node type, CRC vectors
  and malformed cases, stored as text/generated from source rather than binaries.
  Fixture validation must not calculate its expected layout through production
  structs/helpers. Another developer can implement a reader from this document;
  discovered ambiguities are resolved before acceptance.

### M2: Approve platform and filesystem contracts

Execution: https://github.com/metaneutrons/BFS/issues/37
Dependencies: M1 for acceptance; investigation may start earlier.

- M2-A1: A reviewed design defines the feature/error/durability mapping above,
  public core ownership and locking boundaries, and the surface capability
  catalog. Linux-required behavior is distinguished from legacy DOS behavior.
  All decisions needed to implement M3-M6 are resolved, not left as TODOs.
- M2-A2: Open-unlink, replacement rename, restart and old-driver interoperability
  have a bounded, evidenced feasibility assessment under unchanged v2. Include
  crash-state diagrams and isolated counterexamples. Acceptance requires a
  workable design, not merely a successful normal-path prototype.
- M2-A3: Record Linux minimum distribution/kernel/libfuse versions and x86_64
  qualification target, macOS host-test scope, exact AROS architecture/ABI/SDK
  and MorphOS CPU/SDK/OS minimum. Pin build inputs and establish usable runtime
  and hardware access without committing SDK binaries or accepting licenses
  on behalf of the owner. Unavailable runtime access is an explicit blocker.
- M2-A4: Document baseline permissions, mount safety, cache and threading policy,
  signed/unsigned width conversions, endian/alignment requirements, and the
  treatment of every unsupported optional operation. No broad platform-support
  claim follows merely from compiling a target.

### M3: Production host I/O and safe core lifecycle

Execution: https://github.com/metaneutrons/BFS/issues/38
Dependencies: M1, M2.

- M3-A1: Host tools and tests can link the common core build target without
  depending on `tests/` storage code or Amiga headers. Changes to public core
  operations preserve existing Amiga contracts and pass focused regressions.
- M3-A2: Production I/O uses checked positional reads/writes, 64-bit-safe offsets,
  short-I/O/EINTR handling, real stable-storage synchronization, geometry and
  partition-bound checks. Support explicit image subranges and Linux partition
  devices; partition-table discovery is not required. A mount never formats.
- M3-A3: Storage ownership refuses conflicting mounts/checkers, aliases and
  unsuitable devices within a documented enforceable policy. Advisory locks
  alone are not protection against arbitrary external writers. Destructive tests
  accept only harness-created disposable images or loop devices; the driver
  requires explicit user selection of any real partition.
- M3-A4: Read-only mode is enforced from lifecycle to an `O_RDONLY` backend.
  It does not resume deletion, repair, update access times or commit at unmount.
  Write-attempt instrumentation stays at zero and full image bytes stay equal
  on normal and failure paths. A valid live root with pending deletion remains
  readable without resuming it; invalid or incompatible media fail safely.
- M3-A5: Fault probes exercise read, write, sync, close, allocation and geometry
  errors. Report partial/uncertain commits accurately. Test zero-write refusal,
  stable-storage barrier calls and leak-free cleanup with valid counter-probes.

### M4: Standalone conformance program and independent oracle

Execution: https://github.com/metaneutrons/BFS/issues/39
Dependencies: M1, M2, M3.

- M4-A1: Deliver the `bfs-conformance` suite with one deterministic scenario
  catalog and separate core/POSIX backend executables. Only the core executable
  links `libbfs`; the POSIX executable uses mounted-path OS calls. Design the
  separately built DOS adapter now; native target execution is accepted in
  M8/M9. Preserve matching existing Amiga test IDs; do not replace FULL46.
- M4-A2: Direct mode exercises the production host backend without FUSE. Mounted
  executable invokes OS APIs and links no BFS implementation. Expected results come
  from an independent model and explicit per-surface contracts, not from reading
  the same internal structs as the implementation. Prove independence with
  build/link checks and a deliberately linked counter-probe.
- M4-A3: Deliver a versioned text replay format, deterministic seeds, case
  selection, bounded runtime/resource controls, structured JSON results,
  distinct pass/fail/skip/error outcomes and useful exit codes. Record seed,
  scenario/contract version, code, binary, input and environment identities.
  Failed prerequisites, empty selection, missing completion, timeout or cleanup
  failure cannot produce a pass. Test the reporter with deliberately bad runs.
- M4-A4: Add the independent format oracle: decode fixture and committed images,
  walk bounded trees with cycle/range/CRC checks, export normalized namespace,
  metadata and content digests, and reject incompatible formats without writes.
  Share fixtures and semantic manifests, not production decoding algorithms.
- M4-A5: Cover empty and large files, sparse/range boundaries, multi-block I/O,
  directory scale, links, names/encoding, metadata, snapshots, disk-full,
  remount and errors. Classify POSIX-only cases explicitly. Harness acceptance
  uses its reference fixtures/model and existing supported core operations;
  new FUSE semantics are not claimed to pass before M5/M6 exist.
- M4-A6: Tests use owned temporary roots and verify the target before mutation;
  no arbitrary path recursion or formatting real media. Fault adapters separate
  acknowledged writes from persisted writes and model failed/reordered/torn
  persistence. A daemon kill alone is not called simulated power loss.

### M5: Read-only libfuse mount

Execution: https://github.com/metaneutrons/BFS/issues/40
Dependencies: M3, M4.

- M5-A1: Build `bfs-fuse` with libfuse 3. Provide mount/foreground/unmount,
  lookup/getattr, directory streams, open/read/release, readlink, statfs and
  the agreed read-only metadata/xattr view. Separate inode lookup references
  from open handles and handle cancellation/disconnect without leaks or hangs.
- M5-A2: Support live namespace and explicit read-only snapshot selection
  without introducing a colliding synthetic directory. Hide incomplete deleting
  snapshots from selectable snapshots and do not perform recovery writes.
- M5-A3: Every mutation fails appropriately. Unknown format/options, corrupted
  trees, out-of-range requests and encoding errors are diagnosed and bounded.
  Mounted tests and full-image comparison prove M3's zero-write contract.
- M5-A4: Run the conformance program through an actual Linux FUSE mount against
  fixtures and Amiga-created images; compare namespace/content with the oracle.
  Unit tests or mocked callbacks alone do not establish this milestone.

### M6: Read/write libfuse and required core semantics

Execution: https://github.com/metaneutrons/BFS/issues/41
Dependencies: M2, M5.

- M6-A1: Implement the reviewed core changes and FUSE operations for create,
  write, append, truncate, mkdir/rmdir, unlink, hard/symbolic links, replacement
  rename, supported metadata/xattrs and synchronization. Snapshot mounts remain
  read-only. Expose only implemented, tested capabilities.
- M6-A2: Conformance proves open-unlink lifetime, independent handles, atomic
  append under concurrent clients, replacement of files/empty directories,
  rename of open targets, case-only rename, stable directory iteration under
  its defined contract, and lookup/forget accounting. Each negative case has
  the documented error and no unintended namespace change.
- M6-A3: Distinguish `flush`/release from durable sync. Successful fsync and
  directory sync reach the storage barrier; errors propagate to callers.
  Remount observes the promised state, including orphan/replacement recovery.
  Do not promise whole-file atomic overwrites beyond BFS failure semantics.
- M6-A4: Linux-written v2 images are read and modified by the pinned baseline
  Amiga handler, then read again under FUSE. Test normal and interrupted states
  from M2 with the independent oracle and checker. A same-core round trip alone
  does not prove backward compatibility.
- M6-A5: Add regression tests for core changes to existing GCC/Clang,
  ASan/UBSan, analyzer and Amiga suites. Race tests cover supported threading;
  serialize shared state by policy until a separate concurrency change is
  qualified. Unsupported syscalls/options fail deliberately, never silently.

### M7: Linux qualification and CI integration

Execution: https://github.com/metaneutrons/BFS/issues/42
Dependencies: M6.

- M7-A1: A reproducible Linux runner executes direct-core, mounted read-only,
  mounted read/write and oracle comparisons. Gate fast deterministic cases on
  PRs; run bounded fault matrices at qualification. Missing `/dev/fuse`, mount
  permissions or prerequisites is a missing qualification, not a green skip.
- M7-A2: Exercise all supported block sizes and legal feature combinations,
  small/disk-full and fragmented volumes, names/metadata boundaries, files and
  offsets across 2/4 GiB where representable, both superblock failure directions,
  failed barriers, abrupt termination and modeled power-loss states. Sparse
  geometry tests do not claim qualification of maximum-capacity real media.
- M7-A3: Record a deterministic seed corpus and bounded workload/fault matrix
  under #28. Preserve minimal reproductions for failures. An explicitly chosen
  subset of upstream filesystem tests may supplement, not replace, our own
  scenarios; document inapplicability before execution, not after failures.
- M7-A4: Execute a Linux FUSE instance of #29's defined target-72-hour soak with
  operation/deadline, memory/descriptor, integrity and recovery observations.
  Approve resource bounds and schedule before running. Compare regressions on
  identified hosts, not arbitrary universal throughput thresholds. No unexplained
  stall, crash, mismatch or missing cleanup permits acceptance.
- M7-A5: Test every new CI gate with good and deliberately bad inputs, retain
  durable criterion-to-result records, and integrate into the existing required
  aggregate without weakening protection. Do not close #27/#28/#29 merely
  because Linux passed; their remaining platform scopes stay explicit.

### M8: Native AROS read/write port

Execution: https://github.com/metaneutrons/BFS/issues/43
Dependencies: M7 and M2's confirmed AROS target/runtime.

- M8-A1: Build a native handler and required format/check/snapshot/test tools
  using the selected AROS SDK/ABI. Confirm applicable existing tools individually;
  unsupported tools require a reasoned capability entry, not a blanket success.
  Share core and common DOS logic without copying them.
- M8-A2: Audit pointer/BPTR/BSTR widths, packed-data alignment, endian conversion,
  startup/calling conventions, allocator/lock behavior, DOS packets, device
  geometry/64-bit I/O and flush/media-change semantics on the selected target.
  No m68k-only binding or big-endian assumption leaks into little-endian AROS.
- M8-A3: Run the native DOS conformance adapter and storage/remount scenarios
  under the pinned AROS runtime; read FUSE/Amiga images and return AROS-written
  images to the Linux oracle and baseline Amiga reader. Unsupported-format
  refusal, errors, disk-full, snapshot and recovery cases must be exercised.
- M8-A4: Record reproducible build and runtime identities, installation/mount
  commands, support limits and actual device backend. A hosted/VM pass qualifies
  that environment only; native boot/device claims require native runtime
  evidence. Retain the existing m68k emulator regression suite.

### M9: Native MorphOS read/write port

Execution: https://github.com/metaneutrons/BFS/issues/44
Dependencies: M7 and M2's confirmed MorphOS SDK/runtime access.

- M9-A1: Build a native PowerPC handler and applicable utilities/test adapter
  against the selected SDK and OS minimum, without copying core/DOS code or
  relying on the m68k handler running under compatibility emulation.
- M9-A2: Verify PowerPC ABI/startup, pointers/BPTRs, structure alignment,
  allocator/locks, DOS/device interfaces, 64-bit I/O, flush, media-change and
  cache/DMA ownership against the SDK and actual runtime.
- M9-A3: Run the native DOS conformance adapter on a named supported MorphOS
  hardware configuration; preserve binary, OS, firmware and storage identities.
  Include remount/reboot, disk-full, fault/error and snapshot scenarios plus
  bidirectional exchange with FUSE and the pinned Amiga v2 reader.
- M9-A4: Document build/install/diagnosis and limitations with reproducible
  evidence. Missing hardware or inability to exercise device barriers leaves
  this milestone awaiting qualification; a cross-build is not a substitute.

### M10: Cross-platform release and support documentation

Execution: https://github.com/metaneutrons/BFS/issues/45
Dependencies: M7, M8, M9.

- M10-A1: Publish a support/capability matrix separating built, functionally
  tested, fault-tested and physically qualified configurations. Complete user
  and developer docs for Linux, AROS and MorphOS with verified commands and
  explicit experimental/support boundaries. Do not claim Apollo qualification
  without separate #27 evidence.
- M10-A2: Extend the existing hardened release pipeline with target-specific
  artifacts, version/build identities, runtime dependencies, licenses/SBOMs,
  signatures, attestations and clean-install smoke tests. Toolchains/SDKs are
  downloaded by verified identity subject to their distribution terms; no
  foreign binaries or generated disk images enter Git.
- M10-A3: Test candidate archives from a clean environment, perform target
  installation and remount checks, and verify downloaded bytes against the
  qualified build. A failed target gate prevents publication of that target
  as qualified. Release Please remains the version/tag authority.
- M10-A4: After explicit publication authorization, preserve immutable release
  identity and public readback evidence. All applicable criteria of M1-M10
  must be accepted before the epic is completed. A Linux-only preview may be
  separately authorized after M7; it does not close the native-port milestones
  or this initiative.

## Risks, rollout and completion

The leading technical risk is the v2-compatible Linux lifetime/rename contract,
followed by real storage durability and the native platform ABIs. Address these
before expanding the implementation. AROS and MorphOS runtime availability are
qualification dependencies, not reasons to infer success from an SDK build.

Use disposable images first, read-only mounts second, explicitly selected
read/write images third, and controlled partition/hardware testing last.
Never mount the same backing storage concurrently in two operating systems.
Users retain backups. Code rollback cannot undo disk writes; preserve images
and use the tested previous driver only on compatible known-good states.
No version migration or destructive repair is an installation side effect.

No completion date, engineering-hour estimate or CI budget is asserted yet.
Measure build and short-test costs during M3-M5, then budget M7 and hardware
sessions explicitly. Planning schedules no automation and starts no soak run.

Linux is usable at M7; the requested cross-platform initiative is complete at
M10, not after the first mount or first green build. Required evidence includes
exact plan revision, source/binary/input digests, environment, commands, seed,
expected/observed outcomes, limitations and remaining issue references.
Unexplained failures remain findings even if a later rerun passes.

## Sources and decision changes

The source baseline above is the authority for current BFS behavior. External
references checked on 2026-09-08 inform the proposed platform integration:

- [libfuse project and API choices](https://github.com/libfuse/libfuse)
- [libfuse low-level operation contracts](https://libfuse.github.io/doxygen/structfuse__lowlevel__ops.html)
- [AROS ports and native/hosted distinction](https://www.aros.org/introduction/ports.html)
- [AROS developer documentation](https://developers.aros.org/)
- [MorphOS SDK downloads](https://morphos-team.net/downloads)
- [MorphOS Exec API](https://morphos-team.net/sdk/exec.html)

2026-09-08: Initial proposal. Compared with the earlier independent-reader
suggestion, the production FUSE driver intentionally shares the core to test
it outside the emulator. A separate independent oracle retains the format
validation benefit. GitHub tracking, target acceptance and publication remain
subject to explicit owner decisions.
