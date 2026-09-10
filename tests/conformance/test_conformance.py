"""Contract tests for the independently built conformance infrastructure."""

import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess  # nosec B404 - local test executables are invoked without a shell
import sys
import tempfile
import time
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]
ORCHESTRATOR = ROOT / "tools" / "bfs-conformance.py"
ORACLE = ROOT / "tools" / "bfs-format-oracle.py"
PERSISTENCE_MODEL = ROOT / "tools" / "bfs-persistence-model.py"
CORE = ROOT / "build" / "host" / "bfs-conformance-core"
POSIX = ROOT / "build" / "host" / "bfs-conformance-posix"
MKBFS = ROOT / "build" / "host" / "mkbfs"
FIXTURE_WRITER = ROOT / "build" / "host" / "conformance-fixture-writer"
LINK_CHECK = ROOT / "tools" / "check-conformance-linkage.sh"
sys.path.insert(0, str(ROOT / "tools"))

from bfs_command_runner import CommandResult


def run(*command):
    return subprocess.run(command, capture_output=True, text=True, check=False)  # nosec B603


def be32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def put_be32(data, offset, value):
    data[offset:offset + 4] = value.to_bytes(4, "big")


def update_node_crc(node):
    put_be32(node, 4, zlib.crc32(node[:4] + b"\0\0\0\0" + node[8:]) & 0xffffffff)


def load_oracle_module():
    spec = importlib.util.spec_from_file_location("format_oracle", ORACLE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_orchestrator_module():
    spec = importlib.util.spec_from_file_location("conformance", ORCHESTRATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ConformanceTests(unittest.TestCase):
    def test_catalog_and_direct_replay(self):
        completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay",
                        str(ROOT / "tests/conformance/replays/smoke-v1.jsonl"))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(len(result["records"]), 6)
        self.assertTrue(all(record["status"] == "pass" for record in result["records"]))
        self.assertEqual(result["contract_version"], 1)
        self.assertTrue(all(record["contract"].startswith("BFS-CONF-")
                            for record in result["records"]))
        self.assertEqual(len(result["identity"]["catalog_sha256"]), 64)
        self.assertEqual(len(result["identity"]["replay_sha256"]), 64)

    def test_full_direct_replay(self):
        completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay",
                        str(ROOT / "tests/conformance/replays/core-full-v1.jsonl"))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["status"], "pass")

    def test_catalog_preserves_matching_amiga_test_ids(self):
        catalog = json.loads((ROOT / "tests/conformance/scenarios.json").read_text(encoding="utf-8"))
        test_ids = set(re.findall(r"BFS_TEST\(([^,]+),", (ROOT / "tools/bfs-test-cases.def").read_text(
            encoding="utf-8")))
        self.assertEqual(len(test_ids), 46)
        mapped = {test_id for scenario in catalog["scenarios"]
                  for test_id in scenario.get("amiga_test_ids", [])}
        self.assertIn("fill_08", mapped)
        self.assertIn("diskfull_23", mapped)
        self.assertTrue(mapped <= test_ids)

    def test_replay_requires_completion_and_nonempty_selection(self):
        with tempfile.TemporaryDirectory() as temporary:
            replay = Path(temporary) / "bad.jsonl"
            replay.write_text('{"type":"bfs-conformance-replay","format_version":1}\n',
                              encoding="utf-8")
            completed = run(str(ORCHESTRATOR), "--backend", "core", "--replay", str(replay))
        self.assertEqual(completed.returncode, 3)
        self.assertEqual(json.loads(completed.stdout)["status"], "error")

    def test_orchestrator_rejects_a_semantically_wrong_backend_result(self):
        orchestrator = load_orchestrator_module()
        record = orchestrator.validate_backend_result(
            CommandResult(2, '{"id":"regular-file","status":"skip"}', ""), "regular-file")
        record = orchestrator.enforce_scenario_contract(record, ["pass"])
        self.assertEqual(record["code"], "contract-mismatch")

    def test_orchestrator_rejects_an_inconsistent_backend_exit(self):
        orchestrator = load_orchestrator_module()
        record = orchestrator.validate_backend_result(
            CommandResult(1, '{"id":"regular-file","status":"pass"}', ""), "regular-file")
        self.assertEqual(record["code"], "invalid-backend-exit")

    def test_posix_backend_is_not_linked_to_bfs(self):
        self.assertEqual(run(str(LINK_CHECK), str(POSIX)).returncode, 0)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "counterprobe.c"
            binary = Path(temporary) / "counterprobe"
            source.write_text("void bfs_counterprobe(void) {}\nint main(void) { return 0; }\n",
                              encoding="utf-8")
            self.assertEqual(run("/usr/bin/cc", "-o", str(binary), str(source)).returncode, 0)
            self.assertNotEqual(run(str(LINK_CHECK), str(binary)).returncode, 0)

    def test_posix_backend_requires_an_owned_nonsymlink_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "root"
            root.mkdir()
            symlink = Path(temporary) / "alias"
            symlink.symlink_to(root, target_is_directory=True)
            self.assertEqual(run(str(POSIX), "--case", "empty-volume", "--root", str(root)).returncode, 0)
            self.assertEqual(run(str(POSIX), "--case", "empty-volume", "--root", "/").returncode, 3)
            self.assertEqual(run(str(POSIX), "--case", "empty-volume", "--root", str(symlink)).returncode, 3)

    def test_runner_bounds_child_runtime_and_output(self):
        runner = __import__("bfs_command_runner")
        stdout_read, stdout_write = os.pipe()
        stderr_read, stderr_write = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(stdout_read)
            os.close(stderr_read)
            os.write(stdout_write, b"ok")
            os._exit(0)
        os.close(stdout_write)
        os.close(stderr_write)
        status, output, errors, output_limited = runner.collect_output(
            pid, stdout_read, stderr_read, 1)
        self.assertEqual(runner.result_code(status, output_limited), 0)
        self.assertEqual(output, b"ok")
        self.assertEqual(errors, b"")
        self.assertFalse(output_limited)
        stdout_read, stdout_write = os.pipe()
        stderr_read, stderr_write = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(stdout_read)
            os.close(stderr_read)
            os.write(stdout_write, b"x" * (runner.MAX_OUTPUT_BYTES + 1))
            os._exit(0)
        os.close(stdout_write)
        os.close(stderr_write)
        status, output, errors, output_limited = runner.collect_output(pid, stdout_read, stderr_read, 5)
        self.assertTrue(output_limited)
        self.assertEqual(runner.result_code(status, output_limited), 137)
        self.assertLessEqual(len(output) + len(errors), runner.MAX_OUTPUT_BYTES)
        stdout_read, stdout_write = os.pipe()
        stderr_read, stderr_write = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(stdout_read)
            os.close(stderr_read)
            time.sleep(1)
            os._exit(0)
        os.close(stdout_write)
        os.close(stderr_write)
        with self.assertRaises(runner.CommandTimeout):
            runner.collect_output(pid, stdout_read, stderr_read, 0.01)
        with self.assertRaises(ValueError):
            runner.run_command("untrusted", [], 1)

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
            {"inode": 6, "links": 1, "path": "/@bfs-hex-literal", "size": 0, "type": 0,
             "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {"inode": 3, "links": 1, "path": "/folder", "size": 0, "type": 1},
            {"inode": 4, "links": 1, "path": "/folder/nested.txt", "size": 0, "type": 0,
             "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {"inode": 5, "links": 1, "path": "/oracle-link", "size": 10, "type": 2,
             "sha256": "686c69f8298e445d01eb1a57b873b59d3d230693d7e67187660b3e2da2057469"},
            {"inode": 2, "links": 1, "path": "/oracle.txt", "size": 13, "type": 0,
             "sha256": "965876c34808b89ff803cf8e6ad48eda33f01fe2b7291b270f2956f8fe25f594"},
        ])
        self.assertEqual(result["snapshots"][0]["name"], "oracle-snapshot")

    def test_oracle_reports_hard_links_as_distinct_namespace_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "hard-link.bfs"
            self.assertEqual(run(str(FIXTURE_WRITER), str(image), "--hard-link").returncode, 0)
            completed = run(str(ORACLE), str(image))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        items = {item["path"]: item for item in json.loads(completed.stdout)["namespace"]}
        self.assertEqual(items["/oracle.txt"]["inode"], items["/oracle-hardlink"]["inode"])
        self.assertEqual(items["/oracle.txt"]["links"], 2)
        self.assertEqual(items["/oracle-hardlink"]["links"], 2)

    def test_oracle_rejects_crc_and_unsupported_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "oracle.bfs"
            self.assertEqual(run(str(FIXTURE_WRITER), str(image)).returncode, 0)
            corrupted = bytearray(image.read_bytes())
            corrupted[be32(corrupted, 24) * be32(corrupted, 8)] ^= 1
            image.write_bytes(corrupted)
            self.assertEqual(run(str(ORACLE), str(image)).returncode, 3)
            self.assertEqual(run(str(FIXTURE_WRITER), str(Path(temporary) / "future.bfs")).returncode, 0)
            future = Path(temporary) / "future.bfs"
            incompatible = bytearray(future.read_bytes())
            put_be32(incompatible, 4, 3)
            put_be32(incompatible, 236, zlib.crc32(incompatible[:236]) & 0xffffffff)
            future.write_bytes(incompatible)
            self.assertEqual(run(str(ORACLE), str(future)).returncode, 3)

    def test_oracle_rejects_a_cycle_with_a_valid_node_crc(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "cycle.bfs"
            self.assertEqual(run(str(FIXTURE_WRITER), str(image), "--directory-scale").returncode, 0)
            data = bytearray(image.read_bytes())
            block_size, root = be32(data, 8), be32(data, 24)
            node_start = root * block_size
            node = data[node_start:node_start + block_size]
            self.assertGreater(int.from_bytes(node[20:22], "big"), 0)
            capacity = (block_size - 32) // (264 + 4)
            put_be32(node, 28 + capacity * 264, root)
            update_node_crc(node)
            data[node_start:node_start + block_size] = node
            image.write_bytes(data)
            completed = run(str(ORACLE), str(image))
        self.assertEqual(completed.returncode, 3)
        self.assertEqual(json.loads(completed.stdout)["status"], "error")

    def test_oracle_and_conformance_programs_are_independent(self):
        self.assertNotIn("bfs_", ORACLE.read_text(encoding="utf-8"))
        symbols = run("/usr/bin/nm", "-g", str(CORE)).stdout
        self.assertIn("bfs_fs_format", symbols)

    def test_oracle_uses_documented_directory_key_order_for_hash_collisions(self):
        oracle = load_oracle_module()
        first = bytearray(264)
        second = bytearray(264)
        put_be32(first, 0, 1)
        put_be32(second, 0, 1)
        put_be32(first, 4, 42)
        put_be32(second, 4, 42)
        first[8:10] = b"\x01a"
        second[8:10] = b"\x01B"
        self.assertLess(oracle.key_sort_key("directory", first),
                        oracle.key_sort_key("directory", second))

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
        self.assertIn("build-essential clang python3", workflow)


if __name__ == "__main__":
    unittest.main()
