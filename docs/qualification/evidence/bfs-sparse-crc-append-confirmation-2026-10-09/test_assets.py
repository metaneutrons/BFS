# SPDX-License-Identifier: MPL-2.0
"""Receipts are text fixtures, not licensed files or executable assets."""

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
import unittest

import verify_assets as assets


def write_receipt(path, values):
    path.write_text("".join(f"{digest}  {name}\n" for name, digest in values.items()),
                    encoding="ascii")


class AssetReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.entries = assets.summary.EXPECTED_SCHEDULE
        for entry in self.entries:
            run_dir = self.root / entry.run_name
            run_dir.mkdir()
            expected = assets.summary.PARSER.expected_runtime_inputs(
                assets.summary.parser_run_for(entry))
            for name in ("runtime-inputs.sha256", "runtime-inputs.post.sha256"):
                write_receipt(run_dir / name, expected)
            installed = {name.removeprefix("system/"): digest
                         for name, digest in expected.items()}
            installed["C/Copy"] = "a" * 64
            write_receipt(run_dir / "installed-assets.sha256", installed)

    def tearDown(self):
        self.temporary.cleanup()

    def run_check(self):
        with patch.object(assets, "__file__", str(self.root / "verify-assets.py")), \
                redirect_stdout(StringIO()) as captured:
            assets.main()
        return captured.getvalue()

    def test_all_40_pinned_receipts_and_shared_assets_pass(self):
        self.assertIn("all 40 before/after", self.run_check())

    def test_changed_post_run_handler_is_rejected(self):
        path = self.root / self.entries[0].run_name / "runtime-inputs.post.sha256"
        receipt = assets.read_receipt(path)
        receipt["system/L/bfshandler"] = "0" * 64
        write_receipt(path, receipt)
        with self.assertRaisesRegex(ValueError, "Runtime input changed"):
            self.run_check()

    def test_changed_workbench_asset_is_rejected(self):
        path = self.root / self.entries[-1].run_name / "installed-assets.sha256"
        receipt = assets.read_receipt(path)
        receipt["C/Copy"] = "b" * 64
        write_receipt(path, receipt)
        with self.assertRaisesRegex(ValueError, "Non-handler assets differ"):
            self.run_check()

    def test_installed_guest_mismatch_is_rejected(self):
        path = self.root / self.entries[0].run_name / "installed-assets.sha256"
        receipt = assets.read_receipt(path)
        receipt.pop("C/fs-compare-bench")
        write_receipt(path, receipt)
        with self.assertRaisesRegex(ValueError, "contradicts runtime input"):
            self.run_check()

    def test_duplicate_and_empty_receipts_are_rejected(self):
        path = self.root / "bad.sha256"
        path.write_text(f"{'a' * 64}  C/Copy\n{'b' * 64}  C/Copy\n", encoding="ascii")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            assets.read_receipt(path)
        path.write_text("", encoding="ascii")
        with self.assertRaisesRegex(ValueError, "Empty"):
            assets.read_receipt(path)


if __name__ == "__main__":
    unittest.main()
