#!/usr/bin/env python3
"""Run the BFS integration test on native AROS x86_64 in QEMU.

The AROS-NX boot ISO is extended by the handler, a Mountlist for a raw BFS
disk on the first AHCI port, the test program and an S:User-Startup that runs
it. QEMU emulates the q35 machine that `aros test` boots AROS-NX on. The test program mirrors its result log to the debug console, which
QEMU writes to a file; the log is verified like the FS-UAE result.
"""
import argparse
import hashlib
import shutil
import subprocess  # nosec B404
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_runner import stop_process_group, test_inventory, verify_result  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
LOG_PREFIX = "BFS-LOG\t"
DONE_PREFIX = "BFS-DONE\t"
DISK_MIB = 32
SECTORS_PER_TRACK = 32

MOUNTLIST = f"""FileSystem = L:bfshandler
Stacksize = 65536
Priority  = 5
GlobVec   = -1
Device    = ahci.device
Unit      = 0
Surfaces  = 1
BlocksPerTrack = {SECTORS_PER_TRACK}
BlockSize = 512
LowCyl    = 0
HighCyl   = {DISK_MIB * 2048 // SECTORS_PER_TRACK - 1}
Buffers   = 50
BufMemType = 0
DosType   = 0x42465300
Activate  = 1
"""


def run_checked(argv, **kwargs):
    # Trusted local argv, resolved executables and no shell.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args, python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    return subprocess.run([str(value) for value in argv], check=True, shell=False, **kwargs)  # nosec B603


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def user_startup(profile, filter_name):
    arguments = ["BFS0:", "SERIAL"]
    if filter_name:
        arguments.append(filter_name)
    if profile == "quick":
        arguments.append("QUICK")
    return "; BFS integration test, written by emulator-test/aros-ci-test.py\n" \
           "Stack 65536\n" \
           f"C:bfs-test {' '.join(arguments)}\n"


def build_iso(base_iso, handler, test_binary, run_dir, profile, filter_name):
    staging = run_dir / "staging"
    staging.mkdir()
    # The CD file system takes the protection bits from the Rock Ridge
    # modes; without the execute bits AROS refuses to run the program.
    for source, name in ((handler, "bfshandler"), (test_binary, "bfs-test")):
        shutil.copyfile(source, staging / name)
        (staging / name).chmod(0o755)
    (staging / "BFS0").write_text(MOUNTLIST, encoding="ascii")
    (staging / "User-Startup").write_text(user_startup(profile, filter_name), encoding="ascii")
    iso = run_dir / "bfs-test.iso"
    run_checked([shutil.which("xorriso"), "-indev", base_iso, "-outdev", iso,
                 "-map", staging / "bfshandler", "/L/bfshandler",
                 "-map", staging / "bfs-test", "/C/bfs-test",
                 "-map", staging / "BFS0", "/Devs/DOSDrivers/BFS0",
                 "-map", staging / "User-Startup", "/S/User-Startup",
                 "-boot_image", "any", "replay"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso


def wait_for_completion(command, serial, log, timeout):
    deadline = time.monotonic() + timeout
    with log.open("wb") as output:
        # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args, python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,  # nosec B603
                                   shell=False, start_new_session=True)
        try:
            while True:
                if serial.exists() and DONE_PREFIX.encode() in serial.read_bytes():
                    return
                if process.poll() is not None:
                    raise ValueError("QEMU exited before the test completed")
                if time.monotonic() >= deadline:
                    raise ValueError("QEMU timed out before the test completed")
                time.sleep(0.5)
        finally:
            stop_process_group(process)


def extract_result(serial, result):
    """Write the mirrored result log and its completion record."""
    log_lines, done = [], None
    for raw in serial.read_bytes().decode("ascii", errors="replace").splitlines():
        line = raw.rstrip("\r")
        if line.startswith(LOG_PREFIX):
            log_lines.append(line[len(LOG_PREFIX):])
        elif line.startswith(DONE_PREFIX):
            done = line[len(DONE_PREFIX):]
    result.write_text("".join(f"{line}\n" for line in log_lines), encoding="ascii")
    if done is not None:
        result.with_name(result.name + ".done").write_bytes(f"{done}\n".encode("ascii"))


def check_inputs(parser, args):
    for tool in ("qemu-system-x86_64", "xorriso"):
        if not shutil.which(tool):
            parser.error(f"{tool} is required")
    for path in (args.iso, args.handler, args.test_binary, args.bfs):
        if not path.is_file():
            parser.error(f"not found: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--handler", type=Path, required=True)
    parser.add_argument("--test-binary", type=Path, required=True)
    parser.add_argument("--bfs", type=Path, required=True, help="host bfs command")
    parser.add_argument("--profile", choices=("full", "quick"), default="full")
    parser.add_argument("--filter", default="")
    parser.add_argument("--timeout", type=int, default=3600)
    parser.add_argument("--memory", type=int, default=512)
    args = parser.parse_args()
    check_inputs(parser, args)

    runtime = ROOT / "build/emulator"
    runtime.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="run.aros-", dir=runtime))
    print(f"AROS x86_64 evidence directory: {run_dir}", flush=True)
    names = test_inventory(ROOT / "tools/bfs-test-cases.def", args.filter)
    for path in (args.iso, args.handler, args.test_binary):
        print(f"{digest(path)}  {path}", flush=True)

    disk = run_dir / "bfs.img"
    with disk.open("wb") as image:
        image.truncate(DISK_MIB * 1024 * 1024)
    run_checked([args.bfs, "format", disk, "--label", "BFSTest", "--block-size", "4096"],
                stdout=subprocess.DEVNULL)
    iso = build_iso(args.iso, args.handler, args.test_binary, run_dir,
                    args.profile, args.filter)
    serial = run_dir / "serial.log"
    command = [shutil.which("qemu-system-x86_64"), "-machine", "q35", "-cpu", "qemu64",
               "-smp", "1", "-m", str(args.memory),
               "-drive", f"file={disk},format=raw,if=ide,index=0,media=disk",
               "-cdrom", str(iso), "-boot", "d",
               "-serial", f"file:{serial}", "-display", "none",
               "-monitor", "none", "-no-reboot"]
    result = run_dir / "bfs-test.result"
    try:
        wait_for_completion(command, serial, run_dir / "qemu.log", args.timeout)
        extract_result(serial, result)
        print(verify_result(result, args.profile, names), end="")
        print(f"AROS x86_64 integration test passed ({len(names)} checks).")
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        if serial.is_file():
            print(f"--- {serial} (tail) ---", file=sys.stderr)
            print(serial.read_bytes()[-8192:].decode("ascii", errors="replace"), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
