# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/release"))
import build_identity


class BuildIdentityTests(unittest.TestCase):
    def test_completed_build(self):
        record = {"state": "complete", "source": {"source_commit": "a", "sources": {"file": "b"}},
                  "binaries": {"bfshandler": "c"}}
        build_identity.verify(record, record["source"], record["binaries"])
        for key, value, reason in (("state", "building", "did not finish"),
                                   ("source", {}, "source changed"),
                                   ("binaries", {}, "binaries changed")):
            broken = {**record, key: value}
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, reason):
                build_identity.verify(broken, record["source"], record["binaries"])


if __name__ == "__main__":
    unittest.main()
