# SPDX-License-Identifier: MPL-2.0
"""Cheap unit tests for the fixed sparse CRC confirmation evidence parser."""

from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch
import unittest

import summarize_confirmation as summary


def schedule_text(entries=None) -> str:
    rows = entries if entries is not None else summary.EXPECTED_SCHEDULE
    lines = ["\t".join(summary.SCHEDULE_HEADER)]
    lines.extend("\t".join(entry.tsv_fields()) for entry in rows)
    return "\n".join(lines) + "\n"


def phase_values(value: int) -> dict[str, str]:
    return {phase: str(value) for phase in summary.EXPECTED_PHASES}


def fake_rows(aa_values=None) -> list[summary.VerifiedConfirmationRun]:
    rows = []
    for entry in summary.EXPECTED_SCHEDULE:
        if entry.variant == "m5":
            bfs, pfs3 = 100, 200
        elif entry.variant == "sparse":
            bfs, pfs3 = 150, 100
        elif aa_values is not None and entry.family in aa_values:
            bfs, pfs3 = aa_values[entry.family][entry.variant]
        elif entry.variant == "m5a":
            bfs, pfs3 = 100, 200
        else:
            bfs, pfs3 = 120, 220
        rows.append(summary.VerifiedConfirmationRun(
            entry,
            SimpleNamespace(bfs=phase_values(bfs), pfs3=phase_values(pfs3)),
        ))
    return rows


def write_top_level_inputs(root: Path) -> None:
    identities = []
    check_lines = []
    for digest, suffix in summary.INPUT_IDENTITIES:
        path = f"/frozen-workspace/{suffix}"
        identities.append(f"{digest}  {path}")
        check_lines.append(f"{path}: OK")
    (root / "input-identities.sha256").write_text(
        "\n".join(identities) + "\n", encoding="ascii"
    )
    (root / "input-check.log").write_text(
        "\n".join(check_lines) + "\n", encoding="ascii"
    )


def write_startup(run_dir: Path, entry: summary.ScheduleEntry) -> None:
    if entry.order == "bfs-first":
        filesystems = (("DH1", "bfs"), ("DH2", "pfs3"))
    else:
        filesystems = (("DH2", "pfs3"), ("DH1", "bfs"))
    durable = entry.mode_family == "durable"
    arguments = " durable" if durable else ""
    suffix = "durable.tsv" if durable else "tsv"
    lines = [
        f"C:fs-compare-bench {drive}:{arguments} >SYS:Results/{filesystem}.{suffix}"
        for drive, filesystem in filesystems
    ]
    path = run_dir / "system/S/Startup-Sequence"
    path.parent.mkdir(parents=True)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


class FixedScheduleTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = TemporaryDirectory()
        self.root = Path(self.tempdir.name)

    def tearDown(self):
        self.tempdir.cleanup()

    def write_rows(self, rows):
        (self.root / "schedule.tsv").write_text(schedule_text(rows), encoding="ascii")

    def test_schedule_is_exact_balanced_40_run_order(self):
        entries = summary.expected_schedule()
        self.assertEqual(len(entries), 40)
        self.assertEqual([entry.sequence for entry in entries], list(range(1, 41)))
        self.assertEqual(sum(entry.family in summary.PRIMARY_FAMILIES for entry in entries), 32)
        self.assertEqual(sum(entry.family in summary.AA_FAMILIES for entry in entries), 8)
        self.assertEqual(
            [(entry.family, entry.variant, entry.repeat, entry.order) for entry in entries[:8]],
            [
                ("normal", "m5", 1, "bfs-first"),
                ("normal", "sparse", 1, "bfs-first"),
                ("durable", "m5", 1, "bfs-first"),
                ("durable", "sparse", 1, "bfs-first"),
                ("durable", "m5", 2, "pfs3-first"),
                ("durable", "sparse", 2, "pfs3-first"),
                ("normal", "m5", 2, "pfs3-first"),
                ("normal", "sparse", 2, "pfs3-first"),
            ],
        )
        self.assertEqual(
            [entry.family for entry in entries[16:20]],
            ["aa-normal", "aa-normal", "aa-durable", "aa-durable"],
        )
        for family in summary.PRIMARY_FAMILIES:
            family_entries = [entry for entry in entries if entry.family == family]
            self.assertEqual(len(family_entries), 16)
            for order in ("bfs-first", "pfs3-first"):
                for first_handler in ("m5", "sparse"):
                    matching_repeats = {
                        entry.repeat for entry in family_entries
                        if entry.order == order and entry.first_handler == first_handler
                    }
                    self.assertEqual(len(matching_repeats), 2)
        self.write_rows(entries)
        self.assertEqual(summary.validate_schedule_file(self.root), entries)

    def test_missing_schedule_row_is_rejected(self):
        self.write_rows(summary.EXPECTED_SCHEDULE[:-1])
        with self.assertRaisesRegex(summary.QualificationError, "missing schedule rows"):
            summary.validate_schedule_file(self.root)

    def test_extra_schedule_row_is_rejected(self):
        rows = list(summary.EXPECTED_SCHEDULE)
        last = rows[-1]
        rows.append(summary.ScheduleEntry(
            sequence=41,
            family=last.family,
            repeat=last.repeat,
            order=last.order,
            variant=last.variant,
            run_name="confirm-unexpected-extra",
            mode_family=last.mode_family,
            parser_variant=last.parser_variant,
            first_handler=last.first_handler,
        ))
        self.write_rows(rows)
        with self.assertRaisesRegex(summary.QualificationError, "unexpected schedule rows"):
            summary.validate_schedule_file(self.root)

    def test_duplicate_schedule_run_is_rejected(self):
        rows = list(summary.EXPECTED_SCHEDULE)
        rows.append(rows[0])
        self.write_rows(rows)
        with self.assertRaisesRegex(summary.QualificationError, "duplicate scheduled run names"):
            summary.validate_schedule_file(self.root)

    def test_wrong_order_field_is_rejected(self):
        rows = list(summary.EXPECTED_SCHEDULE)
        first = rows[0]
        rows[0] = summary.ScheduleEntry(
            sequence=first.sequence,
            family=first.family,
            repeat=first.repeat,
            order="pfs3-first",
            variant=first.variant,
            run_name=first.run_name,
            mode_family=first.mode_family,
            parser_variant=first.parser_variant,
            first_handler=first.first_handler,
        )
        self.write_rows(rows)
        with self.assertRaisesRegex(summary.QualificationError, "schedule fields differ"):
            summary.validate_schedule_file(self.root)

    def test_reordered_rows_are_rejected(self):
        rows = list(summary.EXPECTED_SCHEDULE)
        rows[0], rows[1] = rows[1], rows[0]
        self.write_rows(rows)
        with self.assertRaisesRegex(summary.QualificationError, "row order differs"):
            summary.validate_schedule_file(self.root)

    def test_missing_and_unexpected_run_directories_are_rejected(self):
        for entry in summary.EXPECTED_SCHEDULE:
            if entry.sequence != 1:
                (self.root / entry.run_name).mkdir()
        (self.root / "confirm-unexpected").mkdir()
        with self.assertRaisesRegex(summary.QualificationError, "missing expected run directories") as error:
            summary.check_run_directories(self.root, summary.EXPECTED_SCHEDULE)
        self.assertIn("unexpected confirm* directories", str(error.exception))


class EvidenceValidationTests(unittest.TestCase):
    def test_mocked_inventory_uses_all_40_runs_and_checks_top_level_inputs(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "schedule.tsv").write_text(schedule_text(), encoding="ascii")
            write_top_level_inputs(root)
            for entry in summary.EXPECTED_SCHEDULE:
                run_dir = root / entry.run_name
                run_dir.mkdir()
                (run_dir / "build.log").write_text("Build completed\n", encoding="utf-8")
                (run_dir / "run.log").write_text("PASS\n", encoding="utf-8")
                write_startup(run_dir, entry)

            parser_runs = []

            def fake_verify(parser_run, evidence_root):
                self.assertEqual(evidence_root, root)
                parser_runs.append(parser_run)
                return SimpleNamespace(
                    run=parser_run,
                    bfs=phase_values(100),
                    pfs3=phase_values(200),
                )

            with patch.object(summary.PARSER, "verify_run", side_effect=fake_verify):
                verified = summary.verify_inventory(root)
            self.assertEqual(len(verified), 40)
            self.assertEqual(len(parser_runs), 40)
            aa_parser_runs = [run for run in parser_runs if run.name.startswith("confirm-aa-")]
            self.assertEqual(len(aa_parser_runs), 8)
            self.assertTrue(all(run.variant == "m5" for run in aa_parser_runs))

    def test_startup_sequence_must_match_declared_filesystem_order(self):
        entry = summary.EXPECTED_SCHEDULE[0]
        with TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            startup = run_dir / "system/S/Startup-Sequence"
            startup.parent.mkdir(parents=True)
            startup.write_text(
                "C:fs-compare-bench DH1:  >SYS:Results/bfs.tsv\n"
                "C:fs-compare-bench DH2:  >SYS:Results/pfs3.tsv\n",
                encoding="ascii",
            )
            summary.check_startup_sequence(entry, run_dir)
            startup.write_text(
                "C:fs-compare-bench DH2:  >SYS:Results/pfs3.tsv\n"
                "C:fs-compare-bench DH1:  >SYS:Results/bfs.tsv\n",
                encoding="ascii",
            )
            with self.assertRaisesRegex(summary.QualificationError, "order disagrees"):
                summary.check_startup_sequence(entry, run_dir)

    def test_durable_startup_sequence_requires_durable_outputs(self):
        entry = next(
            entry for entry in summary.EXPECTED_SCHEDULE
            if entry.family == "durable" and entry.order == "bfs-first"
        )
        with TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            startup = run_dir / "system/S/Startup-Sequence"
            startup.parent.mkdir(parents=True)
            startup.write_text(
                "C:fs-compare-bench DH1: durable >SYS:Results/bfs.durable.tsv\n"
                "C:fs-compare-bench DH2: durable >SYS:Results/pfs3.durable.tsv\n",
                encoding="ascii",
            )
            summary.check_startup_sequence(entry, run_dir)

    def test_aa_variants_map_to_m5_for_identity_check_but_keep_schedule_labels(self):
        entry = next(
            entry for entry in summary.EXPECTED_SCHEDULE if entry.variant == "m5a"
        )
        parser_run = summary.parser_run_for(entry)
        self.assertEqual(entry.variant, "m5a")
        self.assertEqual(parser_run.variant, "m5")
        self.assertEqual(parser_run.family, entry.mode_family)

    def test_old_runtime_identity_check_rejects_wrong_digest(self):
        entry = next(
            entry for entry in summary.EXPECTED_SCHEDULE
            if entry.family == "normal" and entry.variant == "m5"
        )
        parser_run = summary.parser_run_for(entry)
        expected = summary.PARSER.expected_runtime_inputs(parser_run)
        with TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            lines = [f"{digest}  {path}" for path, digest in expected.items()]
            (run_dir / "runtime-inputs.sha256").write_text(
                "\n".join(lines) + "\n", encoding="ascii"
            )
            summary.PARSER.check_runtime_identities(parser_run, run_dir)
            lines[0] = f"{'0' * 64}  {lines[0].split('  ', 1)[1]}"
            (run_dir / "runtime-inputs.sha256").write_text(
                "\n".join(lines) + "\n", encoding="ascii"
            )
            with self.assertRaisesRegex(summary.QualificationError, "identity mismatch"):
                summary.PARSER.check_runtime_identities(parser_run, run_dir)

    def test_failure_marker_in_run_log_is_rejected(self):
        with TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            (run_dir / "build.log").write_text("Build finished\n", encoding="utf-8")
            (run_dir / "run.log").write_text("FAIL: verifier reported a problem\n", encoding="utf-8")
            with self.assertRaisesRegex(summary.QualificationError, "failure marker"):
                summary.check_run_logs(run_dir, "confirm-normal-m5-1-bfs-first")

    def test_phase_inventory_requires_all_23_and_positive_timings(self):
        values = phase_values(10)
        values.pop(summary.EXPECTED_PHASES[-1])
        with self.assertRaisesRegex(summary.QualificationError, "all 23 fixed phases"):
            summary.phase_metrics(values, "synthetic-run", "BFS")
        values = phase_values(10)
        values[summary.EXPECTED_PHASES[0]] = "0"
        with self.assertRaisesRegex(summary.QualificationError, "must be positive"):
            summary.phase_metrics(values, "synthetic-run", "BFS")


class RatioSemanticsTests(unittest.TestCase):
    def test_sparse_candidate_ratios_preserve_bfs_and_pfs3_calibrator(self):
        observed = summary.comparison_observation(
            phase="APPEND_4K_1M_US",
            family="normal",
            repeat=1,
            order="bfs-first",
            first_handler="m5",
            baseline_label="m5",
            candidate_label="sparse",
            baseline_bfs=100,
            candidate_bfs=150,
            baseline_pfs3=200,
            candidate_pfs3=100,
        )
        self.assertEqual(observed["ratios"]["bfs_candidate_over_baseline"], 1.5)
        self.assertEqual(observed["ratios"]["pfs3_candidate_over_baseline"], 0.5)
        self.assertEqual(observed["ratios"]["normalized_bfs_over_pfs3"], 3.0)
        self.assertEqual(observed["baseline"]["variant"], "m5")
        self.assertEqual(observed["candidate"]["variant"], "sparse")

    def test_m5b_over_m5a_direction_and_all_four_control_pairs(self):
        observed = summary.comparison_observation(
            phase="APPEND_4K_1M_US",
            family="aa-normal",
            repeat=1,
            order="bfs-first",
            first_handler="m5a",
            baseline_label="m5a",
            candidate_label="m5b",
            baseline_bfs=100,
            candidate_bfs=120,
            baseline_pfs3=200,
            candidate_pfs3=220,
        )
        self.assertAlmostEqual(observed["ratios"]["bfs_candidate_over_baseline"], 1.2)
        self.assertAlmostEqual(observed["ratios"]["pfs3_candidate_over_baseline"], 1.1)
        self.assertEqual(observed["baseline"]["variant"], "m5a")
        self.assertEqual(observed["candidate"]["variant"], "m5b")

        controls = summary.aa_control_summaries(fake_rows())
        self.assertEqual(len(controls), 23)
        self.assertTrue(all(control["control_pair_count"] == 4 for control in controls))
        self.assertTrue(all(
            [stratum["pair_count"] for stratum in control["strata_by_filesystem_order_mixed_mode"]]
            == [2, 2]
            for control in controls
        ))

    def test_aa_mode_aggregates_do_not_pool_different_normal_and_durable_ratios(self):
        rows = fake_rows({
            "aa-normal": {"m5a": (100, 100), "m5b": (110, 120)},
            "aa-durable": {"m5a": (100, 100), "m5b": (300, 50)},
        })
        phase = next(
            item for item in summary.aa_control_summaries(rows)
            if item["phase"] == summary.EXPECTED_PHASES[0]
        )
        by_mode = {item["family"]: item for item in phase["aggregate_by_mode"]}
        self.assertEqual(by_mode["aa-normal"]["pair_count"], 2)
        self.assertEqual(by_mode["aa-durable"]["pair_count"], 2)
        self.assertAlmostEqual(
            by_mode["aa-normal"]["ratios"]["bfs_candidate_over_baseline"]["median"],
            1.1,
        )
        self.assertAlmostEqual(
            by_mode["aa-durable"]["ratios"]["bfs_candidate_over_baseline"]["median"],
            3.0,
        )
        self.assertAlmostEqual(
            by_mode["aa-normal"]["ratios"]["pfs3_candidate_over_baseline"]["median"],
            1.2,
        )
        self.assertAlmostEqual(
            by_mode["aa-durable"]["ratios"]["pfs3_candidate_over_baseline"]["median"],
            0.5,
        )
        self.assertEqual(
            phase["mixed_mode_aggregate_all_four_controls"]["bfs_candidate_over_baseline"]["pair_count"],
            4,
        )
        self.assertIn("within-mode interpretation", phase["mixed_mode_aggregate_note"])

    def test_sparse_pair_summary_keeps_all_runs_and_two_pairs_per_stratum(self):
        summaries = summary.primary_pair_summaries(fake_rows())
        self.assertEqual(len(summaries), 46)
        first = next(
            item for item in summaries
            if item["family"] == "normal" and item["phase"] == summary.EXPECTED_PHASES[0]
        )
        self.assertEqual(len(first["raw_pairs"]), 8)
        self.assertEqual(
            [stratum["pair_count"] for stratum in first["strata_by_filesystem_order_and_first_handler"]],
            [2, 2, 2, 2],
        )
        self.assertEqual(
            first["raw_pairs"][0]["ratios"]["bfs_candidate_over_baseline"], 1.5
        )
        self.assertEqual(
            first["raw_pairs"][0]["ratios"]["pfs3_candidate_over_baseline"], 0.5
        )

    def test_bfs_pfs3_individual_ratios_and_over_5_counts_include_all_runs(self):
        result = summary.bfs_pfs3_calibration(fake_rows())
        self.assertEqual(len(result), 23)
        self.assertTrue(all(item["run_count"] == 40 for item in result))
        self.assertTrue(all(len(item["individual_ratios"]) == 40 for item in result))
        self.assertTrue(all(item["over_5_count_all_40_runs"] == 0 for item in result))


if __name__ == "__main__":
    unittest.main()
