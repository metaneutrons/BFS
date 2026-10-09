# SPDX-License-Identifier: MPL-2.0
"""Focused adapter tests for the fixed FreeTree goal-root pilot."""

from contextlib import redirect_stdout
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[4]
BUNDLE = Path(__file__).resolve().parent
FLOOR_BUNDLE = ROOT / "docs/qualification/evidence/bfs-floor-resident-pilot-2026-10-09"


def _load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load test module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


# Keep an independently loaded copy of the sealed floor module to prove that
# the goal-root adapter does not alter its pins, schedule, or protocol globals.
floor_summary = _load_module(
    "goal_root_floor_reference",
    FLOOR_BUNDLE / "summarize_pilot.py",
)
floor_protocol_inventory = copy.deepcopy(floor_summary.SPLIT_VERIFIER._inventories())
floor_registered_profile_module = sys.modules.get("bfs_floor_profile_helpers")

summary = _load_module("goal_root_pilot_summary", BUNDLE / "summarize_pilot.py")
floor_registry_preserved_by_adapter_import = (
    sys.modules.get("bfs_floor_profile_helpers") is floor_registered_profile_module
)
goal_protocol_inventory_at_import = copy.deepcopy(summary.SPLIT_VERIFIER._inventories())

FLOOR_TESTS_PATH = FLOOR_BUNDLE / "test_pilot.py"
floor_fixtures = _load_module("goal_root_floor_fixture_helpers", FLOOR_TESTS_PATH)
floor_fixtures.summary = summary


class GoalRootPilotSummaryTests(unittest.TestCase):
    def setUp(self):
        self.fixture_case = floor_fixtures.FloorPilotSummaryTests(
            "test_valid_fixed_inventory_preserves_phases_raw_values_and_separate_pairs"
        )
        self.fixture_case.setUp()
        self.addCleanup(self.fixture_case.doCleanups)
        self.results_root = self.fixture_case.results_root

    def _entry(self, family, repeat=1, variant=None):
        return next(
            entry for entry in summary.EXPECTED_SCHEDULE
            if entry.family == family and entry.repeat == repeat
            and (variant is None or entry.variant == variant)
        )

    def _reject(self):
        with self.assertRaises(summary.QualificationError):
            summary.summarize(self.results_root)

    def test_candidate_pins_schedule_and_floor_module_are_separate(self):
        self.assertEqual(
            summary.PRODUCTION_CANDIDATE_HANDLER_SHA256,
            "d0589bb99ee8d897449a1480dadfcda5a8d4956ccefd4575a794778b3b45cec7",
        )
        self.assertEqual(
            summary.DIAGNOSTIC_CANDIDATE_HANDLER_SHA256,
            "656e17c292320b0a28deb7325d1ade8a4ff0feea806a815d2dc580e77dd8c1fe",
        )
        self.assertEqual(summary.PRODUCTION_BASE_HANDLER_SHA256,
                         floor_summary.PRODUCTION_BASE_HANDLER_SHA256)
        self.assertEqual(summary.DIAGNOSTIC_BASE_HANDLER_SHA256,
                         floor_summary.DIAGNOSTIC_BASE_HANDLER_SHA256)
        self.assertEqual(summary.PRODUCTION_GUEST_SHA256, floor_summary.PRODUCTION_GUEST_SHA256)
        self.assertEqual(summary.DIAGNOSTIC_GUEST_SHA256, floor_summary.DIAGNOSTIC_GUEST_SHA256)
        self.assertEqual(summary.PFS3_SHA256, floor_summary.PFS3_SHA256)
        self.assertEqual(summary.BUNDLE, BUNDLE)

        goal_entries = summary.EXPECTED_SCHEDULE
        floor_entries = floor_summary.EXPECTED_SCHEDULE
        self.assertEqual(len(goal_entries), 14)
        self.assertEqual(len({entry.run_name for entry in goal_entries}), 14)
        self.assertEqual(
            [entry.fields()[:5] for entry in goal_entries],
            [entry.fields()[:5] for entry in floor_entries],
        )
        self.assertEqual(
            [entry.run_name for entry in goal_entries],
            [entry.run_name.replace("floor-", "goal-root-", 1) for entry in floor_entries],
        )
        self.assertTrue(all(entry.run_name.startswith("goal-root-") for entry in goal_entries))
        self.assertTrue(all(entry.run_name.startswith("floor-") for entry in floor_entries))

    def test_protocol_metadata_is_unchanged_and_loaded_independently(self):
        self.assertTrue(floor_registry_preserved_by_adapter_import)
        self.assertIsNot(summary.SPLIT_VERIFIER, floor_summary.SPLIT_VERIFIER)
        self.assertIsNot(summary.PROFILE, floor_summary.PROFILE)
        self.assertEqual(floor_summary.SPLIT_VERIFIER._inventories(), floor_protocol_inventory)
        self.assertEqual(summary.SPLIT_VERIFIER._inventories(), goal_protocol_inventory_at_import)
        self.assertEqual(summary.SPLIT_VERIFIER._inventories(), floor_protocol_inventory)
        self.assertEqual(len(summary._phase_names()), 23)

    def test_diagnostic_stack_override_remains_diagnostic_only(self):
        diagnostic = self._entry("diag", 1, "base")
        diagnostic_run = self.results_root / diagnostic.run_name
        diagnostic_startup = diagnostic_run / "system/S/Startup-Sequence"
        self.assertIn("Stack 32768\n", diagnostic_startup.read_text(encoding="ascii"))
        summary.check_startup_sequence(diagnostic, diagnostic_run)

        production = self._entry("normal", 1, "base")
        production_run = self.results_root / production.run_name
        production_startup = production_run / "system/S/Startup-Sequence"
        self.assertFalse(any(
            line.strip().lower().startswith("stack")
            for line in production_startup.read_text(encoding="ascii").splitlines()
        ))
        summary.check_startup_sequence(production, production_run)
        production_startup.write_text(
            production_startup.read_text(encoding="ascii").replace(
                "FailAt 21\n", "FailAt 21\nStack 32768\n", 1,
            ),
            encoding="ascii",
        )
        with self.assertRaises(summary.QualificationError):
            summary.check_startup_sequence(production, production_run)

    def test_rejects_bad_runtime_hash_missing_post_receipt_and_changed_schedule(self):
        schedule = self.results_root / "schedule.tsv"
        original_schedule = schedule.read_text(encoding="ascii")
        lines = original_schedule.splitlines()
        fields = lines[1].split("\t")
        fields[-1] += "-unexpected"
        lines[1] = "\t".join(fields)
        schedule.write_text("\n".join(lines) + "\n", encoding="ascii")
        self._reject()
        schedule.write_text(original_schedule, encoding="ascii")

        entry = self._entry("diag", 1, "candidate")
        run_dir = self.results_root / entry.run_name
        runtime_receipt = run_dir / "runtime-inputs.sha256"
        original_receipt = runtime_receipt.read_text(encoding="ascii")
        runtime_receipt.write_text(
            original_receipt.replace(entry.handler_sha256, "0" * 64, 1),
            encoding="ascii",
        )
        self._reject()
        runtime_receipt.write_text(original_receipt, encoding="ascii")

        (run_dir / "runtime-inputs.post.sha256").unlink()
        self._reject()

    def test_summary_retains_all_runs_outputs_phases_and_raw_fields(self):
        result = summary.summarize(self.results_root)
        self.assertEqual(result["verified_schedule_count"], 14)
        self.assertEqual(result["verified_phase_count"], 23)
        self.assertEqual(len(result["runs"]), 14)
        self.assertEqual(
            sum(len(run["filesystems"]) for run in result["runs"].values()),
            28,
        )
        for run in result["runs"].values():
            self.assertEqual(set(run["filesystems"]), {"bfs", "pfs3"})
            for filesystem in run["filesystems"].values():
                self.assertEqual(len(filesystem["phases"]), 23)
                self.assertTrue(all(
                    "US" in phase["raw"] for phase in filesystem["phases"].values()
                ))

        self.assertIn("allocator-only FreeTree goal-root pilot", result["scope"])
        self.assertIn("earlier rejected resident-root floor refactor", result["scope"])
        self.assertIn("28 outputs total", result["scope"])
        self.assertIn("without filtering", result["scope"])

        production = self._entry("normal", 1, "base")
        production_bfs = result["runs"][production.run_name]["filesystems"]["bfs"]
        source_values = summary.BENCH_PARSER.load_tsv(
            self.results_root / production.run_name / "system/Results/bfs.tsv",
            "FS_COMPARE_BENCH", 4, "DH1:",
        )
        phase_metrics = summary.BENCH_PARSER.phase_metrics(source_values)
        self.assertEqual(
            production_bfs["global"],
            {key: value for key, value in source_values.items() if key not in phase_metrics},
        )

        diagnostic = self._entry("diag", 1, "candidate")
        diagnostic_bfs = result["runs"][diagnostic.run_name]["filesystems"]["bfs"]
        raw_lookup = diagnostic_bfs["phases"]["LOOKUP_400"]["raw"]
        self.assertIn("WORK_BIO_READS", raw_lookup)
        self.assertEqual(raw_lookup["US"], 1)
        diagnostic_pfs3 = result["runs"][diagnostic.run_name]["filesystems"]["pfs3"]
        self.assertIn(
            "GUEST_READ_CALLS",
            diagnostic_pfs3["phases"]["LOOKUP_400"]["raw"],
        )

    def test_cli_defaults_to_bundle_results_and_accepts_explicit_directory(self):
        explicit = self.results_root / "alternate"
        for argv, expected in (([], BUNDLE / "results"), ([str(explicit)], explicit)):
            output = io.StringIO()
            with patch.object(summary, "summarize", return_value={"ok": True}) as summarize:
                with redirect_stdout(output):
                    self.assertEqual(summary.main(argv), 0)
            self.assertEqual(summarize.call_args.args[0], expected)
            self.assertEqual(json.loads(output.getvalue()), {"ok": True})


if __name__ == "__main__":
    unittest.main()
