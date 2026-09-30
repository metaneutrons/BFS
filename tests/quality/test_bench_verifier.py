# SPDX-License-Identifier: MPL-2.0
"""Positive and corrupt-input probes for the Amiga filesystem bench verifier."""

from pathlib import Path
import shutil
import subprocess  # nosec B404 - invokes only the fixed repository verifier, without a shell
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/qualification/evidence/bfs-pfs3-deep-profile-2026-09-29"
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"


class BenchVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.run_dir = Path(self.temp.name)
        self.results = self.run_dir / "system/Results"
        self.results.mkdir(parents=True)

    def load_evidence(self, directory, suffix):
        source = EVIDENCE / directory
        for name in ("complete.txt", "info-after-format.txt",
                     f"bfs.{suffix}", f"pfs3.{suffix}"):
            shutil.copyfile(source / name, self.results / name)

    def upgrade_deep_compare_to_v5(self):
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t4")
        bucket_names = (
            "FREE_TREE_ALLOCATION_BODY_NODE_WRITES",
            "FREE_TREE_RESERVE_REFILL_NODE_WRITES",
            "FREE_TREE_RESERVE_RETURN_NODE_WRITES",
            "FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES",
            "FREE_TREE_OTHER_NODE_WRITES",
        )
        upgraded = ["FS_DEEP_COMPARE\t5"]
        for line in original[1:]:
            upgraded.append(line)
            name, value = line.split("\t")
            suffix = "_FREE_TREE_NODE_WRITES"
            if name.endswith(suffix):
                free_tree_writes = int(value)
                bucket_values = (
                    free_tree_writes // 2,
                    free_tree_writes // 4,
                    free_tree_writes // 8,
                    free_tree_writes // 8,
                    free_tree_writes - free_tree_writes // 2 - free_tree_writes // 4
                    - free_tree_writes // 8 - free_tree_writes // 8,
                )
                phase = name[:-len(suffix)]
                upgraded.extend(
                    f"{phase}_{bucket}\t{bucket_value}"
                    for bucket, bucket_value in zip(bucket_names, bucket_values)
                )
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t4")
        pfs_lines[0] = "FS_DEEP_COMPARE\t5"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def verify(self, mode):
        return subprocess.run(
            [str(VERIFIER), str(self.run_dir), mode],
            capture_output=True, text=True, check=False,
        )  # nosec B603 - executable path and mode are fixed by this test

    def test_real_deep_compare_evidence_passes(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v5_bucket_sums_pass_for_all_phases(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v5()
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v5_rejects_bucket_sum_mismatch_in_every_phase(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v5()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        for phase in phases:
            metric = f"{phase}_FREE_TREE_OTHER_NODE_WRITES\t"
            with self.subTest(phase=phase):
                matching_line = next(line for line in original.splitlines()
                                     if line.startswith(metric))
                value = int(matching_line.split("\t")[1])
                mutated = original.replace(
                    matching_line, f"{metric}{value + 1}", 1,
                )
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_counterprobes(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("CRC_SAMPLE_STRIDE\t64", "CRC_SAMPLE_STRIDE\t63"),
            ("CLOCK_HZ\t709379", "CLOCK_HZ\t0"),
            ("SMALL_CREATE_40_US\t3467501", "SMALL_CREATE_40_US\t0"),
            ("SMALL_CREATE_40_NODE_CRC_READ_CALLS\t15466",
             "SMALL_CREATE_40_NODE_CRC_READ_CALLS\tbroken"),
            ("SMALL_CREATE_40_NODE_CRC_READ_SAMPLES\t241\n", ""),
            ("PASS\t1", "PASS\t0"),
        )
        for old, new in cases:
            with self.subTest(metric=old):
                self.assertIn(old, original)
                bfs.write_text(original.replace(old, new, 1), encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v5_rejects_missing_or_malformed_bucket(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v5()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("SMALL_CREATE_40_FREE_TREE_ALLOCATION_BODY_NODE_WRITES\t", "broken"),
            ("SMALL_CREATE_40_FREE_TREE_RESERVE_REFILL_NODE_WRITES\t", None),
        )
        for metric, replacement in cases:
            with self.subTest(metric=metric):
                matching_line = next(line for line in original.splitlines()
                                     if line.startswith(metric))
                if replacement is None:
                    mutated = original.replace(matching_line + "\n", "", 1)
                else:
                    mutated = original.replace(
                        matching_line, metric + replacement, 1,
                    )
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_real_deep_profile_evidence_and_missing_peer(self):
        self.load_evidence("deep-bfs-first", "deep.tsv")
        result = self.verify("deep")
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.results / "pfs3.deep.tsv").unlink()
        self.assertNotEqual(self.verify("deep").returncode, 0)


if __name__ == "__main__":
    unittest.main()
