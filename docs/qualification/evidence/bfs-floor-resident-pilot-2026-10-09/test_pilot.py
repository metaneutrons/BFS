# SPDX-License-Identifier: MPL-2.0
"""Synthetic acceptance and corruption probes for the resident-root pilot."""

import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[4]
BUNDLE = Path(__file__).resolve().parent
SUMMARY_PATH = BUNDLE / "summarize_pilot.py"
SUMMARY_SPEC = importlib.util.spec_from_file_location("floor_pilot_summary", SUMMARY_PATH)
summary = importlib.util.module_from_spec(SUMMARY_SPEC)
sys.modules[SUMMARY_SPEC.name] = summary
SUMMARY_SPEC.loader.exec_module(summary)

SPLIT_FIXTURE_PATH = ROOT / "tests/quality/test_split_bench_verifier.py"
SPLIT_FIXTURE_SPEC = importlib.util.spec_from_file_location(
    "floor_pilot_split_fixture_generator", SPLIT_FIXTURE_PATH,
)
split_fixtures = importlib.util.module_from_spec(SPLIT_FIXTURE_SPEC)
sys.modules[SPLIT_FIXTURE_SPEC.name] = split_fixtures
SPLIT_FIXTURE_SPEC.loader.exec_module(split_fixtures)

NORMAL_RESULT_TEMPLATE = (
    ROOT / "docs/qualification/evidence/bfs-leaf-range-performance-2026-10-08"
    / "leaf-range-normal-20261008-m3-5-bfs-first/system/Results"
)
DURABLE_RESULT_TEMPLATE = (
    ROOT / "docs/qualification/evidence/bfs-leaf-range-performance-2026-10-08"
    / "leaf-range-durable-20261008-m3-5-bfs-first/system/Results"
)


def synthetic_asset_receipt(guest_digest):
    assets = {
        "C/fs-compare-bench": guest_digest,
        "L/pfs3aio": summary.PFS3_SHA256,
    }
    for index in range(64):
        asset_path = f"C/SyntheticTool{index:02d}"
        assets[asset_path] = hashlib.sha256(asset_path.encode("ascii")).hexdigest()
    library_path = "Libs/synthetic.library"
    assets[library_path] = hashlib.sha256(library_path.encode("ascii")).hexdigest()
    assert len(assets) == summary.EXPECTED_ASSET_COUNT
    return "".join(
        f"{digest}  {path}\n" for path, digest in sorted(assets.items())
    )


class FloorPilotSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.results_root = Path(self.temp.name) / "results"
        self.results_root.mkdir()
        self._write_fixture()

    def _write_fixture(self):
        schedule_lines = ["\t".join(summary.SCHEDULE_HEADER)]
        for entry in summary.EXPECTED_SCHEDULE:
            schedule_lines.append("\t".join(entry.fields()))
            run_dir = self.results_root / entry.run_name
            (run_dir / "system/S").mkdir(parents=True)
            results = run_dir / "system/Results"
            results.mkdir()

            runtime = "".join(
                f"{digest}  {path}\n"
                for digest, path in summary.expected_runtime_receipt(entry)
            )
            (run_dir / "runtime-inputs.sha256").write_text(runtime, encoding="ascii")
            (run_dir / "runtime-inputs.post.sha256").write_text(runtime, encoding="ascii")
            (run_dir / "installed-assets.sha256").write_text(
                synthetic_asset_receipt(entry.guest_sha256), encoding="ascii",
            )
            (run_dir / "bfs-rdb.json").write_text(json.dumps({
                "rdb": {"partitions": [{"dos_env": {"num_buffer": 30}}]},
            }), encoding="utf-8")
            (run_dir / "bench.fs-uae").write_text(
                "[fs-uae]\n"
                "amiga_model = A1200\n"
                "chip_memory = 2048\n"
                "fast_memory = 8192\n"
                "cpu = 68040\n"
                "uae_cpu_speed = max\n"
                "kickstart_file = /snapshot/A1200.rom\n"
                "hard_drive_0 = /snapshot/system\n"
                "hard_drive_1 = /snapshot/bench-bfs.hdf\n"
                "hard_drive_2 = /snapshot/bench-pfs3.hdf\n",
                encoding="ascii",
            )
            (run_dir / "system/S/Startup-Sequence").write_text(
                self._startup_sequence(entry), encoding="ascii",
            )

            if entry.family == "diag":
                self._write_diagnostic_results(entry, results)
            else:
                self._write_production_results(entry, results)

        (self.results_root / "schedule.tsv").write_text(
            "\n".join(schedule_lines) + "\n", encoding="ascii",
        )

    @staticmethod
    def _startup_sequence(entry):
        if entry.order == "bfs-first":
            order = (("DH1:", "bfs"), ("DH2:", "pfs3"))
        else:
            order = (("DH2:", "pfs3"), ("DH1:", "bfs"))
        if entry.family == "diag":
            mode_arg = "split-write-durable"
            outputs = {"bfs": "bfs.split.tsv", "pfs3": "pfs3.split.tsv"}
        elif entry.mode == "durable-compare":
            mode_arg = "durable"
            outputs = {"bfs": "bfs.durable.tsv", "pfs3": "pfs3.durable.tsv"}
        else:
            mode_arg = ""
            outputs = {"bfs": "bfs.tsv", "pfs3": "pfs3.tsv"}
        commands = [
            f"C:fs-compare-bench {drive} {mode_arg} >SYS:Results/{outputs[filesystem]}"
            for drive, filesystem in order
        ]
        lines = ["FailAt 21"]
        if entry.family == "diag":
            lines.append("Stack 32768")
        return "\n".join([*lines, *commands]) + "\n"

    @staticmethod
    def _set_tsv_value(path, key, value):
        rows = path.read_text(encoding="ascii").splitlines()
        for index, row in enumerate(rows):
            name, old_value = row.split("\t", 1)
            if name == key:
                rows[index] = f"{name}\t{value}"
                path.write_text("\n".join(rows) + "\n", encoding="ascii")
                return
        raise AssertionError(f"fixture lacks {key}: {path}")

    def _write_production_results(self, entry, results_dir):
        if entry.mode == "compare":
            template = NORMAL_RESULT_TEMPLATE
            suffix = "tsv"
        else:
            template = DURABLE_RESULT_TEMPLATE
            suffix = "durable.tsv"
        for name in ("complete.txt", "info-after-format.txt"):
            shutil.copyfile(template / name, results_dir / name)
        for filesystem in ("bfs", "pfs3"):
            output_name = f"{filesystem}.{suffix}"
            shutil.copyfile(template / output_name, results_dir / output_name)

        if entry.family in {"normal", "durable"}:
            lookup = 100 if entry.variant == "base" else 50
            pfs_lookup = 200 if entry.variant == "base" else 100
        else:
            lookup = 80 if entry.variant == "basea" else 100
            pfs_lookup = 120 if entry.variant == "basea" else 150
        self._set_tsv_value(results_dir / f"bfs.{suffix}", "LOOKUP_400_US", lookup)
        self._set_tsv_value(results_dir / f"pfs3.{suffix}", "LOOKUP_400_US", pfs_lookup)

    @staticmethod
    def _diagnostic_rows(filesystem, mode):
        # Reuse the protocol test's valid baseline rows, then add the maintained
        # write-probe schema rows at their exact schema positions.
        baseline = split_fixtures.SplitBenchVerifierTests._valid_rows(
            None, filesystem, "split-durable-compare",
        )
        baseline_values = {
            key: value for row in baseline
            for key, value in [row.split("\t", 1)]
        }
        rows = []
        for key, fixed in summary.SPLIT_VERIFIER.build_schema(filesystem, mode):
            if fixed is not None:
                value = fixed
            elif key in baseline_values:
                value = baseline_values[key]
            elif key == "CLOCK_HZ":
                value = "1000000" if filesystem == "bfs" else "0"
            else:
                value = "0"
            rows.append(f"{key}\t{value}")
        return rows

    def _write_diagnostic_results(self, entry, results_dir):
        (results_dir / "complete.txt").write_bytes(summary.SPLIT_VERIFIER.MARKER)
        (results_dir / "info-after-format.txt").write_bytes(
            b"DH1: Workbench Read/Write BFSTest\nDH2: Workbench Read/Write PFSTest\n"
        )
        for filesystem in ("bfs", "pfs3"):
            rows = self._diagnostic_rows(filesystem, entry.mode)
            if filesystem == "bfs":
                timing = 2 if entry.variant == "base" else 1
                self._replace_row_value(rows, "LOOKUP_400_US", timing)
                self._replace_row_value(rows, "LOOKUP_400_WORK_US", timing)
            (results_dir / f"{filesystem}.split.tsv").write_text(
                "\n".join(rows) + "\n", encoding="ascii",
            )

    @staticmethod
    def _replace_row_value(rows, key, value):
        for index, row in enumerate(rows):
            name, _ = row.split("\t", 1)
            if name == key:
                rows[index] = f"{name}\t{value}"
                return
        raise AssertionError(f"fixture lacks {key}")

    def _entry_dir(self, family, repeat=1, variant=None):
        entry = next(
            entry for entry in summary.EXPECTED_SCHEDULE
            if entry.family == family and entry.repeat == repeat and
            (variant is None or entry.variant == variant)
        )
        return self.results_root / entry.run_name

    def _reject(self):
        with self.assertRaises(summary.QualificationError):
            summary.summarize(self.results_root)

    def test_valid_fixed_inventory_preserves_phases_raw_values_and_separate_pairs(self):
        result = summary.summarize(self.results_root)
        self.assertEqual(result["verified_schedule_count"], 14)
        self.assertEqual(result["verified_phase_count"], 23)
        self.assertEqual(result["verified_installed_asset_count_per_group"], 67)
        self.assertTrue(result["installed_assets_identical_within_group"])
        self.assertEqual(
            result["asset_group_guest_sha256"],
            {
                "production": summary.PRODUCTION_GUEST_SHA256,
                "diagnostic": summary.DIAGNOSTIC_GUEST_SHA256,
            },
        )
        self.assertEqual(len(result["runs"]), 14)

        diag = result["runs"][self._entry_dir("diag").name]["filesystems"]
        self.assertEqual(len(diag["bfs"]["phases"]), 23)
        self.assertEqual(len(diag["pfs3"]["phases"]), 23)
        raw = diag["bfs"]["phases"]["LOOKUP_400"]["raw"]
        self.assertEqual(raw["US"], 2)
        self.assertIn("WORK_BIO_READS", raw)
        self.assertIn("GUEST_READ_CALLS", diag["pfs3"]["phases"]["LOOKUP_400"]["raw"])

        production = result["runs"][self._entry_dir("normal", 1, "base").name]
        self.assertEqual(len(production["filesystems"]["bfs"]["phases"]), 23)
        production_bfs = production["filesystems"]["bfs"]
        self.assertEqual(production_bfs["global"]["PASS"], "1")
        self.assertEqual(production_bfs["global"]["DRIVE"], "DH1:")
        template_values = summary.BENCH_PARSER.load_tsv(
            NORMAL_RESULT_TEMPLATE / "bfs.tsv", "FS_COMPARE_BENCH", 4, "DH1:",
        )
        listing_sum_key = "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL_US"
        listing_sum_phase = listing_sum_key[:-3]
        self.assertEqual(
            production_bfs["phases"][listing_sum_phase]["raw"]["US"],
            int(template_values[listing_sum_key]),
        )
        comparisons = result["comparisons"]
        self.assertEqual(len(comparisons["diagnostic_candidate_over_base"]), 1)
        self.assertEqual(len(comparisons["production_candidate_over_base"]), 4)
        self.assertEqual(len(comparisons["same_handler_baseb_over_basea_controls"]), 2)
        pair = next(
            row for row in comparisons["production_candidate_over_base"]
            if row["family"] == "normal" and row["repeat"] == 1
        )
        self.assertEqual(pair["bfs_ratios"]["LOOKUP_400"]["US"]["ratio"], 0.5)
        control = next(
            row for row in comparisons["same_handler_baseb_over_basea_controls"]
            if row["family"] == "aa-normal"
        )
        self.assertEqual(
            control["bfs_ratios"]["LOOKUP_400"]["US"]["ratio"], 1.25,
        )
        diag_pair = comparisons["diagnostic_candidate_over_base"][0]
        self.assertEqual(diag_pair["bfs_ratios"]["LOOKUP_400"]["US"]["ratio"], 0.5)
        self.assertNotIn("medians", comparisons)
        self.assertIn("No pooling", result["scope"])

    def test_rejects_missing_or_extra_run_directories(self):
        schedule = self.results_root / "schedule.tsv"
        original_schedule = schedule.read_text(encoding="ascii")
        schedule.write_text(
            original_schedule.replace(
                "floor-normal-base-1-bfs-first",
                "floor-normal-base-1-pfs3-first",
                1,
            ),
            encoding="ascii",
        )
        self._reject()
        schedule.write_text(original_schedule, encoding="ascii")

        unexpected = self.results_root / "floor-unexpected"
        unexpected.mkdir()
        self._reject()
        unexpected.rmdir()

        missing = self._entry_dir("aa-durable", 1, "baseb")
        shutil.rmtree(missing)
        self._reject()

    def test_rejects_runtime_pre_and_post_identity_mismatches(self):
        run_dir = self._entry_dir("normal", 1, "base")
        before = run_dir / "runtime-inputs.sha256"
        original = before.read_text(encoding="ascii")
        before.write_text(original.replace(summary.PRODUCTION_BASE_HANDLER_SHA256, "0" * 64),
                          encoding="ascii")
        self._reject()
        before.write_text(original, encoding="ascii")

        after = run_dir / "runtime-inputs.post.sha256"
        post_original = after.read_text(encoding="ascii")
        after.write_text(
            post_original.replace(summary.PRODUCTION_BASE_HANDLER_SHA256, "0" * 64),
            encoding="ascii",
        )
        self._reject()

    def test_rejects_guest_asset_pin_and_noncommon_asset_mapping(self):
        diag_dir = self._entry_dir("diag", 1, "base")
        receipt_path = diag_dir / "installed-assets.sha256"
        original = receipt_path.read_text(encoding="ascii")
        receipt_path.write_text(
            original.replace(summary.DIAGNOSTIC_GUEST_SHA256, "0" * 64),
            encoding="ascii",
        )
        self._reject()
        receipt_path.write_text(original, encoding="ascii")

        second = self._entry_dir("normal", 1, "candidate") / "installed-assets.sha256"
        rows = second.read_text(encoding="ascii").splitlines()
        rows[0] = "0" * 64 + rows[0][64:]
        second.write_text("\n".join(rows) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_configuration_and_startup_mode_order_stack_mismatches(self):
        run_dir = self._entry_dir("diag", 1, "base")
        config = run_dir / "bench.fs-uae"
        original_config = config.read_text(encoding="ascii")
        swapped_config = original_config.replace(
            "hard_drive_1 = /snapshot/bench-bfs.hdf",
            "hard_drive_1 = /snapshot/TEMP.hdf",
        ).replace(
            "hard_drive_2 = /snapshot/bench-pfs3.hdf",
            "hard_drive_2 = /snapshot/bench-bfs.hdf",
        ).replace(
            "hard_drive_1 = /snapshot/TEMP.hdf",
            "hard_drive_1 = /snapshot/bench-pfs3.hdf",
        )
        config.write_text(swapped_config, encoding="ascii")
        self._reject()
        config.write_text(original_config, encoding="ascii")

        startup = run_dir / "system/S/Startup-Sequence"
        original = startup.read_text(encoding="ascii")
        startup.write_text(original.replace("split-write-durable", "split-durable"),
                           encoding="ascii")
        self._reject()
        swapped = original.replace(
            "C:fs-compare-bench DH1: split-write-durable",
            "C:fs-compare-bench TEMP: split-write-durable",
        ).replace(
            "C:fs-compare-bench DH2: split-write-durable",
            "C:fs-compare-bench DH1: split-write-durable",
        ).replace(
            "C:fs-compare-bench TEMP: split-write-durable",
            "C:fs-compare-bench DH2: split-write-durable",
        )
        startup.write_text(swapped, encoding="ascii")
        self._reject()
        startup.write_text(original.replace("Stack 32768", "Stack 4096"), encoding="ascii")
        self._reject()

        production_run = self._entry_dir("normal", 1, "base")
        production_startup = production_run / "system/S/Startup-Sequence"
        production_original = production_startup.read_text(encoding="ascii")
        production_startup.write_text(
            production_original.replace("FailAt 21\n", "FailAt 21\nStack 32768\n", 1),
            encoding="ascii",
        )
        self._reject()

    def test_rejects_malformed_diagnostic_protocol_and_invalid_rdb(self):
        run_dir = self._entry_dir("diag", 1, "base")
        results = run_dir / "system/Results"
        bfs = results / "bfs.split.tsv"
        original = bfs.read_text(encoding="ascii")
        bfs.write_text("\n".join(original.splitlines()[:-1]) + "\n", encoding="ascii")
        self._reject()
        bfs.write_text(original, encoding="ascii")

        rdb = run_dir / "bfs-rdb.json"
        rdb.write_text(json.dumps({
            "rdb": {"partitions": [
                {"dos_env": {"num_buffer": 30}},
                {"dos_env": {"num_buffer": 30}},
            ]},
        }), encoding="utf-8")
        self._reject()
        rdb.write_text(json.dumps({
            "rdb": {"partitions": [{"dos_env": {"num_buffer": 64}}]},
        }), encoding="utf-8")
        self._reject()


if __name__ == "__main__":
    unittest.main()
