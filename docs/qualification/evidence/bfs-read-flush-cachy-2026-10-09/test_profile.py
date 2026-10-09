# SPDX-License-Identifier: MPL-2.0
"""Synthetic positive and corruption probes for the Cachy profile summary."""

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[4]
BUNDLE = Path(__file__).resolve().parent
SUMMARY_PATH = BUNDLE / "summarize_profile.py"
SUMMARY_SPEC = importlib.util.spec_from_file_location("cachy_profile_summary", SUMMARY_PATH)
summary = importlib.util.module_from_spec(SUMMARY_SPEC)
sys.modules[SUMMARY_SPEC.name] = summary
SUMMARY_SPEC.loader.exec_module(summary)

FIXTURE_GENERATOR_PATH = ROOT / "tests/quality/test_split_bench_verifier.py"
FIXTURE_SPEC = importlib.util.spec_from_file_location(
    "split_bench_verifier_fixture_generator", FIXTURE_GENERATOR_PATH
)
fixture_generator = importlib.util.module_from_spec(FIXTURE_SPEC)
sys.modules[FIXTURE_SPEC.name] = fixture_generator
FIXTURE_SPEC.loader.exec_module(fixture_generator)


def synthetic_asset_receipt():
    assets = {
        "C/fs-compare-bench": summary.GUEST_SHA256,
        "L/pfs3aio": summary.PFS3_SHA256,
    }
    for index in range(64):
        asset_path = f"C/SyntheticTool{index:02d}"
        assets[asset_path] = hashlib.sha256(asset_path.encode("ascii")).hexdigest()
    library_path = "Libs/synthetic.library"
    assets[library_path] = hashlib.sha256(library_path.encode("ascii")).hexdigest()
    assert len(assets) == summary.EXPECTED_ASSET_COUNT
    return "".join(f"{digest}  {path}\n" for path, digest in sorted(assets.items()))


class CachyProfileSummaryTests(unittest.TestCase):
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
            (run_dir / "system/S/").mkdir(parents=True)
            (run_dir / "system/Results").mkdir()

            runtime = "".join(
                f"{digest}  {path}\n"
                for digest, path in summary.expected_runtime_receipt(entry)
            )
            (run_dir / "runtime-inputs.sha256").write_text(runtime, encoding="ascii")
            (run_dir / "runtime-inputs.post.sha256").write_text(runtime, encoding="ascii")
            (run_dir / "installed-assets.sha256").write_text(
                synthetic_asset_receipt(), encoding="ascii"
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
                f"kickstart_file = /snapshot/{entry.run_name}/A1200.rom\n"
                f"hard_drive_0 = /snapshot/{entry.run_name}/system\n"
                f"hard_drive_1 = /snapshot/{entry.run_name}/bench-bfs.hdf\n"
                f"hard_drive_2 = /snapshot/{entry.run_name}/bench-pfs3.hdf\n",
                encoding="ascii",
            )
            order = (("DH1:", "bfs"), ("DH2:", "pfs3")) if entry.order == "bfs-first" else (
                ("DH2:", "pfs3"), ("DH1:", "bfs"),
            )
            args = "split-durable" if entry.mode == "split-durable-compare" else "split"
            startup = [
                "FailAt 21",
                "Stack 32768",
                *(
                    f"C:fs-compare-bench {drive} {args} >SYS:Results/{filesystem}.split.tsv"
                    for drive, filesystem in order
                ),
            ]
            (run_dir / "system/S/Startup-Sequence").write_text(
                "\n".join(startup) + "\n", encoding="ascii"
            )

            results = run_dir / "system/Results"
            (results / "complete.txt").write_bytes(summary.VERIFIER.MARKER)
            (results / "info-after-format.txt").write_bytes(
                b"DH1: Workbench Read/Write BFSTest\nDH2: Workbench Read/Write PFSTest\n"
            )
            for filesystem in ("bfs", "pfs3"):
                rows = fixture_generator.SplitBenchVerifierTests._valid_rows(
                    None, filesystem, entry.mode,
                )
                (results / f"{filesystem}.split.tsv").write_text(
                    "\n".join(rows) + "\n", encoding="ascii"
                )

        (self.results_root / "schedule.tsv").write_text(
            "\n".join(schedule_lines) + "\n", encoding="ascii"
        )

    def _entry_dir(self, family="normal", repeat=1, variant="m5"):
        entry = next(entry for entry in summary.EXPECTED_SCHEDULE
                     if (entry.family, entry.repeat, entry.variant) == (family, repeat, variant))
        return self.results_root / entry.run_name

    def _edit_tsv(self, run_dir, filesystem, editor):
        path = run_dir / "system/Results" / f"{filesystem}.split.tsv"
        rows = path.read_text(encoding="ascii").splitlines()
        path.write_text("\n".join(editor(rows)) + "\n", encoding="ascii")

    @staticmethod
    def _set_row(rows, key, value):
        for index, row in enumerate(rows):
            name, _ = row.split("\t", 1)
            if name == key:
                rows[index] = f"{name}\t{value}"
                return rows
        raise AssertionError(f"missing fixture key: {key}")

    def _reject(self):
        with self.assertRaises(summary.QualificationError):
            summary.summarize(self.results_root)

    def test_valid_fixed_inventory_preserves_all_phases_and_counters(self):
        # Give a long scaled phase a distinct duration to prove longest-prefix mapping.
        run_dir = self._entry_dir()
        self._edit_tsv(run_dir, "bfs", lambda rows: self._set_row(
            self._set_row(rows, "LIST_EXNEXT_400_ENTRIES_FIRST_PASS_WORK_US", "7"),
            "LIST_EXNEXT_400_ENTRIES_FIRST_PASS_US", "7",
        ))
        result = summary.summarize(self.results_root)
        self.assertEqual(result["verified_schedule_count"], 12)
        self.assertEqual(result["verified_non_handler_asset_count"], 67)
        self.assertTrue(result["non_handler_assets_identical_across_runs"])
        self.assertEqual(len(result["runs"]), 12)
        bfs = result["runs"][run_dir.name]["filesystems"]["bfs"]
        self.assertEqual(len(bfs["phases"]), 23)
        self.assertEqual(bfs["phases"]["LIST_EXNEXT_400"]["raw"]["WORK_US"], 1)
        self.assertEqual(
            bfs["phases"]["LIST_EXNEXT_400_ENTRIES_FIRST_PASS"]["raw"]["WORK_US"], 7
        )
        self.assertIn("WORK_BIO_READS", bfs["phases"]["LOOKUP_400"]["raw"])
        self.assertEqual(
            set(result["comparisons"]["sparse_over_m5"][0]["bfs_ratios"]["SEQ_READ_8M"]
                ["guest_operations"]),
            set(summary.VERIFIER._inventories()["ops"]),
        )
        self.assertEqual(
            result["comparisons"]["two_pair_medians_by_mode"]["normal"]["pair_count"], 2
        )

    def test_zero_denominators_are_null_and_keep_both_raw_values(self):
        result = summary.summarize(self.results_root)
        phase = result["comparisons"]["sparse_over_m5"][0]["bfs_ratios"]["LOOKUP_400"]
        self.assertEqual(phase["volume_flush_us"]["numerator_sparse"], 0)
        self.assertEqual(phase["volume_flush_us"]["denominator_m5"], 0)
        self.assertIsNone(phase["volume_flush_us"]["ratio"])
        self.assertEqual(phase["guest_operations"]["VERIFY"]["calls"]["numerator_sparse"], 0)
        self.assertIsNone(phase["guest_operations"]["VERIFY"]["calls"]["ratio"])

    def test_same_handler_and_pfs3_control_ratios_are_present(self):
        result = summary.summarize(self.results_root)
        comparisons = result["comparisons"]
        self.assertEqual(len(comparisons["same_m5_handler_m5b_over_m5a"]), 2)
        self.assertEqual(len(comparisons["sparse_over_m5"]), 4)
        self.assertEqual(
            comparisons["same_m5_handler_m5b_over_m5a"][0]["ratio_label"], "M5-B/M5-A"
        )
        aa_counter = comparisons["same_m5_handler_m5b_over_m5a"][0][
            "bfs_io_crc_call_counter_differences"
        ]["LOOKUP_400"]["WORK"]["BIO_READS"]
        self.assertEqual(
            aa_counter, {"m5a": 0, "m5b": 0, "m5b_minus_m5a": 0}
        )
        primary_counter = comparisons["sparse_over_m5"][0][
            "bfs_io_crc_call_counter_differences"
        ]["LOOKUP_400"]["WORK"]["BIO_READS"]
        self.assertEqual(
            primary_counter, {"m5": 0, "sparse": 0, "sparse_minus_m5": 0}
        )
        self.assertIn("pfs3_control_ratios", comparisons["sparse_over_m5"][0])

    def test_accepts_identical_assets_with_different_receipt_order(self):
        path = self._entry_dir("durable", 1, "sparse") / "installed-assets.sha256"
        rows = path.read_text(encoding="ascii").splitlines()
        path.write_text("\n".join(reversed(rows)) + "\n", encoding="ascii")
        result = summary.summarize(self.results_root)
        self.assertTrue(result["non_handler_assets_identical_across_runs"])

    def test_rejects_missing_schedule_row(self):
        path = self.results_root / "schedule.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        path.write_text("\n".join(lines[:-1]) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_schedule_field_drift(self):
        path = self.results_root / "schedule.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        fields = lines[1].split("\t")
        fields[3] = "pfs3-first"
        lines[1] = "\t".join(fields)
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_filtered_run_inventory(self):
        run_dir = self._entry_dir("durable", 2, "m5")
        run_dir.rename(run_dir.with_name("filtered-run"))
        self._reject()

    def test_rejects_unexpected_run_directory(self):
        (self.results_root / "split-unexpected-run").mkdir()
        self._reject()

    def test_rejects_mismatched_runtime_sha256(self):
        path = self._entry_dir() / "runtime-inputs.sha256"
        rows = path.read_text(encoding="ascii").splitlines()
        rows[0] = "0" * 64 + rows[0][64:]
        path.write_text("\n".join(rows) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_post_run_runtime_sha256_drift(self):
        path = self._entry_dir("normal", 2, "sparse") / "runtime-inputs.post.sha256"
        rows = path.read_text(encoding="ascii").splitlines()
        rows[1] = "0" * 64 + rows[1][64:]
        path.write_text("\n".join(rows) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_bad_rdb_buffer_capacity(self):
        path = self._entry_dir() / "bfs-rdb.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["rdb"]["partitions"][0]["dos_env"]["num_buffer"] = 29
        path.write_text(json.dumps(document), encoding="utf-8")
        self._reject()

    def test_rejects_second_rdb_partition(self):
        path = self._entry_dir() / "bfs-rdb.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["rdb"]["partitions"].append({"dos_env": {"num_buffer": 30}})
        path.write_text(json.dumps(document), encoding="utf-8")
        self._reject()

    def test_rejects_non_handler_asset_drift(self):
        path = self._entry_dir("durable", 1, "sparse") / "installed-assets.sha256"
        rows = path.read_text(encoding="ascii").splitlines()
        index = next(i for i, row in enumerate(rows) if row.endswith("  C/SyntheticTool00"))
        rows[index] = "0" * 64 + rows[index][64:]
        path.write_text("\n".join(rows) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_malformed_asset_identity(self):
        path = self._entry_dir() / "installed-assets.sha256"
        rows = path.read_text(encoding="ascii").splitlines()
        rows[0] = "not-a-sha256  C/SyntheticTool00"
        path.write_text("\n".join(rows) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_duplicate_asset_identity_path(self):
        path = self._entry_dir() / "installed-assets.sha256"
        rows = path.read_text(encoding="ascii").splitlines()
        rows[1] = rows[0]
        path.write_text("\n".join(rows) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_config_cpu_drift(self):
        path = self._entry_dir() / "bench.fs-uae"
        path.write_text(
            path.read_text(encoding="ascii").replace("cpu = 68040", "cpu = 68020"),
            encoding="ascii",
        )
        self._reject()

    def test_rejects_duplicate_config_path_basenames(self):
        path = self._entry_dir() / "bench.fs-uae"
        lines = path.read_text(encoding="ascii").splitlines()
        index = next(i for i, line in enumerate(lines) if line.startswith("hard_drive_1 ="))
        lines[index] = "hard_drive_1 = /other/system"
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_startup_order_drift(self):
        path = self._entry_dir() / "system/S/Startup-Sequence"
        lines = path.read_text(encoding="ascii").splitlines()
        lines[2], lines[3] = lines[3], lines[2]
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self._reject()

    def test_rejects_completion_marker_corruption_via_protocol_verifier(self):
        path = self._entry_dir() / "system/Results/complete.txt"
        path.write_bytes(b"BFS-PFS3-SPLIT-COMPLETE")
        self._reject()

    def test_rejects_missing_mount_via_protocol_verifier(self):
        path = self._entry_dir() / "system/Results/info-after-format.txt"
        path.write_bytes(b"DH1 Workbench Read/Write BFSTest\n")
        self._reject()

    def test_rejects_crc_sample_corruption_via_protocol_verifier(self):
        run_dir = self._entry_dir("aa-durable", 1, "m5a")
        self._edit_tsv(run_dir, "bfs", lambda rows: self._set_row(
            rows, "SMALL_CREATE_40_WORK_NODE_CRC_READ_SAMPLES", "1",
        ))
        self._reject()


if __name__ == "__main__":
    unittest.main()
