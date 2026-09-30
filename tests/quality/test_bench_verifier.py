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

    def upgrade_deep_compare_to_v9(self):
        self.upgrade_deep_compare_to_v8()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t8")
        upgraded = ["FS_DEEP_COMPARE\t9"]
        for line in original[1:]:
            upgraded.append(line)
            name, _ = line.split("\t")
            suffix = "_FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY"
            if not name.endswith(suffix):
                continue
            phase = name[:-len(suffix)]
            upgraded.extend((
                f"{phase}_SEALED_COMMITS\t0",
                f"{phase}_SEALED_METADATA_FENCES\t0",
            ))
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t8")
        pfs_lines[0] = "FS_DEEP_COMPARE\t9"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

        contents = bfs.read_text(encoding="ascii")
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        zero_txn_return_metrics = (
            "FREE_TREE_RESERVE_RETURN_CALLS",
            "FREE_TREE_RESERVE_RETURN_RUNS",
            "FREE_TREE_RESERVE_RETURN_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK",
            "FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN",
            "FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES",
            "FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
            "FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES",
            "FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
            "FREE_TREE_RESERVE_RETURN_SKIP_SMALL",
            "FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY",
            "FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY",
        )
        updates = {}
        for phase in phases:
            txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
            passes = self.metric_value(
                contents, phase + "_POST_PUBLISH_RECLAIM_PASSES",
            )
            return_calls = txn_commits + passes
            batch_calls = self.metric_value(
                contents, phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
            )
            updates[phase + "_FREE_TREE_RESERVE_RETURN_CALLS"] = return_calls
            updates.update(self.skip_count_updates(phase, return_calls, batch_calls))
            if return_calls == 0:
                for metric in zero_txn_return_metrics:
                    updates[phase + "_" + metric] = 0
        self.set_metrics(bfs, updates)

    def upgrade_deep_compare_to_v10(self, zero_cpu_counters=False):
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t9")
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        counter_names = (
            "BTREE_MALLOC_CALLS", "BTREE_MALLOC_SAMPLES",
            "BTREE_MALLOC_SAMPLE_TICKS", "BTREE_FREE_CALLS",
            "BTREE_FREE_SAMPLES", "BTREE_FREE_SAMPLE_TICKS",
            "IFACE_ALLOC_CALLS", "IFACE_ALLOC_SAMPLES",
            "IFACE_ALLOC_SAMPLE_TICKS", "FREESPACE_ALLOC_SAMPLES",
            "FREESPACE_ALLOC_SAMPLE_TICKS",
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
        upgraded = ["FS_DEEP_COMPARE\t10"]
        for line in original[1:]:
            upgraded.append(line)
            if "_SEALED_METADATA_FENCES\t" not in line:
                continue
            phase = line.split("\t", 1)[0][:-len("_SEALED_METADATA_FENCES")]
            freespace_samples = phase_values[phase][phase + "_FREESPACE_ALLOCS"]
            if zero_cpu_counters:
                values = (0, 0, 0, 0, 0, 0, 0, 0, 0,
                          freespace_samples, 0)
            else:
                values = (128, 128, 901, 96, 96, 702, 33, 33, 403,
                          freespace_samples, 1 if freespace_samples else 0)
            upgraded.extend(
                f"{phase}_{name}\t{value}"
                for name, value in zip(counter_names, values)
            )
        upgraded.insert(upgraded.index("PASS\t1"), "CPU_SAMPLE_STRIDE\t1")
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t9")
        pfs_lines[0] = "FS_DEEP_COMPARE\t10"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_raw_deep_compare_v8_to_v9(self):
        source = ROOT / "docs/qualification/evidence/bfs-sealed-settlement-2026-10-01/baseline-deep"
        for name in (
            "complete.txt", "info-after-format.txt",
            "bfs.deep-compare.tsv", "pfs3.deep-compare.tsv",
        ):
            shutil.copyfile(source / name, self.results / name)

        bfs = self.results / "bfs.deep-compare.tsv"
        original_bfs = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original_bfs[0], "FS_DEEP_COMPARE\t8")
        upgraded_bfs = ["FS_DEEP_COMPARE\t9"]
        for line in original_bfs[1:]:
            upgraded_bfs.append(line)
            name, _ = line.split("\t")
            suffix = "_FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY"
            if name.endswith(suffix):
                phase = name[:-len(suffix)]
                upgraded_bfs.extend((
                    f"{phase}_SEALED_COMMITS\t0",
                    f"{phase}_SEALED_METADATA_FENCES\t0",
                ))
        bfs.write_text("\n".join(upgraded_bfs) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t8")
        pfs_lines[0] = "FS_DEEP_COMPARE\t9"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v9_all_sealed(self):
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        return_metrics = (
            "FREE_TREE_RESERVE_RETURN_CALLS",
            "FREE_TREE_RESERVE_RETURN_RUNS",
            "FREE_TREE_RESERVE_RETURN_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK",
            "FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN",
            "FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES",
            "FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
            "FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS",
            "FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES",
            "FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
            "FREE_TREE_RESERVE_RETURN_SKIP_SMALL",
            "FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY",
            "FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY",
        )
        updates = {}
        for phase in phases:
            txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
            reserve_writes = self.metric_value(
                contents, phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES",
            )
            other_writes = self.metric_value(
                contents, phase + "_FREE_TREE_OTHER_NODE_WRITES",
            )
            updates[phase + "_SEALED_COMMITS"] = txn_commits
            updates[phase + "_SEALED_METADATA_FENCES"] = txn_commits
            updates[phase + "_POST_PUBLISH_RECLAIM_PASSES"] = 0
            updates[phase + "_MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT"] = 0
            updates[phase + "_SUPERBLOCK_PUBLICATIONS"] = txn_commits
            updates[phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES"] = 0
            updates[phase + "_FREE_TREE_OTHER_NODE_WRITES"] = (
                other_writes + reserve_writes
            )
            for metric in return_metrics:
                updates[phase + "_" + metric] = 0
        self.set_metrics(bfs, updates)

    def upgrade_deep_compare_to_v9_mixed(self):
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
        sealed_commits = txn_commits // 2
        passes = self.metric_value(
            contents, phase + "_POST_PUBLISH_RECLAIM_PASSES",
        )
        return_calls = txn_commits - sealed_commits + passes
        batch_calls = self.metric_value(
            contents, phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
        )
        self.assertGreater(sealed_commits, 0)
        self.assertGreater(return_calls, 0)
        self.assertGreaterEqual(return_calls, batch_calls)
        updates = {
            phase + "_SEALED_COMMITS": sealed_commits,
            phase + "_SEALED_METADATA_FENCES": sealed_commits,
            phase + "_FREE_TREE_RESERVE_RETURN_CALLS": return_calls,
        }
        updates.update(self.skip_count_updates(phase, return_calls, batch_calls))
        self.set_metrics(bfs, updates)

    def skip_count_updates(self, phase, return_calls, batch_calls):
        skip_metrics = (
            "FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
            "FREE_TREE_RESERVE_RETURN_SKIP_SMALL",
            "FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY",
            "FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY",
        )
        skipped_calls = return_calls - batch_calls
        self.assertGreaterEqual(skipped_calls, 0)
        phases = (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        )
        counts = [0] * len(skip_metrics)
        for skip_index in range(skipped_calls):
            counts[(phases.index(phase) + skip_index) % len(skip_metrics)] += 1
        return {
            phase + "_" + metric: count
            for metric, count in zip(skip_metrics, counts)
        }

    @staticmethod
    def set_metrics(path, updates):
        remaining = dict(updates)
        lines = path.read_text(encoding="ascii").splitlines()
        for index, line in enumerate(lines):
            if "\t" not in line:
                continue
            name, _ = line.split("\t")
            if name in remaining:
                lines[index] = f"{name}\t{remaining.pop(name)}"
        if remaining:
            raise AssertionError(f"metrics not found: {sorted(remaining)}")
        path.write_text("\n".join(lines) + "\n", encoding="ascii")

    def verify(self, mode):
        return subprocess.run(
            [str(VERIFIER), str(self.run_dir), mode],
            capture_output=True, text=True, check=False,
        )  # nosec B603 - executable path and mode are fixed by this test

    def test_real_deep_compare_evidence_passes(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_real_cpu_attribution_and_control_evidence_passes(self):
        evidence = ROOT / "docs/qualification/evidence/bfs-cpu-attribution-2026-10-01"
        for run in (
            "cpu-control-bfs-first", "cpu-control-pfs3-first",
            "cpu-attribution-pfs3-first", "cpu-attribution-bfs-first",
            "cpu-attribution-repeat-bfs-first", "cpu-attribution-repeat-pfs3-first",
        ):
            with self.subTest(run=run):
                for name in (
                    "complete.txt", "info-after-format.txt",
                    "bfs.deep-compare.tsv", "pfs3.deep-compare.tsv",
                ):
                    shutil.copyfile(evidence / run / name, self.results / name)
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

    def test_deep_compare_v8_still_rejects_low_return_calls(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v8()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
        return_calls = txn_commits - 1
        batch_calls = self.metric_value(
            contents, phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
        )
        self.assertGreater(txn_commits, 1)
        updates = {
            phase + "_FREE_TREE_RESERVE_RETURN_CALLS": return_calls,
        }
        updates.update(self.skip_count_updates(phase, return_calls, batch_calls))
        self.set_metrics(bfs, updates)
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v9_accepts_all_sealed_commits_without_returns(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9_all_sealed()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        for phase in (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        ):
            txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
            self.assertEqual(
                self.metric_value(contents, phase + "_SEALED_COMMITS"),
                txn_commits,
            )
            self.assertEqual(
                self.metric_value(contents, phase + "_SEALED_METADATA_FENCES"),
                txn_commits,
            )
            self.assertEqual(
                self.metric_value(
                    contents, phase + "_FREE_TREE_RESERVE_RETURN_CALLS",
                ),
                0,
            )
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v9_accepts_raw_baseline_with_only_seal_rows_added(self):
        source = ROOT / "docs/qualification/evidence/bfs-sealed-settlement-2026-10-01/baseline-deep"
        original_bfs = (source / "bfs.deep-compare.tsv").read_text(
            encoding="ascii",
        ).splitlines()
        original_pfs3 = (source / "pfs3.deep-compare.tsv").read_text(
            encoding="ascii",
        ).splitlines()
        self.upgrade_raw_deep_compare_v8_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        upgraded_bfs = bfs.read_text(encoding="ascii").splitlines()
        upgraded_pfs3 = pfs3.read_text(encoding="ascii").splitlines()

        self.assertEqual(upgraded_bfs[0], "FS_DEEP_COMPARE\t9")
        self.assertEqual(
            [line for line in upgraded_bfs if "_SEALED_" not in line],
            ["FS_DEEP_COMPARE\t9", *original_bfs[1:]],
        )
        self.assertEqual(
            sum("_SEALED_" in line for line in upgraded_bfs), 12,
        )
        self.assertEqual(upgraded_pfs3, ["FS_DEEP_COMPARE\t9", *original_pfs3[1:]])
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v10_accepts_cpu_probe_metrics(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v10()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        self.assertIn("CPU_SAMPLE_STRIDE\t1", contents)
        for phase in (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        ):
            for category, calls in (
                ("BTREE_MALLOC", 128), ("BTREE_FREE", 96),
                ("IFACE_ALLOC", 33),
            ):
                self.assertEqual(
                    self.metric_value(contents, phase + "_" + category + "_CALLS"),
                    calls,
                )
                self.assertEqual(
                    self.metric_value(contents, phase + "_" + category + "_SAMPLES"),
                    calls,
                )
            self.assertEqual(
                self.metric_value(contents, phase + "_FREESPACE_ALLOC_SAMPLES"),
                self.metric_value(contents, phase + "_FREESPACE_ALLOCS"),
            )
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v10_accepts_v9_upgrade_with_compatible_zero_counters(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v10(zero_cpu_counters=True)
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        for phase in (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        ):
            for metric in (
                "BTREE_MALLOC_CALLS", "BTREE_MALLOC_SAMPLES",
                "BTREE_MALLOC_SAMPLE_TICKS", "BTREE_FREE_CALLS",
                "BTREE_FREE_SAMPLES", "BTREE_FREE_SAMPLE_TICKS",
                "IFACE_ALLOC_CALLS", "IFACE_ALLOC_SAMPLES",
                "IFACE_ALLOC_SAMPLE_TICKS", "FREESPACE_ALLOC_SAMPLE_TICKS",
            ):
                self.assertEqual(self.metric_value(contents, phase + "_" + metric), 0)
            self.assertEqual(
                self.metric_value(contents, phase + "_FREESPACE_ALLOC_SAMPLES"),
                self.metric_value(contents, phase + "_FREESPACE_ALLOCS"),
            )
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v10_rejects_missing_malformed_and_duplicate_cpu_rows(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v10()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        target = "SMALL_CREATE_40_BTREE_MALLOC_CALLS\t128"
        missing_row = "SMALL_CREATE_40_BTREE_FREE_SAMPLE_TICKS\t702"
        malformed_row = "SMALL_CREATE_40_IFACE_ALLOC_SAMPLE_TICKS\t403"
        cases = (
            ("missing CPU counter", original.replace(missing_row + "\n", "", 1)),
            ("malformed CPU counter", original.replace(malformed_row,
                                                        malformed_row.rsplit("\t", 1)[0] + "\tbad", 1)),
            ("duplicate CPU counter", original.replace(
                "PASS\t1\n", target + "\nPASS\t1\n", 1,
            )),
        )
        for label, mutated in cases:
            with self.subTest(counter=label):
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v10_rejects_cpu_sample_mismatches(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v10()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        freespace_samples = self.metric_value(
            original, "SMALL_CREATE_40_FREESPACE_ALLOC_SAMPLES",
        )
        self.assertGreater(freespace_samples, 0)
        cases = (
            ("BTREE_MALLOC samples", {
                "SMALL_CREATE_40_BTREE_MALLOC_SAMPLES": 127,
            }),
            ("BTREE_FREE samples", {
                "SMALL_CREATE_40_BTREE_FREE_SAMPLES": 95,
            }),
            ("IFACE_ALLOC samples", {
                "SMALL_CREATE_40_IFACE_ALLOC_SAMPLES": 32,
            }),
            ("FREESPACE_ALLOC samples", {
                "SMALL_CREATE_40_FREESPACE_ALLOC_SAMPLES": freespace_samples - 1,
            }),
            ("ticks without a sample", {
                "SMALL_CREATE_40_BTREE_MALLOC_CALLS": 0,
                "SMALL_CREATE_40_BTREE_MALLOC_SAMPLES": 0,
                "SMALL_CREATE_40_BTREE_MALLOC_SAMPLE_TICKS": 1,
            }),
            ("BTREE_FREE ticks without a sample", {
                "SMALL_CREATE_40_BTREE_FREE_CALLS": 0,
                "SMALL_CREATE_40_BTREE_FREE_SAMPLES": 0,
                "SMALL_CREATE_40_BTREE_FREE_SAMPLE_TICKS": 1,
            }),
            ("IFACE_ALLOC ticks without a sample", {
                "SMALL_CREATE_40_IFACE_ALLOC_CALLS": 0,
                "SMALL_CREATE_40_IFACE_ALLOC_SAMPLES": 0,
                "SMALL_CREATE_40_IFACE_ALLOC_SAMPLE_TICKS": 1,
            }),
            ("FREESPACE_ALLOC ticks without a sample", {
                "SMALL_CREATE_40_FREESPACE_ALLOCS": 0,
                "SMALL_CREATE_40_FREESPACE_ALLOC_SAMPLES": 0,
                "SMALL_CREATE_40_FREESPACE_ALLOC_SAMPLE_TICKS": 1,
            }),
            ("CPU sample stride", {
                "CPU_SAMPLE_STRIDE": 2,
            }),
        )
        for label, updates in cases:
            with self.subTest(metric=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v10_rejects_reserve_batch_and_sealed_accounting_errors(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v10()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        reserve_writes = self.metric_value(
            original, phase + "_FREE_TREE_RESERVE_RETURN_NODE_WRITES",
        )
        run_writes = self.metric_value(
            original, phase + "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES",
        )
        batch_writes = self.metric_value(
            original, phase + "_FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES",
        )
        txn_commits = self.metric_value(original, phase + "_TXN_COMMITS")
        passes = self.metric_value(
            original, phase + "_POST_PUBLISH_RECLAIM_PASSES",
        )
        return_calls = self.metric_value(
            original, phase + "_FREE_TREE_RESERVE_RETURN_CALLS",
        )
        sealed_commits = self.metric_value(
            original, phase + "_SEALED_COMMITS",
        )
        publications = self.metric_value(
            original, phase + "_SUPERBLOCK_PUBLICATIONS",
        )
        metadata_fences = self.metric_value(
            original, phase + "_SEALED_METADATA_FENCES",
        )
        batch_calls = self.metric_value(
            original, phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
        )
        bio_updates = self.metric_value(original, phase + "_BIO_UPDATES")
        self.assertEqual(run_writes + batch_writes, reserve_writes)
        cases = (
            ("run and batch writes do not sum", {
                phase + "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES": run_writes + 1,
            }),
            ("sealed return identity", {
                phase + "_FREE_TREE_RESERVE_RETURN_CALLS": return_calls + 1,
                **self.skip_count_updates(phase, return_calls + 1, batch_calls),
            }),
            ("sealed publication identity", {
                phase + "_SUPERBLOCK_PUBLICATIONS": publications + 1,
                phase + "_BIO_UPDATES": bio_updates + 1,
            }),
            ("sealed fence identity", {
                phase + "_SEALED_METADATA_FENCES": metadata_fences + 1,
                phase + "_BIO_UPDATES": bio_updates + 1,
            }),
        )
        self.assertEqual(return_calls + sealed_commits, txn_commits + passes)
        self.assertEqual(publications, txn_commits + passes)
        for label, updates in cases:
            with self.subTest(invariant=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v9_accepts_mixed_sealed_and_legacy_commits(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9_mixed()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
        sealed_commits = self.metric_value(contents, phase + "_SEALED_COMMITS")
        return_calls = self.metric_value(
            contents, phase + "_FREE_TREE_RESERVE_RETURN_CALLS",
        )
        passes = self.metric_value(
            contents, phase + "_POST_PUBLISH_RECLAIM_PASSES",
        )
        publications = self.metric_value(
            contents, phase + "_SUPERBLOCK_PUBLICATIONS",
        )
        self.assertGreater(sealed_commits, 0)
        self.assertLess(sealed_commits, txn_commits)
        self.assertEqual(return_calls + sealed_commits, txn_commits + passes)
        self.assertEqual(txn_commits + passes, publications)
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v9_rejects_missing_malformed_and_duplicate_counters(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        sealed_row = f"{phase}_SEALED_COMMITS\t0"
        cases = (
            ("omitted sealed counter", lambda lines: [
                line for line in lines if line != sealed_row
            ]),
            ("malformed metadata fence", lambda lines: [
                line.replace(f"{phase}_SEALED_METADATA_FENCES\t0",
                             f"{phase}_SEALED_METADATA_FENCES\tbad")
                for line in lines
            ]),
            ("duplicate sealed counter", lambda lines: lines[:-1] + [
                sealed_row, lines[-1],
            ]),
        )
        for label, mutate in cases:
            with self.subTest(counter=label):
                mutated = mutate(original.splitlines())
                bfs.write_text("\n".join(mutated) + "\n", encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v9_rejects_seal_and_accounting_invariant_violations(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        txn_commits = self.metric_value(original, phase + "_TXN_COMMITS")
        return_calls = self.metric_value(
            original, phase + "_FREE_TREE_RESERVE_RETURN_CALLS",
        )
        passes = self.metric_value(
            original, phase + "_POST_PUBLISH_RECLAIM_PASSES",
        )
        free_tree_writes = self.metric_value(
            original, phase + "_FREE_TREE_NODE_WRITES",
        )
        cases = (
            ("sealed exceeds transactions", {
                phase + "_SEALED_COMMITS": txn_commits + 1,
                phase + "_SEALED_METADATA_FENCES": txn_commits + 1,
            }),
            ("missing metadata fence", {
                phase + "_SEALED_COMMITS": 1,
                phase + "_SEALED_METADATA_FENCES": 0,
            }),
            ("insufficient BIO updates", {
                phase + "_BIO_UPDATES": 0,
            }),
            ("bad existing bucket", {
                phase + "_FREE_TREE_OTHER_NODE_WRITES":
                    self.metric_value(original,
                                      phase + "_FREE_TREE_OTHER_NODE_WRITES") + 1,
            }),
            ("bad existing skip sum", {
                phase + "_FREE_TREE_RESERVE_RETURN_SKIP_SHAPE":
                    self.metric_value(
                        original,
                        phase + "_FREE_TREE_RESERVE_RETURN_SKIP_SHAPE",
                    ) + 1,
            }),
            ("bad existing publication count", {
                phase + "_SUPERBLOCK_PUBLICATIONS": txn_commits,
            }),
            ("missing per-pass return accounting", {
                phase + "_FREE_TREE_RESERVE_RETURN_CALLS": txn_commits,
                **self.skip_count_updates(
                    phase, txn_commits,
                    self.metric_value(
                        original,
                        phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
                    ),
                ),
            }),
            ("excess per-pass return accounting", {
                phase + "_FREE_TREE_RESERVE_RETURN_CALLS":
                    txn_commits + passes + 1,
                **self.skip_count_updates(
                    phase, txn_commits + passes + 1,
                    self.metric_value(
                        original,
                        phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
                    ),
                ),
            }),
        )
        self.assertGreater(return_calls, 0)
        self.assertGreater(free_tree_writes, txn_commits)
        for label, updates in cases:
            with self.subTest(invariant=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9_all_sealed()
        contents = bfs.read_text(encoding="ascii")
        self.assertEqual(
            self.metric_value(contents, phase + "_FREE_TREE_RESERVE_RETURN_CALLS"),
            0,
        )
        self.set_metrics(bfs, {
            phase + "_SEALED_COMMITS": 0,
            phase + "_SEALED_METADATA_FENCES": 0,
        })
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v9_rejects_inconsistent_return_batch_accounting(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        values = {
            name: self.metric_value(original, phase + "_" + name)
            for name in (
                "TXN_COMMITS",
                "FREE_TREE_RESERVE_RETURN_CALLS",
                "FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES",
                "FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
                "FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS",
                "FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES",
                "FREE_TREE_RESERVE_RETURN_NODE_WRITES",
            )
        }
        cases = (
            ("run and batch node writes do not sum to reserve writes", {
                phase + "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES":
                    values["FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES"] + 1,
            }),
            ("batch calls exceed return calls", {
                phase + "_FREE_TREE_RESERVE_RETURN_CALLS":
                    values["FREE_TREE_RESERVE_RETURN_BATCH_CALLS"] - 1,
            }),
            ("batch blocks are fewer than batch calls", {
                phase + "_FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS":
                    values["FREE_TREE_RESERVE_RETURN_BATCH_CALLS"] - 1,
            }),
        )
        self.assertGreater(values["FREE_TREE_RESERVE_RETURN_BATCH_CALLS"], 0)
        self.assertGreater(values["FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES"], 0)
        for label, updates in cases:
            with self.subTest(invariant=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        # Keep the aggregate node-write total and skip-call sum valid while
        # making the forbidden zero-call/nonzero-total state explicit.
        batch_node_writes = 1
        reserve_writes = values["FREE_TREE_RESERVE_RETURN_NODE_WRITES"]
        updates = {
            phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS": 0,
            phase + "_FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS": 0,
            phase + "_FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES": batch_node_writes,
            phase + "_FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES":
                reserve_writes - batch_node_writes,
        }
        updates.update(self.skip_count_updates(
            phase, values["FREE_TREE_RESERVE_RETURN_CALLS"], 0,
        ))
        bfs.write_text(original, encoding="ascii")
        self.set_metrics(bfs, updates)
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v9_bounds_reclaim_passes_by_unsealed_commits(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v9()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        txn_commits = self.metric_value(contents, phase + "_TXN_COMMITS")
        sealed_commits = 1
        batch_calls = self.metric_value(
            contents, phase + "_FREE_TREE_RESERVE_RETURN_BATCH_CALLS",
        )
        self.assertGreater(sealed_commits, 0)
        passes = (txn_commits - sealed_commits) * 256 + 1
        return_calls = txn_commits - sealed_commits + passes
        self.assertGreater(return_calls, batch_calls)
        publications = txn_commits + passes
        self.set_metrics(bfs, {
            phase + "_SEALED_COMMITS": sealed_commits,
            phase + "_SEALED_METADATA_FENCES": sealed_commits,
            phase + "_FREE_TREE_RESERVE_RETURN_CALLS": return_calls,
            **self.skip_count_updates(phase, return_calls, batch_calls),
            phase + "_POST_PUBLISH_RECLAIM_PASSES": passes,
            phase + "_MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT": 256,
            phase + "_SUPERBLOCK_PUBLICATIONS": publications,
            phase + "_BIO_UPDATES": publications + sealed_commits + txn_commits,
        })
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
