# SPDX-License-Identifier: MPL-2.0
"""Link-map inventory probes, including libraries not selected for linking."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/release"))
import runtime_inventory as runtime


class RuntimeInventoryTests(unittest.TestCase):
    def test_selected_archive_members_and_startup(self):
        text = ("Archive member included to satisfy reference by file (symbol)\n\n"
                "/toolchain/libnix.a(memcmp.o)\n    /tmp/input.o (memcmp)\n"
                "Memory Configuration\nLOAD /toolchain/ncrt0.o\n"
                "LOAD /toolchain/libamiga.a\nLOAD /tmp/input.o\n")
        self.assertEqual(runtime.selected_inputs(text),
                         {Path("/toolchain/libnix.a"): {"memcmp.o"},
                          Path("/toolchain/ncrt0.o"): set()})

    def test_missing_malformed_and_unknown_inputs(self):
        header = "Archive member included to satisfy reference by file (symbol)\n"
        for text in ("", header, header + "Memory Configuration\n",
                     header + "/lib.a(../bad.o)\nMemory Configuration\n",
                     header + "Memory Configuration\nLOAD /toolchain/crt0.o\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                runtime.selected_inputs(text)

    def test_input_bytes_determine_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libgcc.a"
            library.write_bytes(b"isolated fixture")
            first = runtime.component(library)
            library.write_bytes(b"altered fixture")
            second = runtime.component(library)
            self.assertNotEqual(first["id"], second["id"])
            self.assertEqual(first["license"], "GPL-3.0-or-later WITH GCC-exception-3.1")
            with self.assertRaisesRegex(ValueError, "unreviewed"):
                runtime.component(Path(directory) / "unexpected.a")


if __name__ == "__main__":
    unittest.main()
