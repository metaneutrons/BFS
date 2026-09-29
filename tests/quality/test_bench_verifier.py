# SPDX-License-Identifier: MPL-2.0
"""Positive and corrupt-input probes for the Amiga filesystem bench verifier."""

from pathlib import Path
import shutil
import subprocess
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

    def verify(self, mode):
        return subprocess.run(
            [str(VERIFIER), str(self.run_dir), mode],
            capture_output=True, text=True, check=False,
        )

    def test_real_deep_compare_evidence_passes(self):
        self.load_evidence("deep-compare-bfs-first", "deep-compare.tsv")
        result = self.verify("deep-compare")
        self.assertEqual(result.returncode, 0, result.stderr)

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

    def test_real_deep_profile_evidence_and_missing_peer(self):
        self.load_evidence("deep-bfs-first", "deep.tsv")
        result = self.verify("deep")
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.results / "pfs3.deep.tsv").unlink()
        self.assertNotEqual(self.verify("deep").returncode, 0)


if __name__ == "__main__":
    unittest.main()
