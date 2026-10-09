#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded synthetic rejection oracles for the cursor-path summary."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


DIRECTORY = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("cursor_path_summary", DIRECTORY / "summarize.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUMMARY
SPEC.loader.exec_module(SUMMARY)


class SummaryRejectionOracles(unittest.TestCase):
    def test_exact_inventory_counts_and_missing_duplicate_unexpected_rejection(self):
        self.assertEqual(len(SUMMARY.PILOT_RUNS), 4)
        self.assertEqual(len(SUMMARY.DEEP_RUNS), 4)
        self.assertEqual(len(SUMMARY.PRODUCTION_RUNS), 32)
        self.assertEqual(len(SUMMARY.ALL_RUNS), 40)
        self.assertTrue(SUMMARY.ALL_RUNS[0].name.startswith("cursor-pilot-normal-20261009-"))

        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertTrue(SUMMARY.inventory_errors(root, [run]))
            (root / run.name).mkdir()
            self.assertTrue(SUMMARY.inventory_errors(root, [run, run]))
            (root / "cursor-pilot-normal-20261009-range-9-bfs-first").mkdir()
            self.assertTrue(SUMMARY.inventory_errors(root, [run]))

    def test_missing_pass_duplicate_rows_and_wrong_schema_rejected(self):
        cases = (
            "FS_COMPARE_BENCH\t4\nDRIVE\tDH1:\nSMALL_CREATE_40_US\t10\n",
            "FS_COMPARE_BENCH\t4\nFS_COMPARE_BENCH\t4\nDRIVE\tDH1:\nPASS\t1\n",
            "FS_COMPARE_BENCH\t3\nDRIVE\tDH1:\nPASS\t1\n",
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bfs.tsv"
            for content in cases:
                path.write_text(content, encoding="ascii")
                with self.subTest(content=content), self.assertRaises(SUMMARY.QualificationError):
                    SUMMARY.PARSER.load_tsv(path, "FS_COMPARE_BENCH", 4, "DH1:")

    def test_runtime_identity_mismatch_rejected(self):
        run = next(
            item for item in SUMMARY.ALL_RUNS
            if item.family == "normal" and item.variant == "path"
        )
        expected = SUMMARY.expected_runtime_inputs(run)
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            digest_file = run_dir / "runtime-inputs.sha256"
            digest_file.write_text(
                "".join(f"{digest}  {path}\n" for path, digest in expected.items()),
                encoding="ascii",
            )
            SUMMARY.check_runtime_identities(run, run_dir)
            digest_file.write_text(
                digest_file.read_text(encoding="ascii").replace(
                    SUMMARY.PATH_PRODUCTION, SUMMARY.RANGE_PRODUCTION
                ),
                encoding="ascii",
            )
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.check_runtime_identities(run, run_dir)

    def test_pilot_production_and_deep_probe_digest_mapping(self):
        cases = (
            ("pilot", "range", "79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4",
             "798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52"),
            ("pilot", "path", "60c3203d11221aff46fc6e536578a4c644ab37c0106aa28cd44e01db06e0984e",
             "798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52"),
            ("deep", "range", "a46a153a387de6cac773aee8bc6b757ce0bccf7a6dbc03b2c6ec8f8f8cc7981c",
             "7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661"),
            ("deep", "path", "f09d043afda2e4fcb33e827984ae355224245c28ad18e819b26dc4d4583aaadc",
             "7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661"),
        )
        pfs3 = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"
        for family, variant, handler, guest in cases:
            run = next(
                item for item in SUMMARY.ALL_RUNS
                if item.family == family and item.variant == variant
            )
            expected = {
                "system/L/bfshandler": handler,
                "system/L/pfs3aio": pfs3,
                "system/C/fs-compare-bench": guest,
            }
            with tempfile.TemporaryDirectory() as temporary:
                run_dir = Path(temporary)
                (run_dir / "runtime-inputs.sha256").write_text(
                    "".join(f"{digest}  {path}\n" for path, digest in expected.items()),
                    encoding="ascii",
                )
                SUMMARY.check_runtime_identities(run, run_dir)

    def test_actual_bfs_buffer_capacity_mismatch_rejected(self):
        run = SUMMARY.PILOT_RUNS[0]
        partition = {
            "name": "DH1",
            "dos_env": {
                "dos_type": 0x42465300,
                "dos_type_str": "BFS0",
                "num_buffer": 30,
            },
        }
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            rdb_file = run_dir / "bfs-rdb.json"
            rdb_file.write_text(json.dumps({"rdb": {"partitions": [partition]}}), encoding="utf-8")
            SUMMARY.check_rdb(run, run_dir)
            partition["dos_env"]["num_buffer"] = 64
            rdb_file.write_text(json.dumps({"rdb": {"partitions": [partition]}}), encoding="utf-8")
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.check_rdb(run, run_dir)
            partition["dos_env"]["num_buffer"] = 30
            duplicate_dh1 = {
                "name": "DH1",
                "dos_env": {"dos_type": 0, "dos_type_str": "OTHER", "num_buffer": 30},
            }
            rdb_file.write_text(
                json.dumps({"rdb": {"partitions": [partition, duplicate_dh1]}}),
                encoding="utf-8",
            )
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.check_rdb(run, run_dir)


if __name__ == "__main__":
    unittest.main(verbosity=2)
