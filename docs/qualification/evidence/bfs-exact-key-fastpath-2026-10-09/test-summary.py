#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read-only synthetic counter-probes for the exact-key summary."""

import importlib.util
import contextlib
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


DIRECTORY = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "exact_key_fastpath_summary", DIRECTORY / "summarize.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load exact-key summary module")
SUMMARY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUMMARY
SPEC.loader.exec_module(SUMMARY)

M3_HANDLER = "adef53fe847a498f26c4325159a96e3c1063d67ddb1eb4112268129a2599ad27"
RANGE_HANDLER = "79d6dc6c228b7567337e6b4d547fcbee6d14be049d4aa2bc07d3014414222ea4"
FAST_HANDLER = "27447f991379ccc0eda02254bf5073dc0a11426f0321781cc1eea922f3f04f40"
FAST_PROBE = "557c6248e11c1d1985c8335c2d65eb92aa3c32607c021c0d5433d82faeb6719c"
NORMAL_GUEST = "798d411aa8d38f63bcb0cf315dfd2ab02983350fd58e2638cc119e133cc80b52"
DEEP_GUEST = "7d2ca0b75d497cdc7e88f7df034647f3d9cddeab5d0aefdd16274f417cdb2661"
PFS3_HANDLER = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"


def runtime_digest_text(handler: str, guest: str) -> str:
    return (
        f"{handler}  system/L/bfshandler\n"
        f"{PFS3_HANDLER}  system/L/pfs3aio\n"
        f"{guest}  system/C/fs-compare-bench\n"
    )


class SummaryOracles(unittest.TestCase):
    def test_normal_only_cannot_report_full_qualification(self):
        self.assertEqual(len(SUMMARY.NORMAL_RUNS), 24)
        self.assertTrue(all(run.family == "normal" for run in SUMMARY.NORMAL_RUNS))
        output = io.StringIO()
        with (patch.object(SUMMARY, "verify_inventory", return_value=[]) as verify,
              patch.object(SUMMARY, "print_production_family") as summarize,
              contextlib.redirect_stdout(output)):
            self.assertEqual(SUMMARY.main(["--normal-only"]), 0)
        verify.assert_called_once_with(SUMMARY.NORMAL_RUNS)
        summarize.assert_called_once_with([], "normal", "compare")
        self.assertIn("NOT FULL QUALIFICATION", output.getvalue())
        self.assertNotIn("all 50 required runs verified", output.getvalue())

    def test_fixed_inventories_and_alternating_order(self):
        self.assertEqual(len(SUMMARY.ALL_RUNS), 50)
        self.assertEqual(len(SUMMARY.PILOT_RUNS), 8)
        self.assertEqual(
            {run.family for run in SUMMARY.ALL_RUNS}, {"normal", "durable", "deep"}
        )
        self.assertEqual(
            {run.family for run in SUMMARY.PILOT_RUNS},
            {"pilot-normal", "pilot-durable"},
        )
        self.assertEqual(
            SUMMARY.ALL_RUNS[0].name,
            "exact-key-normal-20261009-m3-1-bfs-first",
        )
        self.assertEqual(
            SUMMARY.ALL_RUNS[-1].name,
            "exact-key-deep-20261009-fast-2-pfs3-first",
        )
        self.assertTrue(all(run.buffers == 30 for run in SUMMARY.ALL_RUNS + SUMMARY.PILOT_RUNS))
        for run in SUMMARY.ALL_RUNS + SUMMARY.PILOT_RUNS:
            self.assertEqual(
                run.order, "bfs-first" if run.repeat % 2 else "pfs3-first"
            )

    def test_inventory_rejects_missing_unexpected_and_duplicate_names(self):
        root = Path("/synthetic/exact-key-evidence")
        names = {run.name for run in SUMMARY.ALL_RUNS}

        def is_dir(path):
            return path == root or (path.parent == root and path.name in names)

        def iterdir(path):
            self.assertEqual(path, root)
            return iter(root / name for name in sorted(names))

        with (patch.object(SUMMARY, "SCRIPT_DIR", root),
              patch.object(Path, "is_dir", is_dir),
              patch.object(Path, "iterdir", iterdir)):
            self.assertEqual(SUMMARY.inventory_errors(SUMMARY.ALL_RUNS), [])
            duplicate = SUMMARY.ALL_RUNS + [SUMMARY.ALL_RUNS[0]]
            self.assertTrue(any(
                "duplicate names" in error
                for error in SUMMARY.inventory_errors(duplicate)
            ))

            names.remove(SUMMARY.ALL_RUNS[0].name)
            self.assertTrue(any(
                "missing expected" in error
                for error in SUMMARY.inventory_errors(SUMMARY.ALL_RUNS)
            ))
            names.add(SUMMARY.ALL_RUNS[0].name)
            names.add("exact-key-normal-20261009-fast-99-bfs-first")
            self.assertTrue(any(
                "unexpected run directories" in error
                for error in SUMMARY.inventory_errors(SUMMARY.ALL_RUNS)
            ))

    def test_pilot_inventory_excludes_deep_diagnostic_family(self):
        root = Path("/synthetic/exact-key-pilot-evidence")
        names = {run.name for run in SUMMARY.PILOT_RUNS}
        names.add("exact-key-deep-20261009-fast-1-bfs-first")

        def is_dir(path):
            return path == root or (path.parent == root and path.name in names)

        def iterdir(path):
            self.assertEqual(path, root)
            return iter(root / name for name in sorted(names))

        with (patch.object(SUMMARY, "SCRIPT_DIR", root),
              patch.object(Path, "is_dir", is_dir),
              patch.object(Path, "iterdir", iterdir)):
            self.assertEqual(SUMMARY.inventory_errors(SUMMARY.PILOT_RUNS), [])

    def test_installed_sha_identities_match_variant_and_deep_probe(self):
        cases = (
            (next(r for r in SUMMARY.ALL_RUNS if r.family == "normal" and r.variant == "m3"),
             M3_HANDLER, NORMAL_GUEST),
            (next(r for r in SUMMARY.ALL_RUNS if r.family == "normal" and r.variant == "range"),
             RANGE_HANDLER, NORMAL_GUEST),
            (next(r for r in SUMMARY.ALL_RUNS if r.family == "normal" and r.variant == "fast"),
             FAST_HANDLER, NORMAL_GUEST),
            (next(r for r in SUMMARY.ALL_RUNS if r.family == "deep"),
             FAST_PROBE, DEEP_GUEST),
            (next(r for r in SUMMARY.PILOT_RUNS if r.variant == "fast"),
             FAST_HANDLER, NORMAL_GUEST),
        )
        for run, handler, guest in cases:
            with self.subTest(run=run.name):
                with (patch.object(Path, "is_file", return_value=True),
                      patch.object(
                          Path, "read_text",
                          return_value=runtime_digest_text(handler, guest),
                      )):
                    SUMMARY.check_runtime_identities(run, Path("/synthetic") / run.name)

        fast_run = cases[2][0]
        wrong = runtime_digest_text("0" * 64, NORMAL_GUEST)
        with (patch.object(Path, "is_file", return_value=True),
              patch.object(Path, "read_text", return_value=wrong)):
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.check_runtime_identities(fast_run, Path("/synthetic") / fast_run.name)

    def test_rdb_requires_unique_dh1_bfs0_with_30_buffers(self):
        run = SUMMARY.ALL_RUNS[0]
        path = Path("/synthetic") / run.name / "bfs-rdb.json"
        valid = {
            "rdb": {
                "partitions": [{
                    "name": "DH1",
                    "dos_env": {
                        "dos_type": SUMMARY.BASE.BFS_DOSTYPE,
                        "dos_type_str": "BFS0",
                        "num_buffer": 30,
                    },
                }],
            },
        }
        with (patch.object(Path, "is_file", return_value=True),
              patch.object(Path, "read_text", return_value=json.dumps(valid))):
            SUMMARY.BASE.check_rdb(run, path.parent)

        wrong_buffers = json.loads(json.dumps(valid))
        wrong_buffers["rdb"]["partitions"][0]["dos_env"]["num_buffer"] = 64
        with (patch.object(Path, "is_file", return_value=True),
              patch.object(Path, "read_text", return_value=json.dumps(wrong_buffers))):
            with self.assertRaises(SUMMARY.QualificationError):
                SUMMARY.BASE.check_rdb(run, path.parent)

    def test_tsv_rejects_duplicate_pass_and_wrong_schema(self):
        run = SUMMARY.ALL_RUNS[0]
        path = Path("/synthetic") / run.name / "system/Results/bfs.tsv"
        valid = "FS_COMPARE_BENCH\t4\nDRIVE\tDH1:\nPASS\t1\nLOOKUP_400_US\t100\n"
        with (patch.object(Path, "is_file", return_value=True),
              patch.object(Path, "read_text", return_value=valid)):
            SUMMARY.BASE.load_tsv(path, "FS_COMPARE_BENCH", 4, "DH1:")

        malformed_inputs = (
            valid + "PASS\t1\n",
            valid.replace("FS_COMPARE_BENCH\t4", "FS_COMPARE_BENCH\t3"),
        )
        for malformed in malformed_inputs:
            with self.subTest(input=malformed):
                with (patch.object(Path, "is_file", return_value=True),
                      patch.object(Path, "read_text", return_value=malformed)):
                    with self.assertRaises(SUMMARY.QualificationError):
                        SUMMARY.BASE.load_tsv(
                            path, "FS_COMPARE_BENCH", 4, "DH1:"
                        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
