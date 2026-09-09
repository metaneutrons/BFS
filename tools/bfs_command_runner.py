#!/usr/bin/env python3
"""Run a selected local executable with bounded resources and no shell."""

from dataclasses import dataclass
import os
from pathlib import Path
import selectors
import time


MAX_OUTPUT_BYTES = 1_048_576


class CommandTimeout(Exception):
    """The child did not exit within its declared execution budget."""


@dataclass(frozen=True)
class CommandResult:
    returncode: int
    stdout: str
    stderr: str


def execute_child(program, arguments, stdout_fd, stderr_fd):
    try:
        os.dup2(stdout_fd, 1)
        os.dup2(stderr_fd, 2)
        os.close(stdout_fd)
        os.close(stderr_fd)
        os.execv(str(program), [str(program), *arguments])
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
    try:
        while selector.get_map() or status is None:
            if time.monotonic() >= deadline:
                terminate(pid)
                raise CommandTimeout()
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
            if status is None:
                completed, child_status = os.waitpid(pid, os.WNOHANG)
                if completed:
                    status = child_status
        return status, bytes(outputs[stdout_fd]), bytes(outputs[stderr_fd])
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


def run_program(program, arguments, timeout_seconds):
    executable = Path(program).resolve()
    stdout_fd, stdout_write = os.pipe()
    stderr_fd, stderr_write = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(stdout_fd)
        os.close(stderr_fd)
        execute_child(executable, arguments, stdout_write, stderr_write)
        os._exit(127)
    os.close(stdout_write)
    os.close(stderr_write)
    status, output, errors = collect_output(pid, stdout_fd, stderr_fd, timeout_seconds)
    return CommandResult(returncode(status), output.decode("utf-8", errors="replace"),
                         errors.decode("utf-8", errors="replace"))
