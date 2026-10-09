#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read-only counter-probes for the retained qualification summary."""
import dataclasses
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

DIRECTORY = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("leaf_range_summary", DIRECTORY / "summarize.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUMMARY
SPEC.loader.exec_module(SUMMARY)


class SummaryOracles(unittest.TestCase):
    def test_exact_inventory_and_all_installed_copies(self):
        self.assertEqual(len(SUMMARY.ALL_RUNS), 44)
        self.assertEqual(SUMMARY.inventory_errors(SUMMARY.ALL_RUNS), [])
        for run in SUMMARY.ALL_RUNS:
            SUMMARY.check_runtime_identities(run, DIRECTORY / run.name)
            SUMMARY.check_rdb(run, DIRECTORY / run.name)

    def test_missing_and_duplicate_inventory_rejected(self):
        missing = dataclasses.replace(SUMMARY.ALL_RUNS[0], name="missing-counter-probe")
        self.assertTrue(SUMMARY.inventory_errors([missing]))
        self.assertTrue(SUMMARY.inventory_errors(SUMMARY.ALL_RUNS + [SUMMARY.ALL_RUNS[0]]))

    def test_wrong_installed_handler_rejected(self):
        run = next(run for run in SUMMARY.ALL_RUNS if run.family == "normal" and run.variant == "range")
        directory = DIRECTORY / run.name
        text = (directory / "runtime-inputs.sha256").read_text(encoding="ascii")
        text = text.replace("system/L/bfshandler", "system/L/wrong-handler")
        with patch.object(Path, "read_text", return_value=text):
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.check_runtime_identities(run, directory)

    def test_duplicate_pass_and_wrong_schema_rejected(self):
        run = SUMMARY.ALL_RUNS[0]
        path = DIRECTORY / run.name / "system/Results/bfs.tsv"
        text = path.read_text(encoding="ascii")
        for malformed in (text + "PASS\t1\n", text.replace("FS_COMPARE_BENCH\t4", "FS_COMPARE_BENCH\t3")):
            with patch.object(Path, "read_text", return_value=malformed):
                with self.assertRaises(SUMMARY.QualificationError):
                    SUMMARY.load_tsv(path, "FS_COMPARE_BENCH", 4, "DH1:")

    def test_wrong_actual_buffer_capacity_rejected(self):
        run = SUMMARY.ALL_RUNS[0]
        directory = DIRECTORY / run.name
        document = json.loads((directory / "bfs-rdb.json").read_text(encoding="utf-8"))
        document["rdb"]["partitions"][0]["dos_env"]["num_buffer"] = 128
        with patch.object(Path, "read_text", return_value=json.dumps(document)):
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.check_rdb(run, directory)


if __name__ == "__main__":
    unittest.main(verbosity=2)
