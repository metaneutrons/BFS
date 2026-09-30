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

    def upgrade_deep_compare_to_v6(self):
        self.upgrade_deep_compare_to_v5()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t5")
        upgraded = ["FS_DEEP_COMPARE\t6"]
        for line in original[1:]:
            upgraded.append(line)
            if "\t" not in line:
                continue
            name, _ = line.split("\t")
            if not name.endswith("_CLOCK_PAIR_TICKS"):
                continue
            phase = name[:-len("_CLOCK_PAIR_TICKS")]
            phase_values = {
                metric_name: int(metric_value)
                for row in original
                if row.startswith(phase + "_") and "\t" in row
                for metric_name, metric_value in [row.split("\t")]
            }
            txn_commits = phase_values[phase + "_TXN_COMMITS"]
            reserve_writes = phase_values[phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"]
            returned_runs = 1
            reclaim_passes = txn_commits
            new_metrics = (
                ("FREE_TREE_RESERVE_RETURN_CALLS", max(txn_commits, 1)),
                ("FREE_TREE_RESERVE_RETURN_RUNS", returned_runs),
                ("FREE_TREE_RESERVE_RETURN_BLOCKS", 1),
                ("FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK", returned_runs),
                ("FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS", 0),
                ("FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS", 0),
                ("FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS", 0),
                ("FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS", 1),
                ("FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN", reserve_writes),
                ("POST_PUBLISH_RECLAIM_PASSES", reclaim_passes),
                ("MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT",
                 1 if reclaim_passes else 0),
                ("SUPERBLOCK_PUBLICATIONS", txn_commits + reclaim_passes),
            )
            upgraded.extend(f"{phase}_{metric}\t{value}" for metric, value in new_metrics)
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t5")
        pfs_lines[0] = "FS_DEEP_COMPARE\t6"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v7(self):
        self.upgrade_deep_compare_to_v6()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t6")
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        phase_values = {
            phase: {
                name: int(value)
                for row in original
                if row.startswith(phase + "_") and "\t" in row
                for name, value in [row.split("\t")]
            }
            for phase in phases
        }
        batch_phase = next(
            phase for phase in phases
            if phase_values[phase][phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"] > 1
        )
        upgraded = ["FS_DEEP_COMPARE\t7"]
        for line in original[1:]:
            name, value = line.split("\t")
            phase = next((phase for phase in phases if name.startswith(phase + "_")), None)
            if phase == batch_phase and name == phase + "_FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN":
                reserve_writes = phase_values[phase][phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"]
                value = str(min(int(value), reserve_writes - 1))
            upgraded.append(f"{name}\t{value}")
            if phase is None or not name.endswith("_SUPERBLOCK_PUBLICATIONS"):
                continue
            reserve_writes = phase_values[phase][phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"]
            batch_calls = 1 if phase == batch_phase else 0
            batch_blocks = 2 if batch_calls else 0
            batch_node_writes = 1 if batch_calls else 0
            run_node_writes = reserve_writes - batch_node_writes
            new_metrics = (
                ("FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES", run_node_writes),
                ("FREE_TREE_RESERVE_RETURN_BATCH_CALLS", batch_calls),
                ("FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS", batch_blocks),
                ("FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES", batch_node_writes),
            )
            upgraded.extend(f"{phase}_{metric}\t{metric_value}"
                            for metric, metric_value in new_metrics)
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t6")
        pfs_lines[0] = "FS_DEEP_COMPARE\t7"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v8(self):
        self.upgrade_deep_compare_to_v7()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t7")
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        phase_values = {
            phase: {
                name: int(value)
                for row in original
                if row.startswith(phase + "_") and "\t" in row
                for name, value in [row.split("\t")]
            }
            for phase in phases
        }
        skip_metrics = (
            "FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
            "FREE_TREE_RESERVE_RETURN_SKIP_SMALL",
            "FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY",
            "FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY",
        )
        upgraded = ["FS_DEEP_COMPARE\t8"]
        for line in original[1:]:
            name, value = line.split("\t")
            upgraded.append(line)
            if not name.endswith("_FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES"):
                continue
            phase = name[:-len("_FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES")]
            batch_calls = phase_values[phase][
                phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS"
            ]
            return_calls = phase_values[phase][
                phase + "_FREE_TREE_RESERVE_RETURN_CALLS"
            ]
            skipped_calls = return_calls - batch_calls
            self.assertGreaterEqual(skipped_calls, 0)
            skip_counts = [0] * len(skip_metrics)
            for skip_index in range(skipped_calls):
                skip_counts[(phases.index(phase) + skip_index) % len(skip_metrics)] += 1
            upgraded.extend(
                f"{phase}_{metric}\t{count}"
                for metric, count in zip(skip_metrics, skip_counts)
            )
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t7")
        pfs_lines[0] = "FS_DEEP_COMPARE\t8"
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

    def test_deep_compare_v6_accepts_consistent_reserve_and_commit_metrics(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v6()
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v6_rejects_reserve_and_commit_invariant_violations(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v6()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        txn_line = next(line for line in original.splitlines()
                        if line.startswith("SMALL_CREATE_40_TXN_COMMITS\t"))
        txn_commits = int(txn_line.split("\t")[1])
        reserve_writes_line = next(
            line for line in original.splitlines()
            if line.startswith("SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_NODE_WRITES\t")
        )
        reserve_writes = int(reserve_writes_line.split("\t")[1])
        cases = (
            ("SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_RUNS\t1",
             "SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_RUNS\t2"),
            ("SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_BLOCKS\t1",
             "SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_BLOCKS\t0"),
            ("SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS\t1",
             "SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS\t2"),
            (f"SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN\t{reserve_writes}",
             f"SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN\t{reserve_writes + 1}"),
            (f"SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_CALLS\t{max(txn_commits, 1)}",
             f"SMALL_CREATE_40_FREE_TREE_RESERVE_RETURN_CALLS\t{max(txn_commits - 1, 0)}"),
            (f"SMALL_CREATE_40_MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT\t{1 if txn_commits else 0}",
             f"SMALL_CREATE_40_MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT\t{txn_commits + 1}"),
            (f"SMALL_CREATE_40_SUPERBLOCK_PUBLICATIONS\t{txn_commits * 2}",
             f"SMALL_CREATE_40_SUPERBLOCK_PUBLICATIONS\t{txn_commits}"),
        )
        for old, new in cases:
            with self.subTest(metric=old):
                self.assertIn(old, original)
                bfs.write_text(original.replace(old, new, 1), encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v7_accepts_reserve_batch_metrics(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v7()
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v7_rejects_batch_write_and_count_violations(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v7()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        batch_phase = "SMALL_CREATE_40"
        run_metric = batch_phase + "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES"
        batch_calls_metric = batch_phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS"
        batch_blocks_metric = batch_phase + "_FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS"
        return_calls_metric = batch_phase + "_FREE_TREE_RESERVE_RETURN_CALLS"
        run_writes = self.metric_value(original, run_metric)
        batch_calls = self.metric_value(original, batch_calls_metric)
        batch_blocks = self.metric_value(original, batch_blocks_metric)
        return_calls = self.metric_value(original, return_calls_metric)
        cases = (
            (f"{run_metric}\t{run_writes}", f"{run_metric}\t{run_writes + 1}"),
            (f"{batch_blocks_metric}\t{batch_blocks}", f"{batch_blocks_metric}\t{batch_calls - 1}"),
            (f"{batch_calls_metric}\t{batch_calls}",
             f"{batch_calls_metric}\t{return_calls + 1}"),
        )
        for old, new in cases:
            with self.subTest(metric=old):
                self.assertIn(old, original)
                bfs.write_text(original.replace(old, new, 1), encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v8_accepts_reserve_return_classification(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v8()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        skip_metrics = (
            "FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
            "FREE_TREE_RESERVE_RETURN_SKIP_SMALL",
            "FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY",
            "FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY",
        )
        self.assertTrue(all(
            sum(self.metric_value(contents, phase + "_" + metric)
                for phase in (
                    "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
                    "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
                )) > 0
            for metric in skip_metrics
        ))
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v8_rejects_classification_and_write_sum_violations(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v8()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        skip_metric = phase + "_FREE_TREE_RESERVE_RETURN_SKIP_SHAPE"
        return_metric = phase + "_FREE_TREE_RESERVE_RETURN_CALLS"
        run_metric = phase + "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES"
        cases = (
            (skip_metric, self.metric_value(original, skip_metric) + 1),
            (return_metric, self.metric_value(original, return_metric) + 1),
            (run_metric, self.metric_value(original, run_metric) + 1),
        )
        for metric, changed_value in cases:
            with self.subTest(metric=metric):
                old_value = self.metric_value(original, metric)
                mutated = original.replace(
                    f"{metric}\t{old_value}", f"{metric}\t{changed_value}", 1,
                )
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    @staticmethod
    def metric_value(contents, metric_name):
        line = next(line for line in contents.splitlines()
                    if line.startswith(metric_name + "\t"))
        return int(line.split("\t")[1])

    def test_real_deep_profile_evidence_and_missing_peer(self):
        self.load_evidence("deep-bfs-first", "deep.tsv")
        result = self.verify("deep")
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.results / "pfs3.deep.tsv").unlink()
        self.assertNotEqual(self.verify("deep").returncode, 0)


if __name__ == "__main__":
    unittest.main()
