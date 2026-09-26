#!/usr/bin/env python3
"""Run the bounded, disposable-image fault and corruption campaign for issue #28."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import subprocess  # nosec B404 - only repository-controlled tools are invoked
import sys
import time
import zlib


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = Path(__file__).with_name("fault-campaign-v1.json")
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"
OUTPUT_TAIL_LIMIT = 8192
CASE_ID_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyz0123456789-")
OUTCOME_CLASSES = frozenset(("consistent", "recoverable", "repairable-leak", "rejected",
                             "inconclusive-device"))
CASE_TYPES = frozenset(("image-mutation", "repairable-leak", "adapter-suite"))
MUTATIONS = frozenset(("none", "primary-superblock-crc-invalid",
                       "both-superblocks-crc-invalid", "directory-root-zero-key-count",
                       "both-superblocks-unsupported-options", "fixture-leaked-allocation",
                       "checksummed-data-byte-flip", "crash-injection-suite",
                       "posix-fault-adapter-suite"))
ADAPTERS = {
    "crash-injection": ROOT / "build" / "host" / "test_crash_inject",
    "posix-transport": ROOT / "build" / "host" / "test_posix_faults",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest(path):
    return digest_bytes(path.read_bytes())


def regular_file(path, description):
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError as error:
        raise RuntimeError(f"{description} is missing: {path}") from error
    require(stat.S_ISREG(mode), f"{description} must be a regular file: {path}")


def directory(path, description):
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError as error:
        raise RuntimeError(f"{description} is missing: {path}") from error
    require(stat.S_ISDIR(mode), f"{description} must be a directory: {path}")


def path_within(path, root):
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise RuntimeError(f"path escapes owned evidence directory: {path}") from error


def executable(path, description):
    regular_file(path, description)
    require(os.access(path, os.X_OK), f"{description} is not executable: {path}")


def git_commit():
    git = shutil.which("git")
    require(git and os.path.isabs(git), "git is required to record source identity")
    completed = subprocess.run([git, "rev-parse", "HEAD"], cwd=ROOT, check=True,
                               capture_output=True, text=True)  # nosec B603
    return completed.stdout.strip()


def output_tail(value):
    return value[-OUTPUT_TAIL_LIMIT:]


def command_record(name, command, timeout, cwd=ROOT):
    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=cwd, check=False, capture_output=True, text=True,
                                   timeout=timeout)  # nosec B603 - commands are repository controlled
        stdout, stderr, returncode = completed.stdout, completed.stderr, completed.returncode
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = (error.stderr or "") + f"\nTimed out after {timeout} seconds"
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        returncode = 124
    return {
        "name": name,
        "command": [str(item) for item in command],
        "cwd": str(cwd),
        "duration_seconds": round(time.monotonic() - started, 3),
        "returncode": returncode,
        "stdout_sha256": digest_bytes(stdout.encode()),
        "stderr_sha256": digest_bytes(stderr.encode()),
        "stdout_tail": output_tail(stdout),
        "stderr_tail": output_tail(stderr),
    }


def load_manifest(path):
    regular_file(path, "campaign manifest")
    try:
        manifest = json.loads(path.read_text(encoding="ascii"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid campaign manifest: {error}") from error
    require(manifest.get("format_version") == 1, "unsupported campaign manifest format")
    require(isinstance(manifest.get("suite_version"), str) and manifest["suite_version"],
            "campaign suite version is missing")
    geometry = manifest.get("geometry")
    require(isinstance(geometry, dict), "campaign geometry is missing")
    for field in ("block_size", "block_count", "format_options"):
        require(isinstance(geometry.get(field), int) and geometry[field] >= 0,
                f"campaign geometry field is invalid: {field}")
    require(isinstance(manifest.get("default_timeout_seconds"), int) and
            manifest["default_timeout_seconds"] > 0, "campaign timeout is invalid")
    cases = manifest.get("cases")
    require(isinstance(cases, list) and cases, "campaign cases are missing")
    seen = set()
    required = ("case_id", "suite_version", "case_type", "family", "scenario", "seed",
                "fault_model", "fault_point", "mutation", "mutation_location", "expected_class",
                "expected_states", "observers", "timeout_seconds")
    for case in cases:
        require(isinstance(case, dict), "campaign case must be an object")
        require(all(field in case for field in required), "campaign case is incomplete")
        identifier = case["case_id"]
        require(isinstance(identifier, str) and identifier and set(identifier) <= CASE_ID_CHARACTERS,
                f"invalid campaign case identifier: {identifier!r}")
        require(identifier not in seen, f"duplicate campaign case identifier: {identifier}")
        seen.add(identifier)
        require(case["suite_version"] == manifest["suite_version"],
                f"campaign case has mismatched suite version: {identifier}")
        require(case["case_type"] in CASE_TYPES, f"unsupported campaign case type: {identifier}")
        require(isinstance(case["family"], str) and case["family"],
                f"campaign family is invalid: {identifier}")
        require(isinstance(case["scenario"], str) and case["scenario"],
                f"campaign scenario is invalid: {identifier}")
        require(isinstance(case["seed"], int) and case["seed"] >= 0,
                f"campaign seed is invalid: {identifier}")
        require(isinstance(case["fault_model"], str) and case["fault_model"],
                f"campaign fault model is invalid: {identifier}")
        require(isinstance(case["fault_point"], str) and case["fault_point"],
                f"campaign fault point is invalid: {identifier}")
        require(isinstance(case["mutation"], dict) and case["mutation"].get("kind") in MUTATIONS,
                f"campaign mutation is invalid: {identifier}")
        require(isinstance(case["mutation_location"], str) and case["mutation_location"],
                f"campaign mutation location is invalid: {identifier}")
        require(case["expected_class"] in OUTCOME_CLASSES,
                f"unknown campaign outcome class: {identifier}")
        require(case["expected_class"] != "inconclusive-device",
                f"device outcome is outside the image campaign: {identifier}")
        require(isinstance(case["expected_states"], list) and case["expected_states"],
                f"campaign expected states are missing: {identifier}")
        require(isinstance(case["observers"], list) and case["observers"],
                f"campaign observers are missing: {identifier}")
        require(isinstance(case["timeout_seconds"], int) and case["timeout_seconds"] > 0,
                f"campaign timeout is invalid: {identifier}")
        if case["case_type"] == "adapter-suite":
            require(case.get("adapter") in ADAPTERS, f"unknown fault adapter: {identifier}")
        else:
            require("adapter" not in case, f"image case cannot declare an adapter: {identifier}")
        require(isinstance(case.get("fixture_leak", False), bool),
                f"campaign fixture leak flag is invalid: {identifier}")
        if case["case_type"] == "repairable-leak":
            require(case["expected_class"] == "repairable-leak",
                    f"repairable case has wrong outcome class: {identifier}")
    return manifest


def prepare_output(path, manifest_path):
    require(not path.exists() and not path.is_symlink(), "refusing to overwrite campaign evidence")
    try:
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    except OSError as error:
        raise RuntimeError(f"cannot create campaign evidence parent: {path.parent}") from error
    directory(path.parent, "campaign evidence parent")
    require(not path.parent.is_symlink(), "campaign evidence parent must not be a symlink")
    path.mkdir(mode=0o700)
    directory(path, "campaign evidence directory")
    copied_manifest = path / "manifest.json"
    shutil.copyfile(manifest_path, copied_manifest)
    regular_file(copied_manifest, "copied campaign manifest")
    return copied_manifest


def be32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def put_be32(data, offset, value):
    data[offset:offset + 4] = value.to_bytes(4, "big")


def superblock_offsets(data):
    require(len(data) >= 1024, "baseline image is too short")
    block_size = be32(data, 8)
    backup = int.from_bytes(data[96:104], "big")
    require(block_size >= 1024 and block_size & (block_size - 1) == 0,
            "baseline image has an invalid block size")
    require(backup + 512 <= len(data), "baseline image has an invalid backup superblock offset")
    return block_size, (0, backup)


def update_superblock_crc(data, offset):
    put_be32(data, offset + 236, zlib.crc32(data[offset:offset + 236]) & 0xffffffff)


def record_range(ranges, before, after, offset, length):
    ranges.append({
        "offset": offset,
        "length": length,
        "before_sha256": digest_bytes(before[offset:offset + length]),
        "after_sha256": digest_bytes(after[offset:offset + length]),
    })


def mutate_image(image, kind):
    regular_file(image, "campaign image")
    before = image.read_bytes()
    after = bytearray(before)
    block_size, slots = superblock_offsets(after)
    ranges = []
    if kind == "none":
        pass
    elif kind == "primary-superblock-crc-invalid":
        after[slots[0] + 236] ^= 1
        record_range(ranges, before, after, slots[0] + 236, 4)
    elif kind == "both-superblocks-crc-invalid":
        for slot in slots:
            after[slot + 236] ^= 1
            record_range(ranges, before, after, slot + 236, 4)
    elif kind == "directory-root-zero-key-count":
        root = be32(after, 24)
        node = root * block_size
        require(root and node + block_size <= len(after), "baseline directory root is invalid")
        put_be32(after, node + 16, 0)
        put_be32(after, node + 4, zlib.crc32(after[node:node + 4] + b"\0\0\0\0" +
                                               after[node + 8:node + block_size]) & 0xffffffff)
        record_range(ranges, before, after, node + 4, 16)
    elif kind == "both-superblocks-unsupported-options":
        for slot in slots:
            put_be32(after, slot + 56, 0x80000000)
            update_superblock_crc(after, slot)
            record_range(ranges, before, after, slot + 56, 184)
    elif kind == "checksummed-data-byte-flip":
        marker = b"live contents"
        offset = after.find(marker)
        require(offset >= 0, "baseline fixture has no live checksummed data")
        after[offset] ^= 1
        record_range(ranges, before, after, offset, 1)
    else:
        raise RuntimeError(f"unsupported image mutation: {kind}")
    image.write_bytes(after)
    regular_file(image, "mutated campaign image")
    return {"kind": kind, "ranges": ranges, "before_sha256": digest_bytes(before),
            "after_sha256": digest_bytes(after)}


def fixture_command(image, geometry, leaked):
    command = [str(ROOT / "build" / "host" / "conformance-fixture-writer"), str(image),
               "--block-size", str(geometry["block_size"]), "--block-count",
               str(geometry["block_count"]), "--format-options",
               str(geometry["format_options"])]
    if leaked:
        command.append("--leak")
    return command


def observe_consistent(image, timeout, commands):
    commands.append(command_record("oracle", [sys.executable, str(ORACLE), str(image)], timeout))
    commands.append(command_record("check", [str(ROOT / "build" / "host" / "bfs"), "check",
                                               str(image)], timeout))
    return all(command["returncode"] == 0 for command in commands[-2:])


def observe_rejected(image, timeout, commands, repair_refusal):
    commands.append(command_record("oracle", [sys.executable, str(ORACLE), str(image)], timeout))
    commands.append(command_record("check", [str(ROOT / "build" / "host" / "bfs"), "check",
                                               str(image)], timeout))
    rejected = all(command["returncode"] != 0 for command in commands[-2:])
    if repair_refusal:
        before = digest(image)
        commands.append(command_record("repair-refusal", [str(ROOT / "build" / "host" / "bfs"),
                                                            "check", str(image), "--repair"], timeout))
        rejected = rejected and commands[-1]["returncode"] != 0 and digest(image) == before
    return rejected


def observe_repairable_leak(image, timeout, commands):
    commands.append(command_record("oracle", [sys.executable, str(ORACLE), str(image)], timeout))
    commands.append(command_record("check", [str(ROOT / "build" / "host" / "bfs"), "check",
                                               str(image)], timeout))
    if commands[-2]["returncode"] != 0 or commands[-1]["returncode"] != 1:
        return False
    commands.append(command_record("repair", [str(ROOT / "build" / "host" / "bfs"), "check",
                                               str(image), "--repair"], timeout))
    commands.append(command_record("check-after-repair", [str(ROOT / "build" / "host" / "bfs"),
                                                             "check", str(image)], timeout))
    commands.append(command_record("oracle-after-repair", [sys.executable, str(ORACLE), str(image)],
                                   timeout))
    return (commands[-3]["returncode"] == 1 and commands[-2]["returncode"] == 0 and
            commands[-1]["returncode"] == 0)


def source_identity(manifest_path):
    binaries = {
        "bfs": ROOT / "build" / "host" / "bfs",
        "fixture_writer": ROOT / "build" / "host" / "conformance-fixture-writer",
        "crash_injection": ADAPTERS["crash-injection"],
        "posix_transport": ADAPTERS["posix-transport"],
        "oracle": ORACLE,
        "runner": Path(__file__).resolve(),
        "manifest": manifest_path,
    }
    for name, path in binaries.items():
        regular_file(path, f"campaign {name}")
    return {"source_commit": git_commit(),
            "binary_hashes": {name: digest(path) for name, path in binaries.items()}}


def execute_case(case, output, geometry, identity):
    case_directory = output / "cases" / case["case_id"]
    case_directory.mkdir(parents=True)
    path_within(case_directory, output)
    image = case_directory / "image.bfs"
    baseline = case_directory / "baseline.bfs"
    commands = []
    fixture = command_record("baseline-builder", fixture_command(baseline, geometry,
                             case.get("fixture_leak", False)), case["timeout_seconds"])
    commands.append(fixture)
    record = {
        "case_id": case["case_id"],
        "suite_version": case["suite_version"],
        "family": case["family"],
        "scenario": case["scenario"],
        "seed": case["seed"],
        "fault_model": case["fault_model"],
        "fault_point": case["fault_point"],
        "mutation_location": case["mutation_location"],
        "expected_class": case["expected_class"],
        "expected_states": case["expected_states"],
        "observer_commands": case["observers"],
        "timeout_seconds": case["timeout_seconds"],
        "source_commit": identity["source_commit"],
        "binary_hashes": identity["binary_hashes"],
        "commands": commands,
    }
    if fixture["returncode"] != 0 or not baseline.exists():
        record.update({"observed_class": "unexpected", "result": "failed",
                       "error": "baseline builder failed"})
        return record
    regular_file(baseline, "campaign baseline")
    record["baseline_sha256"] = digest(baseline)
    shutil.copyfile(baseline, image)
    regular_file(image, "campaign case image")
    try:
        if case["case_type"] == "adapter-suite":
            adapter = ADAPTERS[case["adapter"]]
            record["mutation"] = {"kind": case["mutation"]["kind"], "adapter": case["adapter"]}
            commands.append(command_record("adapter-suite", [str(adapter)], case["timeout_seconds"],
                                           cwd=case_directory))
            matched = commands[-1]["returncode"] == 0 and observe_consistent(
                image, case["timeout_seconds"], commands)
            observed = "consistent" if matched else "unexpected"
        else:
            if case["mutation"]["kind"] == "fixture-leaked-allocation":
                record["mutation"] = {"kind": "fixture-leaked-allocation", "ranges": [],
                                      "before_sha256": digest(image), "after_sha256": digest(image)}
            else:
                record["mutation"] = mutate_image(image, case["mutation"]["kind"])
            if case["expected_class"] == "consistent":
                matched = observe_consistent(image, case["timeout_seconds"], commands)
                observed = "consistent" if matched else "unexpected"
            elif case["expected_class"] == "rejected":
                matched = observe_rejected(image, case["timeout_seconds"], commands,
                                           "repair-refusal" in case["observers"])
                observed = "rejected" if matched else "unexpected"
            else:
                matched = observe_repairable_leak(image, case["timeout_seconds"], commands)
                observed = "repairable-leak" if matched else "unexpected"
        record.update({"observed_class": observed, "result": "passed" if
                       observed == case["expected_class"] else "failed"})
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        record.update({"observed_class": "unexpected", "result": "failed", "error": str(error)})
    if image.exists():
        regular_file(image, "campaign final image")
        record["final_sha256"] = digest(image)
    return record


def append_record(path, record):
    with path.open("a", encoding="ascii") as handle:
        handle.write(json.dumps(record, sort_keys=True, ensure_ascii=True) + "\n")
        handle.flush()
        os.fsync(handle.fileno())


def write_result(output, manifest, manifest_copy, identity, records):
    record_path = output / "cases.jsonl"
    result = {
        "format_version": 1,
        "suite_version": manifest["suite_version"],
        "qualification_scope": "bounded-modelled-image-and-adapter-campaign",
        "unqualified_scopes": ["mounted-fuse-surface", "physical-power-controller-media"],
        "status": "passed" if all(record["result"] == "passed" for record in records) else "failed",
        "case_count": len(records),
        "manifest_sha256": digest(manifest_copy),
        "cases_sha256": digest(record_path),
        "source_commit": identity["source_commit"],
        "binary_hashes": identity["binary_hashes"],
        "platform": platform.platform(),
        "python": platform.python_version(),
    }
    result["evidence_sha256"] = digest_bytes(canonical(result))
    with (output / "result.json").open("x", encoding="ascii") as handle:
        handle.write(json.dumps(result, indent=2, sort_keys=True) + "\n")
        handle.flush()
        os.fsync(handle.fileno())
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validate-only", action="store_true")
    arguments = parser.parse_args()
    manifest_path = arguments.manifest.resolve()
    manifest = load_manifest(manifest_path)
    if arguments.validate_only:
        return 0
    output = arguments.output.resolve()
    manifest_copy = prepare_output(output, manifest_path)
    identity = source_identity(manifest_copy)
    records = []
    record_path = output / "cases.jsonl"
    record_path.touch(exist_ok=False)
    for case in manifest["cases"]:
        record = execute_case(case, output, manifest["geometry"], identity)
        records.append(record)
        append_record(record_path, record)
    result = write_result(output, manifest, manifest_copy, identity, records)
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
