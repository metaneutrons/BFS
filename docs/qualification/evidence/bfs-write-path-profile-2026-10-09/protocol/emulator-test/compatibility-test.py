#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise format refusal and diagnostics on isolated synthetic Amiga media."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess  # nosec B404
import sys
import tempfile
import zlib

from ci_runner import run_emulator

ROOT = Path(__file__).resolve().parent.parent


def run_checked(argv):
    # Fixed local build tools, no shell or guest-provided commands.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args, python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    subprocess.run([str(value) for value in argv], check=True, shell=False)  # nosec B603


CURRENT_VERSION = 3


def alter_copy(image, slot, options=False, damaged=False, version=CURRENT_VERSION + 1):
    with image.open("r+b") as stream:
        offset = image.stat().st_size // 2 if slot else 0
        stream.seek(offset)
        header = bytearray(stream.read(240))
        if struct.unpack_from(">II", header) != (0x42465300, CURRENT_VERSION):
            raise ValueError("fixture does not contain a current superblock")
        if zlib.crc32(header[:236]) != struct.unpack_from(">I", header, 236)[0]:
            raise ValueError("fixture CRC is invalid before mutation")
        struct.pack_into(">I", header, 56 if options else 4, 0x80000000 if options else version)
        if not damaged:
            struct.pack_into(">I", header, 236, zlib.crc32(header[:236]))
        stream.seek(offset)
        stream.write(header)


def superblock_version(image):
    with image.open("rb") as stream:
        header = stream.read(240)
    if zlib.crc32(header[:236]) != struct.unpack_from(">I", header, 236)[0]:
        raise ValueError("superblock CRC is invalid")
    return struct.unpack_from(">I", header, 4)[0]


def prepare_runtime(kickstart):
    """Return the evidence directory and the ROM lines of the emulator configuration."""
    runtime = ROOT / "build/emulator"
    runtime.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run.compat-", dir=runtime))
    print(f"Compatibility evidence: {work}", flush=True)
    if kickstart:
        return work, f"kickstart_file = {kickstart}\n"
    rom = runtime / "aros"
    run_checked([ROOT / "tools/install-aros-rom.sh", rom])
    return work, (f"kickstart_file = {rom / 'aros-amiga-m68k-rom.bin'}\n"
                  f"kickstart_ext_file = {rom / 'aros-amiga-m68k-ext.bin'}\n")


def prepare_media(kickstart):
    work, rom = prepare_runtime(kickstart)
    clean = work / "clean.hdf"
    with clean.open("wb") as stream:
        stream.truncate(32 * 1024 * 1024)
    run_checked([ROOT / "build/host/bfs", "format", clean,
                 "--label", "Compat", "--block-size", "4096"])
    return work, rom, clean


def make_system(case, handler, workbench):
    system = case / "system"
    for directory in ("C", "L", "S"):
        (system / directory).mkdir(parents=True)
    shutil.copyfile(handler, system / "L/bfshandler")
    if workbench:
        # Commands the AROS ROM carries but Kickstart does not.
        for command in ("Delete", "Wait"):
            shutil.copyfile(workbench / "C" / command, system / "C" / command)
    shutil.copyfile(ROOT / "build/amiga/compatibility-probe", system / "C/compatibility-probe")
    return system


def boot(emulator, case, rom, system, image):
    """Boot AROS with image on DH1: and wait for the compatibility probe."""
    config = case / "test.fs-uae"
    config.write_text(f"""[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
{rom}hard_drive_0 = {system}
hard_drive_0_label = System
hard_drive_0_priority = 0
hard_drive_1 = {image}
hard_drive_1_file_system = {system / 'L/bfshandler'}
audio_driver = null
window_hidden = 1
automatic_input_grab = 0
""", encoding="utf-8")
    command = [emulator, str(config)]
    if sys.platform.startswith("linux"):
        command = ["xvfb-run", "-a", *command]
    os.environ["FSEMU_AUDIO_DRIVER"] = "null"
    run_emulator(command, system / "compatibility.result", case / "fs-uae.log", 90)


def refuse_current_media(emulator, handler, supported, images, kickstart, workbench):
    """A driver for an older format must refuse current media unchanged."""
    work, rom = prepare_runtime(kickstart)
    for index, source in enumerate(images):
        if superblock_version(source) != CURRENT_VERSION:
            raise ValueError(f"{source}: not a current-format image")
        case = work / f"refusal-{index}"
        system = make_system(case, handler, workbench)
        image = case / "test.hdf"
        shutil.copyfile(source, image)
        before = hashlib.sha256(image.read_bytes()).digest()
        (system / "S/Startup-Sequence").write_text(
            f"C:compatibility-probe {CURRENT_VERSION} SUPPORTED={supported}\n", encoding="ascii")
        boot(emulator, case, rom, system, image)
        if (system / "compatibility.result").read_bytes() != b"PASS\n":
            raise ValueError(f"{source}: the older driver did not refuse the image")
        if hashlib.sha256(image.read_bytes()).digest() != before:
            raise ValueError(f"{source}: the older driver modified the image")
        print(f"PASS refusal {source.name}: {(system / 'diagnosis.txt').read_text()!r}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--handler", type=Path, default=ROOT / "build/amiga/bfshandler")
    parser.add_argument("--refuse", type=Path, nargs="+", metavar="IMAGE",
                        help="only check that --handler refuses these current-format images")
    parser.add_argument("--supported-version", type=int, default=CURRENT_VERSION - 1,
                        help="format version the --refuse handler implements")
    parser.add_argument("--kickstart", type=Path,
                        help="Kickstart ROM to use instead of the AROS ROM")
    parser.add_argument("--workbench", type=Path,
                        help="Workbench tree whose C commands the Kickstart run needs")
    args = parser.parse_args()
    if bool(args.kickstart) != bool(args.workbench):
        raise ValueError("--kickstart and --workbench go together")
    emulator = shutil.which("fs-uae")
    if not emulator:
        raise ValueError("fs-uae is required")
    if args.refuse:
        refuse_current_media(emulator, args.handler, args.supported_version, args.refuse,
                             args.kickstart, args.workbench)
        return
    work, rom, clean = prepare_media(args.kickstart)
    # expected: 0 compatible, 4 newer version, 3 unknown options. The "v2"
    # scenario carries an older format in both slots, which bfs format replaces.
    scenarios = [("format", None, False, False, 0, True, False),
                 ("snapshot-commands", None, False, False, 0, False, True),
                 ("v3", None, False, False, 0, False, False),
                 ("v2", "both", False, False, 0, False, False),
                 ("new-primary", 0, False, False, 4, False, False),
                 ("new-backup", 1, False, False, 4, False, False),
                 ("options-primary", 0, True, False, 3, False, False),
                 ("options-backup", 1, True, False, 3, False, False),
                 ("damaged-version", 0, False, True, 0, False, False)]
    for name, slot, options, damaged, expected, format_blank, snapshot_commands in scenarios:
        replace_old = slot == "both"
        case = work / name
        system = make_system(case, args.handler, args.workbench)
        shutil.copyfile(ROOT / "build/amiga/cli-fixture", system / "C/cli-fixture")
        shutil.copyfile(ROOT / "build/amiga/bfs", system / "C/bfs")
        image = case / "test.hdf"
        if format_blank:
            with image.open("wb") as stream:
                stream.truncate(32 * 1024 * 1024)
        else:
            shutil.copyfile(clean, image)
        if replace_old:
            alter_copy(image, 0, version=2)
            alter_copy(image, 1, version=2)
        elif slot is not None:
            alter_copy(image, slot, options, damaged)
        before = hashlib.sha256(image.read_bytes()).digest()
        if format_blank or replace_old:
            startup = "FailAt 21\nC:bfs format DH1: Test >SYS:format-message.txt\n"
        elif snapshot_commands:
            startup = """FailAt 11
C:cli-fixture Compat: >SYS:fixture-create.txt
C:bfs snapshot create Compat: invalid/name >SYS:snapshot-invalid-name.txt
C:bfs snapshot dir Compat: smoke INVALID >SYS:snapshot-invalid-option.txt
C:bfs snapshot create Compat: >SYS:snapshot-missing-name.txt
FailAt 21
C:bfs info Missing: >SYS:info-missing-drive.txt
C:bfs check Missing: >SYS:check-missing-drive.txt
C:bfs check Compat: >SYS:check.txt
C:bfs snapshot create Compat: smoke >SYS:snapshot-create.txt
C:bfs snapshot list Compat: >SYS:snapshot-list.txt
C:bfs snapshot dir Compat: smoke >SYS:snapshot-dir.txt
C:bfs snapshot dir Compat: smoke DIRS >SYS:snapshot-dir-dirs.txt
C:bfs snapshot dir Compat: smoke FILES >SYS:snapshot-dir-files.txt
C:bfs snapshot inspect Compat: smoke >SYS:snapshot-inspect.txt
C:bfs snapshot inspect Compat: smoke FILES >SYS:snapshot-inspect-files.txt
C:bfs info Compat: >SYS:info.txt
C:bfs snapshot mount Compat: smoke Compat: >SYS:snapshot-mount-collision.txt
C:bfs snapshot mount Compat: smoke SnapshotView: >SYS:snapshot-mount.txt
C:bfs snapshot mount Compat: smoke SnapshotSecond: >SYS:snapshot-second-mount.txt
C:cli-fixture SnapshotView: PROBE >SYS:snapshot-mounted-before-live-change.txt
C:Delete Compat:cli-file
C:Delete Compat:cli-dir/nested-file
C:Delete Compat:cli-dir/nested-link
C:cli-fixture SnapshotView: PROBE >SYS:snapshot-mounted-file.txt
C:cli-fixture SnapshotSecond: PROBE >SYS:snapshot-second-mounted-file.txt
C:cli-fixture Compat: EXPECTBUSY=smoke >SYS:snapshot-delete-busy.txt
C:bfs snapshot unmount SnapshotView: >SYS:snapshot-unmount.txt
C:cli-fixture SnapshotView: EXPECTABSENT >SYS:snapshot-unmounted.txt
C:cli-fixture Compat: EXPECTBUSY=smoke >SYS:snapshot-delete-still-busy.txt
C:bfs snapshot unmount SnapshotSecond: >SYS:snapshot-second-unmount.txt
C:cli-fixture SnapshotSecond: EXPECTABSENT >SYS:snapshot-second-unmounted.txt
C:bfs snapshot delete Compat: smoke >SYS:snapshot-delete.txt
"""
        elif expected:
            startup = "FailAt 21\nC:bfs format DH1: Test >SYS:format-message.txt\n"
        else:
            startup = ""
        probe_arguments = f"{expected} AFTER_FORMAT" if format_blank else str(expected)
        (system / "S/Startup-Sequence").write_text(
            startup + f"C:compatibility-probe {probe_arguments}\n", encoding="ascii")
        boot(emulator, case, rom, system, image)
        result = system / "compatibility.result"
        if result.read_bytes() != b"PASS\n":
            raise ValueError(f"{name}: guest compatibility probe failed")
        if expected and hashlib.sha256(image.read_bytes()).digest() != before:
            raise ValueError(f"{name}: incompatible media was modified")
        if expected:
            diagnosis = (system / "diagnosis.txt").read_bytes()
            if (system / "format-message.txt").read_bytes() != diagnosis + b"\n":
                raise ValueError(f"{name}: bfs format did not report the driver diagnosis")
        if format_blank:
            if (system / "format-message.txt").read_bytes() != b"Formatting DH1: as \"Test\"...\nFormat complete.\n":
                raise ValueError(f"{name}: bfs format did not complete")
        if replace_old:
            message = (system / "format-message.txt").read_bytes()
            if (not message.startswith(b"BFS format version 2 is not supported.\n") or
                    not message.endswith(b"Formatting DH1: as \"Test\"...\nFormat complete.\n")):
                raise ValueError(f"{name}: bfs format did not replace the older format")
            if superblock_version(image) != CURRENT_VERSION:
                raise ValueError(f"{name}: the replaced medium is not current")
        if snapshot_commands:
            outputs = {path.name: path.read_bytes() for path in system.iterdir() if path.is_file()}
            expected_outputs = {
                "snapshot-create.txt": b"Snapshot created.\n",
                "fixture-create.txt": b"FIXTURE OK\n",
                "snapshot-list.txt": b"Snapshots on Compat:\n",
                "snapshot-dir.txt": b"Directory \"smoke:\" on Compat:\n",
                "snapshot-inspect.txt": b"Directory \"smoke:\" on Compat:\n",
                "info.txt": b"Drive: Compat:\n",
                "check.txt": b"Errors: 0  Warnings: 0\nLeaked blocks: 0\nCLEAN\n",
                "snapshot-mount.txt": b"Mounted snapshot \"smoke\" from Compat: as SnapshotView:\n",
                "snapshot-second-mount.txt": b"Mounted snapshot \"smoke\" from Compat: as SnapshotSecond:\n",
                "snapshot-mounted-before-live-change.txt": b"PROBE OK\n",
                "snapshot-mounted-file.txt": b"PROBE OK\n",
                "snapshot-second-mounted-file.txt": b"PROBE OK\n",
                "snapshot-delete-busy.txt": b"BUSY OK\n",
                "snapshot-unmount.txt": b"Unmounted SnapshotView:\n",
                "snapshot-unmounted.txt": b"ABSENT OK\n",
                "snapshot-delete-still-busy.txt": b"BUSY OK\n",
                "snapshot-second-unmount.txt": b"Unmounted SnapshotSecond:\n",
                "snapshot-second-unmounted.txt": b"ABSENT OK\n",
                "snapshot-delete.txt": b"Snapshot deleted.\n",
                "snapshot-invalid-name.txt": b"Snapshot names cannot contain '/'.\n",
                "snapshot-invalid-option.txt": b"Valid DIR and INSPECT options are DIRS and FILES.\n",
                "snapshot-missing-name.txt": b"CREATE requires a name.\n",
                "info-missing-drive.txt": b"Cannot find handler for Missing:\n",
                "check-missing-drive.txt": b"Cannot find handler for Missing:\n",
            }
            for filename, expected_output in expected_outputs.items():
                if expected_output not in outputs[filename]:
                    raise ValueError(f"{name}: unexpected output from bfs command: {filename}")
            if b"smoke" not in outputs["snapshot-list.txt"]:
                raise ValueError(f"{name}: snapshot list omitted the created snapshot")
            if (b"cli-file" not in outputs["snapshot-dir.txt"] or
                    b"cli-dir" not in outputs["snapshot-dir.txt"] or
                    b"(dir)" not in outputs["snapshot-dir.txt"]):
                raise ValueError(f"{name}: snapshot directory listing omitted fixture entries")
            if b"cli-dir" not in outputs["snapshot-dir-dirs.txt"] or b"cli-file" in outputs["snapshot-dir-dirs.txt"]:
                raise ValueError(f"{name}: DIRS filter is incorrect")
            if b"cli-file" not in outputs["snapshot-dir-files.txt"] or b"cli-dir" in outputs["snapshot-dir-files.txt"]:
                raise ValueError(f"{name}: FILES filter is incorrect")
            if b"cli-file" not in outputs["snapshot-inspect.txt"] or b"cli-dir" not in outputs["snapshot-inspect.txt"]:
                raise ValueError(f"{name}: snapshot inspection omitted fixture entries")
            if (b"cli-file" not in outputs["snapshot-inspect-files.txt"] or
                    b"cli-dir" in outputs["snapshot-inspect-files.txt"]):
                raise ValueError(f"{name}: INSPECT FILES filter is incorrect")
            if b"Mounted snapshot" in outputs["snapshot-mount-collision.txt"]:
                raise ValueError(f"{name}: failed snapshot mount published a target")
        print(f"PASS {name}", flush=True)


if __name__ == "__main__":
    main()
