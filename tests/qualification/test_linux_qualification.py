#!/usr/bin/env python3
"""Contract tests for the versioned M7 qualification matrix."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import linux_qualification
import fuse_soak
import verify_fuse_soak


class LinuxQualificationTests(unittest.TestCase):
    def make_soak_evidence(self, directory, *, preflight=True):
        root = Path(directory)
        event = {
            "cycle": 0,
            "elapsed_seconds": 3.125,
            "cycle_seconds": 3.125,
            "operations": 2255,
            "pressure_writes": 1707,
            "resources": {"rss_kib": 2688, "open_descriptors": 5},
            "checks": {check: "passed" for check in fuse_soak.CYCLE_CHECKS},
            "status": "passed",
        }
        events = root / "events.jsonl"
        events.write_text(json.dumps(event, sort_keys=True) + "\n", encoding="ascii")
        evidence = {
            "events_sha256": hashlib.sha256(events.read_bytes()).hexdigest(),
            "event_count": 1,
            "total_operations": event["operations"],
            "total_pressure_writes": event["pressure_writes"],
            "peak_rss_kib": event["resources"]["rss_kib"],
            "peak_open_descriptors": event["resources"]["open_descriptors"],
        }
        result = {
            "approval_reference": None if preflight else
            "https://github.com/metaneutrons/BFS/issues/29#issuecomment-123",
            "limits": {"target_duration_seconds": 72 * 60 * 60, "cycle_seconds": 60,
                       "maximum_rss_kib": 262144, "maximum_open_descriptors": 128},
            "requested_duration_seconds": 30 if preflight else 72 * 60 * 60,
            "completed_duration_seconds": 30 if preflight else 72 * 60 * 60,
            "completed_cycles": 1,
            "preflight": preflight,
            "qualified": not preflight,
            "status": "passed",
            "evidence": evidence,
        }
        (root / "result.json").write_text(json.dumps(result), encoding="ascii")
        return events, result

    def test_current_matrix_is_valid(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        self.assertEqual(matrix["soak"]["target_duration_seconds"], 72 * 60 * 60)

    def test_rejects_missing_block_size(self):
        matrix = json.loads(linux_qualification.DEFAULT_MATRIX.read_text(encoding="ascii"))
        matrix["fast"]["block_sizes"].pop()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"
            path.write_text(json.dumps(matrix), encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "block size"):
                linux_qualification.load_matrix(path)

    def test_rejects_short_soak(self):
        matrix = json.loads(linux_qualification.DEFAULT_MATRIX.read_text(encoding="ascii"))
        matrix["soak"]["target_duration_seconds"] = 60
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"
            path.write_text(json.dumps(matrix), encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "72 hours"):
                linux_qualification.load_matrix(path)

    def test_rejects_unapproved_short_soak(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        with self.assertRaisesRegex(RuntimeError, "explicit preflight"):
            fuse_soak.select_duration(matrix["soak"], 60, False)

    def test_marks_short_soak_as_preflight(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        self.assertEqual(fuse_soak.select_duration(matrix["soak"], 60, True), 60)

    def test_rejects_target_duration_preflight(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        with self.assertRaisesRegex(RuntimeError, "shorter"):
            fuse_soak.select_duration(matrix["soak"], matrix["soak"]["target_duration_seconds"], True)

    def test_requires_issue_29_approval_reference(self):
        self.assertTrue(fuse_soak.approval_reference_valid(
            "https://github.com/metaneutrons/BFS/issues/29#issuecomment-123"))
        self.assertFalse(fuse_soak.approval_reference_valid(
            "https://github.com/metaneutrons/BFS/issues/42#issuecomment-123"))
        fuse_soak.validate_approval_reference(None, True)
        with self.assertRaisesRegex(RuntimeError, "approval reference"):
            fuse_soak.validate_approval_reference(None, False)

    def test_evidence_output_is_bounded(self):
        self.assertEqual(len(linux_qualification.output_tail("x" * 9000)), 8192)

    def test_soak_profile_has_capacity_and_handle_pressure(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        soak = matrix["soak"]
        self.assertGreaterEqual(soak["client_open_files"], 1)
        self.assertGreaterEqual(soak["minimum_available_bytes"],
                                soak["block_size"] * soak["block_count"])

    def test_client_handle_pressure_is_bounded_and_cleaned_up(self):
        limits = {"client_processes": 2, "client_open_files": 2,
                  "operation_deadline_seconds": 10}
        with tempfile.TemporaryDirectory() as directory:
            operations = fuse_soak.run_clients(Path(directory) / "soak", 0, limits)
        self.assertEqual(operations, 32)

    def test_cycle_wait_does_not_exceed_requested_duration(self):
        self.assertEqual(fuse_soak.next_cycle_wait(60, 30, 3, 3), 27)
        self.assertEqual(fuse_soak.next_cycle_wait(60, 30, 31, 3), 0)

    def test_verifies_complete_preflight_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            self.make_soak_evidence(directory)
            self.assertEqual(verify_fuse_soak.verify(directory),
                             {"qualified": False, "events": 1, "status": "passed"})

    def test_rejects_event_log_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            events, _ = self.make_soak_evidence(directory)
            events.write_text(events.read_text(encoding="ascii").replace("2255", "2256"),
                              encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "evidence summary"):
                verify_fuse_soak.verify(directory)

    def test_rejects_incomplete_cycle_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            events, _ = self.make_soak_evidence(directory)
            event = json.loads(events.read_text(encoding="ascii"))
            del event["checks"]["disk_full"]
            events.write_text(json.dumps(event) + "\n", encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "checks are incomplete"):
                verify_fuse_soak.verify(directory)

    def test_rejects_target_evidence_with_missing_cycles(self):
        with tempfile.TemporaryDirectory() as directory:
            self.make_soak_evidence(directory, preflight=False)
            with self.assertRaisesRegex(RuntimeError, "too few completed cycles"):
                verify_fuse_soak.verify(directory)


if __name__ == "__main__":
    unittest.main()
