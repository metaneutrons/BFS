#!/usr/bin/env python3
"""Positive and isolated negative probes for release inventory gates."""

import io
import json
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "release"))
import release_integrity as release
import sbom_document


TAG = "v0.1.0-qualification"
COMMIT = "a" * 40
PACKAGE = release.package_name(TAG)


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
            "targets": [{"file": f"bfshandler.{cpu}", "flags": [f"-m68{cpu}", "-Os"]}
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
        with self.assertRaises(subprocess.CalledProcessError):
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


if __name__ == "__main__":
    unittest.main()
