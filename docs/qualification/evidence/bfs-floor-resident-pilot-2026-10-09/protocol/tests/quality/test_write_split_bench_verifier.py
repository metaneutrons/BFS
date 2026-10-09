# SPDX-License-Identifier: MPL-2.0
"""Focused schema-2 parser probes for write-detail split benchmarks."""

import importlib.util
from pathlib import Path
import subprocess  # nosec B404 - invokes only the fixed repository verifier, without a shell
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VERIFIER = ROOT / "tools/verify-split-bench.py"
BASE_TESTS = ROOT / "tests/quality/test_split_bench_verifier.py"
BASE_SPEC = importlib.util.spec_from_file_location("base_split_bench_tests", BASE_TESTS)
base_tests = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(base_tests)
verifier = base_tests.verifier
FIXTURE_GENERATOR = base_tests.SplitBenchVerifierTests()


class WriteSplitBenchVerifierTests(unittest.TestCase):
    WRITE_MODES = ("split-write-compare", "split-write-durable-compare")
    PHASE = "SMALL_CREATE_40"

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.results = Path(self.temp.name) / "system/Results"
        self.results.mkdir(parents=True)

    @staticmethod
    def _stage_key(phase, stage, scope, suffix):
        return f"{phase}_{stage}_{scope}_{suffix}"

    def _valid_rows(self, filesystem, mode):
        old_mode = (
            "split-durable-compare"
            if mode == "split-write-durable-compare"
            else "split-compare"
        )
        legacy = {
            row.split("\t", 1)[0]: row.split("\t", 1)[1]
            for row in FIXTURE_GENERATOR._valid_rows(filesystem, old_mode)
        }
        rows = []
        for key, fixed in verifier.build_schema(filesystem, mode):
            value = fixed if fixed is not None else legacy.get(key, "0")
            rows.append(f"{key}\t{value}")

        if filesystem == "bfs" and mode in self.WRITE_MODES:
            # Positive values exercise sidecar-to-legacy identities, all-call
            # heap samples, the stride-17 detail sample, and a durable flush.
            self._replace_value(rows, "SMALL_CREATE_40_WORK_INODE_READ_CALLS", "34")
            self._set_scope(rows, self.PHASE, "WORK", "WRITE_DETAIL_INODE_READ", 34, 34, 22)
            self._replace_value(rows, "SMALL_CREATE_40_WORK_EXTENT_MAPS", "1")
            self._set_scope(rows, self.PHASE, "WORK", "WRITE_DETAIL_EXTENT_MAP", 1, 1, 11)
            self._set_scope(rows, self.PHASE, "WORK", "WRITE_DETAIL_INODE_WRITE", 3, 3, 33)
            self._set_scope(rows, self.PHASE, "WORK", "WRITE_DETAIL_FREESPACE_GOAL", 1, 1, 7)
            for scope, calls, ticks in (
                ("BTREE_MALLOC", 2, 17),
                ("BTREE_FREE", 1, 13),
                ("IFACE_ALLOC", 3, 19),
                ("FREESPACE_ALLOC", 4, 23),
            ):
                self._set_scope(rows, self.PHASE, "WORK", scope, calls, calls, ticks)
            self._set_scope(rows, self.PHASE, "WORK", "DETAIL_INODE_READ", 34, 2, 29)
            self._replace_value(rows, "SMALL_CREATE_40_WORK_FREESPACE_ALLOCS", "4")

            if mode == "split-write-durable-compare":
                self._set_scope(
                    rows, "SEQ_WRITE_8M", "VOLUME_FLUSH", "WRITE_DETAIL_INODE_WRITE", 1, 1, 5
                )

        return rows

    def _set_scope(self, rows, phase, stage, scope, calls, samples, ticks):
        self._replace_value(rows, self._stage_key(phase, stage, scope, "CALLS"), str(calls))
        self._replace_value(rows, self._stage_key(phase, stage, scope, "SAMPLES"), str(samples))
        self._replace_value(rows, self._stage_key(phase, stage, scope, "SAMPLE_TICKS"), str(ticks))

    @staticmethod
    def _replace_value(rows, key, value):
        for index, row in enumerate(rows):
            name, _ = row.split("\t", 1)
            if name == key:
                rows[index] = f"{key}\t{value}"
                return rows
        raise AssertionError(f"missing fixture key: {key}")

    @staticmethod
    def _set_value(rows, key, value):
        return WriteSplitBenchVerifierTests._replace_value(rows, key, value)

    def _write_valid_results(self, mode):
        (self.results / "complete.txt").write_bytes(verifier.MARKER)
        (self.results / "info-after-format.txt").write_bytes(
            b"DH1: Workbench Read/Write BFSTest\nDH2: Workbench Read/Write PFSTest\n"
        )
        for filesystem in ("bfs", "pfs3"):
            rows = self._valid_rows(filesystem, mode)
            (self.results / f"{filesystem}.split.tsv").write_text(
                "\n".join(rows) + "\n", encoding="ascii"
            )

    def _run(self, mode):
        return subprocess.run(
            [sys.executable, str(VERIFIER), str(self.results), mode],
            capture_output=True, text=True, check=False,
        )  # nosec B603 - executable path and mode are fixed by this test

    def _edit_rows(self, filesystem, editor):
        path = self.results / f"{filesystem}.split.tsv"
        rows = path.read_text(encoding="ascii").splitlines()
        path.write_text("\n".join(editor(rows)) + "\n", encoding="ascii")

    def _reject(self, edit, filesystem="bfs", mode="split-write-compare"):
        self._write_valid_results(mode)
        self._edit_rows(filesystem, edit)
        result = self._run(mode)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ERROR:", result.stderr)

    def test_valid_write_results_pass_in_both_modes(self):
        for mode in self.WRITE_MODES:
            with self.subTest(mode=mode):
                self._write_valid_results(mode)
                result = self._run(mode)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_pfs3_has_no_internal_write_rows(self):
        rows = [key for key, _ in verifier.build_schema("pfs3", "split-write-compare")]
        self.assertFalse(any("_WORK_WRITE_DETAIL_" in key for key in rows))
        self.assertFalse(any("_VOLUME_FLUSH_WRITE_DETAIL_" in key for key in rows))
        self.assertFalse(any(key.endswith("_FREESPACE_ALLOCS") for key in rows))

        self._write_valid_results("split-write-compare")
        result = self._run("split-write-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_schema1_and_schema2_are_mode_specific(self):
        self._write_valid_results("split-compare")
        result = self._run("split-write-compare")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ERROR:", result.stderr)

        self._write_valid_results("split-write-compare")
        result = self._run("split-compare")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ERROR:", result.stderr)

    def test_rejects_invalid_write_versions_and_strides(self):
        for key, value in (
            ("WRITE_PROBE_VERSION", "2"),
            ("WRITE_SAMPLE_STRIDE", "0"),
            ("DETAIL_SAMPLE_STRIDE", "16"),
        ):
            with self.subTest(key=key):
                self._reject(lambda rows, k=key, v=value: self._set_value(rows, k, v))

    def test_rejects_missing_new_row(self):
        key = "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_CALLS"
        self._reject(lambda rows: [row for row in rows if not row.startswith(key + "\t")])

    def test_rejects_extra_new_row(self):
        def add_extra(rows):
            index = next(i for i, row in enumerate(rows) if row.startswith("PASS\t"))
            rows.insert(index, "UNEXPECTED_WRITE_DETAIL\t0")
            return rows

        self._reject(add_extra)

    def test_rejects_duplicate_new_row(self):
        key = "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_CALLS"

        def duplicate(rows):
            index = next(i for i, row in enumerate(rows) if row.startswith(key + "\t"))
            rows.insert(index + 1, rows[index])
            return rows

        self._reject(duplicate)

    def test_rejects_out_of_order_new_rows(self):
        def reorder(rows):
            indices = [
                i for i, row in enumerate(rows)
                if row.startswith((
                    "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_CALLS\t",
                    "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_SAMPLES\t",
                ))
            ]
            rows[indices[0]], rows[indices[1]] = rows[indices[1]], rows[indices[0]]
            return rows

        self._reject(reorder)

    def test_rejects_32_bit_call_overflow(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_CALLS", "4294967296"
        ))

    def test_rejects_32_bit_sample_overflow(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_BTREE_MALLOC_SAMPLES", "4294967296"
        ))

    def test_rejects_64_bit_sample_tick_overflow(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_SAMPLE_TICKS",
            "18446744073709551616",
        ))

    def test_rejects_all_call_sample_mismatch(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_SAMPLES", "1"
        ))

    def test_rejects_heap_sample_mismatch(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_BTREE_MALLOC_SAMPLES", "1"
        ))

    def test_rejects_stride_17_sample_mismatch(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_DETAIL_INODE_READ_SAMPLES", "1"
        ))

    def test_rejects_ticks_without_samples(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_DETAIL_DIR_NODE_VIEW_SAMPLE_TICKS", "1"
        ))

    def test_rejects_inode_read_counter_identity_mismatch(self):
        def corrupt(rows):
            self._set_value(
                rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_CALLS", "3"
            )
            self._set_value(
                rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_SAMPLES", "3"
            )
            return rows

        self._reject(corrupt)

    def test_rejects_extent_map_counter_identity_mismatch(self):
        def corrupt(rows):
            self._set_value(
                rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_EXTENT_MAP_CALLS", "2"
            )
            self._set_value(
                rows, "SMALL_CREATE_40_WORK_WRITE_DETAIL_EXTENT_MAP_SAMPLES", "2"
            )
            return rows

        self._reject(corrupt)

    def test_rejects_detail_inode_read_duplicate_counter_mismatch(self):
        def corrupt(rows):
            self._set_value(rows, "SMALL_CREATE_40_WORK_DETAIL_INODE_READ_CALLS", "51")
            self._set_value(rows, "SMALL_CREATE_40_WORK_DETAIL_INODE_READ_SAMPLES", "3")
            return rows

        self._reject(corrupt)

    def test_rejects_freespace_alloc_duplicate_counter_mismatch(self):
        def corrupt(rows):
            self._set_value(rows, "SMALL_CREATE_40_WORK_FREESPACE_ALLOC_CALLS", "3")
            self._set_value(rows, "SMALL_CREATE_40_WORK_FREESPACE_ALLOC_SAMPLES", "3")
            return rows

        self._reject(corrupt)

    def test_rejects_readonly_write_detail_activity(self):
        for scope in ("WRITE_DETAIL_INODE_WRITE", "WRITE_DETAIL_EXTENT_MAP",
                      "WRITE_DETAIL_FREESPACE_GOAL"):
            with self.subTest(scope=scope):
                def corrupt(rows, selected=scope):
                    self._set_value(
                        rows, self._stage_key("LOOKUP_400", "WORK", selected, "CALLS"), "1"
                    )
                    self._set_value(
                        rows, self._stage_key("LOOKUP_400", "WORK", selected, "SAMPLES"), "1"
                    )
                    self._set_value(
                        rows, self._stage_key("LOOKUP_400", "WORK", selected,
                                              "SAMPLE_TICKS"), "1"
                    )
                    return rows

                self._reject(corrupt)

    def test_rejects_readonly_freespace_allocations(self):
        self._reject(lambda rows: self._set_value(
            rows, "LOOKUP_400_WORK_FREESPACE_ALLOCS", "1"
        ))

    def test_rejects_nonzero_counter_when_flush_is_absent(self):
        self._reject(
            lambda rows: self._set_value(
                rows,
                "LOOKUP_400_VOLUME_FLUSH_WRITE_DETAIL_INODE_WRITE_SAMPLE_TICKS",
                "1",
            ),
            mode="split-write-durable-compare",
        )

    def test_rejects_internal_write_row_in_pfs3(self):
        def add_internal_row(rows):
            index = next(i for i, row in enumerate(rows) if row.startswith("PROBE_VERSION\t"))
            rows.insert(index, "SMALL_CREATE_40_WORK_WRITE_DETAIL_INODE_READ_CALLS\t0")
            return rows

        self._reject(add_internal_row, filesystem="pfs3")


if __name__ == "__main__":
    unittest.main()
