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


def alter_copy(image, slot, options=False, damaged=False):
    with image.open("r+b") as stream:
        offset = image.stat().st_size // 2 if slot else 0
        stream.seek(offset)
        header = bytearray(stream.read(240))
        if struct.unpack_from(">II", header) != (0x42465300, 2):
            raise ValueError("fixture does not contain a v2 superblock")
        if zlib.crc32(header[:236]) != struct.unpack_from(">I", header, 236)[0]:
            raise ValueError("fixture CRC is invalid before mutation")
        struct.pack_into(">I", header, 56 if options else 4, 0x80000000 if options else 3)
        if not damaged:
            struct.pack_into(">I", header, 236, zlib.crc32(header[:236]))
        stream.seek(offset)
        stream.write(header)


def prepare_media():
    runtime = ROOT / "build/emulator"
    runtime.mkdir(parents=True, exist_ok=True)
    rom = runtime / "aros"
    run_checked([ROOT / "tools/install-aros-rom.sh", rom])
    work = Path(tempfile.mkdtemp(prefix="run.compat-", dir=runtime))
    print(f"Compatibility evidence: {work}", flush=True)
    clean = work / "clean.hdf"
    with clean.open("wb") as stream:
        stream.truncate(32 * 1024 * 1024)
    run_checked([ROOT / "build/host/mkbfs", clean, "4096", "Compat"])
    return work, rom, clean


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--handler", type=Path, default=ROOT / "build/amiga/bfshandler")
    args = parser.parse_args()
    emulator = shutil.which("fs-uae")
    if not emulator:
        raise ValueError("fs-uae is required")
    work, rom, clean = prepare_media()
    scenarios = [("v2", None, False, False, 0),
                 ("new-primary", 0, False, False, 3),
                 ("new-backup", 1, False, False, 3),
                 ("options-primary", 0, True, False, 2),
                 ("options-backup", 1, True, False, 2),
                 ("damaged-version", 0, False, True, 0)]
    for name, slot, options, damaged, expected in scenarios:
        case = work / name
        system = case / "system"
        for directory in ("C", "L", "S"):
            (system / directory).mkdir(parents=True)
        shutil.copyfile(args.handler, system / "L/bfshandler")
        shutil.copyfile(ROOT / "build/amiga/compatibility-probe", system / "C/compatibility-probe")
        shutil.copyfile(ROOT / "build/amiga/bfsformat", system / "C/bfsformat")
        image = case / "test.hdf"
        shutil.copyfile(clean, image)
        if slot is not None:
            alter_copy(image, slot, options, damaged)
        before = hashlib.sha256(image.read_bytes()).digest()
        startup = "FailAt 21\nC:bfsformat DH1: Test >SYS:format-message.txt\n" if expected else ""
        (system / "S/Startup-Sequence").write_text(
            startup + f"C:compatibility-probe {expected}\n", encoding="ascii")
        config = case / "test.fs-uae"
        config.write_text(f"""[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
kickstart_file = {rom / 'aros-amiga-m68k-rom.bin'}
kickstart_ext_file = {rom / 'aros-amiga-m68k-ext.bin'}
hard_drive_0 = {system}
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
        result = system / "compatibility.result"
        run_emulator(command, result, case / "fs-uae.log", 90)
        if result.read_bytes() != b"PASS\n":
            raise ValueError(f"{name}: guest compatibility probe failed")
        if expected and hashlib.sha256(image.read_bytes()).digest() != before:
            raise ValueError(f"{name}: incompatible media was modified")
        if expected:
            diagnosis = (system / "diagnosis.txt").read_bytes()
            if (system / "format-message.txt").read_bytes() != diagnosis + b"\n":
                raise ValueError(f"{name}: bfsformat did not report the driver diagnosis")
        print(f"PASS {name}", flush=True)


if __name__ == "__main__":
    main()
