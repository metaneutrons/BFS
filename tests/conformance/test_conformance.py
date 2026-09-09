"""Contract tests for the independently built conformance infrastructure."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ORCHESTRATOR = ROOT / "tools" / "bfs-conformance.py"
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"
PERSISTENCE_MODEL = ROOT / "tools" / "bfs-persistence-model.py"
CORE = ROOT / "build" / "host" / "bfs-conformance-core"
POSIX = ROOT / "build" / "host" / "bfs-conformance-posix"
MKBFS = ROOT / "build" / "host" / "mkbfs"
FIXTURE_WRITER = ROOT / "build" / "host" / "conformance-fixture-writer"
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

    def test_full_direct_replay(self):
        completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay",
                        str(ROOT / "tests/conformance/replays/core-full-v1.jsonl"))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["status"], "pass")

    def test_replay_requires_completion_and_nonempty_selection(self):
        with tempfile.TemporaryDirectory() as temporary:
            replay = Path(temporary) / "bad.jsonl"
            replay.write_text('{"type":"bfs-conformance-replay","format_version":1}\n',
                              encoding="utf-8")
            completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay", str(replay))
        self.assertEqual(completed.returncode, 3)
        self.assertEqual(json.loads(completed.stdout)["status"], "error")

    def test_orchestrator_rejects_a_semantically_wrong_backend_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            backend = Path(temporary) / "wrong-backend"
            backend.write_text("#!/usr/bin/env python3\nimport json\n"
                               "print(json.dumps({'id': 'regular-file', 'status': 'skip'}))\n",
                               encoding="utf-8")
            backend.chmod(0o700)
            completed = run(str(ORCHESTRATOR), "--backend", "core", "--case", "regular-file",
                            "--core-program", str(backend))
        self.assertEqual(completed.returncode, 3)
        record = json.loads(completed.stdout)["records"][0]
        self.assertEqual(record["code"], "contract-mismatch")

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

    def test_oracle_exports_namespace_content_and_snapshot_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "oracle.bfs"
            self.assertEqual(run(str(FIXTURE_WRITER), str(image)).returncode, 0)
            completed = run(str(ORACLE), str(image))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["namespace"], [
            {"inode": 1, "links": 1, "path": "/", "size": 0, "type": 1},
            {"inode": 2, "links": 1, "path": "/oracle.txt", "size": 15, "type": 0,
             "sha256": "1f13f9bcc6269144c0c5d7e8103d596585333c9a376a75d5a48951907280e3a7"},
        ])
        self.assertEqual(result["snapshots"][0]["name"], "oracle-snapshot")

    def test_oracle_and_conformance_programs_are_independent(self):
        self.assertNotIn("bfs_", ORACLE.read_text(encoding="utf-8"))
        symbols = run("nm", "-g", str(CORE)).stdout
        self.assertIn("bfs_fs_format", symbols)

    def test_persistence_model_separates_acknowledgement_from_media_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan = Path(temporary) / "persistence.json"
            plan.write_text(json.dumps({"format_version": 1, "writes": [
                {"block": 3, "data": "metadata", "ack": "ok", "persist": "none"},
                {"block": 2, "data": "contents", "ack": "error", "persist": "full",
                 "persist_order": 1},
                {"block": 4, "data": "superblock", "ack": "ok", "persist": "torn"},
            ]}), encoding="utf-8")
            completed = run(str(PERSISTENCE_MODEL), str(plan))
        self.assertEqual(completed.returncode, 0)
        result = json.loads(completed.stdout)
        self.assertEqual(result["acknowledged_blocks"], [3, 4])
        self.assertEqual(result["persisted_blocks"], [
            {"block": 2, "data": "contents"}, {"block": 4, "data": "super"},
        ])

    def test_conformance_is_a_required_host_and_debian_check(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertIn('make host-test conformance-test HOST_CC="$HOST_CC"', workflow)
        self.assertIn("make tools host-test conformance-test HOST_CC=gcc", workflow)


if __name__ == "__main__":
    unittest.main()
