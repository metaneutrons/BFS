# ExAll hit-rate measurement evidence

The debug-only counter of the handler, the programs that read it, the scripts that
build and verify the usage kit, the results of the checks and the sessions folder
for Fabian's real-use data. See the
[report](../../bfs-exall-hit-rate-measurement-2026-10-23.md) and the section
"Hit-rate measurement" of the [plan](../../../plans/bfs-exall-result-cache-v1.md).

- `hitrate-handler.patch` applies to `src/amiga/handler.c` at `b8f3e8a` and adds
  `src/amiga/hitrate.h` (`git apply`). Everything is under `BFS_HITRATE`. Nothing of
  it is in `src/`. `release-identity.txt` records that the patched tree built
  without the define gives the reference handler byte for byte.
- `tools/hitrate.c` reads (`hitrate [DEVICE]`) or clears (`hitrate [DEVICE] RESET`)
  the counters, `tools/hitrate-check.c` is the scripted run, `tools/hitrate-common.h`
  is shared. Build them with `m68k-amigaos-gcc -std=c99 -noixemul -m68020 -O2
  -Isrc/amiga -Itools ... -lamiga` (plus the libnix `-B` and `-L` flags of
  `AMIGA_TOOL_LDFLAGS` in the Makefile) against a tree with the patch applied.
- `build-instrumented.sh CPU OUTPUT` builds the instrumented handler with the
  compiler command of `make amiga`. `run-guest.sh` runs a startup sequence in
  FS-UAE with a BFS volume served by a given handler (adapted from
  `emulator-test/ci-local.sh`). `memory-probe.sh` reads the sizes of the counter's
  types from the compiler.
- `build-kit.sh`, `make-tree.py`, `kit/` and `verify-kit.sh` build and check the
  usage kit. It contains licensed files and is built below `build/`, which git
  ignores. `kit/ANLEITUNG.md` is the German instruction. The system folder of the
  kit cannot start Workbench (see the report), so the kit tests for that at boot
  (`kit/Startup-Sequence`, `tools/openlibs.c`) and falls back to the Shell console;
  `verify-kit.sh KIT RUNDIR wb` exits with 3 if Workbench is not available.
- `kit/attach-to-config.sh`, `kit/hitrate-loop-attached` and `kit/Startup-Zeilen.txt`
  are the route for Fabian's own Workbench: the script writes a copy of an FS-UAE
  configuration with the BFS hard file and the folder `Hitrate` added.
  `verify-attach.sh KIT RUNDIR` tests that route with a stand-in boot folder.
- `build-diag-tools.sh`, `tools/openlibs.c`, `tools/quiet.c`, `tools/taskdump.c`,
  `tools/inventory.c` and `diagnose-workbench-Startup-Sequence` are the diagnosis of
  the Workbench failure (run it with `run-guest.sh`, startup sequence and done file
  `diag.done`).
- `summarize.py` turns a counter file into the decision-rule table in German.
- `results/` holds the scripted run (`scripted-run`), the mutation run that must
  fail (`mutation-check.txt`), the full on-Amiga suite with the instrumented handler,
  `workbench-diagnosis/` (why Workbench does not start; `NOTES.txt` there says what was
  changed in the files), `kit-check-workbench/` and
  `kit-check-shell/` (the kit's boot checks with the Workbench test and the console
  after it), `attach-check/` (the own-Workbench route with a stand-in), the kit build
  log and `memory-probe.txt`. `inputs.sha256` has the hashes of what ran.
- `sessions/` is where the counter file of every real-use session goes.

Verify all retained files with `shasum -a 256 -c SHA256SUMS`. No screenshot is kept.
