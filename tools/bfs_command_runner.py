#!/usr/bin/env python3
"""Run fixed local conformance executables with bounded resources and no shell."""

from dataclasses import dataclass
import os
from pathlib import Path
import selectors
import time


MAX_OUTPUT_BYTES = 1_048_576
ROOT = Path(__file__).resolve().parents[1]


class CommandTimeout(Exception):
    """The child did not exit within its declared execution budget."""


@dataclass(frozen=True)
class CommandResult:
    returncode: int
    stdout: str
    stderr: str


def execute_child(command, arguments, stdout_fd, stderr_fd):
    try:
        os.chdir(ROOT)
        os.dup2(stdout_fd, 1)
        os.dup2(stderr_fd, 2)
        os.close(stdout_fd)
        os.close(stderr_fd)
        if command == "core":
            os.execv("./build/host/bfs-conformance-core",
                     ["bfs-conformance-core", *arguments])  # nosec B606 - fixed executable
        if command == "posix":
            os.execv("./build/host/bfs-conformance-posix",
                     ["bfs-conformance-posix", *arguments])  # nosec B606 - fixed executable
        if command == "git":
            os.execv("/usr/bin/git", ["git", *arguments])  # nosec B606 - fixed executable
        os._exit(127)
    except (OSError, ValueError):
        os._exit(127)


def terminate(pid):
    try:
        os.kill(pid, 9)
    except ProcessLookupError:
        pass
    return os.waitpid(pid, 0)[1]


def collect_output(pid, stdout_fd, stderr_fd, timeout_seconds):
    outputs = {stdout_fd: bytearray(), stderr_fd: bytearray()}
    selector = selectors.DefaultSelector()
    for descriptor in outputs:
        os.set_blocking(descriptor, False)
        selector.register(descriptor, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout_seconds
    status = None
    output_limited = False
    try:
        while selector.get_map() or status is None:
            if time.monotonic() >= deadline:
                terminate(pid)
                raise CommandTimeout()
            if status is None:
                completed, child_status = os.waitpid(pid, os.WNOHANG)
                if completed:
                    status = child_status
            if not selector.get_map():
                if status is None:
                    time.sleep(0.01)
                continue
            for key, _ in selector.select(max(0, deadline - time.monotonic())):
                data = os.read(key.fd, 65536)
                if not data:
                    selector.unregister(key.fd)
                    os.close(key.fd)
                    continue
                remaining = MAX_OUTPUT_BYTES - sum(len(value) for value in outputs.values())
                outputs[key.fd].extend(data[:remaining])
                if len(data) > remaining and status is None:
                    status = terminate(pid)
                    output_limited = True
        return status, bytes(outputs[stdout_fd]), bytes(outputs[stderr_fd]), output_limited
    finally:
        selector.close()
        for descriptor in outputs:
            try:
                os.close(descriptor)
            except OSError:
                pass


def returncode(status):
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    return 128 + os.WTERMSIG(status) if os.WIFSIGNALED(status) else 127


def result_code(status, output_limited):
    return 137 if output_limited else returncode(status)


def run_command(command, arguments, timeout_seconds):
    if command not in {"core", "posix", "git"}:
        raise ValueError("unsupported fixed command")
    stdout_fd, stdout_write = os.pipe()
    stderr_fd, stderr_write = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(stdout_fd)
        os.close(stderr_fd)
        execute_child(command, arguments, stdout_write, stderr_write)
        os._exit(127)
    os.close(stdout_write)
    os.close(stderr_write)
    status, output, errors, output_limited = collect_output(pid, stdout_fd, stderr_fd, timeout_seconds)
    code = result_code(status, output_limited)
    return CommandResult(code, output.decode("utf-8", errors="replace"),
                         errors.decode("utf-8", errors="replace"))
