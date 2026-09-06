# SPDX-License-Identifier: MPL-2.0
"""Positive and corrupt-input probes for the real-emulator acceptance gate."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ci_runner", ROOT / "emulator-test/ci_runner.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class AmigaRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.result = self.root / "result"
        self.marker = self.root / "result.done"
        self.names = RUNNER.test_inventory(ROOT / "tools/bfs-test-cases.def")
        self.valid = ("# BFS Test Log\n# PROFILE\tfull\n# STATUS\tNAME\t[DETAIL]\n" +
                      "".join(f"PASS\t{name}\n" for name in self.names) +
                      f"# SUMMARY\t{len(self.names)}\t{len(self.names)}\t0\n")

    def verify(self, data=None, profile="full"):
        self.result.write_text(self.valid if data is None else data, encoding="ascii")
        return RUNNER.verify_result(self.result, profile, self.names)

    def test_complete_full_and_quick(self):
        self.marker.write_bytes(RUNNER.COMPLETION)
        self.assertEqual(self.verify(), self.valid)
        quick = self.valid.replace("PROFILE\tfull", "PROFILE\tquick")
        self.assertEqual(self.verify(quick, "quick"), quick)

    def test_missing_or_corrupt_completion(self):
        with self.assertRaises(FileNotFoundError):
            self.verify()
        self.marker.write_bytes(b"partial")
        with self.assertRaisesRegex(ValueError, "completion record"):
            self.verify()

    def test_inventory_and_summary_counterprobes(self):
        self.marker.write_bytes(RUNNER.COMPLETION)
        first, second = (f"PASS\t{name}\n" for name in self.names[:2])
        cases = [
            (self.valid.replace(first, ""), "count mismatch"),
            (self.valid.replace(first, second), "inventory mismatch"),
            (self.valid.replace(first, "PASS\tunknown\n"), "inventory mismatch"),
            (self.valid.replace(first, "FAIL\tbasic_01\tread error\n"), "summary"),
            (self.valid + first, "count mismatch"),
            (self.valid.replace("# SUMMARY\t", "# SUMMARY\t0"), "summary"),
            (self.valid.replace("PROFILE\tfull", "PROFILE\tquick"), "profile mismatch"),
            (self.valid[:-1], "truncated"),
            (self.valid.replace("\n", "\r\n"), "invalid guest result"),
        ]
        for data, reason in cases:
            with self.subTest(reason=reason, data=data[:80]):
                with self.assertRaisesRegex(ValueError, reason):
                    self.verify(data)

    def test_failed_case_with_honest_summary(self):
        self.marker.write_bytes(RUNNER.COMPLETION)
        data = self.valid.replace("PASS\tbasic_01\n", "FAIL\tbasic_01\tread error\n")
        data = data.replace(f"# SUMMARY\t{len(self.names)}\t{len(self.names)}\t0",
                            f"# SUMMARY\t{len(self.names) - 1}\t{len(self.names)}\t1")
        with self.assertRaisesRegex(ValueError, "integration failed"):
            self.verify(data)

    def test_inventory_is_nonempty_unique_and_well_formed(self):
        inventory = self.root / "inventory.def"
        for text in ("", "BFS_TEST(a, test_a)\nBFS_TEST(a, test_b)\n", "BFS_TEST(a)\n"):
            inventory.write_text(text, encoding="ascii")
            with self.assertRaises(ValueError):
                RUNNER.test_inventory(inventory)

    def test_child_completion(self):
        script = (f"from pathlib import Path; import time; "
                  f"Path({str(self.result)!r}).write_text({self.valid!r}); "
                  f"Path({str(self.marker)!r}).write_bytes({RUNNER.COMPLETION!r}); "
                  "time.sleep(30)")
        RUNNER.run_emulator([sys.executable, "-c", script], self.result, self.root / "log", 5)
        self.assertEqual(RUNNER.verify_result(self.result, "full", self.names), self.valid)

    def test_exit_and_timeout_are_failures(self):
        cases = [("raise SystemExit(0)", "exited"), ("import time; time.sleep(30)", "timed out")]
        for script, reason in cases:
            with self.subTest(reason=reason):
                with self.assertRaisesRegex(ValueError, reason):
                    RUNNER.run_emulator([sys.executable, "-c", script], self.result,
                                        self.root / "log", 0.2)

    def test_stale_record_cannot_pass(self):
        self.marker.write_bytes(RUNNER.COMPLETION)
        with self.assertRaisesRegex(ValueError, "stale guest"):
            RUNNER.run_emulator([sys.executable, "-c", "raise SystemExit(0)"],
                                self.result, self.root / "log", 1)


if __name__ == "__main__":
    unittest.main()
