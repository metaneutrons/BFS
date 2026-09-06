# SPDX-License-Identifier: MPL-2.0
import json
from pathlib import Path
import subprocess  # nosec B404 - isolated local Git fixtures and the reviewed identity gate
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "tools/release/verify-identity.sh"


class ReleaseIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.git("init", "-q")
        self.git("config", "user.name", "BFS Test")
        self.git("config", "user.email", "test@example.invalid")
        self.write_versions("1.2.3")
        (self.root / "release-please-config.json").write_text(json.dumps({
            "packages": {".": {"draft": True, "force-tag-creation": True}},
        }))
        self.git("add", ".")
        self.git("commit", "-qm", "test: identity fixture")

    def git(self, *arguments):
        return subprocess.run(["git", *arguments], cwd=self.root,  # nosec B603
                              check=True, capture_output=True, text=True)

    def write_versions(self, version):
        (self.root / "version.txt").write_text(version + "\n")
        (self.root / ".release-please-manifest.json").write_text(json.dumps({".": version}))

    def gate(self, tag):
        return subprocess.run(["/bin/bash", str(SCRIPT), tag], cwd=self.root,  # nosec B603
                              capture_output=True, text=True, check=False)

    def test_lightweight_and_annotated_prerelease(self):
        self.git("tag", "v1.2.3")
        self.git("tag", "-a", "v1.2.3-qualification.1", "-m", "test tag")
        for tag in ("v1.2.3", "v1.2.3-qualification.1"):
            result = self.gate(tag)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_wrong_commit_and_version(self):
        self.git("tag", "v1.2.3")
        self.write_versions("1.2.4")
        result = self.gate("v1.2.3")
        self.assertEqual(result.returncode, 1)
        self.assertIn("do not agree", result.stderr)
        self.git("add", ".")
        self.git("commit", "-qm", "test: changed identity")
        result = self.gate("v1.2.3")
        self.assertEqual(result.returncode, 1)
        self.assertIn("but the checkout is", result.stderr)

    def test_invalid_tag_and_missing_draft_protection(self):
        for tag in ("v01.2.3", "v1.2.3-01", "v1.2.3-", "v1.2.3-a..b"):
            result = self.gate(tag)
            self.assertEqual(result.returncode, 1)
            self.assertIn("not a canonical", result.stderr)
        self.git("tag", "v1.2.3")
        (self.root / "release-please-config.json").write_text('{"packages":{".":{"draft":false}}}')
        result = self.gate("v1.2.3")
        self.assertEqual(result.returncode, 1)


if __name__ == "__main__":
    unittest.main()
