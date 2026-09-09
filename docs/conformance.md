# BFS Conformance and Format Oracle

Status: M4 execution artifact. This document defines the test harness protocol;
it does not add a filesystem capability or change the v2 format.

## Boundaries

`bfs-conformance` is an orchestrator around separately built observations.

| Component | Links BFS core | Responsibility |
| --- | --- | --- |
| `build/host/bfs-conformance-core` | Yes | Creates and removes its own temporary image, then exercises documented core operations over the production POSIX transport. |
| `build/host/bfs-conformance-posix` | No | Observes a supplied mounted directory through OS calls only. It is read-only until a later milestone explicitly adds mutation cases. |
| `tools/bfs-format-oracle.py` | No | Independently decodes committed v2 bytes and emits a normalized manifest. It has no write path. |

`tools/check-conformance-linkage.sh` rejects a mounted backend that exports a
`bfs_*` symbol. Its test compiles a deliberately contaminated counterprobe to
prove that this rule detects the failure it is intended to detect.

The Direct backend accepts no image pathname. It allocates a unique directory
below `/tmp`, creates a mode-0600 image within it, formats only that image, and
removes the image and directory before reporting success. Cleanup failure is an
`error`, never a pass. The Mounted backend requires a caller-owned,
non-root directory owned by the calling user and makes no mutation.

## Running

Build and run the deterministic direct smoke replay:

```sh
make conformance-test
tools/bfs-conformance.py --backend core \
  --replay tests/conformance/replays/smoke-v1.jsonl
```

`tests/conformance/replays/core-full-v1.jsonl` adds disk exhaustion and
reclaim, directory scale, links, Latin-1 case-folded names, comments, sparse
ranges, remount, and a negative name case. The bounded `disk-full` case writes
until the production core reports exhaustion, verifies committed data, reclaims
half of the file, and proves that allocation succeeds again.

Mounted mode is meaningful only once a separately qualified mount exists. It
does not treat a missing mount fixture as a pass:

```sh
tools/bfs-conformance.py --backend posix --root /path/to/mounted/bfs \
  --case empty-volume
```

The status is `pass`, `fail`, `skip`, or `error`. Exit codes are respectively
0, 1, 2, and 3. `skip` is reserved for a declared inapplicable case or an
unavailable mounted fixture; it cannot satisfy a full-milestone acceptance
claim. Malformed replay data, missing backend output, timeout, backend crash,
invalid JSON, no selected case, and cleanup failure are `error` outcomes.
Each backend runs through its fixed build-tree `execv` path without a shell,
has a 60-second wall-clock budget, and has at most 1 MiB of combined captured
stdout and stderr. Exceeding either limit cannot produce a pass.

## Replay and Result Format

A replay is JSON Lines. Its first record has type `bfs-conformance-replay`,
`format_version: 1`, a `catalog_version`, and an integer `seed`. It contains
one or more unique `{ "type": "case", "id": "..." }` records followed by
exactly `{ "type": "complete" }`. The catalog is versioned in
`tests/conformance/scenarios.json`; its stable contract IDs let target-specific
implementations report equivalent cases without sharing code. Each scenario
also declares the currently allowed outcome for each backend. The orchestrator
converts a syntactically valid but contract-incompatible backend result into an
`error`; expected outcomes are therefore independent data rather than a value
derived from a production structure or operation result.

Where a scenario has an established native counterpart, `amiga_test_ids` names
the immutable identifier from `tools/bfs-test-cases.def`. The 46-test Amiga
FULL46 inventory remains its own executable suite; the conformance catalog is
a cross-platform mapping, not a replacement or a second native inventory.

The result is a single JSON object with format and catalog versions, selected
backend, seed, per-case records, overall status, and identities for the Git
revision, backend executable bytes, catalog and replay input bytes, platform,
and Python runtime. A backend record must identify the requested case and carry
one of the four statuses. The orchestrator rejects anything else rather than
guessing intent.

## Oracle

`tools/bfs-format-oracle.py IMAGE` reads a complete regular image and emits a
stable JSON manifest for its selected committed superblock, live namespace,
file content SHA-256 values, and visible snapshots. It implements the v2
superblock selection, CRC32, node layout, bounded recursive tree walk, range,
cycle, capacity, key ordering, directory-name hash, inode, extent, and snapshot
rules directly from `docs/on-disk-format.md`. It does not import headers,
link `libbfs`, or call a production codec/tree routine.

The oracle fails closed on incompatible versions/options, CRC failure, invalid
geometry, out-of-range child/extent pointers, cycles, excessive node count,
malformed directory keys, and invalid inode references. It intentionally does
not repair media or infer uncommitted state. The fault adapter models
acknowledged versus persisted writes, reordering, and tears through generated
images; terminating a daemon alone is not evidence of simulated power loss.

`tools/bfs-persistence-model.py` already supplies the protocol-level half of
that adapter. Its versioned JSON plan records each block's acknowledgement and
persistence outcome independently; full, absent, and torn persistence are
distinct, and `persist_order` makes reordering explicit. The resulting media
model must be converted into a disposable image before it is supplied to the
oracle. It never writes a target device itself.

## Native DOS Adapter Design

The catalog is target-neutral. The future AROS and MorphOS adapter will read a
replay, execute its named cases through the native DOS packet/device boundary,
and emit the same one-record JSON schema through a host-captured serial or file
channel. It must map unavailable native runtime/SDK fixtures to `error`, not
to `skip`; only individual catalog cases explicitly marked inapplicable may
skip. That adapter belongs to M8/M9, after their SDK and runtime contracts are
available. It will not copy the classic handler or introduce a second core.
