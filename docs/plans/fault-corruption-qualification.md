# Fault and corruption qualification plan

This document defines the bounded execution design for [issue #28](https://github.com/metaneutrons/BFS/issues/28).
It extends the deterministic M7 matrix; it does not replace it, close it, or
make a general durability claim. The normative v2 layout remains
`docs/on-disk-format.md`, and the operation and recovery contract remains
`docs/failure-semantics.md`.

The plan has two deliberately separate tracks:

1. **Image fault and corruption qualification** operates only on disposable,
   regular-image files and is reproducible in CI or on an identified host.
2. **Physical storage qualification** operates on one named host, storage
   stack, media device, firmware, and power-cut apparatus. It is a bounded
   empirical result for that exact configuration, not evidence about other
   controllers, bridges, media, operating systems, or Apollo hardware.

No physical test is scheduled by this document. Starting a destructive
hardware session requires a separately approved run record.

## Purpose and non-goals

The existing host suites already exercise crash cut points, I/O errors,
partial writes, torn metadata, dual-superblock recovery, allocator failures,
snapshot deletion, structural corruption, fuzzing, `bfs check`, and selected
FUSE cases. M7 records their bounded seed and fault/workload matrix. Issue #28
turns those individual cases into a versioned fault campaign with a declared
outcome contract and retained minimal reproductions.

The campaign must detect the following unacceptable results:

- a crash, timeout, sanitizer finding, or out-of-bounds access;
- a corrupted image accepted as clean or mounted read/write without the
  required recovery path;
- `bfs check` reporting success for an image that violates a declared
  invariant;
- `bfs check --repair` modifying a structurally damaged image outside its
  documented free-space-leak repair scope;
- recovery exposing a namespace, extent, reference, or checksum state that is
  neither a permitted committed state nor an explicit rejection;
- a harness result without the source, inputs, fault point, expected outcome,
  and preserved reproducer needed to investigate it.

The campaign does **not** prove absence of all defects, atomicity of unshared
data overwrites, controller flush correctness, media endurance, or physical
Apollo qualification. A daemon kill is not described as a physical power loss.

## Outcome contract

Every campaign case declares exactly one expected outcome class before it runs.
The runner must fail a case that produces a different class; it may not
reinterpret a surprising result after the fact.

| Class | Meaning | Required observations |
| --- | --- | --- |
| `consistent` | The image mounts through the intended path and satisfies the independent oracle plus `bfs check`. | Mount result, checker result, oracle result, namespace/data probes. |
| `recoverable` | A documented writable recovery path is required before the image is fully usable, for example resuming an interrupted snapshot deletion. | Initial read-only and writable results recorded separately; post-recovery checker and oracle result. |
| `repairable-leak` | A structurally clean image contains only unreachable allocated blocks. `bfs check --repair` may reclaim those blocks. | Pre-repair report, post-repair image hash, second read-only check, oracle result. |
| `rejected` | The image is structurally unsafe or unsupported and must not be treated as usable. | Read-only checker, writable mount and FUSE refusal results; no write by the read-only probes. |
| `inconclusive-device` | A physical run cannot establish the requested storage event or its witness record is incomplete. | Preserve the run and mark the device/profile unqualified; never convert it to a pass. |

`bfs check --repair` is intentionally narrow: on host images it can reclaim
unreachable blocks only after a structurally clean scan. It is not a general
metadata repair promise. A case whose intended outcome is `rejected` must not
be offered to repair as a fallback.

For interrupted writes, a returned error does not prove that no bytes reached
storage. The contract therefore compares the recovered image with the set of
states permitted by `docs/failure-semantics.md`, rather than assuming a failed
operation is entirely absent. A completed, acknowledged sync is tested under
the stronger committed-state expectation declared for that operation.

## Image campaign architecture

The implementation is a runner plus a versioned case manifest; it is not a
second filesystem implementation.

### Inputs and adapters

1. A baseline builder creates a small, deterministic image from a named
   scenario and records the source commit, executable hashes, block geometry,
   format options, seed, and baseline SHA-256.
2. A fault adapter wraps the existing block-device test backend. Adapters model
   returned I/O errors, short/partial transfer, dropped acknowledged write,
   torn write, stale write, reordered write, failed flush, allocation failure,
   and read failure. Each model names the assumptions it makes.
3. A structural mutation adapter changes a declared on-disk field or relation.
   It supports both checksum-invalid mutations and checksum-valid but
   structurally invalid mutations. It records byte ranges and old/new hashes.
4. An operation adapter performs a named filesystem action through the core,
   host CLI, or FUSE path. A failure point is an explicit operation stage or
   I/O sequence number, never an implicit timing race.
5. An observer runs the independent oracle, `bfs check`, the permitted
   `--repair` path where declared, and the relevant read-only/read-write FUSE
   probes. It records all exit codes and classifies the result against the
   predeclared contract.
6. A reducer minimizes a newly failing case while retaining the same observed
   failure. It saves the reduced image, manifest, and command transcript before
   the campaign continues.

The runner refuses an existing evidence directory, missing observer, ambiguous
case identifier, undeclared outcome, or target that is not an owned regular
image. It must clean its temporary mount and process state after every case.

### Case manifest and evidence

The manifest is the source of truth for campaign coverage. A case record must
contain at least:

```text
case_id, suite_version, source_commit, binary_hashes, scenario, seed,
block_size, block_count, format_options, fault_model, fault_point,
mutation_location, expected_class, expected_states, observer_commands,
timeout_seconds, baseline_sha256, result, evidence_sha256
```

Every execution writes an append-only result record with elapsed time, host
identity, commands, exit status, bounded output tails, observed class, and
SHA-256 digests of input, final image, and reproducer. Failed or inconclusive
cases retain their complete minimal reproducer and are not overwritten by a
later successful rerun. A successful campaign retains a criterion-to-result
summary and a durable copy of its manifest.

## Bounded image campaigns

The first implementation phase covers the following families. Bounds are
versioned in the manifest and changed only with an explicit review of changed
cost and coverage.

| Family | Initial bounded cases | Primary outcome classes |
| --- | --- | --- |
| Recognition and superblocks | Unknown version/options; each primary or backup copy damaged; both copies damaged; stale and torn copies. | `consistent` or `rejected` |
| Tree and inode structure | Invalid key count/order, child pointer, level, cycle, duplicate ownership, extent range/overflow, invalid inode/link count, directory relation, snapshot record/cursor. Run CRC-invalid and CRC-valid variants where meaningful. | `rejected` |
| Checksummed data | Corrupt retained bytes, complete replacement, partial overwrite, truncate tail and snapshot-shared data. | `consistent` or `rejected` |
| Allocation and reclamation | Bitmap/tree disagreement, unreachable allocation, reserve/deferred-free exhaustion, interrupted deletion and free-space rebuild. | `repairable-leak`, `recoverable`, or `rejected` |
| Operation interruption | Create, write, truncate, rename/replace, link/unlink, disk-full recovery, snapshot create/delete, and sync. Sweep declared cut points over both ordinary and ordered-data format options. | `consistent`, `recoverable`, or `rejected` |
| Storage protocol faults | Error return, short transfer, dropped acknowledged write, torn write, stale write, reorder, ignored/failed flush, read error. | Predeclared per operation |
| Surface containment | Core, `bfs check`, `bfs check --repair`, FUSE read-only, FUSE read/write and independent oracle have compatible outcomes. | All classes above |

The initial small-image geometry is deliberately exhaustive only where that is
realistic: a case may sweep every declared I/O cut point or every metadata
block in a fixture, but it must state the bound. Larger geometries, all bit
positions, and arbitrary media capacities are sampled with fixed seeds and are
not presented as exhaustive.

The existing M7 seed corpus and fault/workload matrix remain inputs to this
plan. New image-campaign seeds are added only with the scenario and operation
bound that consumes them; a bare list of random seeds is not coverage.

## Harness acceptance before filesystem claims

The runner itself requires good- and bad-input tests before it is used for a
qualification claim:

- a known-good baseline must yield its declared `consistent` result;
- a deliberately corrupted result record, image digest, incomplete observer,
  duplicate case ID, unknown outcome class, and missing cleanup must fail;
- a fault adapter must prove that its injected event occurred, for example by
  a trace of accepted, dropped, torn, or reordered writes;
- a reducer must reproduce the original failure before its output replaces the
  larger case artifact;
- a checker repair test must prove both allowed leak repair and refusal on a
  structural error;
- the CI job must retain the manifest and criterion-to-result record even on
  failure, with bounded logs and no generated images committed to Git.

The direct-core campaign can run in CI at a cost-approved bound. FUSE cases
run only where `/dev/fuse` is available; its absence is a missing
qualification, not a successful skip. Long fuzzing and physical trials are
identified host jobs, never concealed as a pull-request gate.

## Separate physical storage qualification

Physical qualification tests whether an exact storage stack produces outcomes
compatible with the declared persistence contract after externally induced
power loss. It does not prove that a device never lies about writes or flushes:
it can only detect violations within the tested operations, timings, media
state, and sample count.

### Preconditions and safety boundary

- Use a dedicated, disposable host or target controller and a dedicated
  sacrificial device. Never use a system disk, user partition, mounted
  filesystem, shared image, or a device selected by a loose path pattern.
- The present Linux `bfs` administration command intentionally accepts regular
  images only. Do not weaken that guard for this work. A physical run uses a
  separately reviewed target-specific device backend and an exact allow-listed
  device identity.
- The harness validates transport, model, firmware, stable serial or a
  privacy-preserving device-identity hash, capacity, expected test signature,
  and absence of mounts before each destructive step. A mismatch is a refusal.
- Power control has a dry-run mode, independent operator confirmation, an
  emergency stop, and a separate witness system/power domain. The witness
  records the command and observed relay state; it never asserts a timing it
  cannot measure.
- Every run begins from a freshly provisioned test image and has a verified
  recovery/reimage procedure. It may damage the test device; no endurance or
  data-retention claim is inferred from surviving a session.

### Device profiles and controller behaviour

Each result is bound to one profile:

```text
host model; firmware; OS/kernel; storage driver; bus and bridge; device model;
device firmware; capacity; logical/physical sector sizes; write-cache setting;
flush/FUA configuration; power topology; relay/controller firmware; BFS build;
format geometry and options
```

At a minimum, test separately any available write-cache-enabled and
write-cache-disabled setting and every distinct controller or USB/SATA/NVMe
bridge. A bridge replacement, firmware update, kernel/driver change, or cache
policy change creates a new profile. A host fault adapter can model stale,
reordered, or falsely acknowledged writes; it cannot certify a physical
controller as honest.

### Media-specific bounds

A physical-media result also records the exact tested address range and the
number of written bytes, power cycles, and recovery cycles. Where the device
exposes them, capture health, media-error, wear, and error-log counters before
and after the session; unavailable counters are recorded as unavailable, not
invented from a generic device class. The evidence includes a complete
read/verify pass over the provisioned BFS test range before and after the
approved session.

This is deliberately narrower than a device endurance, retention, bad-sector,
or whole-capacity qualification. Those need their own approved duration,
address-range, temperature, and wear budget. An observed media error, changed
health counter, or unverifiable range ends the affected device profile and
preserves its raw evidence. It is not papered over by reformatting the medium.

### Approved physical scenarios

After a profile-specific pilot establishes safe operation timing, the approved
run selects finite counts for each scenario. It records the count before the
first destructive cut.

1. **Before sync:** power is cut during an uncommitted create, write, rename,
   unlink, truncate, snapshot operation, or disk-full recovery. The recovered
   image may be an allowed prior or later committed state, but may not be an
   unsafe accepted state.
2. **During sync/flush:** power is cut at predeclared points surrounding the
   target's acknowledgement. The result is classified using the operation's
   documented uncertain-commit contract.
3. **After acknowledged sync:** a witness records the completed operation and
   the host's acknowledgement before the cut. The declared committed state
   must survive subsequent recovery; a contrary result is a device-profile
   failure, not silently a filesystem pass.
4. **Recovery:** after power restoration, capture the raw test image before a
   writable recovery action, run the same oracle/checker sequence as image
   campaigns, then record any required recovery and post-recovery state.

The initial profile budget is a pilot plus a preapproved finite sample count
per operation and power window. It is not acceptable to turn a failed cut into
an unbounded rerun campaign. Unexpected results stop the affected profile,
preserve the evidence, and require investigation before any additional cut.

### Physical evidence and claim boundary

For every cut retain the source and binary identities, device profile, image
and baseline digests, operation transcript, host and witness clocks, requested
and observed power-control state, raw pre-recovery capture, oracle/checker
results, mount result, and final image digest. Preserve the original raw
capture read-only before `bfs check --repair` or writable recovery.

A successful physical result may state only: the named profile passed the
named bounded scenarios and sample counts under the recorded apparatus. It
does not establish safety on other devices, power supplies, controllers,
bridges, firmware, kernel versions, cache policies, or Apollo hardware. Apollo
execution remains separate issue #27 evidence.

## Delivery order and closure record

1. Approve the case-manifest schema, outcome contract, and retention layout.
2. Implement and negatively test the image campaign runner and one case from
   each family.
3. Expand the bounded manifest, run it on an identified host, and turn each
   unexplained result into a minimized regression before continuing.
4. Review the physical apparatus and profile-specific run record without
   starting a destructive session.
5. Only after explicit approval, execute the physical pilot and the approved
   finite session; preserve every outcome including `inconclusive-device`.

Issue #28 closes only after its approved image scope and any explicitly
authorized physical profiles have complete expected-versus-observed records,
all unexpected outcomes are resolved or documented as remaining limitations,
and the final record names the continuing unqualified hardware scope. A Linux
M7 pass, an emulator result, or one physical device profile alone does not
close the broader issue.
