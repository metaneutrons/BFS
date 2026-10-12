# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import subprocess  # nosec B404 - runs the reviewed signing helper against a fake cosign
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "tools/release/sign-blob.sh"

# Fails the first $FAIL_COUNT calls after leaving a partial bundle, then writes
# a bundle that names the call. Every call is appended to calls.log.
FAKE_COSIGN = """#!/bin/bash
set -euo pipefail
printf '%s\\n' "$*" >> "$FAKE_DIR/calls.log"
calls=$(wc -l < "$FAKE_DIR/calls.log")
[[ "$1 $2 $3" == "sign-blob --yes --bundle" ]] || exit 64
if ((calls <= FAIL_COUNT)); then
    printf 'partial' > "$4"
    printf 'createLogEntryConflict 409\\n' >&2
    exit 1
fi
printf '{"call": %d}' "$calls" > "$4"
"""


class ReleaseSigningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        fake = self.root / "bin" / "cosign"
        fake.parent.mkdir()
        fake.write_text(FAKE_COSIGN)
        fake.chmod(0o755)
        self.payload = self.root / "bfs-v1.2.3-source.tar.gz"
        self.payload.write_bytes(b"payload")
        self.bundle = self.root / "bfs-v1.2.3-source.tar.gz.sigstore.json"

    def sign(self, fail_count, path=None, **settings):
        environment = dict(os.environ, PATH=f"{self.root / 'bin'}:{os.environ['PATH']}",
                           FAKE_DIR=str(self.root), FAIL_COUNT=str(fail_count),
                           BFS_SIGN_RETRY_DELAY="0")
        environment.update(settings)
        return subprocess.run(["/bin/bash", str(SCRIPT), str(path or self.payload)],  # nosec B603
                              env=environment, capture_output=True, text=True, check=False)

    def calls(self):
        log = self.root / "calls.log"
        return log.read_text().splitlines() if log.exists() else []

    def test_first_attempt(self):
        result = self.sign(0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [f"sign-blob --yes --bundle {self.bundle} {self.payload}"])
        self.assertEqual(self.bundle.read_text(), '{"call": 1}')
        self.assertNotIn("::warning::", result.stdout)

    def test_retries_with_a_new_run_after_failures(self):
        result = self.sign(2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.calls()), 3)
        self.assertEqual(self.bundle.read_text(), '{"call": 3}')
        self.assertEqual(result.stdout.count("::warning::Signing"), 2)
        self.assertIn("attempt 2 of 3", result.stdout)

    def test_gives_up_and_leaves_no_bundle(self):
        result = self.sign(9)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.calls()), 3)
        self.assertFalse(self.bundle.exists())
        self.assertIn("failed after 3 attempts", result.stderr)

    def test_single_attempt_setting(self):
        result = self.sign(1, BFS_SIGN_ATTEMPTS="1")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.calls()), 1)
        self.assertFalse(self.bundle.exists())

    def test_rejects_missing_or_linked_payload_and_bad_settings(self):
        link = self.root / "link.tar.gz"
        link.symlink_to(self.payload)
        for path in (self.root / "missing.tar.gz", link):
            result = self.sign(0, path=path)
            self.assertEqual(result.returncode, 2)
            self.assertIn("not a regular file", result.stderr)
        for settings in ({"BFS_SIGN_ATTEMPTS": "0"}, {"BFS_SIGN_ATTEMPTS": "x"},
                         {"BFS_SIGN_RETRY_DELAY": "-1"}):
            result = self.sign(0, **settings)
            self.assertEqual(result.returncode, 2)
            self.assertIn("invalid signing retry settings", result.stderr)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
