#!/usr/bin/env python3
"""Actual Linux FUSE qualification for the read-only BFS adapter."""

import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess  # nosec B404 - local build products are invoked without a shell
import tempfile
import time
import zlib


ROOT = Path(__file__).resolve().parents[2]
FUSE = ROOT / "build" / "host" / "bfs-fuse"
FIXTURE = ROOT / "build" / "host" / "conformance-fixture-writer"
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"
CONFORMANCE = ROOT / "tools" / "bfs-conformance.py"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(*arguments, cwd=ROOT, timeout=60):
    return subprocess.run(arguments, cwd=cwd, check=False, capture_output=True, text=True,
                          timeout=timeout)  # nosec B603


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def mount(image, mountpoint, snapshot=None):
    arguments = [str(FUSE), "--image", str(image)]
    if snapshot:
        arguments.extend(["--snapshot", snapshot])
    arguments.append(str(mountpoint))
    process = subprocess.Popen(arguments, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)  # nosec B603
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            _, errors = process.communicate()
            raise RuntimeError(f"bfs-fuse exited before mount: {errors}")
        if mountpoint.is_mount():
            return process
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


def escape_component(name):
    raw = os.fsencode(name)
    if (raw and raw not in (b".", b"..") and b"/" not in raw and b"\0" not in raw and
            not raw.startswith(b"@bfs-hex-")):
        return os.fsdecode(raw)
    return "@bfs-hex-" + raw.hex().upper()


def mounted_manifest(root):
    entries = []

    def visit(directory, relative):
        for entry in os.scandir(directory):
            component = entry.name
            path = relative / component
            display = "/" + "/".join(path.parts)
            node = entry.stat(follow_symlinks=False)
            item = {"inode": node.st_ino, "links": node.st_nlink, "path": display, "size": node.st_size}
            if stat.S_ISDIR(node.st_mode):
                item["type"] = 1
                entries.append(item)
                visit(Path(entry.path), path)
            elif stat.S_ISLNK(node.st_mode):
                item["type"] = 2
                item["sha256"] = hashlib.sha256(os.fsencode(os.readlink(entry.path))).hexdigest()
                entries.append(item)
            elif stat.S_ISREG(node.st_mode):
                item["type"] = 0
                item["sha256"] = sha256(Path(entry.path))
                entries.append(item)
            else:
                raise RuntimeError(f"unsupported mounted file type: {display}")

    root_stat = root.stat()
    entries.append({"inode": root_stat.st_ino, "links": root_stat.st_nlink, "path": "/", "size": 0, "type": 1})
    visit(root, Path())
    return sorted(entries, key=lambda item: item["path"])


def oracle_manifest(image):
    completed = run(str(ORACLE), str(image))
    require(completed.returncode == 0, completed.stderr)
    result = json.loads(completed.stdout)
    require(result["status"] == "ok", completed.stdout)
    expected = []
    for item in result["namespace"]:
        clone = dict(item)
        if clone["path"] != "/":
            clone["path"] = "/" + "/".join(escape_component(component)
                                                for component in clone["path"].split("/")[1:])
        expected.append(clone)
    return sorted(expected, key=lambda item: item["path"])


def expect_erofs(operation):
    try:
        operation()
    except OSError as error:
        require(error.errno == errno.EROFS, f"expected EROFS, got {error}")
        return
    raise RuntimeError("mutation unexpectedly succeeded")


def run_conformance(root, cases):
    arguments = [str(CONFORMANCE), "--backend", "posix", "--root", str(root)]
    for case in cases:
        arguments.extend(["--case", case])
    completed = run(*arguments)
    require(completed.returncode == 0, completed.stderr + completed.stdout)
    result = json.loads(completed.stdout)
    require(result["status"] == "pass", completed.stdout)


def exercise_fixture(image, mountpoint):
    before = sha256(image)
    process = mount(image, mountpoint)
    try:
        require((mountpoint / "oracle.txt").is_file(), "direct lookup failed for oracle.txt")
        mounted = mounted_manifest(mountpoint)
        oracle = oracle_manifest(image)
        require(mounted == oracle,
                f"mounted namespace differs from oracle: mounted={mounted}, oracle={oracle}")
        require((mountpoint / "oracle.txt").read_bytes() == b"live contents",
                "live namespace did not expose post-snapshot content")
        run_conformance(mountpoint, ["empty-volume", "read-only-refusal", "soft-link",
                                    "directory-scale", "comment-metadata", "name-encoding"])
        require(os.getxattr(mountpoint / "oracle.txt", "user.bfs.protection") == b"00000000",
                "protection xattr differs")
        expect_erofs(lambda: os.mkdir(mountpoint / "mutation"))
        expect_erofs(lambda: os.unlink(mountpoint / "oracle.txt"))
        expect_erofs(lambda: os.rename(mountpoint / "oracle.txt", mountpoint / "renamed"))
    finally:
        unmount(process, mountpoint)
    require(sha256(image) == before, "read-only FUSE mount changed the fixture image")

    process = mount(image, mountpoint, snapshot="oracle-snapshot")
    try:
        run_conformance(mountpoint, ["regular-file", "snapshot"])
        require((mountpoint / "oracle.txt").read_bytes() == b"oracle contents",
                "selected snapshot did not expose its immutable content")
    finally:
        unmount(process, mountpoint)
    require(sha256(image) == before, "read-only snapshot mount changed the fixture image")


def update_superblock_crc(data, offset):
    data[offset + 236:offset + 240] = zlib.crc32(data[offset:offset + 236]).to_bytes(4, "big")


def exercise_rejections(image, temporary):
    backup_offset = len(image.read_bytes()) // 2

    def corrupt(data):
        data[0] ^= 1
        data[backup_offset] ^= 1

    def future(data):
        data[7] = 3
        data[backup_offset + 7] = 3
        update_superblock_crc(data, 0)
        update_superblock_crc(data, backup_offset)

    for label, mutate in (("corrupt", corrupt), ("future", future)):
        bad_image = temporary / f"{label}.bfs"
        shutil.copyfile(image, bad_image)
        data = bytearray(bad_image.read_bytes())
        mutate(data)
        bad_image.write_bytes(data)
        mountpoint = temporary / f"{label}-mount"
        mountpoint.mkdir()
        completed = run(str(FUSE), "--image", str(bad_image), str(mountpoint))
        require(completed.returncode != 0, f"{label} image unexpectedly mounted")


def exercise_amiga_image(image, mountpoint):
    before = sha256(image)
    process = mount(image, mountpoint)
    try:
        mounted = mounted_manifest(mountpoint)
        oracle = oracle_manifest(image)
        require(mounted == oracle,
                f"Amiga-written image differs between mount and oracle: mounted={mounted}, oracle={oracle}")
    finally:
        unmount(process, mountpoint)
    require(sha256(image) == before, "read-only FUSE mount changed the Amiga image")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amiga-image", type=Path)
    args = parser.parse_args()
    require(os.name == "posix" and Path("/dev/fuse").exists(),
            "/dev/fuse is required; this is a failed qualification, not a skip")
    require(FUSE.is_file() and os.access(FUSE, os.X_OK), "bfs-fuse is not built")
    require(FIXTURE.is_file() and os.access(FIXTURE, os.X_OK), "fixture writer is not built")
    require(shutil.which("fusermount3"), "fusermount3 is required")
    with tempfile.TemporaryDirectory(prefix="bfs-fuse-test-") as directory:
        temporary = Path(directory)
        image = temporary / "fixture.bfs"
        require(run(str(FIXTURE), str(image), "--fuse-directory-scale").returncode == 0,
                "cannot create FUSE fixture")
        mountpoint = temporary / "mount"
        mountpoint.mkdir()
        exercise_fixture(image, mountpoint)
        exercise_rejections(image, temporary)
        if args.amiga_image:
            require(args.amiga_image.is_file(), "Amiga image is missing")
            exercise_amiga_image(args.amiga_image, mountpoint)
    print("FUSE qualification passed")


if __name__ == "__main__":
    main()
