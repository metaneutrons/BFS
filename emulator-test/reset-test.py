#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check that a keyboard reset commits writes the delayed-commit timer still holds.

The guest appends numbered records to a BFS volume and reports each accepted
record on the host-visible system drive. The host presses Ctrl-Amiga-Amiga in
the emulator window, waits until the guest stops, and checks the image: it
must be clean and hold every reported record.

The image is checked on the host because the AROS ROM does not boot again
after the software reboot it performs once all reset handlers have answered;
plain ColdReboot() stops the same way without BFS. With --kickstart the test
runs on a Kickstart ROM instead and also requires the machine to boot again.
"""

import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import signal
import subprocess  # nosec B404
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
RECORD_SIZE = 16


def run_checked(argv, **kwargs):
    # Fixed local tools and arguments, no shell or guest-provided commands.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args, python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    return subprocess.run([str(value) for value in argv], check=True, shell=False, **kwargs)  # nosec B603


def wait_for(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() >= deadline:
            raise ValueError(f"timed out waiting for {what}")
        time.sleep(0.1)


def record(sequence):
    return b"REC %08x ok\n" % sequence


def progress_sequence(path):
    try:
        data = path.read_bytes()
    except OSError:
        return -1
    if len(data) != RECORD_SIZE or not data.startswith(b"REC "):
        return -1
    return int(data[4:12], 16)


def free_display():
    for number in range(91, 120):
        if not Path(f"/tmp/.X{number}-lock").exists():
            return f":{number}"
    raise ValueError("no free X display")


def rom_lines(rom, kickstart):
    if kickstart:
        return f"kickstart_file = {kickstart}\n"
    return (f"kickstart_file = {rom / 'aros-amiga-m68k-rom.bin'}\n"
            f"kickstart_ext_file = {rom / 'aros-amiga-m68k-ext.bin'}\n")


def prepare(work, rom, handler, probe, kickstart=None, workbench=None):
    system = work / "system"
    for directory in ("C", "L", "S"):
        (system / directory).mkdir(parents=True)
    shutil.copyfile(handler, system / "L/bfshandler")
    shutil.copyfile(probe, system / "C/reset-probe")
    if workbench:
        # Kickstart has no Wait command in ROM.
        shutil.copyfile(workbench / "C/Wait", system / "C/Wait")
    # The writer runs on the first boot only; a later boot must not touch DH1.
    (system / "S/Startup-Sequence").write_text(
        "FailAt 21\nIf EXISTS SYS:reset-started\n  Echo >SYS:reset-rebooted \"\"\n"
        "  Wait 600\nElse\n"
        "  Echo >SYS:reset-started \"\"\n  C:reset-probe\nEndIf\n", encoding="ascii")
    image = work / "reset.hdf"
    with image.open("wb") as stream:
        stream.truncate(32 * 1024 * 1024)
    run_checked([ROOT / "build/host/bfs", "format", image, "--label", "Reset",
                 "--block-size", "4096"], stdout=subprocess.DEVNULL)
    config = work / "test.fs-uae"
    config.write_text(f"""[fs-uae]
amiga_model = A1200
chip_memory = 2048
fast_memory = 8192
cpu = 68040
uae_cpu_speed = max
uae_cpu_24bit_addressing = false
{rom_lines(rom, kickstart)}hard_drive_0 = {system}
hard_drive_0_label = System
hard_drive_0_priority = 0
hard_drive_1 = {image}
hard_drive_1_file_system = {system / 'L/bfshandler'}
audio_driver = null
fullscreen = 0
automatic_input_grab = 0
""", encoding="utf-8")
    return system, image, config


def press_reset(display):
    environment = dict(os.environ, DISPLAY=display)
    window = run_checked(["xdotool", "search", "--sync", "--onlyvisible", "--class", "fs-uae"],
                         env=environment, capture_output=True, text=True).stdout.split()[0]
    run_checked(["xdotool", "mousemove", "--window", window, "40", "40"], env=environment)
    run_checked(["xdotool", "windowfocus", window], env=environment)
    run_checked(["xdotool", "keydown", "Control_L", "Super_L", "Super_R"], env=environment)
    time.sleep(1.0)
    run_checked(["xdotool", "keyup", "Super_R", "Super_L", "Control_L"], env=environment)


def wait_until_stopped(progress, timeout):
    """The guest has stopped once the progress file no longer changes."""
    deadline = time.monotonic() + timeout
    last, stable_since = progress_sequence(progress), time.monotonic()
    while time.monotonic() - stable_since < 3.0:
        if time.monotonic() >= deadline:
            raise ValueError("the guest kept writing after the reset")
        time.sleep(0.2)
        current = progress_sequence(progress)
        if current != last:
            last, stable_since = current, time.monotonic()
    return last


def load_oracle():
    spec = importlib.util.spec_from_file_location("format_oracle",
                                                  ROOT / "tools/bfs-format-oracle.py")
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    return oracle


def check_image(image, reported):
    run_checked([ROOT / "build/host/bfs", "check", image], stdout=subprocess.DEVNULL)
    oracle = load_oracle()
    data = image.read_bytes()
    namespace, _ = oracle.build_manifest(data, oracle.select_superblock(data))
    item = next(entry for entry in namespace if entry["path"] == "/reset-records")
    count, partial = divmod(item["size"], RECORD_SIZE)
    expected = hashlib.sha256(b"".join(record(i) for i in range(count))).hexdigest()
    if partial or item["sha256"] != expected:
        raise ValueError("the record file holds unexpected bytes")
    print(f"records on volume {count}, last reported {reported + 1}", flush=True)
    if count < reported + 1:
        raise ValueError("records reported before the reset are missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--handler", type=Path, default=ROOT / "build/amiga/bfshandler")
    parser.add_argument("--probe", type=Path, default=ROOT / "build/amiga/reset-probe")
    parser.add_argument("--write-seconds", type=float, default=4.0)
    parser.add_argument("--kickstart", type=Path,
                        help="Kickstart ROM to use instead of the AROS ROM")
    parser.add_argument("--workbench", type=Path,
                        help="Workbench tree whose C:Wait the Kickstart run needs")
    args = parser.parse_args()
    if bool(args.kickstart) != bool(args.workbench):
        raise ValueError("--kickstart and --workbench go together")
    for tool in ("fs-uae", "Xvfb", "xdotool"):
        if not shutil.which(tool):
            raise ValueError(f"{tool} is required")
    runtime = ROOT / "build/emulator"
    runtime.mkdir(parents=True, exist_ok=True)
    rom = runtime / "aros"
    run_checked([ROOT / "tools/install-aros-rom.sh", rom])
    work = Path(tempfile.mkdtemp(prefix="run.reset-", dir=runtime))
    print(f"Reset evidence: {work}", flush=True)
    system, image, config = prepare(work, rom, args.handler, args.probe,
                                    args.kickstart, args.workbench)

    display = free_display()
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1024x768x24"],  # nosec B603 B607
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    emulator = None
    try:
        time.sleep(1.0)
        environment = dict(os.environ, DISPLAY=display, FSEMU_AUDIO_DRIVER="null")
        with (work / "fs-uae.log").open("wb") as log:
            # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
            emulator = subprocess.Popen([shutil.which("fs-uae"), str(config)],  # nosec B603
                                        stdout=log, stderr=subprocess.STDOUT,
                                        env=environment, start_new_session=True)
            progress = system / "reset-progress"
            wait_for(lambda: progress_sequence(progress) >= 0, 120, "the writer")
            time.sleep(args.write_seconds)
            press_reset(display)
            reported = wait_until_stopped(progress, 30)
            if args.kickstart:
                # Kickstart reboots once every reset handler has answered.
                wait_for((system / "reset-rebooted").exists, 60, "the reboot")
                print("machine booted again after the reset", flush=True)
    finally:
        for process in (emulator, xvfb):
            if process and process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
    check_image(image, reported)
    print("PASS reset", flush=True)


if __name__ == "__main__":
    main()
