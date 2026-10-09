#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Adversarial unit tests for the strict sparse CRC microprobe consumer."""

from __future__ import annotations

import json
import hashlib
import tempfile
import unittest
from pathlib import Path

import summarize_micro as micro


MICRO_CONFIG_TEMPLATE = (
    "[fs-uae]\n"
    "amiga_model = A1200\n"
    "chip_memory = 2048\n"
    "fast_memory = 8192\n"
    "cpu = 68040\n"
    "uae_cpu_speed = max\n"
    "uae_cpu_24bit_addressing = false\n"
    "kickstart_file = /home/fabian/Amiga/kick.a1200.47.102.rom\n"
    "hard_drive_0 = /home/fabian/.cache/bfs-performance/sparse-crc-2026-10-09.xN615F/build/micro/run-{run}/system\n"
    "hard_drive_0_label = System\n"
    "hard_drive_0_priority = 0\n"
    "floppy_speed = 0\n"
    "window_hidden = 1\n"
    "automatic_input_grab = 0\n"
    "audio_driver = null\n"
)


class SparseMicroSummaryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.runs = [self.root / "run-1", self.root / "run-2"]
        for index, run_dir in enumerate(self.runs, start=1):
            self._write_run(run_dir, index)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _input_hashes(self, index: int) -> str:
        return (
            f"{micro.PROBE_SHA256}  /cache/run-{index}/system/C/crc32-sparse-probe\n"
            f"{micro.CONFIG_SHA256S[index - 1]}  /cache/run-{index}/crc32.fs-uae\n"
            f"{micro.ROM_SHA256}  /rom/kick.a1200.47.102.rom\n"
        )

    def _config_text(self, index: int) -> str:
        return MICRO_CONFIG_TEMPLATE.format(run=index)

    def _timing_rows(self, run_index: int) -> list[str]:
        rows = []
        for case_index, (case, length, repeats, _pattern) in enumerate(micro.TIMING_CASES):
            for sample in range(micro.SAMPLE_COUNT):
                order = "baseline-first" if sample % 2 == 0 else "sparse-first"
                baseline = 1000 + run_index * 100 + case_index * 20 + sample * 3
                sparse = baseline + ((sample % 3) - 1) * 7
                rows.append(
                    f"TIMING\t{case}\t{length}\t{repeats}\t{sample}\t{order}\t{baseline}\t{sparse}"
                )
        return rows

    def _report(self, run_index: int) -> str:
        lines = ["SPARSE_CRC_PROBE\t1", *micro.META_ROWS, *micro.FIXTURE_ROWS]
        for name, value in micro.COUNT_ROWS_BEFORE_CLOCK:
            lines.append(f"COUNT\t{name}\t{value}")
        lines.append("CLOCK_HZ\t709379")
        for name, value in micro.COUNT_ROWS_AFTER_CLOCK:
            lines.append(f"COUNT\t{name}\t{value}")
        lines.append(micro.TIMING_HEADER)
        lines.extend(self._timing_rows(run_index))
        lines.append("STATUS\tPASS")
        return "\n".join(lines) + "\n"

    def _write_run(self, run_dir: Path, run_index: int) -> None:
        result_dir = run_dir / "system" / "Results"
        result_dir.mkdir(parents=True, exist_ok=True)
        (run_dir / "crc32.fs-uae").write_bytes(self._config_text(run_index).encode("ascii"))
        (run_dir / "inputs.sha256").write_text(self._input_hashes(run_index), encoding="ascii")
        (result_dir / "crc32-sparse-probe.tsv").write_text(
            self._report(run_index), encoding="ascii"
        )
        (result_dir / "crc32-sparse-probe.done").write_bytes(micro.DONE_MARKER)

    def _report_lines(self, run_index: int = 1) -> list[str]:
        return self._report(run_index).splitlines()

    def _write_report_lines(self, run_index: int, lines: list[str]) -> None:
        report = self.runs[run_index - 1] / "system" / "Results" / "crc32-sparse-probe.tsv"
        report.write_text("\n".join(lines) + "\n", encoding="ascii")

    def _replace_report_line(self, run_index: int, prefix: str, replacement: str) -> None:
        lines = self._report_lines(run_index)
        matches = [index for index, line in enumerate(lines) if line.startswith(prefix)]
        self.assertEqual(len(matches), 1, f"fixture prefix {prefix!r} must be unique")
        lines[matches[0]] = replacement
        self._write_report_lines(run_index, lines)

    def _assert_rejected(self) -> None:
        with self.assertRaises(micro.ValidationError):
            micro.summarize_runs(self.runs)

    def test_valid_fixture_and_independent_oracle(self) -> None:
        self.assertEqual(
            hashlib.sha256(self._config_text(1).encode("ascii")).hexdigest(),
            micro.CONFIG_SHA256S[0],
        )
        self.assertEqual(
            hashlib.sha256(self._config_text(2).encode("ascii")).hexdigest(),
            micro.CONFIG_SHA256S[1],
        )
        count, digest = micro.regenerate_oracle_digest()
        self.assertEqual(count, 1297)
        self.assertEqual(digest, 2555652288)
        self.assertEqual(micro.binascii.crc32(b"123456789") & 0xFFFFFFFF, 0xCBF43926)
        self.assertEqual(micro.crc32_bitwise(0, b"123456789"), 0xCBF43926)

        dirkey = micro.pattern_bytes("dirkey_like_264", 4096)
        self.assertEqual(dirkey[:13], bytes(micro.dense_byte(i) for i in range(13)))
        self.assertEqual(dirkey[13:264], bytes(251))
        self.assertEqual(dirkey[264:277], bytes(micro.dense_byte(i) for i in range(264, 277)))
        self.assertEqual(dirkey[277:528], bytes(251))

        summary = micro.summarize_runs(self.runs)
        self.assertEqual(summary["oracle"]["vector_cases"], 1297)
        self.assertEqual(summary["oracle"]["fnv1a32"], 2555652288)
        self.assertEqual(len(summary["runs"]), 2)
        self.assertEqual(len(summary["runs"][0]["timing_rows"]), 30)
        self.assertEqual(summary["pooled"]["pair_count"], 60)
        dense = summary["pooled"]["case_summary"]["dense4096"]
        self.assertEqual(dense["pair_count"], 12)
        self.assertEqual(len(dense["baseline_ticks"]), 12)
        self.assertEqual(len(dense["sparse_ticks"]), 12)
        self.assertEqual(len(dense["paired_delta_ticks"]), 12)
        self.assertEqual(len(dense["sparse_over_baseline"]), 12)
        self.assertEqual(dense["baseline_min_ticks"], min(dense["baseline_ticks"]))
        self.assertEqual(dense["baseline_max_ticks"], max(dense["baseline_ticks"]))
        self.assertEqual(dense["sparse_min_ticks"], min(dense["sparse_ticks"]))
        self.assertEqual(dense["sparse_max_ticks"], max(dense["sparse_ticks"]))
        self.assertEqual(
            dense["slower_pairs"],
            sum(sparse > baseline for baseline, sparse in zip(
                dense["baseline_ticks"], dense["sparse_ticks"]
            )),
        )
        json.dumps(summary)

    def test_missing_run_directory_is_rejected(self) -> None:
        self.runs[1].rename(self.root / "held-run-2")
        self._assert_rejected()

    def test_crlf_report_and_identity_records_are_rejected(self) -> None:
        for relative in ("inputs.sha256", "system/Results/crc32-sparse-probe.tsv"):
            with self.subTest(relative=relative):
                for index, run_dir in enumerate(self.runs, start=1):
                    self._write_run(run_dir, index)
                record = self.runs[0] / relative
                record.write_bytes(record.read_bytes().replace(b"\n", b"\r\n"))
                self._assert_rejected()

    def test_missing_timing_row_is_rejected(self) -> None:
        lines = self._report_lines()
        del lines[lines.index(micro.TIMING_HEADER) + 1]
        self._write_report_lines(1, lines)
        self._assert_rejected()

    def test_duplicate_timing_case_sample_is_rejected(self) -> None:
        lines = self._report_lines()
        header = lines.index(micro.TIMING_HEADER)
        lines[header + 2] = lines[header + 1]
        self._write_report_lines(1, lines)
        self._assert_rejected()

    def test_wrong_counts_and_wrong_digest_are_rejected(self) -> None:
        self._replace_report_line(1, "COUNT\tvector_cases\t", "COUNT\tvector_cases\t1296")
        self._assert_rejected()
        self._write_run_report(1)
        self._replace_report_line(
            1, "COUNT\toracle_digest_fnv1a32\t", "COUNT\toracle_digest_fnv1a32\t1"
        )
        self._assert_rejected()

    def _write_run_report(self, run_index: int) -> None:
        path = self.runs[run_index - 1] / "system" / "Results" / "crc32-sparse-probe.tsv"
        path.write_text(self._report(run_index), encoding="ascii")

    def test_error_status_is_rejected(self) -> None:
        lines = self._report_lines()
        lines[-1] = "STATUS\tFAIL"
        self._write_report_lines(1, lines)
        self._assert_rejected()

    def test_wrong_timing_order_length_and_repeats_are_rejected(self) -> None:
        for field, replacement in ((5, "sparse-first"), (2, "4095"), (3, "63")):
            with self.subTest(field=field):
                self._write_run_report(1)
                lines = self._report_lines()
                row_index = lines.index(micro.TIMING_HEADER) + 1
                fields = lines[row_index].split("\t")
                fields[field] = replacement
                lines[row_index] = "\t".join(fields)
                self._write_report_lines(1, lines)
                self._assert_rejected()

    def test_zero_clock_and_zero_ticks_are_rejected(self) -> None:
        self._replace_report_line(1, "CLOCK_HZ\t", "CLOCK_HZ\t0")
        self._assert_rejected()
        self._write_run_report(1)
        lines = self._report_lines()
        row_index = lines.index(micro.TIMING_HEADER) + 1
        fields = lines[row_index].split("\t")
        fields[6] = "0"
        lines[row_index] = "\t".join(fields)
        self._write_report_lines(1, lines)
        self._assert_rejected()

    def test_wrong_probe_config_or_rom_identity_is_rejected(self) -> None:
        cases = (
            (micro.PROBE_SHA256, "0" * 64, "probe"),
            (micro.CONFIG_SHA256S[0], "1" * 64, "config"),
            (micro.ROM_SHA256, "2" * 64, "ROM"),
        )
        for expected, wrong, kind in cases:
            with self.subTest(identity=kind):
                for index in range(2):
                    self._write_run(self.runs[index], index + 1)
                lines = (self.runs[0] / "inputs.sha256").read_text(encoding="ascii").splitlines()
                target = {micro.PROBE_SHA256: 0, micro.CONFIG_SHA256S[0]: 1, micro.ROM_SHA256: 2}[expected]
                fields = lines[target].split("  ")
                fields[0] = wrong
                lines[target] = "  ".join(fields)
                (self.runs[0] / "inputs.sha256").write_text("\n".join(lines) + "\n", encoding="ascii")
                self._assert_rejected()

    def test_missing_or_tampered_retained_config_is_rejected(self) -> None:
        config = self.runs[0] / "crc32.fs-uae"
        config.unlink()
        self._assert_rejected()

        self._write_run(self.runs[0], 1)
        config.write_bytes(config.read_bytes().replace(b"cpu = 68040", b"cpu = 68030"))
        self._assert_rejected()

    def test_same_or_nested_run_directories_are_rejected(self) -> None:
        with self.assertRaises(micro.ValidationError):
            micro.summarize_runs((self.runs[0], self.runs[0]))
        with self.assertRaises(micro.ValidationError):
            micro.summarize_runs((self.root, self.runs[0]))


if __name__ == "__main__":
    unittest.main()
