#!/usr/bin/env python3
"""Run the approved M7 Linux FUSE soak and preserve its evidence."""

import argparse
import json
import multiprocessing
import os
from pathlib import Path
import queue
import signal
import subprocess  # nosec B404 - repository build products are invoked without a shell
import time

import linux_qualification


ROOT = linux_qualification.ROOT
FUSE = ROOT / "build" / "host" / "bfs-fuse"
FIXTURE = ROOT / "build" / "host" / "conformance-fixture-writer"
CHECKER = ROOT / "build" / "host" / "bfsfsck"
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(*command, timeout=60):
    return subprocess.run(command, cwd=ROOT, check=False, capture_output=True, text=True,
                          timeout=timeout)  # nosec B603


def mount(image, mountpoint):
    process = subprocess.Popen([str(FUSE), "--image", str(image), "--read-write", str(mountpoint)],
                               cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)  # nosec B603
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if mountpoint.is_mount():
            return process
        if process.poll() is not None:
            _, errors = process.communicate()
            raise RuntimeError(f"bfs-fuse exited before mount: {errors}")
        time.sleep(0.05)
    process.send_signal(signal.SIGTERM)
    _, errors = process.communicate(timeout=5)
    raise RuntimeError(f"bfs-fuse mount timed out: {errors}")


def unmount(process, mountpoint):
    completed = run("fusermount3", "-u", str(mountpoint))
    require(completed.returncode == 0, completed.stderr)
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


def write_cycle_file(root, client_id, cycle, deadline, result):
    try:
        directory = root / f"client-{client_id:02d}"
        directory.mkdir(exist_ok=True)
        temporary = directory / f"cycle-{cycle:08d}.tmp"
        final = directory / f"cycle-{cycle:08d}"
        payload = f"{client_id}:{cycle}:M7\n".encode("ascii") * 64
        with temporary.open("xb") as target:
            target.write(payload)
            target.flush()
            os.fsync(target.fileno())
        os.rename(temporary, final)
        if final.read_bytes() != payload:
            raise RuntimeError("readback differs")
        linked = directory / f"cycle-{cycle:08d}.link"
        os.link(final, linked)
        linked.unlink()
        final.unlink()
        if time.monotonic() > deadline:
            raise RuntimeError("operation deadline exceeded")
        result.put(None)
    except (OSError, RuntimeError) as error:
        result.put(str(error))


def run_clients(root, cycle, limits):
    context = multiprocessing.get_context("fork")
    results = context.Queue()
    deadline = time.monotonic() + limits["operation_deadline_seconds"]
    clients = [context.Process(target=write_cycle_file,
                               args=(root, client, cycle, deadline, results))
               for client in range(limits["client_processes"])]
    for client in clients:
        client.start()
    for client in clients:
        client.join(timeout=limits["operation_deadline_seconds"])
        require(client.exitcode == 0, f"client exited with {client.exitcode}")
    try:
        failures = [results.get(timeout=1) for _ in clients]
    except queue.Empty as error:
        raise RuntimeError("client completed without a result") from error
    require(not any(failures), f"client workload failed: {failures}")


def verify_image(image):
    for command in ((str(ORACLE), str(image)), (str(CHECKER), str(image))):
        completed = run(*command)
        require(completed.returncode == 0, completed.stderr + completed.stdout)


def append_event(path, event):
    with path.open("a", encoding="ascii") as stream:
        stream.write(json.dumps(event, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def prepare_image(output, block_count):
    image = output / "soak.bfs"
    completed = run(str(FIXTURE), str(image), "--directory-scale", "--hard-link",
                    "--block-size", "4096", "--block-count", str(block_count))
    require(completed.returncode == 0, "cannot create soak fixture")
    verify_image(image)
    return image


def run_cycle(image, mountpoint, cycle, limits):
    process = mount(image, mountpoint)
    try:
        run_clients(mountpoint / "soak", cycle, limits)
        resources = daemon_resources(process.pid)
        require(resources["rss_kib"] <= limits["maximum_rss_kib"], "daemon RSS limit exceeded")
        require(resources["open_descriptors"] <= limits["maximum_open_descriptors"],
                "daemon descriptor limit exceeded")
        return resources
    finally:
        unmount(process, mountpoint)


def run_soak(image, output, limits, duration):
    events = output / "events.jsonl"
    mountpoint = output / "mount"
    mountpoint.mkdir()
    started = time.monotonic()
    cycle = 0
    while time.monotonic() - started < duration:
        cycle_started = time.monotonic()
        resources = run_cycle(image, mountpoint, cycle, limits)
        verify_image(image)
        append_event(events, {"cycle": cycle, "elapsed_seconds": round(time.monotonic() - started, 3),
                              "cycle_seconds": round(time.monotonic() - cycle_started, 3),
                              "resources": resources, "status": "passed"})
        cycle += 1
        remaining = limits["cycle_seconds"] - (time.monotonic() - cycle_started)
        if remaining > 0:
            time.sleep(remaining)
    return {"completed_cycles": cycle, "completed_duration_seconds": round(time.monotonic() - started, 3)}


def select_duration(limits, requested, preflight):
    duration = requested or limits["target_duration_seconds"]
    require(duration > 0 and (duration == limits["target_duration_seconds"] or preflight),
            "only an explicit preflight may use a shorter soak duration")
    return duration


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", type=Path, default=linux_qualification.DEFAULT_MATRIX)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--approval-reference", required=True)
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument("--preflight", action="store_true")
    arguments = parser.parse_args()
    matrix_path = arguments.matrix.resolve()
    matrix = linux_qualification.load_matrix(matrix_path)
    limits = matrix["soak"]
    duration = select_duration(limits, arguments.duration_seconds, arguments.preflight)
    require(bool(arguments.approval_reference.strip()), "approval reference is required")
    require(not arguments.output.exists(), "refusing to overwrite soak evidence")
    require(Path("/dev/fuse").is_char_device(), "/dev/fuse is required for the soak")
    require(FUSE.is_file() and FIXTURE.is_file() and CHECKER.is_file(), "build soak prerequisites first")
    linux_qualification.verify_dispatcher()
    arguments.output.mkdir(parents=True)
    evidence = {"approval_reference": arguments.approval_reference, "environment": linux_qualification.environment(matrix_path),
                "limits": limits, "requested_duration_seconds": duration, "preflight": arguments.preflight}
    try:
        result = run_soak(prepare_image(arguments.output, 16384), arguments.output, limits, duration)
        evidence.update(result)
        evidence["qualified"] = result["completed_duration_seconds"] >= limits["target_duration_seconds"]
        evidence["status"] = "passed"
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        evidence.update({"status": "failed", "error": str(error), "qualified": False})
        arguments.output.joinpath("result.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="ascii")
        raise SystemExit(str(error)) from error
    arguments.output.joinpath("result.json").write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
