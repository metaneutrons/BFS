# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
# Exercise the actual fixed repository script without a shell command string.
import subprocess  # nosec B404
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/check-morphos-elf.sh"

HEADER = """ELF Header:
  Magic:   7f 45 4c 46 01 02 01 00 00 00 00 00 00 00 00 00
  Class:                             {cls}
  Data:                              {data}
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  Type:                              EXEC (Executable file)
  Machine:                           {machine}
"""
GOOD = {"cls": "ELF32", "data": "2's complement, big endian", "machine": "PowerPC"}


class MorphosElfTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        # A readelf that prints the header stored next to each file.
        fake = self.dir / "readelf"
        fake.write_text('#!/bin/bash\n[[ "$1" == -h ]] || exit 2\n'
                        'cat "$2.header" || exit 1\n', encoding="utf-8")
        fake.chmod(0o755)

    def make(self, name, header=None, **fields):
        path = self.dir / name
        path.write_bytes(b"\x7fELF")
        if header is not None:
            Path(f"{path}.header").write_text(header, encoding="utf-8")
        else:
            Path(f"{path}.header").write_text(HEADER.format(**{**GOOD, **fields}),
                                              encoding="utf-8")
        return path

    def check(self, *files):
        return subprocess.run([str(SCRIPT), *map(str, files)],  # nosec B603
                              env={**os.environ, "READELF": str(self.dir / "readelf")},
                              capture_output=True, text=True, check=False)

    def test_powerpc_big_endian_elf32_passes(self):
        result = self.check(self.make("a"), self.make("b"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("ELF32, big endian, PowerPC"), 2)

    def test_each_deviation_fails(self):
        for fields in ({"cls": "ELF64"},
                       {"data": "2's complement, little endian"},
                       {"machine": "PowerPC64"},
                       {"machine": "MC68000"},
                       {"machine": "Advanced Micro Devices X86-64"}):
            with self.subTest(fields=fields):
                self.assertEqual(self.check(self.make("x", **fields)).returncode, 1)

    def test_one_bad_file_fails_the_whole_check(self):
        result = self.check(self.make("good"), self.make("bad", machine="ARM"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("bad", result.stderr)

    def test_missing_or_unreadable_file_fails(self):
        self.assertEqual(self.check(self.dir / "missing").returncode, 1)
        unreadable = self.dir / "plain"
        unreadable.write_text("not an ELF file\n", encoding="utf-8")
        self.assertEqual(self.check(unreadable).returncode, 1)

    def test_no_arguments_is_a_usage_error(self):
        self.assertEqual(self.check().returncode, 2)


if __name__ == "__main__":
    unittest.main()
