#!/usr/bin/env python3
"""Positive and isolated negative probes for release inventory gates."""

import io
import json
import os
import pathlib
import subprocess  # nosec B404
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

TOOLS = pathlib.Path(__file__).resolve().parents[2] / "tools" / "release"
sys.path.insert(0, str(TOOLS))
import release_integrity as release
import sbom_document


TAG = "v0.1.0-qualification"
COMMIT = "a" * 40
PACKAGE = release.package_name(TAG)
SOURCE = release.source_package_name(TAG)


def tar_entry(name, data=b"", kind=tarfile.REGTYPE, mode=0o644, mtime=1):
    entry = tarfile.TarInfo(name)
    entry.type, entry.mode, entry.mtime = kind, mode, mtime
    entry.linkname = "../../escape" if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE) else ""
    return entry, data


def source_entries(mtime=1):
    return [tar_entry(SOURCE, kind=tarfile.DIRTYPE, mode=0o755, mtime=mtime),
            tar_entry(f"{SOURCE}/README.md", b"source\n", mtime=mtime),
            tar_entry(f"{SOURCE}/tools", kind=tarfile.DIRTYPE, mode=0o755, mtime=mtime),
            tar_entry(f"{SOURCE}/tools/run.sh", b"#!/bin/sh\n", mode=0o755, mtime=mtime)]


def write_source(path, entries, headers=None):
    headers = {"comment": COMMIT} if headers is None else headers
    with tarfile.open(path, "w:gz", format=tarfile.PAX_FORMAT, pax_headers=headers) as archive:
        for entry, data in entries:
            entry.size = len(data) if entry.isfile() else 0
            archive.addfile(entry, io.BytesIO(data) if entry.isfile() else None)
    return path


class IntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.files = {name: b"\x00\x00\x03\xf3fixture" if name in release.BINARIES else b"fixture\n"
                      for name in release.ARCHIVE_FILES}
        component = {"id": "runtime-" + "a" * 64, "name": "libnix", "input": "libnix.a",
                     "sha256": "a" * 64, "license": "LicenseRef-libnix-Public-Domain",
                     "source_url": "https://github.com/AmigaPorts/libnix"}
        self.runtime = {"schema": 1, "toolchain": "test-fixture", "components": [component],
                        "binaries": {name: {"sha256": release.digest(self.files[name]),
                                            "inputs": [{"component": component["id"], "members": ["memcmp.o"]}]}
                                     for name in release.BINARIES}}
        self.files["RUNTIME-COMPONENTS.json"] = json.dumps(self.runtime).encode()
        self.metadata = {
            "schema": 1, "tag": TAG, "source_commit": COMMIT,
            "source_date_epoch": 1, "toolchain": "test-fixture",
            "targets": [{"file": f"bfshandler.{cpu}", "flags": [f"-m68{cpu}", "-O2"]}
                        for cpu in release.CPUS],
            "files": {name: release.digest(data) for name, data in self.files.items()
                      if name != "BUILD-METADATA.json"},
        }
        self.store_metadata()

    def store_metadata(self):
        self.files["BUILD-METADATA.json"] = json.dumps(self.metadata).encode()

    def archive(self, extras=(), replacement=None):
        path = self.root / "fixture.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            for name, data in self.files.items():
                entry = tarfile.TarInfo(f"{PACKAGE}/{name}")
                entry.size = len(data)
                if replacement is not None and name == "README.md":
                    entry = replacement
                    data = b""
                archive.addfile(entry, io.BytesIO(data))
            for entry in extras:
                archive.addfile(entry)
        return path

    def test_valid_archive_and_metadata(self):
        files = release.read_archive(self.archive(), TAG)
        self.assertEqual(files, self.files)
        release.verify_metadata(files, TAG, COMMIT)

    def test_duplicate_and_extra_archive_entries(self):
        for name, message in ((f"{PACKAGE}/README.md", "duplicate"),
                              (f"{PACKAGE}/nested/extra", "inventory"),
                              ("../escape", "inventory"), ("/absolute", "inventory")):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                release.read_archive(self.archive([tarfile.TarInfo(name)]), TAG)
        self.assertFalse((self.root.parent / "escape").exists())

    def test_nonregular_archive_entries(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.DIRTYPE, tarfile.FIFOTYPE):
            entry = tarfile.TarInfo(f"{PACKAGE}/README.md")
            entry.type = kind
            entry.linkname = "../../escape"
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "non-regular"):
                release.read_archive(self.archive(replacement=entry), TAG)

    def test_corrupted_compressed_archive(self):
        path = self.archive()
        path.write_bytes(path.read_bytes()[:-8])
        with self.assertRaisesRegex(ValueError, "invalid gzip archive"):
            release.read_archive(path, TAG)

    def test_metadata_identity_flags_and_digests(self):
        cases = (("source_commit", "b" * 40, "source commit"),
                 ("targets", [], "CPU targets"), ("files", {}, "digest inventory"),
                 ("tag", "v1.0.0", "identity"), ("toolchain", "", "toolchain"))
        for key, value, message in cases:
            original = self.metadata[key]
            self.metadata[key] = value
            self.store_metadata()
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, message):
                release.verify_metadata(self.files, TAG, COMMIT)
            self.metadata[key] = original

    def candidate(self):
        # Archive decoding has separate real-parser probes; these isolate candidate/SBOM checks.
        decoder = patch.object(release, "read_archive", return_value=self.files)
        decoder.start()
        self.addCleanup(decoder.stop)
        for suffix in (".tar.gz", ".lha"):
            name = PACKAGE + suffix
            payload = self.root / name
            payload.write_bytes(b"payload")
            document = sbom_document.document(name, release.digest(b"payload"), self.files)
            (self.root / f"{name}.spdx.json").write_text(json.dumps(document), encoding="utf-8")
            (self.root / f"{name}.sigstore.json").write_text("{}", encoding="utf-8")
        write_source(self.root / f"{SOURCE}.tar.gz", source_entries())
        (self.root / f"{SOURCE}.tar.gz.sigstore.json").write_text("{}", encoding="utf-8")
        self.sums()

    def sums(self):
        lines = [f"{release.digest(path.read_bytes())}  {path.name}\n"
                 for path in sorted(self.root.iterdir()) if path.name != "SHA256SUMS"]
        (self.root / "SHA256SUMS").write_text("".join(lines), encoding="ascii")

    def test_valid_candidate(self):
        self.candidate()
        release.verify_candidate(TAG, self.root)

    def test_missing_duplicate_or_unsafe_checksums(self):
        self.candidate()
        sums = self.root / "SHA256SUMS"
        lines = sums.read_text().splitlines(keepends=True)
        for contents, message in (("".join(lines[:-1]), "incomplete"),
                                  ("".join(lines + lines[:1]), "duplicate"),
                                  ("0" * 64 + "  ../escape\n", "malformed")):
            sums.write_text(contents, encoding="ascii")
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                release.verify_candidate(TAG, self.root)

    def test_extra_directory_and_symlink(self):
        self.candidate()
        extra = self.root / "extra"
        extra.mkdir()
        with self.assertRaisesRegex(ValueError, "inventory"):
            release.verify_candidate(TAG, self.root)
        extra.rmdir()
        extra.symlink_to("missing")
        with self.assertRaisesRegex(ValueError, "inventory"):
            release.verify_candidate(TAG, self.root)

    def test_symlink_replacing_asset(self):
        self.candidate()
        path = self.root / f"{PACKAGE}.lha"
        path.unlink()
        path.symlink_to(f"{PACKAGE}.tar.gz")
        with self.assertRaisesRegex(ValueError, "non-regular"):
            release.verify_candidate(TAG, self.root)

    def test_source_archive_names_the_payload_commit(self):
        self.candidate()
        (self.root / f"{SOURCE}.tar.gz.sigstore.json").unlink()
        with self.assertRaisesRegex(ValueError, "inventory"):
            release.verify_candidate(TAG, self.root)
        self.candidate()
        for headers, mtime in (({"comment": "b" * 40}, 1), (None, 2)):
            write_source(self.root / f"{SOURCE}.tar.gz", source_entries(mtime), headers)
            self.sums()
            with self.subTest(mtime=mtime), self.assertRaisesRegex(ValueError, "different commits"):
                release.verify_candidate(TAG, self.root)

    def test_tampered_payload(self):
        self.candidate()
        (self.root / f"{PACKAGE}.lha").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            release.verify_candidate(TAG, self.root)

    def test_ambiguous_or_wrong_sbom(self):
        self.candidate()
        path = self.root / f"{PACKAGE}.lha.spdx.json"
        document = json.loads(path.read_bytes())
        document["packages"] *= 2
        path.write_text(json.dumps(document), encoding="utf-8")
        self.sums()
        with self.assertRaisesRegex(ValueError, "exactly one"):
            release.verify_candidate(TAG, self.root)
        document["packages"] = document["packages"][:1]
        document["packages"][0]["checksums"][0]["checksumValue"] = "0" * 64
        path.write_text(json.dumps(document), encoding="utf-8")
        self.sums()
        with self.assertRaisesRegex(ValueError, "SBOM payload digest"):
            release.verify_candidate(TAG, self.root)

    def test_incomplete_sbom_inventory(self):
        self.candidate()
        path = self.root / f"{PACKAGE}.lha.spdx.json"
        original = path.read_bytes()
        for key in ("files", "relationships", "hasExtractedLicensingInfos"):
            document = json.loads(original)
            document[key] = document[key][:-1]
            path.write_text(json.dumps(document), encoding="utf-8")
            self.sums()
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "SBOM file, runtime"):
                release.verify_candidate(TAG, self.root)

    def test_runtime_inventory_counterprobes(self):
        original = json.dumps(self.runtime)
        for key in ("components", "binaries", "toolchain"):
            runtime = json.loads(original)
            runtime[key] = [] if key == "components" else {} if key == "binaries" else "wrong"
            self.files["RUNTIME-COMPONENTS.json"] = json.dumps(runtime).encode()
            with self.subTest(key=key), self.assertRaises(ValueError):
                release.verify_runtime(self.files, "test-fixture")
        runtime = json.loads(original)
        runtime["binaries"]["bfshandler"]["sha256"] = "0" * 64
        self.files["RUNTIME-COMPONENTS.json"] = json.dumps(runtime).encode()
        with self.assertRaisesRegex(ValueError, "binary digest"):
            release.verify_runtime(self.files, "test-fixture")


class SourceArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = pathlib.Path(self.temporary.name) / f"{SOURCE}.tar.gz"

    def test_valid_source_archive(self):
        commit, epoch, files = release.read_source_archive(write_source(self.path, source_entries()), TAG)
        self.assertEqual((commit, epoch), (COMMIT, 1))
        self.assertEqual(files, {"README.md": (0o644, b"source\n"), "tools/run.sh": (0o755, b"#!/bin/sh\n")})

    def test_source_archive_counterprobes(self):
        owned = tar_entry(f"{SOURCE}/owned", b"x")
        owned[0].uid = 1000
        cases = (("duplicate", [tar_entry(f"{SOURCE}/README.md", b"again\n")]),
                 ("outside its root", [tar_entry("../escape", b"x")]),
                 ("outside its root", [tar_entry("/absolute", b"x")]),
                 ("outside its root", [tar_entry("other/README.md", b"x")]),
                 ("non-regular", [tar_entry(f"{SOURCE}/link", kind=tarfile.SYMTYPE)]),
                 ("non-regular", [tar_entry(f"{SOURCE}/hard", kind=tarfile.LNKTYPE)]),
                 ("file mode", [tar_entry(f"{SOURCE}/open", b"x", mode=0o666)]),
                 ("host owner", [owned]),
                 ("timestamps", [tar_entry(f"{SOURCE}/late", b"x", mtime=2)]),
                 ("directory inventory", [tar_entry(f"{SOURCE}/empty", kind=tarfile.DIRTYPE, mode=0o755)]),
                 ("directory inventory", [tar_entry(f"{SOURCE}/nested/file", b"x")]))
        for message, extra in cases:
            write_source(self.path, source_entries() + extra)
            with self.subTest(message=message, name=extra[0][0].name), self.assertRaisesRegex(ValueError, message):
                release.read_source_archive(self.path, TAG)

    def test_source_archive_without_commit_or_gzip(self):
        for headers in ({}, {"comment": "HEAD"}):
            write_source(self.path, source_entries(), headers)
            with self.subTest(headers=headers), self.assertRaisesRegex(ValueError, "commit identity"):
                release.read_source_archive(self.path, TAG)
        write_source(self.path, source_entries())
        self.path.write_bytes(self.path.read_bytes()[:-8])
        with self.assertRaisesRegex(ValueError, "invalid gzip archive"):
            release.read_source_archive(self.path, TAG)


class SourceArchiveGitTests(unittest.TestCase):
    """Create archives from a real repository and compare them with its commits."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        repository = self.root / "repository"
        environment = patch.dict(os.environ, {
            "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_AUTHOR_NAME": "Fixture", "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
            "GIT_COMMITTER_NAME": "Fixture", "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
            "GIT_AUTHOR_DATE": "1700000000 +0000", "GIT_COMMITTER_DATE": "1700000000 +0000"})
        environment.start()
        self.addCleanup(environment.stop)
        self.addCleanup(os.chdir, os.getcwd())
        (repository / "tools").mkdir(parents=True)
        (repository / "docs/qualification/evidence").mkdir(parents=True)
        os.chdir(repository)
        release.git("init", "-q")
        pathlib.Path("README.md").write_bytes(b"source\n")
        pathlib.Path("tools/run.sh").write_bytes(b"#!/bin/sh\n")
        pathlib.Path("tools/run.sh").chmod(0o755)
        pathlib.Path("docs/guide.md").write_bytes(b"guide\n")
        pathlib.Path("docs/qualification/evidence/run.log").write_bytes(b"retained\n")
        self.commit = self.commit_all("initial")

    def commit_all(self, message):
        release.git("add", "-A")
        release.git("commit", "-q", "-m", message)
        return release.git("rev-parse", "HEAD").decode().strip()

    def create(self, name, commit=None):
        output = self.root / name
        # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit.dangerous-subprocess-use-audit
        subprocess.run([sys.executable, str(TOOLS / "create-source-archive.py"), TAG,  # nosec B603
                        commit or self.commit, str(output)], check=True, capture_output=True, shell=False)
        return output

    def rewrite(self, output, change):
        path = output / f"{SOURCE}.tar.gz"
        with tarfile.open(path, "r:gz") as archive:
            headers = dict(archive.pax_headers)
            members = [(entry, archive.extractfile(entry).read() if entry.isfile() else b"")
                       for entry in archive.getmembers()]
        target = self.root / "rewritten"
        target.mkdir(exist_ok=True)
        write_source(target / path.name, change(members), headers)
        return target

    def test_reproducible_archive_matches_commit(self):
        first, second = self.create("first"), self.create("second")
        name = f"{SOURCE}.tar.gz"
        self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
        release.verify_source_archive(TAG, first, self.commit)
        _, epoch, files = release.read_source_archive(first / name, TAG)
        self.assertEqual(epoch, 1700000000)
        self.assertEqual(files, {"README.md": (0o644, b"source\n"), "docs/guide.md": (0o644, b"guide\n"),
                                 "tools/run.sh": (0o755, b"#!/bin/sh\n")})

    def test_archive_of_another_commit(self):
        pathlib.Path("README.md").write_bytes(b"changed\n")
        later = self.commit_all("later")
        with self.assertRaisesRegex(ValueError, "commit mismatch"):
            release.verify_source_archive(TAG, self.create("later", later), self.commit)

    def test_altered_missing_or_extra_content(self):
        output = self.create("valid")
        probes = (
            ("differs from the commit", lambda members: [
                (entry, b"changed\n" if entry.name.endswith("README.md") else data) for entry, data in members]),
            ("inventory differs", lambda members: [
                member for member in members if not member[0].name.endswith("README.md")]),
            ("inventory differs", lambda members: members + [tar_entry(f"{SOURCE}/extra.txt", mtime=1700000000)]),
        )
        for message, change in probes:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                release.verify_source_archive(TAG, self.rewrite(output, change), self.commit)

    def test_changed_mode(self):
        def drop_execute(members):
            for entry, _ in members:
                if entry.name.endswith("run.sh"):
                    entry.mode = 0o644
            return members
        output = self.rewrite(self.create("valid"), drop_execute)
        with self.assertRaisesRegex(ValueError, "differs from the commit"):
            release.verify_source_archive(TAG, output, self.commit)


if __name__ == "__main__":
    unittest.main()
