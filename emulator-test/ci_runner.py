#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Run one isolated emulator and accept only a complete guest test inventory."""

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

COMPLETION = b"BFS-TEST-COMPLETE\t1\n"


def test_inventory(path):
    names = []
    for line in path.read_text(encoding="ascii").splitlines():
        if line == "/* SPDX-License-Identifier: MPL-2.0 */":
            continue
        match = re.fullmatch(r"BFS_TEST\(([a-z0-9_]+), (test_[a-z0-9_]+)\)", line)
        if not match:
            raise ValueError("invalid guest test inventory")
        names.append(match[1])
    if not names or len(set(names)) != len(names):
        raise ValueError("empty or duplicate guest test inventory")
    return names


def verify_result(result, profile, names):
    if profile not in ("full", "quick"):
        raise ValueError("invalid integration profile")
    if not names or len(set(names)) != len(names):
        raise ValueError("empty or duplicate expected tests")
    if result.stat().st_size > 1024 * 1024:
        raise ValueError("guest result exceeds size limit")
    data = result.read_bytes()
    if not data.endswith(b"\n") or b"\r" in data:
        raise ValueError("truncated or invalid guest result")
    lines = data.decode("ascii").splitlines()
    expected_header = ["# BFS Test Log", f"# PROFILE\t{profile}", "# STATUS\tNAME\t[DETAIL]"]
    if lines[:3] != expected_header:
        raise ValueError("guest log header/profile mismatch")
    if len(lines) != len(names) + 4:
        raise ValueError("guest result inventory count mismatch")
    passed = 0
    for name, line in zip(names, lines[3:-1], strict=True):
        fields = line.split("\t")
        if fields == ["PASS", name]:
            passed += 1
        elif len(fields) == 3 and fields[:2] == ["FAIL", name] and fields[2]:
            continue
        else:
            raise ValueError(f"guest result inventory mismatch at {name}")
    failed = len(names) - passed
    if lines[-1] != f"# SUMMARY\t{passed}\t{len(names)}\t{failed}":
        raise ValueError("guest summary does not match individual results")
    if failed:
        raise ValueError(f"Amiga integration failed ({failed}/{len(names)} failed)")
    marker = result.with_name(result.name + ".done")
    if marker.read_bytes() != COMPLETION:
        raise ValueError("invalid guest completion record")
    return data.decode("ascii")


def stop_process_group(process):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    # A launcher may exit before its emulator or X server children do.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def run_emulator(command, result, log, timeout):
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    completion = result.with_name(result.name + ".done")
    if result.exists() or completion.exists():
        raise ValueError("stale guest result or completion record")
    def interrupted(signum, _frame):
        raise SystemExit(128 + signum)

    previous = signal.signal(signal.SIGTERM, interrupted)
    process = None
    try:
        with log.open("wb") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            deadline = time.monotonic() + timeout
            while not completion.exists():
                if process.poll() is not None:
                    raise ValueError("emulator exited before guest completion")
                if time.monotonic() >= deadline:
                    raise ValueError("emulator timed out before guest completion")
                time.sleep(0.1)
    finally:
        try:
            if process is not None:
                stop_process_group(process)
        finally:
            signal.signal(signal.SIGTERM, previous)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--profile", choices=("full", "quick"), required=True)
    parser.add_argument("--timeout", type=int, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("emulator command is required")
    try:
        names = test_inventory(args.inventory)
        run_emulator(command, args.result, args.log, args.timeout)
        print(verify_result(args.result, args.profile, names), end="")
        print(f"Amiga integration test passed ({len(names)} checks).")
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        for path in (args.result, args.log):
            if path.is_file():
                print(f"--- {path} ---", file=sys.stderr)
                with path.open("rb") as stream:
                    stream.seek(max(0, path.stat().st_size - 16384))
                    print(stream.read().decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
