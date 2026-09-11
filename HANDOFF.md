# BFS Handoff

Updated: 2026-09-11
Repository: `metaneutrons/BFS`

## Current Objective

Start the approved M7-A4 Linux/FUSE qualification soak on Cachy. This is a
real 72-hour target run, not a preflight. It must run from current `main`,
preserve durable evidence, and use tmpfs only for the repeated workload image.

Fabian authorized proceeding through the actual start. Do not report M7 as
qualified until the full duration has completed and the independent verifier
has passed.

## Exact Repository State

- `main` contains the M7 hybrid-storage runner at
  `adb55a298aa675674884659afe03c461cbc63529`:
  [PR #61](https://github.com/metaneutrons/BFS/pull/61), merged 2026-09-11.
- It requires durable `OUTPUT` and supports a distinct
  `IMAGE_DIRECTORY`. The runner copies the final 8 MiB image back to
  durable evidence and records its SHA-256 digest.
- A fresh target preflight on that merged commit is still required before the
  target run. The earlier PR preflight was successful but does not substitute
  for this final check.
- No qualifying 72-hour run is running. No target-run approval comment has
  been posted to [issue #29](https://github.com/metaneutrons/BFS/issues/29).
  This is intentional: no GitHub record should claim a start before one occurs.

## Cachy Facts Already Verified

- Host: Cachy, kernel `7.1.3-2-cachyos`, x86_64, 12 CPUs, about 24 GiB RAM.
- `/var/tmp` is durable Btrfs.
- `/dev/shm` is tmpfs with sufficient capacity for the 8 MiB BFS image.
- libfuse: fusermount3 3.18.2.
- `systemd-run --user` works for detached user services.
- Existing checkout used for preflight:
  `/var/tmp/bfs-m7-preflight.Ey78T0/BFS`.
- The prior hybrid preflight on the PR commit passed: one cycle in 30.009 s,
  2,255 operations, 1,707 pressure writes, 2,692 KiB peak RSS, 5 FDs. Its
  evidence was durable Btrfs and its workload image was tmpfs.

## Required Start Sequence

1. Restore terminal execution first. The prior Codex session could not create
   any process, including `/usr/bin/true`; therefore it could not access
   Cachy. This was an execution-environment failure, not a BFS test failure.

2. On Cachy, obtain the exact merged source without changing the remote:
   ```sh
   REPO=/var/tmp/bfs-m7-preflight.Ey78T0/BFS
   git -C "$REPO" fetch origin main
   git -C "$REPO" switch --detach origin/main
   git -C "$REPO" rev-parse HEAD
   ```
   The final command must print
   `adb55a298aa675674884659afe03c461cbc63529`.

3. Confirm no older target service is active. Do not stop any unrelated
   service. Then create new, unique preflight paths:
   ```sh
   STAMP=$(date -u +%Y%m%dT%H%M%SZ)
   PREFLIGHT_OUTPUT=/var/tmp/bfs-m7-preflight-adb55a2-$STAMP
   PREFLIGHT_IMAGE=/dev/shm/bfs-m7-preflight-image-adb55a2-$STAMP
   cd "$REPO"
   make linux-qualification-soak-preflight \
     OUTPUT="$PREFLIGHT_OUTPUT" \
     IMAGE_DIRECTORY="$PREFLIGHT_IMAGE"
   make linux-qualification-soak-verify OUTPUT="$PREFLIGHT_OUTPUT"
   ```
   Inspect `result.json`, `events.jsonl`, and the preserved
   `soak.bfs`. The evidence storage must be Btrfs, the workload storage
   tmpfs, and `qualified` must remain `false` because this is only a
   preflight.

4. After that successful preflight, create an explicit approval comment on
   [issue #29](https://github.com/metaneutrons/BFS/issues/29). State:
   - operator: Fabian Schmieder, repository maintainer;
   - commit: `adb55a298aa675674884659afe03c461cbc63529`;
   - actual UTC start and planned UTC end, exactly 72 hours later;
   - durable evidence path on `/var/tmp` and tmpfs workload path in
     `/dev/shm`;
   - detached user-service unit name;
   - acceptance criterion: runner plus independent verifier complete with
     `qualified: true`, no failed/incomplete checks or resource-limit
     breach.

   Copy the resulting issue-comment URL. It is required as
   `APPROVAL_REFERENCE`; the runner rejects other approval URLs.

5. Start the target run detached. Use fresh paths and the exact approval URL:
   ```sh
   STAMP=$(date -u +%Y%m%dT%H%M%SZ)
   UNIT=bfs-m7-soak-adb55a2-$STAMP
   OUTPUT=/var/tmp/bfs-m7-evidence-adb55a2-$STAMP
   IMAGE_DIRECTORY=/dev/shm/bfs-m7-image-adb55a2-$STAMP
   APPROVAL_REFERENCE=https://github.com/metaneutrons/BFS/issues/29#issuecomment-REPLACE
   systemd-run --user --unit "$UNIT" --collect \
     /usr/bin/env bash -lc \
     "cd '$REPO' && exec make linux-qualification-soak \
       APPROVAL_REFERENCE='$APPROVAL_REFERENCE' \
       OUTPUT='$OUTPUT' IMAGE_DIRECTORY='$IMAGE_DIRECTORY'"
   systemctl --user status "$UNIT" --no-pager
   ```
   Record the actual unit, UTC start, evidence path, and approval URL in a
   follow-up comment on issue #29. Do not claim successful qualification at
   launch.

6. At completion, inspect the service status and run:
   ```sh
   make linux-qualification-soak-verify OUTPUT="$OUTPUT"
   ```
   Preserve the complete durable evidence directory. Publish a concise result
   comment on issue #29 including run duration, event hash, final-image hash,
   operation/pressure-write totals, resource maxima, verifier result, and the
   `qualified` value. Investigate any non-success result; do not rerun over
   an existing evidence directory.

## Other Work That Must Not Distract From M7 Start

- [PR #63](https://github.com/metaneutrons/BFS/pull/63) unifies the AmigaOS
  administration interface under `bfs`. It is open, based on PR #61's former
  branch, and must be rebased/retargeted only after its CI is green. It is not
  a prerequisite for M7.
- [Issue #62](https://github.com/metaneutrons/BFS/issues/62) tracks that CLI
  unification.
- [Issue #64](https://github.com/metaneutrons/BFS/issues/64) plans read-only
  AmigaOS snapshot mounts. No handler implementation exists yet.
- [PR #57](https://github.com/metaneutrons/BFS/pull/57) is the Release Please
  release PR. Do not merge or publish a release merely because the M7 target
  has started. Release decisions come after qualification evidence is complete.

## Guardrails

- Never place the complete evidence directory in tmpfs.
- Never reuse an existing output or image directory.
- Do not claim real Apollo 68080 hardware coverage from emulator evidence.
- Do not close issue #29 solely because the FUSE soak passes; its broader
  remaining scope must be reviewed after evidence is available.
- Do not move or recreate release tags.
