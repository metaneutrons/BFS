#!/usr/bin/env python3
"""Strict release inventories; archive entries are never extracted to disk."""

import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile


CPUS = ("020", "030", "040", "060", "080")
BINARIES = ("bfshandler", "bfs-test", "bfs") + tuple(
    f"bfshandler.{cpu}" for cpu in CPUS
)
ARCHIVE_FILES = frozenset((*BINARIES, "BUILD-METADATA.json", "LICENSE", "README.md",
                           "RUNTIME-COMPONENTS.json", "THIRD-PARTY.md", "LICENSE.libnix",
                           "LICENSE.libgcc", "LICENSE.libgcc-exception"))
# Retained qualification records stay in git; a source package does not need them.
SOURCE_EXCLUDED = ("docs/qualification/evidence",)
MAX_ENTRY_BYTES = 32 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def package_name(tag):
    require(re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", tag), "invalid release tag")
    return f"bfs-{tag}-amiga"


def source_package_name(tag):
    package_name(tag)
    return f"bfs-{tag}-source"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*arguments):
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args, python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
    return subprocess.run(["git", *arguments], check=True, capture_output=True, shell=False).stdout  # nosec B603 B607


def require_gzip(path):
    try:
        subprocess.run(["gzip", "-t", str(path)], check=True)
    except subprocess.CalledProcessError as error:
        raise ValueError("invalid gzip archive") from error


def read_archive(path, tag):
    package = package_name(tag)
    with tempfile.TemporaryDirectory(prefix="bfs-archive-") as temporary:
        if path.suffix == ".lha":
            normalized = pathlib.Path(temporary) / "normalized.tar"
            # libarchive converts entries to a structured format, without extraction.
            subprocess.run(
                ["bsdtar", "-cf", str(normalized), "--format=pax", f"@{path.resolve()}"],
                check=True,
            )
        else:
            require_gzip(path)
            normalized = path
        with tarfile.open(normalized, "r:*") as archive:
            entries = archive.getmembers()
            names = [entry.name for entry in entries]
            require(len(names) == len(set(names)), "duplicate archive entry")
            expected = {f"{package}/{name}" for name in ARCHIVE_FILES}
            require(set(names) in (expected, expected | {package}), "unexpected archive inventory")
            files = {}
            for entry in entries:
                if entry.name == package:
                    require(entry.isdir(), "package root is not a directory")
                    continue
                require(entry.isfile() and not entry.issparse(), "non-regular archive entry")
                require(0 < entry.size <= MAX_ENTRY_BYTES, "invalid archive entry size")
                stream = archive.extractfile(entry)
                require(stream is not None, "archive entry has no data")
                with stream:
                    data = stream.read()
                require(len(data) == entry.size, "truncated archive entry")
                files[pathlib.PurePosixPath(entry.name).name] = data
            return files


def verify_metadata(files, tag, commit):
    metadata = json.loads(files["BUILD-METADATA.json"])
    require(metadata.get("schema") == 1 and metadata.get("tag") == tag, "metadata identity mismatch")
    require(re.fullmatch(r"[0-9a-f]{40}", commit), "invalid expected source commit")
    require(metadata.get("source_commit") == commit, "metadata source commit mismatch")
    require(type(metadata.get("source_date_epoch")) is int and metadata["source_date_epoch"] >= 0,
            "invalid source timestamp")
    require(isinstance(metadata.get("toolchain"), str) and metadata["toolchain"], "missing toolchain identity")
    expected_targets = [{"file": f"bfshandler.{cpu}", "flags": [f"-m68{cpu}", "-O2"]} for cpu in CPUS]
    require(metadata.get("targets") == expected_targets, "CPU targets or flags differ")
    hashes = {name: digest(data) for name, data in files.items() if name != "BUILD-METADATA.json"}
    require(metadata.get("files") == hashes, "archive file digest inventory differs")
    require(files["bfshandler"] == files["bfshandler.020"], "default handler differs from 68020")
    for name in BINARIES:
        require(files[name][:4] == b"\x00\x00\x03\xf3", f"{name} is not an Amiga HUNK executable")
    verify_runtime(files, metadata["toolchain"])


def verify_runtime(files, toolchain):
    runtime = json.loads(files["RUNTIME-COMPONENTS.json"])
    require(runtime.get("schema") == 1 and runtime.get("toolchain") == toolchain,
            "runtime provenance identity mismatch")
    components = runtime.get("components", [])
    require(isinstance(components, list) and components, "empty runtime component inventory")
    identities = set()
    for component in components:
        sha256 = component.get("sha256", "")
        require(re.fullmatch(r"[0-9a-f]{64}", sha256), "invalid runtime input digest")
        identifier = component.get("id")
        require(identifier == "runtime-" + sha256 and identifier not in identities,
                "duplicate or invalid runtime component identity")
        identities.add(identifier)
        require(component.get("name") in ("libgcc", "libnix"), "unreviewed runtime component")
        license_id = ("GPL-3.0-or-later WITH GCC-exception-3.1" if component["name"] == "libgcc"
                      else "LicenseRef-libnix-Public-Domain")
        require(component.get("license") == license_id, "runtime license mismatch")
    binaries = runtime.get("binaries", {})
    require(set(binaries) == set(BINARIES), "runtime binary inventory mismatch")
    used = set()
    for name, binary in binaries.items():
        require(binary.get("sha256") == digest(files[name]), "runtime binary digest mismatch")
        inputs = binary.get("inputs", [])
        require(isinstance(inputs, list) and inputs, "missing linked runtime inputs")
        seen = set()
        for item in inputs:
            identifier = item.get("component")
            require(identifier in identities and identifier not in seen, "invalid runtime input reference")
            seen.add(identifier)
            members = item.get("members")
            require(isinstance(members, list) and len(members) == len(set(members))
                    and all(isinstance(member, str) and re.fullmatch(r"[A-Za-z0-9_.-]+\.o", member)
                            for member in members), "invalid archive member inventory")
        used.update(seen)
    require(used == identities, "unreferenced runtime component")
    return runtime


def verify_archives(tag, directory, commit):
    package = package_name(tag)
    tar_files = read_archive(directory / f"{package}.tar.gz", tag)
    lha_files = read_archive(directory / f"{package}.lha", tag)
    require(tar_files == lha_files, "TAR and LHA payload bytes differ")
    verify_metadata(tar_files, tag, commit)


def source_entry(entry, root):
    parts = entry.name.split("/")
    require(parts[0] == root and not {"", ".", ".."} & set(parts), "source archive entry outside its root")
    require(entry.uid == 0 and entry.gid == 0, "source archive entry carries a host owner")
    if entry.isdir():
        require(entry.mode == 0o755, "unexpected source directory mode")
    else:
        require(entry.isfile() and not entry.issparse() and len(parts) > 1, "non-regular source archive entry")
        require(entry.mode in (0o644, 0o755), "unexpected source file mode")
        require(entry.size <= MAX_ENTRY_BYTES, "invalid source archive entry size")
    return "/".join(parts[1:])


def read_source_archive(path, tag):
    """Return the commit, timestamp and {path: (mode, bytes)} of a source archive."""
    root = source_package_name(tag)
    require_gzip(path)
    with tarfile.open(path, "r:gz") as archive:
        # git archive records the commit in the pax global header.
        commit = archive.pax_headers.get("comment", "")
        require(re.fullmatch(r"[0-9a-f]{40}", commit), "source archive lacks its commit identity")
        entries = archive.getmembers()
        require(len({entry.name for entry in entries}) == len(entries), "duplicate source archive entry")
        require(len({entry.mtime for entry in entries}) == 1, "source archive timestamps differ")
        files, directories = {}, set()
        for entry in entries:
            relative = source_entry(entry, root)
            if entry.isdir():
                directories.add(relative)
                continue
            stream = archive.extractfile(entry)
            require(stream is not None, "source archive entry has no data")
            with stream:
                data = stream.read()
            require(len(data) == entry.size, "truncated source archive entry")
            files[relative] = (entry.mode, data)
    parents = {"/".join(path.split("/")[:depth]) for path in files for depth in range(path.count("/") + 1)}
    require(directories == parents, "source archive directory inventory differs")
    return commit, entries[0].mtime, files


def git_blob_id(data):
    # Git's object name, compared with the commit tree; signatures provide authenticity.
    return hashlib.sha1(b"blob %d\0" % len(data) + data, usedforsecurity=False).hexdigest()


def source_tree(commit):
    """Return {path: (mode, blob)} of the commit without the excluded paths."""
    tree = {}
    for record in git("ls-tree", "-r", "-z", "--full-tree", commit).split(b"\0"):
        if not record:
            continue
        meta, name = record.split(b"\t", 1)
        mode, kind, blob = meta.decode("ascii").split(" ")
        path = name.decode("utf-8")
        if any(path == prefix or path.startswith(prefix + "/") for prefix in SOURCE_EXCLUDED):
            continue
        require(kind == "blob" and mode in ("100644", "100755"), f"unsupported source tree entry: {path}")
        tree[path] = (0o755 if mode == "100755" else 0o644, blob)
    return tree


def verify_source_archive(tag, directory, commit):
    require(re.fullmatch(r"[0-9a-f]{40}", commit), "invalid expected source commit")
    path = directory / f"{source_package_name(tag)}.tar.gz"
    archived, epoch, files = read_source_archive(path, tag)
    require(archived == commit, "source archive commit mismatch")
    require(epoch == int(git("show", "-s", "--format=%ct", commit)), "source archive timestamp differs from the commit")
    tree = source_tree(commit)
    require(set(files) == set(tree), "source archive inventory differs from the commit")
    for name, (mode, data) in files.items():
        require((mode, git_blob_id(data)) == tree[name], f"source file differs from the commit: {name}")


def verify_sbom(path, payload, tag):
    from sbom_document import document as expected_document

    document = json.loads(path.read_bytes())
    require(document.get("spdxVersion") == "SPDX-2.3", "invalid SBOM version")
    packages = document.get("packages", [])
    described = {relationship.get("relatedSpdxElement") for relationship in document.get("relationships", [])
                 if relationship.get("spdxElementId") == "SPDXRef-DOCUMENT"
                 and relationship.get("relationshipType") == "DESCRIBES"}
    matches = [package for package in packages if package.get("SPDXID") in described
               and package.get("name") == "bfs"]
    require(len(matches) == 1, "SBOM must describe exactly one BFS payload package")
    package, = matches
    checksums = [checksum for checksum in package.get("checksums", []) if checksum.get("algorithm") == "SHA256"]
    require(checksums == [{"algorithm": "SHA256", "checksumValue": digest(payload.read_bytes())}],
            "SBOM payload digest mismatch or ambiguity")
    require(package.get("versionInfo") == tag.removeprefix("v").split("-", 1)[0], "SBOM version mismatch")
    files = read_archive(payload, tag)
    metadata = json.loads(files["BUILD-METADATA.json"])
    verify_metadata(files, tag, metadata.get("source_commit", ""))
    expected = expected_document(payload.name, digest(payload.read_bytes()), files)
    require(document == expected, "SBOM file, runtime, license or relationship inventory mismatch")
    return metadata


def verify_candidate(tag, directory):
    package = package_name(tag)
    payloads = [f"{package}.tar.gz", f"{package}.lha"]
    source = f"{source_package_name(tag)}.tar.gz"
    expected = {"SHA256SUMS", source, source + ".sigstore.json"} | {
        name + suffix for name in payloads for suffix in ("", ".spdx.json", ".sigstore.json")}
    require(not directory.is_symlink() and directory.is_dir(), "invalid candidate directory")
    entries = list(directory.iterdir())
    require({path.name for path in entries} == expected, "unexpected candidate asset inventory")
    require(all(path.is_file() and not path.is_symlink() for path in entries), "non-regular candidate asset")
    lines = (directory / "SHA256SUMS").read_text(encoding="ascii").splitlines()
    inventory = {}
    for line in lines:
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9._-]+)", line)
        require(match is not None, "malformed checksum entry")
        checksum, name = match.groups()
        require(name not in inventory, "duplicate checksum entry")
        inventory[name] = checksum
    require(set(inventory) == expected - {"SHA256SUMS"}, "incomplete checksum inventory")
    for name, checksum in inventory.items():
        require(digest((directory / name).read_bytes()) == checksum, f"checksum mismatch: {name}")
    for name in payloads:
        metadata = verify_sbom(directory / f"{name}.spdx.json", directory / name, tag)
    commit, epoch, _ = read_source_archive(directory / source, tag)
    require((commit, epoch) == (metadata["source_commit"], metadata["source_date_epoch"]),
            "source archive and Amiga payloads name different commits or timestamps")


def main():
    require(len(sys.argv) in (4, 5),
            "usage: release_integrity.py archives|source|candidate TAG DIRECTORY [COMMIT]")
    mode, tag, directory = sys.argv[1:4]
    if mode in ("archives", "source"):
        require(len(sys.argv) == 5, "expected source commit is required")
        verify = verify_archives if mode == "archives" else verify_source_archive
        verify(tag, pathlib.Path(directory), sys.argv[4])
    else:
        require(mode == "candidate" and len(sys.argv) == 4, "invalid verification mode")
        verify_candidate(tag, pathlib.Path(directory))
    print(f"Release {mode} integrity checks passed.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        sys.exit(f"ERROR: {error}")
