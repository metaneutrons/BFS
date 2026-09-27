"""Contract tests for the bounded #28 image fault campaign."""

import copy
import json
from pathlib import Path
import subprocess  # nosec B404 - invokes the local campaign entry point
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "qualification"))

import fault_campaign
import verify_fault_campaign


class FaultCampaignTests(unittest.TestCase):
    def manifest(self):
        return json.loads(fault_campaign.DEFAULT_MANIFEST.read_text(encoding="ascii"))

    def write_manifest(self, directory, manifest):
        path = Path(directory) / "campaign.json"
        path.write_text(json.dumps(manifest), encoding="ascii")
        return path

    def run_campaign(self, directory):
        output = Path(directory) / "evidence"
        completed = subprocess.run([sys.executable, str(ROOT / "tests" / "qualification" /
                                            "fault_campaign.py"), "--output", str(output)],
                                   cwd=ROOT, capture_output=True, text=True, check=False)  # nosec B603
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return output

    def test_manifest_declares_each_initial_family(self):
        manifest = fault_campaign.load_manifest(fault_campaign.DEFAULT_MANIFEST)
        self.assertEqual(manifest["format_version"], 1)
        self.assertEqual({case["family"] for case in manifest["cases"]}, {
            "recognition-and-superblocks", "tree-and-inode-structure",
            "checksummed-data", "allocation-and-reclamation", "operation-interruption",
            "storage-protocol-faults",
        })
        self.assertIn("repairable-leak", {case["expected_class"] for case in manifest["cases"]})

    def test_rejects_duplicate_case_identifier(self):
        manifest = self.manifest()
        manifest["cases"].append(copy.deepcopy(manifest["cases"][0]))
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "duplicate"):
                fault_campaign.load_manifest(self.write_manifest(directory, manifest))

    def test_rejects_unknown_outcome_and_incomplete_observers(self):
        manifest = self.manifest()
        manifest["cases"][0]["expected_class"] = "maybe-clean"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "outcome"):
                fault_campaign.load_manifest(self.write_manifest(directory, manifest))
        manifest = self.manifest()
        manifest["cases"][0]["observers"] = []
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "observers"):
                fault_campaign.load_manifest(self.write_manifest(directory, manifest))

    def test_refuses_to_overwrite_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "evidence"
            output.mkdir()
            with self.assertRaisesRegex(RuntimeError, "overwrite"):
                fault_campaign.prepare_output(output, fault_campaign.DEFAULT_MANIFEST)

    def test_creates_a_missing_evidence_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "new" / "evidence"
            copied_manifest = fault_campaign.prepare_output(output, fault_campaign.DEFAULT_MANIFEST)
            self.assertTrue(copied_manifest.is_file())
            self.assertTrue(output.is_dir())

    def test_campaign_evidence_is_independently_verifiable(self):
        with tempfile.TemporaryDirectory() as directory:
            output = self.run_campaign(directory)
            self.assertEqual(verify_fault_campaign.verify(output), {
                "cases": 9, "qualified": True, "status": "passed",
            })
            result = json.loads((output / "result.json").read_text(encoding="ascii"))
            self.assertEqual(result["qualification_scope"],
                             "bounded-modelled-image-and-adapter-campaign")
            self.assertIn("physical-power-controller-media", result["unqualified_scopes"])

    def test_verifier_rejects_tampered_case_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = self.run_campaign(directory)
            records = output / "cases.jsonl"
            records.write_text(records.read_text(encoding="ascii").replace("20260926", "20260927", 1),
                               encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "case-record digest"):
                verify_fault_campaign.verify(output)


if __name__ == "__main__":
    unittest.main()
