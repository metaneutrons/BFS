# Sealed settlement review and reproduction

The primary agent retained responsibility for the ownership design, production
changes and integration. Luna authored bounded tests and independently reviewed
the source and test oracles. No reviewer reviewed their own test changes.

## Findings resolved before measurement

The independent source review required protecting the legacy extent root and
the recorded backup superblock block. The final gate rejects legacy extent
contexts, validates both working and committed placements, and uses the shared
`bfs_sb_backup_offset` decoder rather than another local implementation.

A failed publication sync can leave a valid readable new superblock only in
volatile storage. A fresh writable mount now synchronizes its selected state
before allocation or mount recovery; read-only inspection does not sync. The
buffered fixture exercises the same BIO without a power cut, failure of this
mount barrier, read-only inspection, and subsequent power-cut recovery.

The primary agent identified that a failed flush may persist only the new
superblock, not its referenced COW graph. The sealed path therefore completes
a successful metadata fence before any matching superblock write, while
allocation remains frozen. The independent reviewer confirmed this follows
from the BIO contract; an unsuccessful flush is not an all-or-none operation.
The fixture exercises both SB-only persistence at publication and persistence
of one non-SB block at the failed metadata fence.

The direct mixed-provenance test was strengthened to verify the entire
32-slot historical emergency-pool permutation, scratch in the inactive suffix,
the exact active set, the exact free-count delta and every ordinary candidate.
The natural convergence test was strengthened to witness the sealed path at
every explicit mutation commit, not merely state that the legacy path could
also produce. The final independent review verified the file identities and
closed both oracle gaps without a source or test blocker.

## Counterprobe

`sf-metadata-fence-counterprobe.c` is a build-only driver that includes the
persistence fixture. Copy it to `build/` before compiling, preserving its
relative include. Link it against all production core sources and
`tests/block_device_emu.c`, with the normal host flags. It derives the actual
publication sync ordinal from a successful trace, then injects an SB-only failed
flush and requires a valid new superblock plus full data and strict fsck after
a power cut.

For the negative executable, use an isolated copy of `src/core/txn.c` and remove
only the `bfs_bio_sync(fs->bio)` metadata-fence call and its error return in the
sealed branch of `txn_commit_working`. Do not edit the production source.
The recorded positive exits 0 and the negative exits 1:

```
SB-only failed publication: sync=2 new_sb=1 graph_data_fsck=1
SB-only failed publication: sync=1 new_sb=1 graph_data_fsck=0
negative exit=1 (expected 1)
```

This is a graph/data failure witness, not only a sync-count assertion.

## Local verification commands

```
make -j4 check BUILD_HOST=build/host-sf
ASAN_OPTIONS=detect_leaks=0 make -j4 host-test BUILD_HOST=build/sanitize-sf \
  HOST_CFLAGS='-std=c99 -Wall -Wextra -Werror -g -O1 -pthread -fno-omit-frame-pointer -fsanitize=address,undefined -I include -I tests -DBFS_HOST=1 -D_POSIX_C_SOURCE=200809L'
make -j4 amiga amiga-perf-probe-handler BUILD_AMIGA=build/amiga-sf
```

Both final complete host suites pass 425 tests in 43 suites. The natural
checksummed create/write, overwrite and delete fixture witnesses 72 sealed
commits across 12 cycles at each of 1 KiB and 4 KiB, with strict read-only
remount/fsck and exact return to empty-volume free accounting after deletion.
LeakSanitizer is unsupported on this macOS runtime and is not claimed.

The same functional core source was copied to an owned temporary directory on
Cachy before the final diagnostics-only counter additions. The rebuilt normal
handler is byte-identical after those additions.
After the build, `make fuse-test`, `make conformance-test` and `make fuse-analyze`
pass, including actual read-only/writable FUSE mounts and 20 conformance tests.
An initial incomplete source copy omitted the workflow file read by one
conformance test; copying that existing file resolved the setup error. No CI
workflow was executed and no workflow was edited.

## Diagnostic schema review

The first candidate deep run used schema 8 and completed both guest data checks,
but its verifier rejected missing ordinary reserve-return calls. Sealed commits
legitimately skip that path. Raw evidence and schema 4–8 rules remain unchanged.
The candidate's fresh diagnostic therefore pairs probe ABI 10 with schema 9,
including explicit successful metadata-fence and sealed-commit counters.

Independent review found two initial schema-9 gaps: omission of the previous
batch-consistency branch and permission for legacy reclaim passes in an
all-sealed phase. Both were fixed before the fresh run. Completed phases now
require exact ordinary-return plus sealed-commit accounting, matching fences,
the previous run/batch/skip relations, sufficient physical sync requests and
reclaim passes bounded only by unsealed commits. Focused countertests retain
explicit schema-8 rejection and cover the corrected schema-9 relations.

The normal eight-run images keep their unchanged schema-1 guest binary. The
new probe counters are compiled out of normal handlers, whose SHA-256 remains
`f3b593adb3f377d4ce755f4d08c6e378fb7bb4d0d41fdad2d86a900280ab783c`.

The primary agent additionally checked the actual legacy counter call sites:
ordinary reserve return occurs once initially and once per reclaim pass. Exact
completed-phase accounting is therefore `return_calls + sealed_commits ==
txn_commits + reclaim_passes`, not transactions alone. A positive test upgrades
the real baseline's schema-8 output with zero appended sealed/fence counters
without changing any common counter value. The all-sealed guest run has zero
passes, so this correction does not alter its actual accounting or raw output.
Both initial independent-review blockers were resolved before the fresh run.
The final focused verifier suite passes 20 tests and the complete final quality
suite passes 64. The main agent read the final correction and reproduced the
strict checks against the raw candidate output; no common baseline or candidate
counter was edited.
The TSV and guest info/completion bytes are copied unchanged. Only trailing
horizontal whitespace in copied logs is normalized for repository whitespace
checks; the original logs remain in `build/`. The recorded source diff uses
zero context to avoid treating unified-diff blank context lines as whitespace
errors. The relative evidence manifest describes the final committed copies.

## Scope limits

Snapshots, legacy extent contexts, deeper or full free leaves and absent scratch
keep the existing commit path. The selective-persistence proof in this increment
covers the eligible sealed path, not arbitrary selective persistence of the
legacy fallback. Real power cuts, controller flush lies and individual hardware
media remain a separate device-bound qualification. No CI, push, PR, merge or
other GitHub mutation is part of this evidence.
