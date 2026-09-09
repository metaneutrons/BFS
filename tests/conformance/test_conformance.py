"""Contract tests for the independently built conformance infrastructure."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ORCHESTRATOR = ROOT / "tools" / "bfs-conformance.py"
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"
CORE = ROOT / "build" / "host" / "bfs-conformance-core"
POSIX = ROOT / "build" / "host" / "bfs-conformance-posix"
MKBFS = ROOT / "build" / "host" / "mkbfs"
LINK_CHECK = ROOT / "tools" / "check-conformance-linkage.sh"


def run(*command):
    return subprocess.run(command, capture_output=True, text=True, check=False)


class ConformanceTests(unittest.TestCase):
    def test_catalog_and_direct_replay(self):
        completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay",
                        str(ROOT / "tests/conformance/replays/smoke-v1.jsonl"))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(len(result["records"]), 6)
        self.assertTrue(all(record["status"] == "pass" for record in result["records"]))

    def test_replay_requires_completion_and_nonempty_selection(self):
        with tempfile.TemporaryDirectory() as temporary:
            replay = Path(temporary) / "bad.jsonl"
            replay.write_text('{"type":"bfs-conformance-replay","format_version":1}\n',
                              encoding="utf-8")
            completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay", str(replay))
        self.assertEqual(completed.returncode, 3)
        self.assertEqual(json.loads(completed.stdout)["status"], "error")

    def test_posix_backend_is_not_linked_to_bfs(self):
        self.assertEqual(run(str(LINK_CHECK), str(POSIX)).returncode, 0)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "counterprobe.c"
            binary = Path(temporary) / "counterprobe"
            source.write_text("void bfs_counterprobe(void) {}\nint main(void) { return 0; }\n",
                              encoding="utf-8")
            self.assertEqual(run("cc", "-o", str(binary), str(source)).returncode, 0)
            self.assertNotEqual(run(str(LINK_CHECK), str(binary)).returncode, 0)

    def test_oracle_fails_closed_on_non_bfs_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "not-bfs.img"
            image.write_bytes(b"\0" * 4096)
            completed = run(str(ORACLE), str(image))
        self.assertEqual(completed.returncode, 3)
        self.assertEqual(json.loads(completed.stdout)["status"], "error")

    def test_oracle_walks_a_committed_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "oracle.bfs"
            with image.open("wb") as output:
                output.truncate(4096 * 512)
            self.assertEqual(run(str(MKBFS), str(image), "4096", "Oracle").returncode, 0)
            completed = run(str(ORACLE), str(image))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["namespace"], [{"inode": 1, "links": 1, "path": "/",
                                                "size": 0, "type": 1}])

    def test_oracle_and_conformance_programs_are_independent(self):
        self.assertNotIn("bfs_", ORACLE.read_text(encoding="utf-8"))
        symbols = run("nm", "-g", str(CORE)).stdout
        self.assertIn("bfs_fs_format", symbols)


if __name__ == "__main__":
    unittest.main()
