# BFS release readiness

Epic: https://github.com/metaneutrons/BFS/issues/6
Decision state: Accepted by the repository owner on 2026-09-06.

## Outcome and boundaries

Deliver an audited BFS release candidate with verified failure handling, the
repository-standard hardened profile, and tested release assets for 68020,
68030, 68040, 68060 and Apollo 68080. AMMX optimization and claims of production
qualification on physical hardware are outside this initiative.

Baseline on 2026-09-06: commit `4897439`, 70 modified tracked files and 26 new
files in the working tree. The API standard check passed 16 checks and failed
the branch and tag rulesets. Classic protection was also absent. Recovery was
partially implemented; earlier passing tests do not qualify this working tree.
Release PR #4 remained open. No previous plan or tracking issues existed.

## Design and decisions

Preserve the existing C99 core, on-disk format and Amiga handler architecture.
Keep core error semantics, allocator ownership and snapshot reference counts
consistent across their public boundaries. Failed operations must not silently
publish partial namespace changes, reuse live blocks or damage snapshots.
Recovery must either restore a proven committed state or reject further use
until remount; stale open file handles must not silently access reclaimed blocks.

Use functional Conventional Commit packages: repository conventions, external
build dependencies, storage primitives, filesystem operations, Amiga integration
and release automation. Tests and documentation belong with the behavior they
cover. Keep coupled changes together when splitting would break a build.
Use separate dependent PRs where needed to preserve functional deliveries under
the required squash-only merge policy. Verify each committed tree, not merely a
working tree containing later fixes.

This plan owns scope and acceptance. The linked issues own execution status,
blockers, implementation PRs and evidence. Additional confirmed defects found
while qualifying the existing scope must be tracked and fixed. Scope changes or
waivers require an explicit reviewed decision, never a silently weakened gate.

## Delivery and acceptance

### M1: Repository and build discipline

Execution: https://github.com/metaneutrons/BFS/issues/7
Dependencies: None.

- M1-A1: Versioned plan, epic and milestone issues link both ways with one
  acceptance authority and no duplicate progress checklist.
- M1-A2: Hooks, community files, pinned actions and mandatory quality checks
  satisfy the applicable repository-standard rules. Every new gate passes a
  valid input and rejects a deliberately invalid isolated input for the
  expected reason. Confirmed exceptions are documented with their basis.
- M1-A3: Required CI downloads use verified identities; no downloaded binaries
  enter the committed tree. Clean-checkout build and emulator setup work without
  operator-local assets. Optional proprietary benchmarks remain optional.

### M2: Filesystem correctness

Execution: https://github.com/metaneutrons/BFS/issues/8
Dependencies: M1-A3 for reproducible test inputs.

- M2-A1: Allocator operations have unambiguous ownership on failure. Tests cover
  reserve refill errors, double frees, exhaustion and rollback.
- M2-A2: Transactions and snapshot create/delete handle I/O and commit failures
  without inconsistent references or an unsafe retry. Recovery failure blocks
  subsequent operations. Remount and open-handle behavior are tested.
- M2-A3: File, directory and comment mutations preserve namespace consistency
  on failure, with regression tests for the confirmed partial-operation paths.
- M2-A4: Superblock, extent and B-tree validation rejects malformed geometry,
  references and ordering. Scans terminate on corrupt trees. fsck accepts valid
  live/snapshot graphs and rejects incorrect reference counts.
- M2-A5: All confirmed audit findings are fixed and supported by focused
  regression evidence. Documented size and crash-consistency limitations are
  verified; reporting an error alone does not establish recoverability.

### M3: Amiga integration

Execution: https://github.com/metaneutrons/BFS/issues/9
Dependencies: M2.

- M3-A1: Handler and utilities compile for their supported targets with the
  defined warning policy. Required DOS/I/O return values are handled, and the
  tools cannot report successful tests after a failed prerequisite.
- M3-A2: The complete AROS integration suite passes with the qualified handler
  and test binary. Record compiler, ROM and tested binary identities.
- M3-A3: All five CPU-specific handler builds succeed. The 68080 artifact is
  identified separately; build evidence is not described as physical Apollo
  hardware qualification. AMMX is not introduced.

### M4: Candidate qualification and release

Execution: https://github.com/metaneutrons/BFS/issues/10
Dependencies: M1, M2 and M3.

- M4-A1: The final candidate passes host tests with GCC and Clang, address and
  undefined-behavior sanitizers, static analysis, and at least 85% core line
  coverage. Preserve code identity and actual results, including omissions.
- M4-A2: GitHub CI passes for the delivered revisions. Branch and tag rulesets
  are active with no bypass and the required aggregate context is demonstrably
  produced. The API standard checker and local survey have no unresolved
  applicable findings. Do not weaken protection to complete a merge.
- M4-A3: A real prerelease exercises Release Please and the hardened publication
  path: reproducible archive creation, SBOM, signatures, attestations, clean-room
  emulator smoke test and downloaded-byte verification. Valid artifacts pass;
  isolated tampered copies fail. A failed gate prevents promotion.
- M4-A4: Functional deliveries are reviewed, committed, pushed and merged;
  documentation describes actual behavior and verified installation commands.
  Record the published candidate identity and artifact digests. Qualification,
  merge and publication are separate claims with separate evidence.

The initiative is complete only when every applicable criterion above is
satisfied and every milestone is accepted. Green checks on an earlier revision
or a partial PR merge do not establish completion.

## Migration, risks and verification cost

The broad unfinished working tree must be decomposed without losing changes.
Failure handling touches on-disk ownership, so focused fault injection precedes
full qualification. Run the expensive complete matrix at qualification points;
repeat affected checks after subsequent changes and explain retained evidence.

No on-disk migration is planned. Code rollback does not restore data already
written, so destructive probes use isolated images. Release tests permanently
consume their prerelease tags; finish local positive and negative probes first.
Physical hardware evidence remains explicitly absent until it is measured.
No completion date or runner-cost estimate is asserted without measured basis.

## Decision changes

2026-09-06: Owner accepted functional deliveries and evidence-based completion
after the status review. This establishes the initial acceptance baseline.
