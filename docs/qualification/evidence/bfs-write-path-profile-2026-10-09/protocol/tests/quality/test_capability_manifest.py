"""Keep implemented platform surfaces tied to the capability ledger."""

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LEDGER = ROOT / "docs/platform-contract/capabilities.md"
MANIFEST = ROOT / "docs/platform-contract/capabilities.json"


class CapabilityManifestTests(unittest.TestCase):
    def test_manifest_declares_each_implemented_source_surface(self):
        manifest = json.loads(MANIFEST.read_text())
        self.assertEqual(manifest["schemaVersion"], 1)
        declared = {surface["path"] for surface in manifest["surfaces"]}
        implemented = {
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "src").iterdir()
            if path.is_dir() and any(path.iterdir())
        }
        self.assertEqual(declared, implemented)

    def test_manifest_capabilities_exist_in_the_ledger(self):
        ledger_ids = set(re.findall(r"`(BFS-CAP-[A-Z0-9-]+)`", LEDGER.read_text()))
        manifest = json.loads(MANIFEST.read_text())
        for surface in manifest["surfaces"]:
            self.assertTrue(surface["capabilities"], surface["path"])
            self.assertTrue((ROOT / surface["path"]).is_dir(), surface["path"])
            for capability in surface["capabilities"]:
                self.assertIn(capability, ledger_ids)


if __name__ == "__main__":
    unittest.main()
