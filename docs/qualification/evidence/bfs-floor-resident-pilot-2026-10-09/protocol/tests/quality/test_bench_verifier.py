# SPDX-License-Identifier: MPL-2.0
"""Positive and corrupt-input probes for the Amiga filesystem bench verifier."""

from pathlib import Path
import re
import shutil
import subprocess  # nosec B404 - invokes only the fixed repository verifier, without a shell
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/qualification/evidence/bfs-pfs3-deep-profile-2026-09-29"
VERIFIER = ROOT / "emulator-test/verify-bench-results.sh"
UINT32_MAX = 4294967295
UINT64_MAX = 18446744073709551615
CPU_SCOPES = (
    "PACKET", "PACKET_OPEN", "PACKET_READ", "PACKET_WRITE", "PACKET_END",
    "PACKET_DELETE", "PACKET_FLUSH", "PACKET_OTHER", "CORE_CREATE",
    "CORE_DELETE", "CORE_FILE_WRITE", "CORE_SYNC", "IFACE_FREE",
    "SEAL_COMMIT",
)
CPU_SCOPE_VALUES = {
    "PACKET": (35, 35, 350),
    "PACKET_OPEN": (2, 2, 20),
    "PACKET_READ": (3, 3, 30),
    "PACKET_WRITE": (4, 4, 40),
    "PACKET_END": (5, 5, 50),
    "PACKET_DELETE": (6, 6, 60),
    "PACKET_FLUSH": (7, 7, 70),
    "PACKET_OTHER": (8, 8, 80),
    "CORE_CREATE": (11, 11, 101),
    "CORE_DELETE": (0, 0, 0),
    "CORE_FILE_WRITE": (13, 13, 131),
    "CORE_SYNC": (17, 17, 170),
    "IFACE_FREE": (19, 19, 190),
    "SEAL_COMMIT": (23, 23, 230),
}
LISTING_V4_METRICS = (
    "LIST_EXNEXT_40_ENTRIES_FIRST_PASS_US",
    "LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXALL_40_ENTRIES_FIRST_PASS_US",
    "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXNEXT_400_ENTRIES_FIRST_PASS_US",
    "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXALL_400_ENTRIES_FIRST_PASS_US",
    "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXNEXT_1000_ENTRIES_FIRST_PASS_US",
    "LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL_US",
    "LIST_EXALL_1000_ENTRIES_FIRST_PASS_US",
    "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL_US",
)
DEEP_COMPARE_V12_PHASES = (
    "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40", "SEQ_WRITE_8M",
    "SEQ_READ_8M", "SMALL_DELETE_40", "APPEND_4K_1M", "APPEND_1K_256K",
    "APPEND_READ_1280K", "LIST_EXNEXT_40_ENTRIES_FIRST_PASS",
    "LIST_EXNEXT_40_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_40_ENTRIES_FIRST_PASS",
    "LIST_EXALL_40_ENTRIES_REPEAT10_TOTAL", "LIST_EXNEXT_400_ENTRIES_FIRST_PASS",
    "LIST_EXNEXT_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_400_ENTRIES_FIRST_PASS",
    "LIST_EXALL_400_ENTRIES_REPEAT10_TOTAL", "LIST_EXNEXT_1000_ENTRIES_FIRST_PASS",
    "LIST_EXNEXT_1000_ENTRIES_REPEAT10_TOTAL", "LIST_EXALL_1000_ENTRIES_FIRST_PASS",
    "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL",
)
LOOKUP_COUNTERS = (
    "INODE_READ_CALLS", "BTREE_SEARCH_CALLS", "BTREE_INDEX_HINT_HITS",
    "BTREE_LEAF_HINT_HITS", "CACHE_READ_CALLS", "CACHE_READ_HITS",
    "CACHE_READ_MISSES", "FREE_TREE_NODE_VIEWS", "FREE_TREE_RESIDENT_VIEWS",
    "DIR_TREE_NODE_VIEWS", "DIR_TREE_RESIDENT_VIEWS", "INODE_TREE_NODE_VIEWS",
    "INODE_TREE_RESIDENT_VIEWS", "REFCOUNT_TREE_NODE_VIEWS",
    "REFCOUNT_TREE_RESIDENT_VIEWS", "OTHER_TREE_NODE_VIEWS",
    "OTHER_TREE_RESIDENT_VIEWS",
)
NODE_LEVEL_COUNTERS = (
    "DIR_TREE_LEAF_NODE_VIEWS", "DIR_TREE_LEAF_RESIDENT_VIEWS",
    "DIR_TREE_INTERNAL_NODE_VIEWS", "DIR_TREE_INTERNAL_RESIDENT_VIEWS",
    "INODE_TREE_LEAF_NODE_VIEWS", "INODE_TREE_LEAF_RESIDENT_VIEWS",
    "INODE_TREE_INTERNAL_NODE_VIEWS", "INODE_TREE_INTERNAL_RESIDENT_VIEWS",
)
DETAIL_SCOPES = (
    "DETAIL_INODE_READ", "DETAIL_INODE_VALIDATE", "DETAIL_INODE_SEARCH",
    "DETAIL_INODE_NODE_VIEW", "DETAIL_DIR_NODE_VIEW",
    "DETAIL_INODE_BINARY_SEARCH", "DETAIL_DIR_BINARY_SEARCH",
    "DETAIL_NODE_STRUCTURE", "DETAIL_CACHE_PEEK", "DETAIL_BUFFER_ALLOC",
    "DETAIL_BUFFER_FREE", "DETAIL_EXALL_FILL",
)
DETAIL_SAMPLE_STRIDE = 17


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

    def upgrade_deep_compare_to_v11(self):
        self.upgrade_deep_compare_to_v10()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t10")
        upgraded = ["FS_DEEP_COMPARE\t11"]
        for line in original[1:]:
            if line.startswith("PASS\t"):
                upgraded.append(line)
                continue
            upgraded.append(line)
            name = line.split("\t", 1)[0]
            suffix = "_FREESPACE_ALLOC_SAMPLE_TICKS"
            if not name.endswith(suffix):
                continue
            phase = name[:-len(suffix)]
            # Contract-only synthetic values; these are not forecasts of
            # workload operation counts or success-only production behavior.
            for scope in CPU_SCOPES:
                calls, samples, ticks = CPU_SCOPE_VALUES[scope]
                upgraded.extend((
                    f"{phase}_{scope}_CALLS\t{calls}",
                    f"{phase}_{scope}_SAMPLES\t{samples}",
                    f"{phase}_{scope}_SAMPLE_TICKS\t{ticks}",
                ))
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t10")
        pfs_lines[0] = "FS_DEEP_COMPARE\t11"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v12(self):
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        original_bfs = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original_bfs[0], "FS_DEEP_COMPARE\t11")
        original_values = {
            name: value
            for row in original_bfs
            if "\t" in row
            for name, value in [row.split("\t")]
        }
        first_phase = DEEP_COMPARE_V12_PHASES[0]
        old_counters = [
            name[len(first_phase) + 1:]
            for name in original_values
            if name.startswith(first_phase + "_") and name != first_phase + "_US"
        ]
        # These are schema-shape fixtures, not measured workload predictions.
        # Keep new counters internally consistent and make each listing expose
        # at least its expected number of inode metadata reads.
        output_bfs = ["FS_DEEP_COMPARE\t12", "DRIVE\tDH1:"]
        for phase in DEEP_COMPARE_V12_PHASES:
            if phase in DEEP_COMPARE_V12_PHASES[:6]:
                output_bfs.append(f"{phase}_US\t{original_values[phase + '_US']}")
                output_bfs.extend(
                    f"{phase}_{counter}\t{original_values[phase + '_' + counter]}"
                    for counter in old_counters
                )
            else:
                output_bfs.append(f"{phase}_US\t100")
                output_bfs.extend(f"{phase}_{counter}\t0" for counter in old_counters)

            values = {
                "INODE_READ_CALLS": 0,
                "BTREE_SEARCH_CALLS": 8,
                "BTREE_INDEX_HINT_HITS": 2,
                "BTREE_LEAF_HINT_HITS": 1,
                "CACHE_READ_CALLS": 100,
                "CACHE_READ_HITS": 80,
                "CACHE_READ_MISSES": 20,
                "FREE_TREE_NODE_VIEWS": 0,
                "FREE_TREE_RESIDENT_VIEWS": 0,
                "DIR_TREE_NODE_VIEWS": 10,
                "DIR_TREE_RESIDENT_VIEWS": 8,
                "INODE_TREE_NODE_VIEWS": 20,
                "INODE_TREE_RESIDENT_VIEWS": 18,
                "REFCOUNT_TREE_NODE_VIEWS": 0,
                "REFCOUNT_TREE_RESIDENT_VIEWS": 0,
                "OTHER_TREE_NODE_VIEWS": 0,
                "OTHER_TREE_RESIDENT_VIEWS": 0,
            }
            if phase.startswith("LIST_"):
                if "_40_ENTRIES_" in phase:
                    expected = 40
                elif "_400_ENTRIES_" in phase:
                    expected = 400
                else:
                    expected = 1000
                if phase.endswith("REPEAT10_TOTAL"):
                    expected *= 10
                values["INODE_READ_CALLS"] = expected
            output_bfs.extend(
                f"{phase}_{counter}\t{values[counter]}"
                for counter in LOOKUP_COUNTERS
            )

        output_bfs.extend(
            line for line in original_bfs
            if line.startswith(("CLOCK_HZ\t", "CRC_SAMPLE_STRIDE\t",
                                "CPU_SAMPLE_STRIDE\t", "PASS\t"))
        )
        bfs.write_text("\n".join(output_bfs) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        original_pfs = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(original_pfs[0], "FS_DEEP_COMPARE\t11")
        original_values = {
            name: value
            for row in original_pfs
            if "\t" in row
            for name, value in [row.split("\t")]
        }
        output_pfs = ["FS_DEEP_COMPARE\t12", "DRIVE\tDH2:"]
        for phase in DEEP_COMPARE_V12_PHASES:
            value = original_values.get(phase + "_US", "100")
            output_pfs.append(f"{phase}_US\t{value}")
        output_pfs.append("PASS\t1")
        pfs3.write_text("\n".join(output_pfs) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v13(self):
        self.upgrade_deep_compare_to_v12()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t12")
        values = {
            name: value
            for row in original
            if "\t" in row
            for name, value in [row.split("\t")]
        }
        phase_values = {}
        for phase in DEEP_COMPARE_V12_PHASES:
            phase_values[phase] = {
                name[len(phase) + 1:]: value
                for name, value in values.items()
                if name.startswith(phase + "_")
            }
            phase_data = phase_values[phase]
            inode_reads = int(phase_data["INODE_READ_CALLS"])
            if phase.startswith("LIST_"):
                # Listing search calls are one per inode read in this synthetic
                # profile, so the corresponding B-tree total must be large enough.
                phase_data["BTREE_SEARCH_CALLS"] = str(max(
                    int(phase_data["BTREE_SEARCH_CALLS"]), inode_reads,
                ))

        output = ["FS_DEEP_COMPARE\t13", "DRIVE\tDH1:"]
        current_phase = None
        for line in original[2:]:
            name, value = line.split("\t")
            if name == "PASS":
                output.append(line)
                continue
            if name in ("CLOCK_HZ", "CRC_SAMPLE_STRIDE", "CPU_SAMPLE_STRIDE"):
                output.append(line)
                if name == "CPU_SAMPLE_STRIDE":
                    output.append(f"DETAIL_SAMPLE_STRIDE\t{DETAIL_SAMPLE_STRIDE}")
                continue
            if name.endswith("_US") and not any(
                name.startswith(phase + "_") for phase in DEEP_COMPARE_V12_PHASES
            ):
                output.append(line)
                continue
            phase = next((candidate for candidate in DEEP_COMPARE_V12_PHASES
                          if name.startswith(candidate + "_")), None)
            if phase is None:
                self.fail(f"unexpected schema-12 metric: {name}")
            current_phase = phase
            phase_data = phase_values[phase]
            suffix = name[len(phase) + 1:]
            output.append(f"{name}\t{phase_data.get(suffix, value)}")
            if suffix != "OTHER_TREE_RESIDENT_VIEWS":
                continue

            inode_reads = int(phase_data["INODE_READ_CALLS"])
            inode_views = int(phase_data["INODE_TREE_NODE_VIEWS"])
            dir_views = int(phase_data["DIR_TREE_NODE_VIEWS"])
            searches = int(phase_data["BTREE_SEARCH_CALLS"])
            if phase.startswith("LIST_"):
                inode_validate = inode_reads
                inode_search = inode_reads
                exall_fill = inode_reads + 1 if phase.startswith("LIST_EXALL_") else 0
            else:
                inode_validate = inode_reads + (4 if any(
                    word in phase for word in ("CREATE", "WRITE", "DELETE", "APPEND")
                ) else 0)
                inode_search = min(searches, 2)
                exall_fill = 0
            detail_calls = {
                "DETAIL_INODE_READ": inode_reads,
                "DETAIL_INODE_VALIDATE": inode_validate,
                "DETAIL_INODE_SEARCH": inode_search,
                "DETAIL_INODE_NODE_VIEW": inode_views,
                "DETAIL_DIR_NODE_VIEW": dir_views,
                "DETAIL_INODE_BINARY_SEARCH": min(searches, 2),
                "DETAIL_DIR_BINARY_SEARCH": min(searches, 1),
                "DETAIL_NODE_STRUCTURE": inode_views + dir_views,
                "DETAIL_CACHE_PEEK": int(phase_data["CACHE_READ_CALLS"]),
                "DETAIL_BUFFER_ALLOC": 1 if any(
                    word in phase for word in ("CREATE", "WRITE", "APPEND")
                ) else 0,
                "DETAIL_BUFFER_FREE": 1 if any(
                    word in phase for word in ("DELETE", "WRITE", "APPEND")
                ) else 0,
                "DETAIL_EXALL_FILL": exall_fill,
            }
            for scope in DETAIL_SCOPES:
                calls = detail_calls[scope]
                samples = calls // DETAIL_SAMPLE_STRIDE
                # Zero ticks with nonzero samples is valid; sampling contributes
                # no timing lower bound, and scopes are inclusive/nested.
                output.extend((
                    f"{phase}_{scope}_CALLS\t{calls}",
                    f"{phase}_{scope}_SAMPLES\t{samples}",
                    f"{phase}_{scope}_SAMPLE_TICKS\t0",
                ))
        bfs.write_text("\n".join(output) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t12")
        pfs_lines[0] = "FS_DEEP_COMPARE\t13"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v14(self):
        self.upgrade_deep_compare_to_v13()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t13")
        values = {
            name: value
            for row in original
            if "\t" in row
            for name, value in [row.split("\t")]
        }
        phase_values = {}
        deferred_writes = {}
        for phase in DEEP_COMPARE_V12_PHASES:
            phase_data = {
                name[len(phase) + 1:]: value
                for name, value in values.items()
                if name.startswith(phase + "_")
            }
            phase_values[phase] = phase_data
            known_tree_writes = sum(int(phase_data[name]) for name in (
                "FREE_TREE_NODE_WRITES", "DIR_TREE_NODE_WRITES",
                "INODE_TREE_NODE_WRITES", "REFCOUNT_TREE_NODE_WRITES",
                "OTHER_TREE_NODE_WRITES",
            ))
            known_without_other = sum(int(phase_data[name]) for name in (
                "FREE_TREE_NODE_WRITES", "DIR_TREE_NODE_WRITES",
                "INODE_TREE_NODE_WRITES", "REFCOUNT_TREE_NODE_WRITES",
            ))
            node_writes = max(int(phase_data["NODE_WRITES"]), known_tree_writes)
            phase_data["NODE_WRITES"] = str(node_writes)
            phase_data["OTHER_TREE_NODE_WRITES"] = str(
                node_writes - known_without_other
            )
            deferred_writes[phase] = (
                0 if phase.startswith("LIST_") else min(node_writes, 2)
            )

        output = []
        for line in original:
            if line == "FS_DEEP_COMPARE\t13":
                output.append("FS_DEEP_COMPARE\t14")
                continue
            if line == "PASS\t1" or line.split("\t", 1)[0] in (
                "CLOCK_HZ", "CRC_SAMPLE_STRIDE", "CPU_SAMPLE_STRIDE",
                "DETAIL_SAMPLE_STRIDE",
            ):
                output.append(line)
                continue
            name, value = line.split("\t")
            phase = next((candidate for candidate in DEEP_COMPARE_V12_PHASES
                          if name.startswith(candidate + "_")), None)
            if phase is None:
                output.append(line)
                continue
            suffix = name[len(phase) + 1:]
            value = phase_values[phase].get(suffix, value)
            output.append(f"{name}\t{value}")
            if suffix == "DETAIL_EXALL_FILL_SAMPLE_TICKS":
                output.append(
                    f"{phase}_DEFERRED_NODE_WRITES\t{deferred_writes[phase]}"
                )
        bfs.write_text("\n".join(output) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t13")
        pfs_lines[0] = "FS_DEEP_COMPARE\t14"
        pfs3.write_text("\n".join(pfs_lines) + "\n", encoding="ascii")

    def upgrade_deep_compare_to_v15(self):
        self.upgrade_deep_compare_to_v14()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii").splitlines()
        self.assertEqual(original[0], "FS_DEEP_COMPARE\t14")
        values = {
            name: value
            for row in original
            if "\t" in row
            for name, value in [row.split("\t")]
        }
        upgraded = ["FS_DEEP_COMPARE\t15"]
        lookup_suffix = "_OTHER_TREE_RESIDENT_VIEWS"
        for line in original[1:]:
            upgraded.append(line)
            if "\t" not in line:
                continue
            name, _ = line.split("\t")
            if not name.endswith(lookup_suffix):
                continue
            phase = name[:-len(lookup_suffix)]
            if phase not in DEEP_COMPARE_V12_PHASES:
                continue
            for tree in ("DIR_TREE", "INODE_TREE"):
                aggregate_views = int(values[f"{phase}_{tree}_NODE_VIEWS"])
                aggregate_resident = int(values[f"{phase}_{tree}_RESIDENT_VIEWS"])
                internal_views = min(1, aggregate_views)
                internal_resident = min(1, aggregate_resident)
                upgraded.extend((
                    f"{phase}_{tree}_LEAF_NODE_VIEWS\t{aggregate_views - internal_views}",
                    f"{phase}_{tree}_LEAF_RESIDENT_VIEWS\t{aggregate_resident - internal_resident}",
                    f"{phase}_{tree}_INTERNAL_NODE_VIEWS\t{internal_views}",
                    f"{phase}_{tree}_INTERNAL_RESIDENT_VIEWS\t{internal_resident}",
                ))
        bfs.write_text("\n".join(upgraded) + "\n", encoding="ascii")

        pfs3 = self.results / "pfs3.deep-compare.tsv"
        pfs_lines = pfs3.read_text(encoding="ascii").splitlines()
        self.assertEqual(pfs_lines[0], "FS_DEEP_COMPARE\t14")
        pfs_lines[0] = "FS_DEEP_COMPARE\t15"
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

    def load_durable_evidence(self, directory, header="FS_DURABLE_COMPARE\t1",
                              marker="BFS-PFS3-DURABLE-COMPLETE",
                              suffix="durable.tsv"):
        source = EVIDENCE / directory
        shutil.copyfile(source / "info-after-format.txt",
                        self.results / "info-after-format.txt")
        (self.results / "complete.txt").write_text(marker + "\n", encoding="ascii")
        for filesystem in ("bfs", "pfs3"):
            lines = (source / f"{filesystem}.tsv").read_text(
                encoding="ascii").splitlines()
            self.assertEqual(lines[0], "FS_COMPARE_BENCH\t1")
            lines[0] = header
            (self.results / f"{filesystem}.{suffix}").write_text(
                "\n".join(lines) + "\n", encoding="ascii")

    def test_durable_compare_evidence_passes(self):
        for directory in ("compare-bfs-first", "compare-pfs3-first"):
            with self.subTest(directory=directory):
                self.load_durable_evidence(directory)
                result = self.verify("durable-compare")
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_durable_compare_rejects_plain_compare_schema(self):
        self.load_durable_evidence("compare-bfs-first", header="FS_COMPARE_BENCH\t1")
        self.assertNotEqual(self.verify("durable-compare").returncode, 0)

    def test_durable_compare_rejects_plain_compare_marker(self):
        self.load_durable_evidence("compare-bfs-first", marker="BFS-PFS3-COMPLETE")
        self.assertNotEqual(self.verify("durable-compare").returncode, 0)

    def test_compare_rejects_durable_schema(self):
        self.load_durable_evidence("compare-bfs-first", marker="BFS-PFS3-COMPLETE",
                                   suffix="tsv")
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def upgrade_compare_to_v2(self, suffix="tsv", header="FS_COMPARE_BENCH",
                              filesystems=("bfs", "pfs3")):
        for filesystem in filesystems:
            path = self.results / f"{filesystem}.{suffix}"
            lines = path.read_text(encoding="ascii").splitlines()
            self.assertEqual(lines[0], f"{header}\t1")
            lines[0] = f"{header}\t2"
            position = next(index for index, line in enumerate(lines)
                            if line.startswith("SMALL_READ_40_US\t")) + 1
            lines[position:position] = ["LIST_EXNEXT_400_US\t7000", "LIST_EXALL_400_US\t3000"]
            path.write_text("\n".join(lines) + "\n", encoding="ascii")

    def test_compare_v2_accepts_listing_phases(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v2()
        result = self.verify("compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_durable_compare_v2_accepts_listing_phases(self):
        self.load_durable_evidence("compare-bfs-first")
        self.upgrade_compare_to_v2(suffix="durable.tsv", header="FS_DURABLE_COMPARE")
        result = self.verify("durable-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compare_v2_rejects_mixed_versions_and_missing_listing(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v2(filesystems=("bfs",))
        self.assertNotEqual(self.verify("compare").returncode, 0)

        self.load_evidence("compare-bfs-first", "tsv")
        for filesystem in ("bfs", "pfs3"):
            path = self.results / f"{filesystem}.tsv"
            lines = path.read_text(encoding="ascii").splitlines()
            lines[0] = "FS_COMPARE_BENCH\t2"
            path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v2()
        path = self.results / "pfs3.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        lines = [line for line in lines if not line.startswith("LIST_EXALL_400_US")]
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def upgrade_compare_to_v3(self, suffix="tsv", header="FS_COMPARE_BENCH",
                              filesystems=("bfs", "pfs3")):
        self.upgrade_compare_to_v2(suffix, header, filesystems)
        for filesystem in filesystems:
            path = self.results / f"{filesystem}.{suffix}"
            lines = path.read_text(encoding="ascii").splitlines()
            lines[0] = f"{header}\t3"
            position = next(index for index, line in enumerate(lines)
                            if line.startswith("SMALL_DELETE_40_US\t")) + 1
            lines[position:position] = ["APPEND_4K_1M_US\t30000",
                                        "APPEND_1K_256K_US\t20000",
                                        "APPEND_READ_1280K_US\t40000"]
            path.write_text("\n".join(lines) + "\n", encoding="ascii")

    def test_compare_v3_accepts_append_phases(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v3()
        result = self.verify("compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_durable_compare_v3_accepts_append_phases(self):
        self.load_durable_evidence("compare-bfs-first")
        self.upgrade_compare_to_v3(suffix="durable.tsv", header="FS_DURABLE_COMPARE")
        result = self.verify("durable-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compare_v3_rejects_mixed_versions_and_missing_or_reordered_append(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v3(filesystems=("bfs",))
        self.upgrade_compare_to_v2(filesystems=("pfs3",))
        self.assertNotEqual(self.verify("compare").returncode, 0)

        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v2()
        for filesystem in ("bfs", "pfs3"):
            path = self.results / f"{filesystem}.tsv"
            lines = path.read_text(encoding="ascii").splitlines()
            lines[0] = "FS_COMPARE_BENCH\t3"
            path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v3()
        path = self.results / "bfs.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        lines = [line for line in lines if not line.startswith("APPEND_1K_256K_US")]
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v3()
        path = self.results / "pfs3.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        first = next(index for index, line in enumerate(lines)
                     if line.startswith("APPEND_4K_1M_US\t"))
        lines[first], lines[first + 1] = lines[first + 1], lines[first]
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def append_compare_v4_metrics(self, suffix="tsv", header="FS_COMPARE_BENCH",
                                  filesystems=("bfs", "pfs3")):
        for filesystem in filesystems:
            path = self.results / f"{filesystem}.{suffix}"
            lines = path.read_text(encoding="ascii").splitlines()
            self.assertEqual(lines[0], f"{header}\t3")
            lines[0] = f"{header}\t4"
            position = next(index for index, line in enumerate(lines)
                            if line.startswith("PASS\t"))
            lines[position:position] = [
                f"{metric}\t{1000 + index}"
                for index, metric in enumerate(LISTING_V4_METRICS)
            ]
            path.write_text("\n".join(lines) + "\n", encoding="ascii")

    def upgrade_compare_to_v4(self, suffix="tsv", header="FS_COMPARE_BENCH",
                              filesystems=("bfs", "pfs3")):
        self.upgrade_compare_to_v3(suffix, header, filesystems)
        self.append_compare_v4_metrics(suffix, header, filesystems)

    def test_compare_v4_accepts_listing_scaling_phases(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v4()
        result = self.verify("compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_durable_compare_v4_accepts_listing_scaling_phases(self):
        self.load_durable_evidence("compare-bfs-first")
        self.upgrade_compare_to_v4(suffix="durable.tsv",
                                   header="FS_DURABLE_COMPARE")
        result = self.verify("durable-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compare_v4_rejects_missing_listing_metric(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v4()
        path = self.results / "pfs3.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        lines = [line for line in lines
                 if not line.startswith("LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL_US\t")]
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def test_compare_v4_rejects_reordered_listing_metrics(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v4()
        path = self.results / "bfs.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        first = next(index for index, line in enumerate(lines)
                     if line.startswith(LISTING_V4_METRICS[0] + "\t"))
        lines[first], lines[first + 1] = lines[first + 1], lines[first]
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def test_compare_v4_rejects_mixed_schema_versions(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v3()
        self.append_compare_v4_metrics(filesystems=("bfs",))
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def test_compare_v4_rejects_mislabeled_listing_metric(self):
        self.load_evidence("compare-bfs-first", "tsv")
        self.upgrade_compare_to_v4()
        path = self.results / "bfs.tsv"
        lines = path.read_text(encoding="ascii").splitlines()
        metric = LISTING_V4_METRICS[8]
        index = next(index for index, line in enumerate(lines)
                     if line.startswith(metric + "\t"))
        lines[index] = lines[index].replace("FIRST_PASS", "COLD_PASS")
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

    def test_compare_rejects_unknown_schema_version(self):
        self.load_evidence("compare-bfs-first", "tsv")
        for filesystem in ("bfs", "pfs3"):
            path = self.results / f"{filesystem}.tsv"
            lines = path.read_text(encoding="ascii").splitlines()
            lines[0] = "FS_COMPARE_BENCH\t5"
            path.write_text("\n".join(lines) + "\n", encoding="ascii")
        self.assertNotEqual(self.verify("compare").returncode, 0)

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

    def test_deep_compare_v11_accepts_inclusive_cpu_scopes_and_packet_partition(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        self.assertIn("FS_DEEP_COMPARE\t11", contents)
        self.assertIn("CPU_SAMPLE_STRIDE\t1", contents)
        for phase in (
            "SMALL_CREATE_40", "LOOKUP_400", "SMALL_READ_40",
            "SEQ_WRITE_8M", "SEQ_READ_8M", "SMALL_DELETE_40",
        ):
            rows = [
                line for line in contents.splitlines()
                if line.startswith(phase + "_")
            ]
            cpu_rows = [
                line for line in rows
                if any(line.startswith(phase + "_" + scope + "_")
                       for scope in CPU_SCOPES)
            ]
            self.assertEqual(len(cpu_rows), len(CPU_SCOPES) * 3)
            for scope, expected in CPU_SCOPE_VALUES.items():
                self.assertEqual(
                    tuple(self.metric_value(
                        contents, f"{phase}_{scope}_{suffix}",
                    ) for suffix in ("CALLS", "SAMPLES", "SAMPLE_TICKS")),
                    expected,
                )
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v12_accepts_append_and_listing_probe_profile(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v12()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        self.assertIn("FS_DEEP_COMPARE\t12", contents)
        # Keep this fixture above Linux's single argv-string ceiling: passing
        # the metric inventory as one awk -v argument must not regress.
        inventory = " ".join(line.split("\t", 1)[0]
                             for line in contents.splitlines()[2:-1])
        self.assertGreater(len(inventory.encode("ascii")), 131072)
        for phase in DEEP_COMPARE_V12_PHASES:
            for counter in LOOKUP_COUNTERS:
                self.assertIn(f"{phase}_{counter}\t", contents)
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_detail_scope_order_matches_probe_header_macro(self):
        header = (ROOT / "src/amiga/perf_probe.h").read_text(encoding="ascii")
        lines = header.splitlines()
        macro_start = next((index for index, line in enumerate(lines)
                            if line.startswith("#define BFS_PERF_DETAIL_SCOPES(X)")), None)
        self.assertIsNotNone(macro_start, "detail scope macro is missing")
        macro_lines = [lines[macro_start]]
        while macro_lines[-1].rstrip().endswith("\\"):
            macro_lines.append(lines[macro_start + len(macro_lines)])
        header_scopes = tuple(re.findall(
            r"X\(\s*(DETAIL_[A-Z0-9_]+)\s*,", "\n".join(macro_lines),
        ))
        self.assertEqual(header_scopes, DETAIL_SCOPES)

        verifier = VERIFIER.read_text(encoding="ascii")
        match = re.search(
            r"deep_compare_detail_scopes=\(([^)]*)\)", verifier,
        )
        self.assertIsNotNone(match, "verifier detail scope inventory is missing")
        self.assertEqual(tuple(match.group(1).split()), header_scopes)

    def test_deep_compare_node_level_counter_order_matches_probe_header_macro(self):
        header = (ROOT / "src/amiga/perf_probe.h").read_text(encoding="ascii")
        lines = header.splitlines()
        macro_start = next((index for index, line in enumerate(lines)
                            if line.startswith("#define BFS_PERF_NODE_LEVEL_COUNTERS(X)")), None)
        self.assertIsNotNone(macro_start, "node-level counter macro is missing")
        macro_lines = [lines[macro_start]]
        while macro_lines[-1].rstrip().endswith("\\"):
            macro_lines.append(lines[macro_start + len(macro_lines)])
        header_counters = tuple(re.findall(
            r"X\(\s*([A-Z0-9_]+)\s*,", "\n".join(macro_lines),
        ))
        self.assertEqual(header_counters, NODE_LEVEL_COUNTERS)

        verifier = VERIFIER.read_text(encoding="ascii")
        match = re.search(
            r"deep_compare_node_level_counters=\(([^)]*)\)", verifier,
        )
        self.assertIsNotNone(match, "verifier node-level counter inventory is missing")
        self.assertEqual(tuple(match.group(1).split()), header_counters)

    def test_deep_compare_v13_accepts_detail_sampling_profile(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v13()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        self.assertIn("FS_DEEP_COMPARE\t13", contents)
        self.assertIn("CPU_SAMPLE_STRIDE\t1\nDETAIL_SAMPLE_STRIDE\t17\n", contents)
        inventory = " ".join(line.split("\t", 1)[0]
                             for line in contents.splitlines()[2:-1])
        self.assertGreater(len(inventory.encode("ascii")), 131072)
        for phase in DEEP_COMPARE_V12_PHASES:
            for scope in DETAIL_SCOPES:
                for suffix in ("CALLS", "SAMPLES", "SAMPLE_TICKS"):
                    self.assertIn(f"{phase}_{scope}_{suffix}\t", contents)
        # Inclusive scopes have no partition or packet-duration sum rule.
        self.set_metrics(bfs, {
            "SMALL_CREATE_40_DETAIL_CACHE_PEEK_SAMPLE_TICKS": UINT64_MAX,
        })
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v13_rejects_missing_reordered_and_mixed_rows(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v13()
        bfs = self.results / "bfs.deep-compare.tsv"
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        original_bfs = bfs.read_text(encoding="ascii")
        first_row = "SMALL_CREATE_40_DETAIL_INODE_READ_CALLS\t"
        lines = original_bfs.splitlines()
        row_index = next(index for index, line in enumerate(lines)
                         if line.startswith(first_row))
        missing = "\n".join(lines[:row_index] + lines[row_index + 1:]) + "\n"
        lines[row_index], lines[row_index + 1] = lines[row_index + 1], lines[row_index]
        reordered = "\n".join(lines) + "\n"
        cases = (
            ("missing detail row", missing),
            ("reordered detail rows", reordered),
            ("duplicate detail row", original_bfs.replace(
                "PASS\t1\n", lines[row_index] + "\nPASS\t1\n", 1,
            )),
        )
        for label, mutated in cases:
            with self.subTest(shape=label):
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        bfs.write_text(original_bfs, encoding="ascii")
        pfs_original = pfs3.read_text(encoding="ascii")
        pfs3.write_text(pfs_original.replace(
            "FS_DEEP_COMPARE\t13\n", "FS_DEEP_COMPARE\t12\n", 1,
        ), encoding="ascii")
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v13_checks_stride_and_unsigned_detail_bounds(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v13()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("global detail stride", {
                "DETAIL_SAMPLE_STRIDE": 16,
            }),
            ("sample floor", {
                "SMALL_CREATE_40_DETAIL_CACHE_PEEK_SAMPLES": 4,
            }),
            ("ticks with zero samples", {
                "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_SAMPLE_TICKS": 1,
            }),
            ("ULONG calls overflow", {
                "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_CALLS": UINT32_MAX + 1,
                "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_SAMPLES": (UINT32_MAX + 1) // DETAIL_SAMPLE_STRIDE,
            }),
            ("ULONG samples overflow", {
                "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_SAMPLES": UINT32_MAX + 1,
            }),
            ("uint64 ticks overflow", {
                "SMALL_CREATE_40_DETAIL_CACHE_PEEK_SAMPLE_TICKS": UINT64_MAX + 1,
            }),
        )
        for label, updates in cases:
            with self.subTest(bound=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        # Upper ULONG call count and UINT64 ticks remain exact; a nonzero sample
        # count is allowed to have zero ticks.
        bfs.write_text(original, encoding="ascii")
        self.set_metrics(bfs, {
            "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_CALLS": UINT32_MAX,
            "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_SAMPLES": UINT32_MAX // DETAIL_SAMPLE_STRIDE,
            "SMALL_CREATE_40_DETAIL_BUFFER_ALLOC_SAMPLE_TICKS": UINT64_MAX,
        })
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v13_rejects_detail_accounting_errors(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v13()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        exall = "LIST_EXALL_400_ENTRIES_FIRST_PASS"
        exnext = "LIST_EXNEXT_400_ENTRIES_FIRST_PASS"
        read_calls = self.metric_value(original, exall + "_INODE_READ_CALLS")
        btree_searches = self.metric_value(original, "SMALL_CREATE_40_BTREE_SEARCH_CALLS")
        cases = (
            ("inode read wrapper identity", {
                "SMALL_CREATE_40_DETAIL_INODE_READ_CALLS": 1,
                "SMALL_CREATE_40_DETAIL_INODE_READ_SAMPLES": 0,
            }),
            ("inode search bounded by B-tree searches", {
                "SMALL_CREATE_40_DETAIL_INODE_SEARCH_CALLS": btree_searches + 1,
                "SMALL_CREATE_40_DETAIL_INODE_SEARCH_SAMPLES": (btree_searches + 1) // DETAIL_SAMPLE_STRIDE,
            }),
            ("inode node view identity", {
                "SMALL_CREATE_40_DETAIL_INODE_NODE_VIEW_CALLS": 21,
                "SMALL_CREATE_40_DETAIL_INODE_NODE_VIEW_SAMPLES": 1,
            }),
            ("directory node view identity", {
                "SMALL_CREATE_40_DETAIL_DIR_NODE_VIEW_CALLS": 11,
                "SMALL_CREATE_40_DETAIL_DIR_NODE_VIEW_SAMPLES": 0,
            }),
            ("listing validate identity", {
                f"{exall}_DETAIL_INODE_VALIDATE_CALLS": read_calls + 1,
                f"{exall}_DETAIL_INODE_VALIDATE_SAMPLES": (read_calls + 1) // DETAIL_SAMPLE_STRIDE,
            }),
            ("listing search identity", {
                f"{exall}_DETAIL_INODE_SEARCH_CALLS": read_calls - 1,
                f"{exall}_DETAIL_INODE_SEARCH_SAMPLES": (read_calls - 1) // DETAIL_SAMPLE_STRIDE,
            }),
            ("ExAll fill covers inode reads", {
                f"{exall}_DETAIL_EXALL_FILL_CALLS": read_calls - 1,
                f"{exall}_DETAIL_EXALL_FILL_SAMPLES": (read_calls - 1) // DETAIL_SAMPLE_STRIDE,
            }),
            ("ExNext has no ExAll fill", {
                f"{exnext}_DETAIL_EXALL_FILL_CALLS": 1,
                f"{exnext}_DETAIL_EXALL_FILL_SAMPLES": 0,
            }),
        )
        for label, updates in cases:
            with self.subTest(accounting=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v14_accepts_deferred_node_write_accounting(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v14()
        bfs = self.results / "bfs.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        self.assertIn("FS_DEEP_COMPARE\t14", contents)
        self.assertIn("CPU_SAMPLE_STRIDE\t1\nDETAIL_SAMPLE_STRIDE\t17\n", contents)
        self.assertGreater(len(contents.encode("ascii")), 131072)

        deferred_total = 0
        for phase in DEEP_COMPARE_V12_PHASES:
            rows = contents.splitlines()
            deferred_index = next(index for index, line in enumerate(rows)
                                  if line.startswith(phase + "_DEFERRED_NODE_WRITES\t"))
            self.assertEqual(
                rows[deferred_index - 1].split("\t", 1)[0],
                phase + "_DETAIL_EXALL_FILL_SAMPLE_TICKS",
            )
            deferred = self.metric_value(
                contents, phase + "_DEFERRED_NODE_WRITES",
            )
            node_writes = self.metric_value(contents, phase + "_NODE_WRITES")
            tree_writes = sum(self.metric_value(contents, phase + "_" + counter)
                              for counter in (
                                  "FREE_TREE_NODE_WRITES", "DIR_TREE_NODE_WRITES",
                                  "INODE_TREE_NODE_WRITES", "REFCOUNT_TREE_NODE_WRITES",
                                  "OTHER_TREE_NODE_WRITES",
                              ))
            self.assertLessEqual(deferred, node_writes)
            self.assertEqual(tree_writes, node_writes)
            if phase.startswith("LIST_"):
                self.assertEqual(deferred, 0)
            deferred_total += deferred
        self.assertGreater(deferred_total, 0)
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v14_rejects_missing_reordered_and_mixed_rows(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v14()
        bfs = self.results / "bfs.deep-compare.tsv"
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        lines = original.splitlines()
        target = "SMALL_CREATE_40_DEFERRED_NODE_WRITES\t"
        deferred_index = next(index for index, line in enumerate(lines)
                              if line.startswith(target))
        missing = "\n".join(lines[:deferred_index] + lines[deferred_index + 1:]) + "\n"
        reordered_lines = lines.copy()
        reordered_lines[deferred_index - 1], reordered_lines[deferred_index] = (
            reordered_lines[deferred_index], reordered_lines[deferred_index - 1],
        )
        reordered = "\n".join(reordered_lines) + "\n"
        for label, mutated in (("missing deferred counter", missing),
                               ("deferred counter order", reordered)):
            with self.subTest(shape=label):
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        bfs.write_text(original, encoding="ascii")
        pfs_original = pfs3.read_text(encoding="ascii")
        pfs3.write_text(pfs_original.replace(
            "FS_DEEP_COMPARE\t14\n", "FS_DEEP_COMPARE\t13\n", 1,
        ), encoding="ascii")
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v14_rejects_seal_and_deferred_write_accounting_errors(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v14()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        create_phase = "SMALL_CREATE_40"
        current_other_writes = self.metric_value(
            original, create_phase + "_OTHER_TREE_NODE_WRITES",
        )
        append_read = "APPEND_READ_1280K"
        cases = (
            ("deferred writes exceed node writes", {
                create_phase + "_DEFERRED_NODE_WRITES": self.metric_value(
                    original, create_phase + "_NODE_WRITES",
                ) + 1,
            }),
            ("tree classes do not sum to node writes", {
                create_phase + "_OTHER_TREE_NODE_WRITES": current_other_writes + 1,
            }),
            ("listing deferred writes", {
                "LIST_EXNEXT_400_ENTRIES_FIRST_PASS_DEFERRED_NODE_WRITES": 1,
            }),
            # This schema-9 identity remains strict: a sealed commit must have
            # a classified free-tree write, even when other tree writes are deferred.
            ("sealed commit without free-tree write", {
                append_read + "_TXN_COMMITS": 1,
                append_read + "_SEALED_COMMITS": 1,
                append_read + "_SEALED_METADATA_FENCES": 1,
                append_read + "_BIO_UPDATES": 3,
                append_read + "_NODE_WRITES": 4,
                append_read + "_OTHER_TREE_NODE_WRITES": 4,
                append_read + "_DEFERRED_NODE_WRITES": 4,
                append_read + "_POST_PUBLISH_RECLAIM_PASSES": 0,
                append_read + "_MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT": 0,
                append_read + "_SUPERBLOCK_PUBLICATIONS": 1,
            }),
        )
        for label, updates in cases:
            with self.subTest(invariant=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v15_accepts_node_level_partitions(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v15()
        bfs = self.results / "bfs.deep-compare.tsv"
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        contents = bfs.read_text(encoding="ascii")
        self.assertIn("FS_DEEP_COMPARE\t15\n", contents)
        self.assertGreater(len(contents.encode("ascii")), 131072)

        for phase in DEEP_COMPARE_V12_PHASES:
            lines = contents.splitlines()
            lookup_index = next(index for index, line in enumerate(lines)
                                if line.startswith(phase + "_OTHER_TREE_RESIDENT_VIEWS\t"))
            self.assertEqual(
                tuple(line.split("\t", 1)[0] for line in
                      lines[lookup_index + 1:lookup_index + 1 + len(NODE_LEVEL_COUNTERS)]),
                tuple(phase + "_" + counter for counter in NODE_LEVEL_COUNTERS),
            )
        pfs_contents = pfs3.read_text(encoding="ascii")
        self.assertIn("FS_DEEP_COMPARE\t15\n", pfs_contents)
        self.assertNotIn("_LEAF_NODE_VIEWS\t", pfs_contents)
        self.assertNotIn("_INTERNAL_NODE_VIEWS\t", pfs_contents)
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v15_rejects_missing_reordered_and_mixed_schema_rows(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v15()
        bfs = self.results / "bfs.deep-compare.tsv"
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        lines = original.splitlines()
        first_prefix = "SMALL_CREATE_40_DIR_TREE_LEAF_NODE_VIEWS\t"
        first_index = next(index for index, line in enumerate(lines)
                           if line.startswith(first_prefix))
        missing = "\n".join(lines[:first_index] + lines[first_index + 1:]) + "\n"
        reordered_lines = lines.copy()
        reordered_lines[first_index], reordered_lines[first_index + 1] = (
            reordered_lines[first_index + 1], reordered_lines[first_index],
        )
        reordered = "\n".join(reordered_lines) + "\n"
        for shape, mutated in (("missing node-level counter", missing),
                               ("reordered node-level counters", reordered)):
            with self.subTest(shape=shape):
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        bfs.write_text(original, encoding="ascii")
        pfs_original = pfs3.read_text(encoding="ascii")
        pfs3.write_text(pfs_original.replace(
            "FS_DEEP_COMPARE\t15\n", "FS_DEEP_COMPARE\t14\n", 1,
        ), encoding="ascii")
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v15_rejects_partition_and_resident_bounds_errors(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v15()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        phase = "SMALL_CREATE_40"
        updates = (
            ("DIR views do not partition", {
                phase + "_DIR_TREE_LEAF_NODE_VIEWS": self.metric_value(
                    original, phase + "_DIR_TREE_LEAF_NODE_VIEWS",
                ) + 1,
            }),
            ("INODE resident views do not partition", {
                phase + "_INODE_TREE_LEAF_RESIDENT_VIEWS": self.metric_value(
                    original, phase + "_INODE_TREE_LEAF_RESIDENT_VIEWS",
                ) + 1,
            }),
        )
        for label, metric_updates in updates:
            with self.subTest(invariant=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, metric_updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

        for tree in ("DIR_TREE", "INODE_TREE"):
            aggregate_views = self.metric_value(
                original, phase + "_" + tree + "_NODE_VIEWS",
            )
            aggregate_resident = self.metric_value(
                original, phase + "_" + tree + "_RESIDENT_VIEWS",
            )
            self.assertGreater(aggregate_resident, 0)
            for level, violating_counter, other_counter in (
                ("leaf", "LEAF", "INTERNAL"),
                ("internal", "INTERNAL", "LEAF"),
            ):
                with self.subTest(tree=tree, level=level):
                    bfs.write_text(original, encoding="ascii")
                    self.set_metrics(bfs, {
                        phase + f"_{tree}_{violating_counter}_NODE_VIEWS": 0,
                        phase + f"_{tree}_{other_counter}_NODE_VIEWS": aggregate_views,
                        phase + f"_{tree}_{violating_counter}_RESIDENT_VIEWS": aggregate_resident,
                        phase + f"_{tree}_{other_counter}_RESIDENT_VIEWS": 0,
                    })
                    self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v12_rejects_missing_reordered_and_mixed_rows(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v12()
        bfs = self.results / "bfs.deep-compare.tsv"
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        original_bfs = bfs.read_text(encoding="ascii")
        original_pfs3 = pfs3.read_text(encoding="ascii")
        missing_row = "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL_CACHE_READ_MISSES\t20\n"
        self.assertIn(missing_row, original_bfs)
        missing = original_bfs.replace(missing_row, "", 1)
        lines = original_bfs.splitlines()
        first = next(index for index, line in enumerate(lines)
                     if line.startswith("APPEND_4K_1M_US\t"))
        lines[first], lines[first + 1] = lines[first + 1], lines[first]
        reordered = "\n".join(lines) + "\n"
        bfs.write_text(missing, encoding="ascii")
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)
        bfs.write_text(reordered, encoding="ascii")
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)
        bfs.write_text(original_bfs, encoding="ascii")
        pfs3.write_text(original_pfs3.replace(
            "FS_DEEP_COMPARE\t12\n", "FS_DEEP_COMPARE\t11\n", 1,
        ), encoding="ascii")
        self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v12_rejects_lookup_accounting_invariant_violations(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v12()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("cache accounting", {
                "LIST_EXALL_400_ENTRIES_FIRST_PASS_CACHE_READ_HITS": 79,
            }),
            ("resident views exceed visits", {
                "LIST_EXALL_400_ENTRIES_FIRST_PASS_DIR_TREE_RESIDENT_VIEWS": 11,
            }),
            ("hint hits exceed searches", {
                "LIST_EXALL_400_ENTRIES_FIRST_PASS_BTREE_INDEX_HINT_HITS": 7,
                "LIST_EXALL_400_ENTRIES_FIRST_PASS_BTREE_LEAF_HINT_HITS": 2,
            }),
            ("listing data read", {
                "LIST_EXALL_400_ENTRIES_FIRST_PASS_DATA_READS": 1,
            }),
            ("listing write", {
                "LIST_EXNEXT_40_ENTRIES_FIRST_PASS_BIO_WRITES": 1,
            }),
            ("listing commit", {
                "LIST_EXALL_1000_ENTRIES_REPEAT10_TOTAL_TXN_COMMITS": 1,
            }),
            ("CRC sample stride", {
                "SMALL_CREATE_40_NODE_CRC_READ_SAMPLES": 0,
            }),
            ("CPU sample stride", {
                "CPU_SAMPLE_STRIDE": 2,
            }),
        )
        for label, updates in cases:
            with self.subTest(invariant=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v11_rejects_missing_malformed_and_duplicate_scopes(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        missing_row = "LOOKUP_400_IFACE_FREE_SAMPLE_TICKS\t190"
        malformed_row = "SMALL_READ_40_CORE_CREATE_SAMPLE_TICKS\t101"
        duplicate_row = "SMALL_DELETE_40_PACKET_OTHER_CALLS\t8"
        self.assertIn(missing_row + "\n", original)
        self.assertIn(malformed_row + "\n", original)
        self.assertIn(duplicate_row + "\n", original)
        cases = (
            ("missing scope row", original.replace(missing_row + "\n", "", 1)),
            ("malformed scope row", original.replace(
                malformed_row + "\n",
                "SMALL_READ_40_CORE_CREATE_SAMPLE_TICKS\tbad\n", 1,
            )),
            ("duplicate scope row", original.replace(
                "PASS\t1\n", duplicate_row + "\nPASS\t1\n", 1,
            )),
        )
        for label, mutated in cases:
            with self.subTest(scope=label):
                bfs.write_text(mutated, encoding="ascii")
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v11_rejects_sample_and_zero_call_tick_errors(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("sample differs from call count", {
                "SMALL_CREATE_40_CORE_CREATE_SAMPLES": 10,
            }),
            ("ticks with zero calls", {
                "SMALL_CREATE_40_CORE_DELETE_SAMPLE_TICKS": 1,
            }),
        )
        for label, updates in cases:
            with self.subTest(scope=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v11_rejects_mixed_version_headers(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        pfs3 = self.results / "pfs3.deep-compare.tsv"
        original = pfs3.read_text(encoding="ascii")
        pfs3.write_text(original.replace(
            "FS_DEEP_COMPARE\t11\n", "FS_DEEP_COMPARE\t10\n", 1,
        ), encoding="ascii")
        result = self.verify("deep-compare")
        self.assertNotEqual(result.returncode, 0)

    def test_deep_compare_v11_rejects_packet_partition_call_and_tick_errors(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("packet call partition", {
                "SMALL_CREATE_40_PACKET_CALLS": 36,
                "SMALL_CREATE_40_PACKET_SAMPLES": 36,
            }),
            ("packet tick partition", {
                "SMALL_CREATE_40_PACKET_SAMPLE_TICKS": 351,
            }),
            ("exact uint64 packet tick partition", {
                "SMALL_CREATE_40_PACKET_SAMPLE_TICKS": 9007199254740993,
                "SMALL_CREATE_40_PACKET_OPEN_SAMPLE_TICKS": 9007199254740992,
                "SMALL_CREATE_40_PACKET_READ_SAMPLE_TICKS": 0,
                "SMALL_CREATE_40_PACKET_WRITE_SAMPLE_TICKS": 0,
                "SMALL_CREATE_40_PACKET_END_SAMPLE_TICKS": 0,
                "SMALL_CREATE_40_PACKET_DELETE_SAMPLE_TICKS": 0,
                "SMALL_CREATE_40_PACKET_FLUSH_SAMPLE_TICKS": 0,
                "SMALL_CREATE_40_PACKET_OTHER_SAMPLE_TICKS": 0,
            }),
        )
        for label, updates in cases:
            with self.subTest(partition=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v11_accepts_maximum_unsigned_scope_values(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        updates = {}
        for scope in CPU_SCOPES:
            updates[f"SMALL_CREATE_40_{scope}_CALLS"] = UINT32_MAX
            updates[f"SMALL_CREATE_40_{scope}_SAMPLES"] = UINT32_MAX
            updates[f"SMALL_CREATE_40_{scope}_SAMPLE_TICKS"] = UINT64_MAX
        updates.update({
            "SMALL_CREATE_40_PACKET_OPEN_CALLS": UINT32_MAX,
            "SMALL_CREATE_40_PACKET_OPEN_SAMPLES": UINT32_MAX,
            "SMALL_CREATE_40_PACKET_OPEN_SAMPLE_TICKS": UINT64_MAX,
            "SMALL_CREATE_40_PACKET_READ_CALLS": 0,
            "SMALL_CREATE_40_PACKET_READ_SAMPLES": 0,
            "SMALL_CREATE_40_PACKET_READ_SAMPLE_TICKS": 0,
            "SMALL_CREATE_40_PACKET_WRITE_CALLS": 0,
            "SMALL_CREATE_40_PACKET_WRITE_SAMPLES": 0,
            "SMALL_CREATE_40_PACKET_WRITE_SAMPLE_TICKS": 0,
            "SMALL_CREATE_40_PACKET_END_CALLS": 0,
            "SMALL_CREATE_40_PACKET_END_SAMPLES": 0,
            "SMALL_CREATE_40_PACKET_END_SAMPLE_TICKS": 0,
            "SMALL_CREATE_40_PACKET_DELETE_CALLS": 0,
            "SMALL_CREATE_40_PACKET_DELETE_SAMPLES": 0,
            "SMALL_CREATE_40_PACKET_DELETE_SAMPLE_TICKS": 0,
            "SMALL_CREATE_40_PACKET_FLUSH_CALLS": 0,
            "SMALL_CREATE_40_PACKET_FLUSH_SAMPLES": 0,
            "SMALL_CREATE_40_PACKET_FLUSH_SAMPLE_TICKS": 0,
            "SMALL_CREATE_40_PACKET_OTHER_CALLS": 0,
            "SMALL_CREATE_40_PACKET_OTHER_SAMPLES": 0,
            "SMALL_CREATE_40_PACKET_OTHER_SAMPLE_TICKS": 0,
            "SMALL_CREATE_40_PACKET_CALLS": UINT32_MAX,
            "SMALL_CREATE_40_PACKET_SAMPLES": UINT32_MAX,
            "SMALL_CREATE_40_PACKET_SAMPLE_TICKS": UINT64_MAX,
        })
        self.set_metrics(bfs, updates)
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deep_compare_v11_rejects_unsigned_overflow_and_lossy_counts(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v11()
        bfs = self.results / "bfs.deep-compare.tsv"
        original = bfs.read_text(encoding="ascii")
        cases = (
            ("ULONG call overflow", {
                "SMALL_CREATE_40_CORE_CREATE_CALLS": UINT32_MAX + 1,
                "SMALL_CREATE_40_CORE_CREATE_SAMPLES": UINT32_MAX + 1,
            }),
            ("ULONG sample overflow", {
                "SMALL_CREATE_40_CORE_CREATE_SAMPLES": UINT32_MAX + 1,
            }),
            ("uint64 tick overflow", {
                "SMALL_CREATE_40_CORE_SYNC_SAMPLE_TICKS": UINT64_MAX + 1,
            }),
            ("lossy large sample mismatch", {
                "SMALL_CREATE_40_CORE_FILE_WRITE_CALLS": 9007199254740992,
                "SMALL_CREATE_40_CORE_FILE_WRITE_SAMPLES": 9007199254740993,
            }),
            ("self-consistent but out-of-range packet total", {
                "SMALL_CREATE_40_PACKET_CALLS": 7000000000,
                "SMALL_CREATE_40_PACKET_SAMPLES": 7000000000,
                "SMALL_CREATE_40_PACKET_SAMPLE_TICKS": 7,
                "SMALL_CREATE_40_PACKET_OPEN_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_OPEN_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_OPEN_SAMPLE_TICKS": 1,
                "SMALL_CREATE_40_PACKET_READ_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_READ_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_READ_SAMPLE_TICKS": 1,
                "SMALL_CREATE_40_PACKET_WRITE_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_WRITE_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_WRITE_SAMPLE_TICKS": 1,
                "SMALL_CREATE_40_PACKET_END_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_END_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_END_SAMPLE_TICKS": 1,
                "SMALL_CREATE_40_PACKET_DELETE_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_DELETE_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_DELETE_SAMPLE_TICKS": 1,
                "SMALL_CREATE_40_PACKET_FLUSH_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_FLUSH_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_FLUSH_SAMPLE_TICKS": 1,
                "SMALL_CREATE_40_PACKET_OTHER_CALLS": 1000000000,
                "SMALL_CREATE_40_PACKET_OTHER_SAMPLES": 1000000000,
                "SMALL_CREATE_40_PACKET_OTHER_SAMPLE_TICKS": 1,
            }),
        )
        for label, updates in cases:
            with self.subTest(scope=label):
                bfs.write_text(original, encoding="ascii")
                self.set_metrics(bfs, updates)
                self.assertNotEqual(self.verify("deep-compare").returncode, 0)

    def test_deep_compare_v10_does_not_apply_schema11_integer_bounds(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        self.upgrade_deep_compare_to_v10()
        bfs = self.results / "bfs.deep-compare.tsv"
        self.set_metrics(bfs, {
            "SMALL_CREATE_40_BTREE_MALLOC_CALLS": UINT32_MAX + 1,
            "SMALL_CREATE_40_BTREE_MALLOC_SAMPLES": UINT32_MAX + 1,
            "SMALL_CREATE_40_BTREE_MALLOC_SAMPLE_TICKS": UINT64_MAX + 1,
        })
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

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
