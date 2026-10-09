# SPDX-License-Identifier: MPL-2.0
"""Rejection-oracle tests for the scan-admission evidence summarizer."""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("summarize.py")
SPEC = importlib.util.spec_from_file_location("scan_admission_summary", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"could not import summarizer: {SCRIPT}")
SUMMARY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUMMARY
SPEC.loader.exec_module(SUMMARY)


class SummarizerRejectionTests(unittest.TestCase):
    def write_runtime_inputs(self, run: SUMMARY.Run, run_dir: Path,
                             replace: dict[str, str] | None = None) -> None:
        inputs = SUMMARY.expected_runtime_inputs(run)
        if replace:
            inputs.update(replace)
        lines = [f"{digest}  {path}" for path, digest in inputs.items()]
        (run_dir / "runtime-inputs.sha256").write_text("\n".join(lines) + "\n", encoding="ascii")

    def write_valid_rdb(self, run: SUMMARY.Run, run_dir: Path,
                        buffers: int = 30) -> None:
        document = {
            "rdb": {
                "partitions": [{
                    "name": "DH1",
                    "dos_env": {
                        "dos_type": SUMMARY.PARSER.BFS_DOSTYPE,
                        "dos_type_str": "BFS0",
                        "num_buffer": buffers,
                    },
                }],
            },
        }
        (run_dir / "bfs-rdb.json").write_text(json.dumps(document), encoding="utf-8")

    def write_minimal_results(self, run: SUMMARY.Run, run_dir: Path,
                              pass_value: str = "1", malformed: bool = False) -> None:
        suffix, header, schema = SUMMARY.expected_schema(run.mode)
        results_dir = run_dir / "system/Results"
        results_dir.mkdir(parents=True, exist_ok=True)
        for filesystem, drive in (("bfs", "DH1:"), ("pfs3", "DH2:")):
            rows = [f"{header}\t{schema}", f"DRIVE\t{drive}", "LOOKUP_400_US\t100"]
            if malformed:
                rows.append("BROKEN\tTSV\tROW")
            rows.append(f"PASS\t{pass_value}")
            (results_dir / f"{filesystem}.{suffix}").write_text(
                "\n".join(rows) + "\n", encoding="ascii"
            )

    def make_valid_run_dir(self, root: Path, run: SUMMARY.Run,
                           buffers: int = 30) -> Path:
        run_dir = root / run.name
        run_dir.mkdir()
        self.write_runtime_inputs(run, run_dir)
        self.write_valid_rdb(run, run_dir, buffers)
        self.write_minimal_results(run, run_dir)
        return run_dir

    def test_default_inventory_rejects_missing_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            errors = SUMMARY.inventory_errors(root, SUMMARY.ALL_RUNS)
        self.assertEqual(len(SUMMARY.ALL_RUNS), 40)
        self.assertTrue(any("missing expected run directories" in error for error in errors))
        self.assertIn(SUMMARY.ALL_RUNS[0].name, "\n".join(errors))

    def test_unexpected_run_directory_is_rejected(self):
        expected = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / expected.name).mkdir()
            (root / "admission-pilot-normal-20261009-m5-9-bfs-first").mkdir()
            errors = SUMMARY.inventory_errors(root, [expected])
        self.assertTrue(any("unexpected run directories" in error for error in errors))

    def test_pilot_and_deep_selections_are_exact_four_run_inventories(self):
        self.assertEqual(len(SUMMARY.PILOT_RUNS), 4)
        self.assertEqual(len(SUMMARY.DEEP_RUNS), 4)
        self.assertEqual(len(SUMMARY.PRODUCTION_RUNS), 32)

    def test_admission_inventory_names_and_runtime_hashes(self):
        pilot_names = {run.name for run in SUMMARY.PILOT_RUNS}
        deep_names = {run.name for run in SUMMARY.DEEP_RUNS}
        self.assertIn("admission-pilot-normal-20261009-admission-1-bfs-first", pilot_names)
        self.assertIn("admission-deep-20261009-admission-2-pfs3-first", deep_names)
        self.assertTrue(all(run.buffers == 30 for run in SUMMARY.ALL_RUNS))
        self.assertEqual(
            SUMMARY.expected_runtime_inputs(
                next(run for run in SUMMARY.PILOT_RUNS if run.variant == "admission")
            )["system/L/bfshandler"],
            SUMMARY.ADMISSION_PRODUCTION,
        )
        self.assertEqual(
            SUMMARY.expected_runtime_inputs(
                next(run for run in SUMMARY.DEEP_RUNS if run.variant == "admission")
            )["system/L/bfshandler"],
            SUMMARY.ADMISSION_ABI16_PROBE,
        )

    def test_deep_schema_override_does_not_mutate_imported_parser(self):
        self.assertEqual(SUMMARY.expected_schema("deep-compare")[2], 15)
        self.assertEqual(SUMMARY.PARSER.expected_schema("deep-compare")[2], 14)

    def test_wrong_handler_digest_is_rejected(self):
        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            self.write_runtime_inputs(run, run_dir, {"system/L/bfshandler": "0" * 64})
            with self.assertRaisesRegex(SUMMARY.QualificationError, "installed input identity mismatch"):
                SUMMARY.check_runtime_identities(run, run_dir)

    def test_wrong_guest_digest_is_rejected(self):
        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            self.write_runtime_inputs(run, run_dir, {"system/C/fs-compare-bench": "1" * 64})
            with self.assertRaisesRegex(SUMMARY.QualificationError, "installed input identity mismatch"):
                SUMMARY.check_runtime_identities(run, run_dir)

    def test_rdb_capacity_other_than_thirty_is_rejected(self):
        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            self.write_valid_rdb(run, run_dir, buffers=29)
            with self.assertRaisesRegex(SUMMARY.QualificationError, "de_NumBuffers=30"):
                SUMMARY.check_rdb(run, run_dir)

    def test_fake_pass_row_cannot_override_repository_verifier_rejection(self):
        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_dir = self.make_valid_run_dir(root, run)
            rejected = types.SimpleNamespace(returncode=1, stderr="ERROR: required row missing", stdout="")
            with patch.object(SUMMARY.subprocess, "run", return_value=rejected) as invoke:
                with self.assertRaisesRegex(SUMMARY.QualificationError, "strict repository verifier failed"):
                    SUMMARY.verify_run(run, root)
            self.assertEqual(invoke.call_args.args[0][-2:], [str(run_dir), run.mode])

    def test_non_one_pass_value_is_rejected(self):
        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            self.write_minimal_results(run, run_dir, pass_value="looks-good")
            suffix, header, schema = SUMMARY.expected_schema(run.mode)
            with self.assertRaisesRegex(SUMMARY.QualificationError, "PASS=1"):
                SUMMARY.PARSER.load_tsv(
                    run_dir / "system/Results" / f"bfs.{suffix}", header, schema, "DH1:"
                )

    def test_malformed_tsv_row_is_rejected(self):
        run = SUMMARY.PILOT_RUNS[0]
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            self.write_minimal_results(run, run_dir, malformed=True)
            suffix, header, schema = SUMMARY.expected_schema(run.mode)
            with self.assertRaisesRegex(SUMMARY.QualificationError, "malformed TSV row"):
                SUMMARY.PARSER.load_tsv(
                    run_dir / "system/Results" / f"bfs.{suffix}", header, schema, "DH1:"
                )

    def test_paired_output_includes_both_orders_and_correct_aggregate(self):
        def verified(variant, repeat, order, bfs, pfs3):
            run = SUMMARY.make_run(
                f"mock-{variant}-{repeat}-{order}", "compare", "pilot", variant,
                repeat, order,
            )
            return SUMMARY.VerifiedRun(run, bfs, pfs3)

        phases = {"LOOKUP_400_US": 100, "SMALL_CREATE_40_US": 200}
        rows = [
            verified("m5", 1, "bfs-first", phases, {key: value * 3 for key, value in phases.items()}),
            verified("admission", 1, "bfs-first", {key: value * 1.5 for key, value in phases.items()},
                     {key: value * 4.5 for key, value in phases.items()}),
            verified("m5", 2, "pfs3-first", {key: value * 2 for key, value in phases.items()},
                     {key: value * 6 for key, value in phases.items()}),
            verified("admission", 2, "pfs3-first", phases,
                     {key: value * 3 for key, value in phases.items()}),
        ]
        output = io.StringIO()
        with redirect_stdout(output):
            SUMMARY.summarize_paired_admission_m5("pilot", rows)
        rendered = output.getvalue()
        self.assertIn("1/bfs-first", rendered)
        self.assertIn("2/pfs3-first", rendered)
        self.assertIn("Paired admission/M5 timings", rendered)
        self.assertIn("admission_over_M5", rendered)
        self.assertIn("LOOKUP_400\tBFS\t0.5000\t1.0000\t1.5000\t1/2", rendered)
        self.assertIn("LOOKUP_400\tPFS3\t0.5000\t1.0000\t1.5000\t1/2", rendered)


if __name__ == "__main__":
    unittest.main()
