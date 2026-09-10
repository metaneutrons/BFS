#!/usr/bin/env python3
"""Run the approved M7 Linux FUSE soak and preserve its evidence."""

import argparse
import datetime
import errno
import json
import multiprocessing
import os
from pathlib import Path
import platform
import queue
import re
import signal
import shutil
import subprocess  # nosec B404 - repository build products are invoked without a shell
import time

import linux_qualification


ROOT = linux_qualification.ROOT
FUSE = ROOT / "build" / "host" / "bfs-fuse"
FIXTURE = ROOT / "build" / "host" / "conformance-fixture-writer"
CHECKER = ROOT / "build" / "host" / "bfsfsck"
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"
APPROVAL_REFERENCE = re.compile(
    r"https://github\.com/metaneutrons/BFS/issues/29#issuecomment-[1-9][0-9]*\Z"
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(*command, timeout=60):
    return subprocess.run(command, cwd=ROOT, check=False, capture_output=True, text=True,
                          timeout=timeout)  # nosec B603


def mount(image, mountpoint, snapshot=None, read_write=True):
    command = [str(FUSE), "--image", str(image)]
    if read_write:
        command.append("--read-write")
    if snapshot:
        command.extend(["--snapshot", snapshot])
    command.append(str(mountpoint))
    process = subprocess.Popen(command,
                               cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)  # nosec B603
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if mountpoint.is_mount():
            return process
        if process.poll() is not None:
            _, errors = process.communicate()
            raise RuntimeError(f"bfs-fuse exited before mount: {errors}")
        time.sleep(0.05)
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
    try:
        _, errors = process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        _, errors = process.communicate(timeout=5)
    raise RuntimeError(f"bfs-fuse mount timed out: {errors}")


def unmount(process, mountpoint):
    completed = run("fusermount3", "-u", str(mountpoint))
    if completed.returncode != 0:
        if process.poll() is None:
            process.kill()
        _, errors = process.communicate(timeout=5)
        raise RuntimeError(f"fusermount3 failed: {completed.stderr}{errors}")
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired as error:
        process.kill()
        process.wait(timeout=5)
        raise RuntimeError("bfs-fuse did not exit after unmount") from error
    _, errors = process.communicate()
    require(process.returncode == 0, errors)


def daemon_resources(pid):
    status = Path(f"/proc/{pid}/status").read_text(encoding="ascii")
    rss_line = next(line for line in status.splitlines() if line.startswith("VmRSS:"))
    return {"rss_kib": int(rss_line.split()[1]), "open_descriptors": len(list(Path(f"/proc/{pid}/fd").iterdir()))}


def utc_timestamp():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def approval_reference_valid(reference):
    return isinstance(reference, str) and bool(APPROVAL_REFERENCE.fullmatch(reference.strip()))


def validate_approval_reference(reference, preflight):
    if preflight:
        require(reference is None or approval_reference_valid(reference),
                "approval reference must be an issue #29 approval comment URL")
    else:
        require(approval_reference_valid(reference),
                "approval reference must be an issue #29 approval comment URL")


def host_identity(storage_path, limits):
    require(shutil.which("findmnt") is not None, "findmnt is required for storage evidence")
    mount = run("findmnt", "--noheadings", "--output", "SOURCE,FSTYPE", "--target", str(storage_path))
    require(mount.returncode == 0 and mount.stdout.strip(), "cannot identify soak backing storage")
    storage = os.statvfs(storage_path)
    available_bytes = storage.f_frsize * storage.f_bavail
    require(available_bytes >= limits["minimum_available_bytes"], "insufficient available soak storage")
    meminfo = Path("/proc/meminfo").read_text(encoding="ascii")
    memory_line = next(line for line in meminfo.splitlines() if line.startswith("MemTotal:"))
    fuse = run("fusermount3", "--version")
    require(fuse.returncode == 0, "cannot identify libfuse version")
    return {
        "hostname": platform.node(),
        "kernel": platform.release(),
        "machine": platform.machine(),
        "cpu_count": os.cpu_count(),
        "memory_total_kib": int(memory_line.split()[1]),
        "fuse_version": (fuse.stdout + fuse.stderr).strip(),
        "backing_storage": mount.stdout.split(),
        "available_bytes": available_bytes,
    }


def write_cycle_files(root, client_id, cycle, deadline, open_files, result):
    descriptors = []
    operations = 0
    try:
        directory = root / f"client-{client_id:02d}"
        directory.mkdir(parents=True, exist_ok=True)
        for slot in range(open_files):
            temporary = directory / f"cycle-{cycle:08d}-{slot:02d}.tmp"
            final = directory / f"cycle-{cycle:08d}-{slot:02d}"
            payload = f"{client_id}:{cycle}:{slot}:M7\n".encode("ascii") * 64
            descriptor = os.open(temporary, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
            descriptors.append((descriptor, temporary, final, payload))
            require(os.write(descriptor, payload) == len(payload), "short client write")
            os.fsync(descriptor)
            require(os.pread(descriptor, len(payload), 0) == payload, "client readback differs")
            operations += 3
        for descriptor, temporary, final, payload in descriptors:
            os.close(descriptor)
            os.rename(temporary, final)
            linked = final.with_suffix(".link")
            os.link(final, linked)
            require(linked.read_bytes() == payload, "hard-link readback differs")
            linked.unlink()
            final.unlink()
            operations += 5
        descriptors.clear()
        if time.monotonic() > deadline:
            raise RuntimeError("operation deadline exceeded")
        result.put({"operations": operations, "error": None})
    except (OSError, RuntimeError) as error:
        result.put({"operations": operations, "error": str(error)})
    finally:
        for descriptor, _, _, _ in descriptors:
            try:
                os.close(descriptor)
            except OSError:
                pass


def run_clients(root, cycle, limits):
    context = multiprocessing.get_context("fork")
    results = context.Queue()
    deadline = time.monotonic() + limits["operation_deadline_seconds"]
    clients = [context.Process(target=write_cycle_files,
                               args=(root, client, cycle, deadline,
                                     limits["client_open_files"], results))
               for client in range(limits["client_processes"])]
    try:
        for client in clients:
            client.start()
        for client in clients:
            client.join(timeout=max(0, deadline - time.monotonic()))
            require(not client.is_alive(), "client operation deadline exceeded")
            require(client.exitcode == 0, f"client exited with {client.exitcode}")
        reports = [results.get(timeout=1) for _ in clients]
    except queue.Empty as error:
        raise RuntimeError("client completed without a result") from error
    finally:
        for client in clients:
            if client.is_alive():
                client.terminate()
                client.join(timeout=5)
    failures = [report["error"] for report in reports if report["error"]]
    require(not failures, f"client workload failed: {failures}")
    return sum(report["operations"] for report in reports)


def verify_image(image):
    for command in ((str(ORACLE), str(image)), (str(CHECKER), str(image))):
        completed = run(*command)
        require(completed.returncode == 0, completed.stderr + completed.stdout)


def append_event(path, event):
    with path.open("a", encoding="ascii") as stream:
        stream.write(json.dumps(event, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def prepare_image(output, limits):
    image = output / "soak.bfs"
    completed = run(str(FIXTURE), str(image), "--directory-scale", "--hard-link",
                    "--block-size", str(limits["block_size"]),
                    "--block-count", str(limits["block_count"]))
    require(completed.returncode == 0, "cannot create soak fixture")
    verify_image(image)
    return image


def exercise_fragmentation_and_disk_pressure(root, cycle, limits):
    pressure_root = root / "pressure"
    pressure_root.mkdir(exist_ok=True)
    fragments = []
    operations = 0
    for index in range(16):
        path = pressure_root / f"fragment-{index:02d}"
        payload = bytes([index]) * limits["block_size"]
        path.write_bytes(payload)
        fragments.append((path, payload))
        operations += 1
    for path, _ in fragments[::2]:
        path.unlink()
        operations += 1
    for path, payload in fragments[1::2]:
        require(path.read_bytes() == payload, f"fragment data differs for {path.name}")
        operations += 1

    pressure = pressure_root / f"pressure-{cycle:08d}"
    descriptor = os.open(pressure, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
    writes = 0
    exhausted = False
    try:
        payload = b"P" * limits["block_size"]
        for _ in range(limits["block_count"] * 2):
            try:
                require(os.write(descriptor, payload) == len(payload), "short disk-pressure write")
                writes += 1
            except OSError as error:
                require(error.errno == errno.ENOSPC, f"expected ENOSPC, got {error}")
                exhausted = True
                break
        require(exhausted, "disk-pressure workload did not exhaust the volume")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    pressure.unlink()
    recovered = pressure_root / "recovered-space"
    recovered.write_bytes(b"recovered")
    require(recovered.read_bytes() == b"recovered", "space did not recover after unlink")
    recovered.unlink()
    for path, _ in fragments[1::2]:
        path.unlink()
    return {"operations": operations + writes + 4, "pressure_writes": writes}


def verify_snapshot(image, mountpoint):
    process = mount(image, mountpoint, snapshot="oracle-snapshot", read_write=False)
    try:
        require((mountpoint / "oracle.txt").read_bytes() == b"oracle contents",
                "snapshot contents differ")
        try:
            (mountpoint / "snapshot-mutation").write_bytes(b"denied")
        except OSError as error:
            require(error.errno == errno.EROFS, f"snapshot write returned {error}")
        else:
            raise RuntimeError("snapshot mount accepted a write")
    finally:
        unmount(process, mountpoint)


def run_cycle(image, mountpoint, cycle, limits):
    process = mount(image, mountpoint)
    try:
        client_operations = run_clients(mountpoint / "soak", cycle, limits)
        pressure = exercise_fragmentation_and_disk_pressure(mountpoint / "soak", cycle, limits)
        resources = daemon_resources(process.pid)
        require(resources["rss_kib"] <= limits["maximum_rss_kib"], "daemon RSS limit exceeded")
        require(resources["open_descriptors"] <= limits["maximum_open_descriptors"],
                "daemon descriptor limit exceeded")
    finally:
        unmount(process, mountpoint)
    verify_image(image)
    verify_snapshot(image, mountpoint)
    verify_image(image)
    return {"operations": client_operations + pressure["operations"],
            "pressure_writes": pressure["pressure_writes"], "resources": resources}


def next_cycle_wait(cycle_seconds, duration, elapsed, cycle_elapsed):
    return max(0, min(cycle_seconds - cycle_elapsed, duration - elapsed))


def run_soak(image, output, limits, duration):
    events = output / "events.jsonl"
    mountpoint = output / "mount"
    mountpoint.mkdir()
    started = time.monotonic()
    cycle = 0
    while time.monotonic() - started < duration:
        cycle_started = time.monotonic()
        result = run_cycle(image, mountpoint, cycle, limits)
        append_event(events, {"cycle": cycle, "elapsed_seconds": round(time.monotonic() - started, 3),
                              "cycle_seconds": round(time.monotonic() - cycle_started, 3),
                              "operations": result["operations"],
                              "pressure_writes": result["pressure_writes"],
                              "resources": result["resources"], "status": "passed"})
        cycle += 1
        elapsed = time.monotonic() - started
        remaining = next_cycle_wait(limits["cycle_seconds"], duration, elapsed,
                                    time.monotonic() - cycle_started)
        if remaining > 0:
            time.sleep(remaining)
    return {"completed_cycles": cycle, "completed_duration_seconds": round(time.monotonic() - started, 3)}


def select_duration(limits, requested, preflight):
    duration = requested or limits["target_duration_seconds"]
    require(duration > 0, "soak duration is invalid")
    if preflight:
        require(duration < limits["target_duration_seconds"],
                "a preflight must be shorter than the target soak")
    else:
        require(duration == limits["target_duration_seconds"],
                "only an explicit preflight may use a shorter soak duration")
    return duration


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", type=Path, default=linux_qualification.DEFAULT_MATRIX)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--approval-reference")
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument("--preflight", action="store_true")
    arguments = parser.parse_args()
    matrix_path = arguments.matrix.resolve()
    matrix = linux_qualification.load_matrix(matrix_path)
    limits = matrix["soak"]
    duration = select_duration(limits, arguments.duration_seconds, arguments.preflight)
    validate_approval_reference(arguments.approval_reference, arguments.preflight)
    require(not arguments.output.exists(), "refusing to overwrite soak evidence")
    require(Path("/dev/fuse").is_char_device(), "/dev/fuse is required for the soak")
    require(Path("/proc/meminfo").is_file(), "/proc resource metrics are required for the soak")
    require(FUSE.is_file() and FIXTURE.is_file() and CHECKER.is_file(), "build soak prerequisites first")
    arguments.output.mkdir(parents=True)
    evidence = {"approval_reference": arguments.approval_reference,
                "environment": linux_qualification.environment(matrix_path),
                "limits": limits, "requested_duration_seconds": duration, "preflight": arguments.preflight,
                "started_at_utc": utc_timestamp()}
    try:
        linux_qualification.verify_dispatcher()
        evidence["host"] = host_identity(arguments.output.parent, limits)
        result = run_soak(prepare_image(arguments.output, limits), arguments.output, limits, duration)
        evidence.update(result)
        evidence["qualified"] = (not arguments.preflight and
                                  result["completed_duration_seconds"] >= limits["target_duration_seconds"])
        evidence["status"] = "passed"
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        evidence.update({"status": "failed", "error": str(error), "qualified": False})
        evidence["completed_at_utc"] = utc_timestamp()
        arguments.output.joinpath("result.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="ascii")
        raise SystemExit(str(error)) from error
    evidence["completed_at_utc"] = utc_timestamp()
    arguments.output.joinpath("result.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
