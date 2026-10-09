# SPDX-License-Identifier: MPL-2.0
"""Fixed-inventory and identity oracles for the four-start attribution."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[3]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


summary = load_module("write_attribution_summary", BUNDLE / "summarize_profile.py")
fixtures = load_module("write_attribution_fixture", ROOT / "tests/quality/test_write_split_bench_verifier.py")
generator = fixtures.WriteSplitBenchVerifierTests()
prior = load_module("prior_attribution_fixture", BUNDLE.parent / "bfs-read-flush-cachy-2026-10-09/test_profile.py")


class WriteProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.results = Path(self.temp.name) / "results"
        self.results.mkdir()
        lines = ["sequence\tmode\torder\trun_name"]
        for index, (mode, order) in enumerate(summary.SCHEDULE, 1):
            name = f"write-{mode}-{order}"
            run = self.results / name
            outputs = run / "system/Results"
            outputs.mkdir(parents=True)
            (run / "system/S").mkdir()
            receipt = "".join(f"{digest}  {path}\n" for digest, path in (
                (summary.HANDLER, "system/L/bfshandler"),
                (summary.PFS3, "system/L/pfs3aio"),
                (summary.GUEST, "system/C/fs-compare-bench"),
            ))
            for filename in ("runtime-inputs.sha256", "runtime-inputs.post.sha256"):
                (run / filename).write_text(receipt)
            assets = prior.synthetic_asset_receipt().replace(prior.summary.GUEST_SHA256, summary.GUEST)
            (run / "installed-assets.sha256").write_text(assets)
            (run / "bfs-rdb.json").write_text('{"rdb":{"partitions":[{"dos_env":{"num_buffer":30}}]}}')
            (run / "bench.fs-uae").write_text(
                "[fs-uae]\namiga_model=A1200\ncpu=68040\nuae_cpu_speed=max\n"
                "chip_memory=2048\nfast_memory=8192\nkickstart_file=/private/kick.rom\n"
                "hard_drive_0=/run/system\nhard_drive_1=/run/bench-bfs.hdf\n"
                "hard_drive_2=/run/bench-pfs3.hdf\n")
            args = "split-write-durable" if mode.endswith("-durable-compare") else "split-write"
            order_fs = [("DH1:", "bfs"), ("DH2:", "pfs3")]
            if order == "pfs3-first":
                order_fs.reverse()
            (run / "system/S/Startup-Sequence").write_text("Stack 32768\n" + "".join(
                f"C:fs-compare-bench {drive} {args} >SYS:Results/{fs}.split.tsv\n"
                for drive, fs in order_fs))
            (outputs / "complete.txt").write_bytes(summary.common.VERIFIER.MARKER)
            (outputs / "info-after-format.txt").write_text(
                "DH1: Workbench Read/Write BFSTest\nDH2: Workbench Read/Write PFSTest\n")
            for fs in ("bfs", "pfs3"):
                (outputs / f"{fs}.split.tsv").write_text("\n".join(generator._valid_rows(fs, mode)) + "\n")
            lines.append(f"{index}\t{mode}\t{order}\t{name}")
        (self.results / "schedule.tsv").write_text("\n".join(lines) + "\n")
        self.run = self.results / "write-split-write-compare-bfs-first"

    def test_complete_inventory_retains_all_fields(self):
        result = summary.summarize(self.results)
        self.assertEqual(result["verified_schedule_count"], 4)
        for run in result["runs"].values():
            self.assertEqual(len(run["filesystems"]["bfs"]["phases"]), 23)
        fields = result["runs"][self.run.name]["filesystems"]["bfs"]["phases"]["SMALL_CREATE_40"]
        self.assertEqual(fields["raw"]["WORK_WRITE_DETAIL_INODE_READ_CALLS"], 34)
        self.assertEqual(fields["tick_microseconds"]["WORK_WRITE_DETAIL_INODE_READ_SAMPLE_TICKS"], 22)

    def test_rejects_extra_run(self):
        (self.results / "extra-run").mkdir()
        with self.assertRaises(ValueError):
            summary.summarize(self.results)

    def test_rejects_changed_schedule(self):
        schedule = self.results / "schedule.tsv"
        schedule.write_text(schedule.read_text().replace("1\tsplit-write", "9\tsplit-write", 1))
        with self.assertRaises(ValueError):
            summary.summarize(self.results)

    def test_rejects_post_run_identity_change(self):
        receipt = self.run / "runtime-inputs.post.sha256"
        receipt.write_text(receipt.read_text().replace(summary.HANDLER, "0" * 64))
        with self.assertRaises(ValueError):
            summary.summarize(self.results)

    def test_rejects_changed_asset(self):
        receipt = self.run / "installed-assets.sha256"
        rows = receipt.read_text().splitlines()
        rows[0] = "0" * 64 + rows[0][64:]
        receipt.write_text("\n".join(rows) + "\n")
        with self.assertRaises(ValueError):
            summary.summarize(self.results)

    def test_rejects_changed_buffers(self):
        path = self.run / "bfs-rdb.json"
        path.write_text(path.read_text().replace("30", "31"))
        with self.assertRaises(ValueError):
            summary.summarize(self.results)

    def test_rejects_wrong_startup_mode(self):
        path = self.run / "system/S/Startup-Sequence"
        path.write_text(path.read_text().replace(" split-write ", " split "))
        with self.assertRaises(ValueError):
            summary.summarize(self.results)

    def test_rejects_corrupt_protocol(self):
        path = self.run / "system/Results/bfs.split.tsv"
        path.write_text(path.read_text().replace("PASS\t1", "PASS\t0"))
        with self.assertRaises(ValueError):
            summary.summarize(self.results)


if __name__ == "__main__":
    unittest.main()
