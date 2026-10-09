"""Contract tests for the bounded #28 image fault campaign."""

import copy
import json
from pathlib import Path
import subprocess  # nosec B404 - invokes the local campaign entry point
import sys
import tempfile
import unittest
from unittest.mock import patch


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

    def test_backup_recovery_accepts_clean_or_repairable_state(self):
        for statuses, expected_names in (
                ([0, 0], ["oracle", "check"]),
                ([0, 1, 1, 0, 0], ["oracle", "check", "repair",
                                    "check-after-repair", "oracle-after-repair"])):
            with self.subTest(statuses=statuses):
                records = []
                with patch.object(fault_campaign, "command_record",
                                  side_effect=[{"name": name, "returncode": status}
                                               for name, status in zip(expected_names, statuses)]):
                    self.assertTrue(fault_campaign.observe_recovery(
                        Path("fixture.bfs"), 30, records, require_repair=False))
                self.assertEqual([record["name"] for record in records], expected_names)

    def test_deliberate_leak_still_requires_repair(self):
        records = []
        with patch.object(fault_campaign, "command_record",
                          side_effect=[{"name": "oracle", "returncode": 0},
                                       {"name": "check", "returncode": 0}]):
            self.assertFalse(fault_campaign.observe_recovery(
                Path("fixture.bfs"), 30, records, require_repair=True))
        self.assertEqual([record["name"] for record in records], ["oracle", "check"])


if __name__ == "__main__":
    unittest.main()
