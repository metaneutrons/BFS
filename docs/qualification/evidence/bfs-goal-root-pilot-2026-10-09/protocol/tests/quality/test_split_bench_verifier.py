# SPDX-License-Identifier: MPL-2.0
"""Positive and corrupt-input probes for the split bench verifier."""

import importlib.util
from pathlib import Path
import subprocess  # nosec B404 - invokes only the fixed repository verifier, without a shell
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VERIFIER = ROOT / "tools/verify-split-bench.py"
SPEC = importlib.util.spec_from_file_location("split_bench_verifier", VERIFIER)
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class SplitBenchVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.results = Path(self.temp.name) / "system/Results"
        self.results.mkdir(parents=True)

    def _valid_rows(self, filesystem, mode):
        rows = []
        flush_phases = verifier.FLUSH_PHASES if mode == "split-durable-compare" else set()
        phases = set(verifier._inventories()["phases"])
        work_keys = {f"{phase}_WORK_US" for phase in phases}
        phase_us_keys = {f"{phase}_US" for phase in phases}
        for key, fixed in verifier.build_schema(filesystem, mode):
            if fixed is not None:
                value = fixed
            elif key == "CLOCK_HZ":
                value = "1000000" if filesystem == "bfs" else "0"
            elif key in work_keys:
                value = "1"
            elif key.endswith("_VOLUME_FLUSH_US") and key[:-len("_VOLUME_FLUSH_US")] in phases:
                phase = key[:-len("_VOLUME_FLUSH_US")]
                value = "1" if phase in flush_phases else "0"
            elif key.endswith("_HAS_VOLUME_FLUSH") and key[:-len("_HAS_VOLUME_FLUSH")] in phases:
                phase = key[:-len("_HAS_VOLUME_FLUSH")]
                value = "1" if phase in flush_phases else "0"
            elif key in phase_us_keys:
                phase = key[:-len("_US")]
                value = "2" if phase in flush_phases else "1"
            elif key.endswith("_GUEST_READ_CALLS"):
                value = "129" if "SEQ_READ_8M" in key else (
                    "22" if "APPEND_READ_1280K" in key else "0"
                )
            elif key.endswith("_GUEST_VERIFY_CALLS"):
                value = "128" if "SEQ_READ_8M" in key else (
                    "20" if "APPEND_READ_1280K" in key else "0"
                )
            elif key.endswith("_GUEST_OPEN_CALLS"):
                value = "1" if "SEQ_READ_8M" in key else (
                    "2" if "APPEND_READ_1280K" in key else "0"
                )
            elif key.endswith("_GUEST_CLOSE_CALLS"):
                value = "1" if "SEQ_READ_8M" in key else (
                    "2" if "APPEND_READ_1280K" in key else "0"
                )
            else:
                value = "0"
            rows.append(f"{key}\t{value}")
        return rows

    def _write_valid_results(self, mode="split-compare"):
        (self.results / "complete.txt").write_bytes(verifier.MARKER)
        (self.results / "info-after-format.txt").write_bytes(
            b"DH1: Workbench Read/Write BFSTest\nDH2: Workbench Read/Write PFSTest\n"
        )
        for filesystem in ("bfs", "pfs3"):
            (self.results / f"{filesystem}.split.tsv").write_text(
                "\n".join(self._valid_rows(filesystem, mode)) + "\n", encoding="ascii"
            )

    def _run(self, mode="split-compare"):
        return subprocess.run(
            [sys.executable, str(VERIFIER), str(self.results), mode],
            capture_output=True, text=True, check=False,
        )  # nosec B603 - executable path and mode are fixed by this test

    def _edit_rows(self, filesystem, editor):
        path = self.results / f"{filesystem}.split.tsv"
        rows = path.read_text(encoding="ascii").splitlines()
        path.write_text("\n".join(editor(rows)) + "\n", encoding="ascii")

    def _set_value(self, rows, key, value):
        for index, row in enumerate(rows):
            name, _ = row.split("\t", 1)
            if name == key:
                rows[index] = f"{name}\t{value}"
                return rows
        raise AssertionError(f"missing fixture key: {key}")

    def _reject(self, edit, filesystem="bfs", mode="split-compare"):
        self._write_valid_results(mode)
        self._edit_rows(filesystem, edit)
        result = self._run(mode)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ERROR:", result.stderr)

    def test_valid_split_results_pass_in_both_modes(self):
        for mode in ("split-compare", "split-durable-compare"):
            with self.subTest(mode=mode):
                self._write_valid_results(mode)
                result = self._run(mode)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_row(self):
        self._reject(lambda rows: [row for row in rows if not row.startswith(
            "SMALL_CREATE_40_WORK_US\t")])

    def test_rejects_duplicate_row(self):
        def duplicate(rows):
            index = next(i for i, row in enumerate(rows)
                         if row.startswith("SMALL_CREATE_40_WORK_US\t"))
            rows.insert(index + 1, rows[index])
            return rows
        self._reject(duplicate)

    def test_rejects_unknown_row(self):
        def unknown(rows):
            rows.insert(-1, "UNEXPECTED\t0")
            return rows
        self._reject(unknown)

    def test_rejects_out_of_order_rows(self):
        def reorder(rows):
            indices = [i for i, row in enumerate(rows)
                       if row.startswith(("SMALL_CREATE_40_US\t",
                                          "SMALL_CREATE_40_WORK_US\t"))]
            rows[indices[0]], rows[indices[1]] = rows[indices[1]], rows[indices[0]]
            return rows
        self._reject(reorder)

    def test_rejects_malformed_row(self):
        def malformed(rows):
            rows[1] = "DRIVE DH1:"
            return rows
        self._reject(malformed)

    def test_rejects_negative_integer(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_US", "-1"))

    def test_rejects_zero_clock_rate(self):
        self._reject(lambda rows: self._set_value(rows, "CLOCK_HZ", "0"))

    def test_rejects_missing_pfs3_clock_zero(self):
        self._reject(lambda rows: self._set_value(rows, "CLOCK_HZ", "1"), filesystem="pfs3")

    def test_rejects_nonpositive_work_duration(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_US", "0"))

    def test_rejects_unsigned_32_bit_overflow(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_US", "4294967296"))

    def test_rejects_phase_total_mismatch(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_US", "2"))

    def test_rejects_internal_counters_in_pfs3(self):
        def add_counter(rows):
            rows.insert(-5, "SMALL_CREATE_40_WORK_BIO_READS\t0")
            return rows
        self._reject(add_counter, filesystem="pfs3")

    def test_rejects_crc_sample_count_mismatch(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_NODE_CRC_READ_SAMPLES", "1"))

    def test_rejects_cpu_sample_count_mismatch(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_PACKET_READ_SAMPLES", "1"))

    def test_rejects_nonzero_counter_when_flush_is_absent(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_VOLUME_FLUSH_BIO_READS", "1"))

    def test_rejects_inconsistent_packet_category_sum(self):
        def corrupt(rows):
            self._set_value(rows, "SMALL_CREATE_40_WORK_PACKET_READ_CALLS", "1")
            self._set_value(rows, "SMALL_CREATE_40_WORK_PACKET_READ_SAMPLES", "1")
            self._set_value(rows, "SMALL_CREATE_40_WORK_PACKET_READ_SAMPLE_TICKS", "1")
            return rows
        self._reject(corrupt)

    def test_rejects_data_reads_above_bio_reads(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_DATA_READS", "1"))

    def test_rejects_unexpected_read_counts(self):
        self._reject(lambda rows: self._set_value(
            rows, "SEQ_READ_8M_GUEST_READ_CALLS", "128"))

    def test_rejects_unexpected_append_read_counts(self):
        self._reject(lambda rows: self._set_value(
            rows, "APPEND_READ_1280K_GUEST_VERIFY_CALLS", "19"))

    def test_rejects_write_activity_in_readonly_phase(self):
        def corrupt(rows):
            self._set_value(rows, "LOOKUP_400_GUEST_WRITE_CALLS", "1")
            self._set_value(rows, "LOOKUP_400_GUEST_WRITE_US", "1")
            return rows
        self._reject(corrupt)

    def test_rejects_work_writes_in_readonly_phase(self):
        self._reject(lambda rows: self._set_value(
            rows, "LOOKUP_400_WORK_BIO_WRITES", "1"))

    def test_rejects_crc_ticks_without_crc_calls(self):
        self._reject(lambda rows: self._set_value(
            rows, "SMALL_CREATE_40_WORK_NODE_CRC_READ_SAMPLE_TICKS", "1"))

    def test_rejects_hidden_flush_phase(self):
        self._reject(lambda rows: self._set_value(
            rows, "LOOKUP_400_HAS_VOLUME_FLUSH", "1"),
            mode="split-durable-compare")

    def test_rejects_wrong_completion_marker(self):
        self._write_valid_results()
        (self.results / "complete.txt").write_bytes(b"BFS-PFS3-SPLIT-COMPLETE")
        result = self._run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ERROR:", result.stderr)

    def test_rejects_missing_mount_record(self):
        self._write_valid_results()
        (self.results / "info-after-format.txt").write_bytes(b"DH1 Read/Write BFSTest\n")
        result = self._run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ERROR:", result.stderr)


if __name__ == "__main__":
    unittest.main()
