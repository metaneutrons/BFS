#!/usr/bin/env python3
"""Actual Linux FUSE qualification for the BFS adapter."""

import argparse
import ctypes
import errno
import hashlib
import json
import multiprocessing
import os
from pathlib import Path
import platform
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


def mount(image, mountpoint, snapshot=None, read_write=False):
    arguments = [str(FUSE), "--image", str(image)]
    if read_write:
        arguments.append("--read-write")
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


def small_buffer_dirent_names(directory):
    syscall_number = {
        "aarch64": 61,
        "amd64": 217,
        "arm64": 61,
        "x86_64": 217,
    }.get(platform.machine().lower())
    require(syscall_number is not None, "unsupported Linux architecture for getdents64 qualification")
    descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    names = set()
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        last_next_offset = -1
        for _ in range(128):
            buffer = ctypes.create_string_buffer(128)
            count = libc.syscall(syscall_number, descriptor, buffer, len(buffer))
            if count < 0:
                error = ctypes.get_errno()
                raise OSError(error, os.strerror(error))
            if count == 0:
                return names
            offset = 0
            while offset < count:
                record_length = int.from_bytes(buffer.raw[offset + 16:offset + 18], "little")
                require(record_length >= 20 and offset + record_length <= count,
                        "invalid getdents64 record")
                next_offset = int.from_bytes(buffer.raw[offset + 8:offset + 16], "little")
                require(next_offset > last_next_offset,
                        f"directory stream did not advance: {next_offset} after {last_next_offset}")
                last_next_offset = next_offset
                name = buffer.raw[offset + 19:offset + record_length].split(b"\0", 1)[0]
                if name not in (b".", b".."):
                    names.add(os.fsdecode(name))
                offset += record_length
        raise RuntimeError("directory stream did not terminate within 128 getdents64 calls")
    finally:
        os.close(descriptor)


def oracle_result(image):
    completed = run(str(ORACLE), str(image))
    require(completed.returncode == 0, completed.stderr)
    result = json.loads(completed.stdout)
    require(result["status"] == "ok", completed.stdout)
    return result


def oracle_manifest(image):
    result = oracle_result(image)
    expected = []
    for item in result["namespace"]:
        clone = dict(item)
        if clone["path"] != "/":
            clone["path"] = "/" + "/".join(escape_component(component)
                                                for component in clone["path"].split("/")[1:])
        expected.append(clone)
    return sorted(expected, key=lambda item: item["path"])


def require_statfs(root, superblock):
    result = os.statvfs(root)
    require(result.f_bsize == superblock["block_size"], "statfs block size differs")
    require(result.f_frsize == superblock["block_size"], "statfs fragment size differs")
    require(result.f_blocks == superblock["block_count"], "statfs block count differs")
    require(result.f_bfree == superblock["free_blocks"], "statfs free block count differs")
    require(result.f_bavail == superblock["free_blocks"], "statfs available block count differs")
    require(result.f_namemax == 255, "statfs name limit differs")


def expect_erofs(operation):
    try:
        operation()
    except OSError as error:
        require(error.errno == errno.EROFS, f"expected EROFS, got {error}")
        return
    raise RuntimeError("mutation unexpectedly succeeded")


def expect_errno(expected, operation):
    try:
        operation()
    except OSError as error:
        require(error.errno == expected, f"expected errno {expected}, got {error}")
        return
    raise RuntimeError(f"operation unexpectedly succeeded; expected errno {expected}")


def expect_errnos(expected, operation):
    try:
        operation()
    except OSError as error:
        require(error.errno in expected, f"expected one of {expected}, got {error}")
        return
    raise RuntimeError(f"operation unexpectedly succeeded; expected one of {expected}")


def append_client(path, payload, repetitions):
    descriptor = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CLOEXEC)
    try:
        for _ in range(repetitions):
            total = 0
            while total < len(payload):
                count = os.write(descriptor, payload[total:])
                require(count > 0, "append write made no progress")
                total += count
    finally:
        os.close(descriptor)


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
        oracle_result_value = oracle_result(image)
        oracle = oracle_manifest(image)
        require(mounted == oracle,
                f"mounted namespace differs from oracle: mounted={mounted}, oracle={oracle}")
        require_statfs(mountpoint, oracle_result_value["superblock"])
        expected_names = {Path(item["path"]).name for item in oracle
                          if item["path"] != "/" and Path(item["path"]).parent == Path("/")}
        streamed_names = small_buffer_dirent_names(mountpoint)
        require(streamed_names == expected_names,
                f"small-buffer directory stream differs from oracle: "
                f"streamed={sorted(streamed_names)}, expected={sorted(expected_names)}")
        require((mountpoint / "oracle.txt").read_bytes() == b"live contents",
                "live namespace did not expose post-snapshot content")
        run_conformance(mountpoint, ["empty-volume", "read-only-refusal", "soft-link",
                                    "directory-scale", "comment-metadata", "name-encoding"])
        metadata = mountpoint / "oracle.txt"
        require(os.getxattr(metadata, "user.bfs.comment") == b"fixture",
                "comment xattr differs")
        require(os.getxattr(metadata, "user.bfs.protection") == b"00000000",
                "protection xattr differs")
        names = set(os.listxattr(metadata))
        expected_xattrs = {
            "user.bfs.comment", "user.bfs.protection", "user.bfs.uid", "user.bfs.gid",
            "user.bfs.create_datestamp", "user.bfs.modify_datestamp",
        }
        require(names == expected_xattrs, f"xattr list differs: {sorted(names)}")
        require(os.getxattr(metadata, "user.bfs.uid").isdigit(), "uid xattr is not decimal")
        require(os.getxattr(metadata, "user.bfs.gid").isdigit(), "gid xattr is not decimal")
        for name in ("user.bfs.create_datestamp", "user.bfs.modify_datestamp"):
            fields = os.getxattr(metadata, name).split(b":")
            require(len(fields) == 3 and all(field.isdigit() for field in fields),
                    f"{name} is not a BFS datestamp")
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


def exercise_writable_metadata(work):
    durable = work / "durable"
    descriptor = os.open(durable, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
    try:
        require(os.write(descriptor, b"base") == 4, "initial write was short")
        os.fsync(descriptor)
        os.ftruncate(descriptor, 9)
        os.lseek(descriptor, 4, os.SEEK_SET)
        require(os.read(descriptor, 5) == b"\0" * 5, "truncate extension is not zero-filled")
    finally:
        os.close(descriptor)
    os.setxattr(durable, "user.bfs.comment", b"M6 writable mount")
    require(os.getxattr(durable, "user.bfs.comment") == b"M6 writable mount",
            "writable comment xattr differs")
    expect_errno(errno.EOPNOTSUPP, lambda: os.setxattr(durable, "user.bfs.uid", b"1"))
    expect_errnos((errno.EPERM, errno.EOPNOTSUPP), lambda: os.chmod(durable, 0o600))
    removable = work / "removable-comment"
    removable.write_bytes(b"comment")
    os.setxattr(removable, "user.bfs.comment", b"remove me")
    os.removexattr(removable, "user.bfs.comment")
    expect_errno(errno.ENODATA, lambda: os.getxattr(removable, "user.bfs.comment"))
    node = work / "mknod-file"
    os.mknod(node, stat.S_IFREG | 0o600)
    require(node.is_file(), "regular mknod did not create a file")
    return durable


def exercise_writable_links(work, durable):
    positions = work / "positions"
    positions.write_bytes(b"abcdef")
    first = os.open(positions, os.O_RDONLY | os.O_CLOEXEC)
    second = os.open(positions, os.O_RDONLY | os.O_CLOEXEC)
    try:
        require(os.read(first, 2) == b"ab", "first handle initial read differs")
        require(os.read(second, 3) == b"abc", "second handle offset is not independent")
        require(os.read(first, 2) == b"cd", "first handle offset changed unexpectedly")
    finally:
        os.close(first)
        os.close(second)
    linked = work / "durable-link"
    os.link(durable, linked)
    require(linked.read_bytes() == durable.read_bytes(), "hard-link data differs")
    symbolic = work / "durable-symlink"
    os.symlink("durable", symbolic)
    require(os.readlink(symbolic) == "durable", "symlink target differs")


def exercise_replacement_semantics(work):
    open_target = work / "open-target"
    replacement = work / "replacement"
    open_target.write_bytes(b"old")
    replacement.write_bytes(b"new")
    descriptor = os.open(open_target, os.O_RDWR | os.O_CLOEXEC)
    try:
        os.rename(replacement, open_target)
        require(os.fstat(descriptor).st_nlink == 0, "replaced open target does not report zero links")
        os.lseek(descriptor, 0, os.SEEK_SET)
        require(os.read(descriptor, 3) == b"old", "replacement lost open target data")
        require(os.write(descriptor, b"+") == 1, "write through replaced handle was short")
    finally:
        os.close(descriptor)
    require(open_target.read_bytes() == b"new", "replacement name did not expose new file")
    source_directory = work / "source-directory"
    target_directory = work / "target-directory"
    source_directory.mkdir()
    target_directory.mkdir()
    os.rename(source_directory, target_directory)
    require(target_directory.is_dir() and not source_directory.exists(), "empty directory replacement differs")
    os.rmdir(target_directory)
    require(not target_directory.exists(), "rmdir did not remove empty directory")


def exercise_writable_type_errors(work):
    type_file = work / "type-file"
    type_file.write_bytes(b"file")
    type_directory = work / "type-directory"
    type_directory.mkdir()
    expect_errno(errno.ENOTDIR, lambda: os.rmdir(type_file))
    expect_errno(errno.EISDIR, lambda: os.unlink(type_directory))
    file_source = work / "file-source"
    file_source.write_bytes(b"file")
    directory_target = work / "directory-target"
    directory_target.mkdir()
    expect_errno(errno.EISDIR, lambda: os.rename(file_source, directory_target))
    directory_source = work / "directory-source"
    directory_source.mkdir()
    file_target = work / "file-target"
    file_target.write_bytes(b"file")
    expect_errno(errno.ENOTDIR, lambda: os.rename(directory_source, file_target))


def exercise_open_unlink_and_append(work):
    unlinked = work / "unlinked"
    unlinked.write_bytes(b"before")
    descriptor = os.open(unlinked, os.O_RDWR | os.O_CLOEXEC)
    try:
        os.unlink(unlinked)
        require(os.fstat(descriptor).st_nlink == 0, "unlinked open file does not report zero links")
        os.lseek(descriptor, 0, os.SEEK_SET)
        require(os.read(descriptor, 6) == b"before", "unlinked handle lost data")
        os.lseek(descriptor, 0, os.SEEK_END)
        require(os.write(descriptor, b"-after") == 6, "write through unlinked handle was short")
    finally:
        os.close(descriptor)
    require(not unlinked.exists(), "unlinked name is still visible")
    append_path = work / "append"
    append_path.write_bytes(b"")
    workers = 6
    repetitions = 24
    payloads = [f"worker-{worker:02d}\n".encode("ascii") for worker in range(workers)]
    context = multiprocessing.get_context("fork")
    processes = [context.Process(target=append_client, args=(append_path, payload, repetitions))
                 for payload in payloads]
    for child in processes:
        child.start()
    for child in processes:
        child.join(timeout=20)
        require(child.exitcode == 0, f"append client exited with {child.exitcode}")
    records = append_path.read_bytes().splitlines(keepends=True)
    require(len(records) == workers * repetitions, "atomic append lost or merged records")
    for payload in payloads:
        require(records.count(payload) == repetitions, f"atomic append record count differs for {payload!r}")


def exercise_large_offsets(work):
    sparse = work / "sparse-offsets"
    descriptor = os.open(sparse, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
    try:
        for offset, payload in ((2 * 1024**3, b"two-gib"), (4 * 1024**3, b"four-gib")):
            require(os.pwrite(descriptor, payload, offset) == len(payload),
                    f"short sparse write at {offset}")
            require(os.pread(descriptor, len(payload), offset) == payload,
                    f"sparse read differs at {offset}")
        require(os.fstat(descriptor).st_size == 4 * 1024**3 + len(b"four-gib"),
                "sparse file size differs")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def exercise_disk_pressure(work, block_size):
    fragments = []
    for index in range(16):
        path = work / f"fragment-{index:02d}"
        path.write_bytes(bytes([index]) * block_size)
        fragments.append(path)
    for path in fragments[::2]:
        path.unlink()
    for path in fragments[1::2]:
        require(path.read_bytes() == bytes([int(path.name[-2:])]) * block_size,
                f"fragment data differs for {path.name}")

    pressure = work / "pressure"
    descriptor = os.open(pressure, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
    try:
        payload = b"P" * block_size
        exhausted = False
        for _ in range(2048):
            try:
                require(os.write(descriptor, payload) == len(payload), "short disk-pressure write")
            except OSError as error:
                require(error.errno == errno.ENOSPC, f"expected ENOSPC, got {error}")
                exhausted = True
                break
        require(exhausted, "disk-pressure workload did not exhaust the volume")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    pressure.unlink()
    recovered = work / "recovered-space"
    recovered.write_bytes(b"recovered")
    require(recovered.read_bytes() == b"recovered", "space did not recover after unlink")


def exercise_name_and_metadata_boundaries(work):
    maximum_name = "n" * 255
    boundary = work / maximum_name
    boundary.write_bytes(b"boundary")
    require(boundary.read_bytes() == b"boundary", "255-byte name differs")
    os.setxattr(boundary, "user.bfs.comment", b"c" * 79)
    require(os.getxattr(boundary, "user.bfs.comment") == b"c" * 79,
            "maximum comment differs")
    expect_errno(errno.ENAMETOOLONG, lambda: (work / ("x" * 256)).write_bytes(b"invalid"))


def exercise_writable_fixture(image, mountpoint):
    process = mount(image, mountpoint, read_write=True)
    try:
        work = mountpoint / "rw"
        work.mkdir()
        durable = exercise_writable_metadata(work)
        exercise_writable_links(work, durable)
        exercise_replacement_semantics(work)
        exercise_writable_type_errors(work)
        exercise_open_unlink_and_append(work)
        exercise_large_offsets(work)
        exercise_name_and_metadata_boundaries(work)
        case_name = work / "case-name"
        case_name.write_bytes(b"case")
        os.rename(case_name, work / "CASE-NAME")
        require((work / "CASE-NAME").read_bytes() == b"case", "case-only rename differs")
        expect_errno(errno.ENOTEMPTY, lambda: os.rmdir(work))
        directory = os.open(work, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        unmount(process, mountpoint)


def create_interrupted_open_unlink_image(image, mountpoint, output):
    process = mount(image, mountpoint, read_write=True)
    descriptor = None
    try:
        orphan = mountpoint / "interrupted-orphan"
        descriptor = os.open(orphan, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        require(os.write(descriptor, b"interrupted state") == 17,
                "interrupted fixture write was short")
        os.fsync(descriptor)
        os.unlink(orphan)
        require(os.fstat(descriptor).st_nlink == 0,
                "interrupted fixture did not retain a zero-link handle")
        os.fsync(descriptor)
        process.kill()
        process.wait(timeout=10)
        deadline = time.monotonic() + 10
        while mountpoint.is_mount() and time.monotonic() < deadline:
            time.sleep(0.05)
        # A killed daemon can leave a disconnected FUSE endpoint behind even
        # after Path.is_mount() becomes false. Detach it before remounting.
        detached = run("fusermount3", "-u", "-z", str(mountpoint))
        require(detached.returncode == 0 or not mountpoint.is_mount(), detached.stderr)
        require(not mountpoint.is_mount(), "FUSE mount survived interrupted daemon")
    finally:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        process.communicate()
    shutil.copyfile(image, output)
    oracle_result(output)

    process = mount(image, mountpoint, read_write=True)
    try:
        work = mountpoint / "rw"
        require((work / "durable").read_bytes() == b"base" + b"\0" * 5,
                "fsync/remount lost durable file state")
        require(os.getxattr(work / "durable", "user.bfs.comment") == b"M6 writable mount",
                "comment xattr did not survive remount")
        require((work / "open-target").read_bytes() == b"new",
                "replacement state did not survive remount")
        require(not (work / "unlinked").exists(), "unlinked file returned after remount")
        require((work / "CASE-NAME").is_file(), "case-only rename did not survive remount")
    finally:
        unmount(process, mountpoint)


def update_superblock_crc(data, offset):
    data[offset + 236:offset + 240] = zlib.crc32(data[offset:offset + 236]).to_bytes(4, "big")


def exercise_rejections(image, temporary):
    backup_offset = oracle_result(image)["superblock"]["backup_offset"]

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

    mountpoint = temporary / "snapshot-write-mount"
    mountpoint.mkdir()
    completed = run(str(FUSE), "--image", str(image), "--read-write", "--snapshot",
                    "oracle-snapshot", str(mountpoint))
    require(completed.returncode != 0, "writable snapshot mount unexpectedly succeeded")


def exercise_amiga_image(image, mountpoint):
    before = sha256(image)
    process = mount(image, mountpoint)
    try:
        mounted = mounted_manifest(mountpoint)
        oracle_result_value = oracle_result(image)
        oracle = oracle_manifest(image)
        require(mounted == oracle,
                f"Amiga-written image differs between mount and oracle: mounted={mounted}, oracle={oracle}")
        require_statfs(mountpoint, oracle_result_value["superblock"])
    finally:
        unmount(process, mountpoint)
    require(sha256(image) == before, "read-only FUSE mount changed the Amiga image")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amiga-image", type=Path)
    parser.add_argument("--writable-image-output", type=Path)
    parser.add_argument("--interrupted-image-output", type=Path)
    parser.add_argument("--block-size", type=int, default=4096)
    parser.add_argument("--block-count", type=int, default=512)
    parser.add_argument("--format-options", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--hard-link", action="store_true")
    parser.add_argument("--disk-pressure", action="store_true")
    parser.add_argument("--interrupted-daemon", action="store_true")
    args = parser.parse_args()
    legal_block_sizes = {1024, 2048, 4096, 8192, 16384, 32768, 65536}
    require(args.block_size in legal_block_sizes, "unsupported BFS block size")
    require(args.block_count >= 64, "fixture block count is too small")
    require(0 <= args.format_options <= 7, "unsupported BFS format options")
    require(os.name == "posix" and Path("/dev/fuse").exists(),
            "/dev/fuse is required; this is a failed qualification, not a skip")
    require(FUSE.is_file() and os.access(FUSE, os.X_OK), "bfs-fuse is not built")
    require(FIXTURE.is_file() and os.access(FIXTURE, os.X_OK), "fixture writer is not built")
    require(shutil.which("fusermount3"), "fusermount3 is required")
    with tempfile.TemporaryDirectory(prefix="bfs-fuse-test-") as directory:
        temporary = Path(directory)
        image = temporary / "fixture.bfs"
        fixture_arguments = [str(FIXTURE), str(image), "--directory-scale", "--block-size",
                             str(args.block_size), "--block-count", str(args.block_count),
                             "--format-options", str(args.format_options)]
        if args.hard_link:
            fixture_arguments.append("--hard-link")
        require(run(*fixture_arguments).returncode == 0,
                "cannot create FUSE fixture")
        require(oracle_result(image)["superblock"]["options"] == args.format_options,
                "fixture format options differ")
        mountpoint = temporary / "mount"
        mountpoint.mkdir()
        exercise_fixture(image, mountpoint)
        exercise_writable_fixture(image, mountpoint)
        if args.disk_pressure:
            process = mount(image, mountpoint, read_write=True)
            try:
                exercise_disk_pressure(mountpoint / "rw", args.block_size)
            finally:
                unmount(process, mountpoint)
        exercise_rejections(image, temporary)
        if args.amiga_image:
            require(args.amiga_image.is_file(), "Amiga image is missing")
            exercise_amiga_image(args.amiga_image, mountpoint)
        if args.writable_image_output:
            shutil.copyfile(image, args.writable_image_output)
            oracle_result(args.writable_image_output)
        if args.interrupted_image_output:
            create_interrupted_open_unlink_image(image, mountpoint,
                                                 args.interrupted_image_output)
        if args.interrupted_daemon:
            create_interrupted_open_unlink_image(image, mountpoint,
                                                 temporary / "interrupted.bfs")
    print("FUSE qualification passed")


if __name__ == "__main__":
    main()
