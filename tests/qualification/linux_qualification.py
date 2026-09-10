#!/usr/bin/env python3
"""Run and record the reproducible M7 Linux FUSE qualification matrix."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess  # nosec B404 - repository build products are invoked without a shell
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MATRIX = Path(__file__).with_name("linux-m7-matrix.json")
LEGAL_BLOCK_SIZES = (1024, 2048, 4096, 8192, 16384, 32768, 65536)
OUTPUT_TAIL_LIMIT = 8192


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_matrix(path):
    matrix = json.loads(path.read_text(encoding="ascii"))
    require(matrix.get("format_version") == 1, "unsupported matrix format")
    require(matrix.get("dispatcher", {}).get("function") == "fuse_session_loop",
            "matrix must require the serialized dispatcher")
    require(matrix.get("dispatcher", {}).get("profile") ==
            "serialized callbacks; concurrent client processes may queue requests",
            "matrix must describe the serialized callback profile")
    require(matrix.get("seed_corpus") == [1, 2, 3, 4, 5, 20260910],
            "matrix seed corpus changed without a new format version")
    fast = matrix.get("fast", {})
    require(tuple(fast.get("block_sizes", ())) == LEGAL_BLOCK_SIZES,
            "matrix must cover every legal BFS block size")
    require(fast.get("block_count", 0) >= 64, "matrix volume is too small")
    require(fast.get("operation_timeout_seconds", 0) > 0, "matrix timeout is invalid")
    soak = matrix.get("soak", {})
    require(soak.get("target_duration_seconds") == 72 * 60 * 60,
            "M7 soak target must remain 72 hours")
    for field in ("cycle_seconds", "client_processes", "client_open_files",
                  "operation_deadline_seconds", "maximum_rss_kib",
                  "maximum_open_descriptors", "block_size", "block_count",
                  "minimum_available_bytes"):
        require(isinstance(soak.get(field), int) and soak[field] > 0,
                f"invalid soak limit: {field}")
    require(soak["block_size"] in LEGAL_BLOCK_SIZES, "soak block size is unsupported")
    require(soak["block_count"] >= 64, "soak volume is too small")
    require(soak["minimum_available_bytes"] >=
            soak["block_size"] * soak["block_count"],
            "soak capacity reserve is too small")
    return matrix


def git_executable():
    executable = shutil.which("git")
    require(executable is not None and os.path.isabs(executable), "git is required")
    return executable


def git_commit():
    completed = subprocess.run([git_executable(), "rev-parse", "HEAD"], cwd=ROOT, check=True,
                               capture_output=True, text=True)  # nosec B603
    return completed.stdout.strip()


def verify_dispatcher():
    source = (ROOT / "src/fuse/bfs_fuse.c").read_text(encoding="ascii")
    require("fuse_session_loop(session)" in source, "serialized FUSE dispatcher is absent")
    require("fuse_session_loop_mt" not in source, "multithreaded FUSE dispatcher is enabled")


def make_executable():
    executable = shutil.which("make")
    require(executable is not None and os.path.isabs(executable), "make is required")
    return executable


def output_tail(text):
    return text[-OUTPUT_TAIL_LIMIT:]


def make_record(name, command, timeout):
    started = time.monotonic()
    completed = subprocess.run(command, cwd=ROOT, check=False, capture_output=True, text=True,
                               timeout=timeout)  # nosec B603
    return {
        "name": name,
        "command": command,
        "duration_seconds": round(time.monotonic() - started, 3),
        "returncode": completed.returncode,
        "stdout_sha256": hashlib.sha256(completed.stdout.encode()).hexdigest(),
        "stderr_sha256": hashlib.sha256(completed.stderr.encode()).hexdigest(),
        "stdout_tail": output_tail(completed.stdout),
        "stderr_tail": output_tail(completed.stderr),
    }


def run_record(records, name, command, timeout):
    record = make_record(name, command, timeout)
    records.append(record)
    require(record["returncode"] == 0, f"{name} failed; inspect the evidence record")


def run_fast_matrix(matrix, records):
    timeout = matrix["fast"]["operation_timeout_seconds"]
    run_record(records, "direct-core-and-conformance",
               [make_executable(), "host-test", "conformance-test", "HOST_CC=gcc"], timeout)
    for block_size in matrix["fast"]["block_sizes"]:
        command = [sys.executable, "tests/fuse/test_fuse_mount.py", "--block-size",
                   str(block_size), "--block-count", str(matrix["fast"]["block_count"]),
                   "--hard-link", "--disk-pressure"]
        run_record(records, f"mounted-fuse-{block_size}", command, timeout)


def environment(matrix_path):
    return {
        "commit": git_commit(),
        "matrix_sha256": digest(matrix_path),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "dispatcher": "fuse_session_loop",
        "concurrency_profile": "serialized callbacks; concurrent client processes may queue requests",
    }


def write_record(output, matrix_path, records, status, error=None):
    result = environment(matrix_path)
    result.update({"records": records, "status": status})
    if error:
        result["error"] = error
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validate-only", action="store_true")
    arguments = parser.parse_args()
    matrix_path = arguments.matrix.resolve()
    matrix = load_matrix(matrix_path)
    if arguments.validate_only:
        return
    require(os.name == "posix" and Path("/dev/fuse").is_char_device(),
            "/dev/fuse is required; this is a missing qualification, not a skip")
    require(not arguments.output.exists(), "refusing to overwrite qualification evidence")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    records = []
    try:
        verify_dispatcher()
        run_fast_matrix(matrix, records)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        write_record(arguments.output, matrix_path, records, "failed", str(error))
        raise SystemExit(str(error)) from error
    write_record(arguments.output, matrix_path, records, "passed")


if __name__ == "__main__":
    main()
