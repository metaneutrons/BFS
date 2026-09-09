# SPDX-License-Identifier: MPL-2.0
import json
import os
from pathlib import Path
# Exercise the actual fixed repository script without a shell command string.
import subprocess  # nosec B404
import unittest

ROOT = Path(__file__).resolve().parents[2]
JOBS = ("commit-hygiene", "coverage", "fuse-conformance", "host-tests",
        "integration-test", "linux-build", "quality", "sanitizers", "static-analysis")


class AggregateGateTests(unittest.TestCase):
    def check(self, results):
        return subprocess.run(["/bin/bash", str(ROOT / "tools/check-ci-results.sh")],  # nosec B603
                              env={**os.environ, "NEEDS": json.dumps(results)},
                              capture_output=True, check=False).returncode

    def test_all_required_jobs_pass(self):
        self.assertEqual(self.check({job: {"result": "success"} for job in JOBS}), 0)

    def test_missing_and_extra_jobs_fail(self):
        for omitted in JOBS:
            self.assertEqual(self.check({job: {"result": "success"}
                                         for job in JOBS if job != omitted}), 1)
        self.assertEqual(self.check({}), 1)
        jobs = {job: {"result": "success"} for job in JOBS}
        jobs["unexpected"] = {"result": "success"}
        self.assertEqual(self.check(jobs), 1)

    def test_each_nonpassing_state_fails(self):
        for job in JOBS:
            for state in ("failure", "cancelled", "skipped", None):
                with self.subTest(job=job, state=state):
                    jobs = {name: {"result": "success"} for name in JOBS}
                    jobs[job] = {"result": state}
                    self.assertEqual(self.check(jobs), 1)


if __name__ == "__main__":
    unittest.main()
