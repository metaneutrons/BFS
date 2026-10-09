# SPDX-License-Identifier: MPL-2.0
"""Exercise benchmark cache-capacity configuration without creating RDB images."""

import json
import os
from pathlib import Path
import shutil
import subprocess  # nosec B404 - executes fixed repository scripts in temporary projects
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT / "emulator-test/build-bench-image.sh"
SERIES = ROOT / "emulator-test/bench-series.sh"


RDBTOOL_STUB = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
with open(os.environ["RDBTOOL_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\n")

if "-f" in args:
    image = Path(args[args.index("-f") + 1])
else:
    image = Path(args[args.index("-r") + 1])

if "create" in args:
    image.touch()
    add = args.index("add")
    end = next((index for index in range(add + 1, len(args)) if args[index] == "+"), len(args))
    buffers = next((item.split("=", 1)[1] for item in args[add + 1:end]
                    if item.startswith("num_buffer=")), "30")
    image.with_name(image.name + ".capacity").write_text(buffers, encoding="ascii")
elif "info" in args:
    buffers = image.with_name(image.name + ".capacity").read_text(encoding="ascii")
    print(f"Partition: name=DH1\nnum_buffer: {buffers}")
elif "json" in args:
    buffers = image.with_name(image.name + ".capacity").read_text(encoding="ascii")
    print(json.dumps({"rdb": {"partitions": [{"name": "DH1", "dos_env": {
        "num_buffer": int(buffers)
    }}]}}))
'''


class BenchCacheCapacityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.temp_root = Path(self.temp.name)

    @staticmethod
    def executable(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        path.chmod(0o755)
        return path

    def make_builder_project(self, name):
        project = self.temp_root / name
        emulator = project / "emulator-test"
        emulator.mkdir(parents=True)
        shutil.copyfile(BUILDER, emulator / BUILDER.name)
        shutil.copyfile(ROOT / "emulator-test/bench-config.sh", emulator / "bench-config.sh")

        assets = project / "assets"
        (assets / "C").mkdir(parents=True)
        (assets / "C/Format").write_text("mock command\n", encoding="ascii")
        for path in (
            project / "mock/A1200.rom",
            project / "mock/pfs3aio",
            project / "build/amiga/bfshandler",
            project / "build/amiga/fs-compare-bench",
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("mock asset\n", encoding="ascii")
        self.executable(project / "build/host/bfs", "#!/bin/sh\nexit 0\n")

        mock_bin = project / "mock-bin"
        self.executable(mock_bin / "rdbtool", RDBTOOL_STUB)
        self.executable(mock_bin / "dd", "#!/bin/sh\nexit 0\n")
        log = project / "rdbtool.jsonl"
        return project, {
            **os.environ,
            "PATH": str(mock_bin) + os.pathsep + os.environ.get("PATH", ""),
            "RDBTOOL_LOG": str(log),
            "BFS_AMIGA_ASSETS_DIR": str(assets),
            "BFS_ROM_FILE": str(project / "mock/A1200.rom"),
            "BFS_PFS3_HANDLER": str(project / "mock/pfs3aio"),
            "BFS_BENCH_HANDLER_FILE": str(project / "build/amiga/bfshandler"),
            "BFS_BENCH_GUEST_FILE": str(project / "build/amiga/fs-compare-bench"),
            "BFS_BENCH_FORMATTER_FILE": str(project / "build/host/bfs"),
        }, log

    def run_script(self, script, env, *args):
        return subprocess.run(
            ["/bin/bash", str(script), *args],
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def make_series_project(self, name):
        project = self.temp_root / name
        emulator = project / "emulator-test"
        emulator.mkdir(parents=True)
        shutil.copyfile(SERIES, emulator / SERIES.name)
        shutil.copyfile(ROOT / "emulator-test/bench-config.sh", emulator / "bench-config.sh")

        builder_log = project / "builder.jsonl"
        builder_stub = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path

record = {
    "buffers": os.environ.get("BFS_BENCH_BUFFERS"),
    "handler": os.environ.get("BFS_BENCH_HANDLER_FILE"),
    "run_dir": os.environ["BFS_BENCH_RUN_DIR"],
    "order": os.environ["BFS_BENCH_ORDER"],
}
with open(os.environ["SERIES_BUILDER_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(record) + "\n")
run_dir = Path(record["run_dir"])
run_dir.mkdir(parents=True)
(run_dir / "bench-bfs.hdf").write_text("mock image", encoding="ascii")
'''
        self.executable(emulator / "build-bench-image.sh", builder_stub)
        self.executable(emulator / "run-bench.sh", "#!/bin/bash\nexit 0\n")

        handlers = project / "handlers"
        handlers.mkdir()
        plain = handlers / "plain-handler"
        literal_at = handlers / "handler@8"
        plain.write_text("mock handler\n", encoding="ascii")
        literal_at.write_text("mock handler with @ in filename\n", encoding="ascii")
        env = {
            **os.environ,
            "SERIES_BUILDER_LOG": str(builder_log),
        }
        env.pop("BFS_BENCH_BUFFERS", None)
        if os.uname().sysname == "Linux":
            mock_bin = project / "mock-bin"
            self.executable(mock_bin / "xvfb-run", "#!/bin/bash\nshift\nexec \"$@\"\n")
            env["PATH"] = str(mock_bin) + os.pathsep + env.get("PATH", "")
        return project, env, builder_log, plain, literal_at

    def test_builder_defaults_to_thirty_and_only_sets_bfs_dosenv(self):
        project, env, log = self.make_builder_project("builder-default")
        run_dir = project / "runs/default"
        env["BFS_BENCH_RUN_DIR"] = str(run_dir)
        result = self.run_script(project / "emulator-test/build-bench-image.sh", env)
        self.assertEqual(result.returncode, 0, result.stderr)

        calls = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        creates = [call for call in calls if "create" in call]
        bfs = next(call for call in creates if call[call.index("-f") + 1].endswith("bench-bfs.hdf"))
        pfs3 = next(call for call in creates if call[call.index("-f") + 1].endswith("bench-pfs3.hdf"))
        bfs_add = bfs[bfs.index("add") + 1:bfs.index("+", bfs.index("add") + 1)]
        pfs3_add = pfs3[pfs3.index("add") + 1:pfs3.index("+", pfs3.index("add") + 1)]
        self.assertIn("num_buffer=30", bfs_add)
        self.assertEqual(pfs3_add, [
            "name=DH2", "start=2", "end=1023", "dostype=0x50465303", "bootable=False",
        ])
        self.assertFalse(any("num_buffer=" in arg or "buffers=" in arg for arg in pfs3))
        self.assertIn("version=20.0", pfs3)

        info = (run_dir / "bfs-rdb-info.txt").read_text(encoding="utf-8")
        rdb_json = json.loads((run_dir / "bfs-rdb.json").read_text(encoding="utf-8"))
        self.assertIn("num_buffer: 30", info)
        self.assertEqual(rdb_json["rdb"]["partitions"][0]["dos_env"]["num_buffer"], 30)
        read_only = [call for call in calls if "-r" in call]
        self.assertEqual([call[call.index("-r") + 2] for call in read_only], ["info", "json"])

    def test_builder_forwards_custom_capacity(self):
        project, env, log = self.make_builder_project("builder-custom")
        env["BFS_BENCH_RUN_DIR"] = str(project / "runs/custom")
        env["BFS_BENCH_BUFFERS"] = "007"
        result = self.run_script(project / "emulator-test/build-bench-image.sh", env)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        bfs = next(
            call for call in calls
            if "create" in call and call[call.index("-f") + 1].endswith("bench-bfs.hdf")
        )
        bfs_add = bfs[bfs.index("add") + 1:bfs.index("+", bfs.index("add") + 1)]
        self.assertIn("num_buffer=7", bfs_add)

    def test_builder_rejects_bad_capacity_before_creating_run_directory(self):
        project, env, _ = self.make_builder_project("builder-invalid")
        run_dir = project / "runs/invalid"
        env["BFS_BENCH_RUN_DIR"] = str(run_dir)
        env["BFS_BENCH_BUFFERS"] = "129"
        result = self.run_script(project / "emulator-test/build-bench-image.sh", env)
        self.assertEqual(result.returncode, 2)
        self.assertIn("BFS_BENCH_BUFFERS", result.stderr)
        self.assertFalse(run_dir.exists())

    def test_series_capacity_suffix_common_default_and_literal_path(self):
        project, env, log, plain, literal_at = self.make_series_project("series-forwarding")
        env["BFS_BENCH_BUFFERS"] = "12"
        result = self.run_script(
            project / "emulator-test/bench-series.sh",
            env,
            "forwarding", "1", "compare",
            f"suffix={plain}@7", f"one={plain}@1", f"max={plain}@128",
            f"common={plain}", f"literal={literal_at}",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        self.assertEqual([row["buffers"] for row in rows], ["7", "1", "128", "12", "12"])
        self.assertEqual(rows[0]["handler"], str(plain))
        self.assertEqual(rows[4]["handler"], str(literal_at))

        default_project, default_env, default_log, default_handler, _ = self.make_series_project(
            "series-default"
        )
        result = self.run_script(
            default_project / "emulator-test/bench-series.sh",
            default_env,
            "default", "1", "compare", f"default={default_handler}",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        default_row = json.loads(default_log.read_text(encoding="utf-8").splitlines()[0])
        self.assertEqual(default_row["buffers"], "30")

    def test_series_rejects_invalid_capacities_before_creating_run_directories(self):
        project, env, log, handler, _ = self.make_series_project("series-invalid")
        script = project / "emulator-test/bench-series.sh"
        for index, capacity in enumerate(("", "0", "129", "1.5", "abc", "9" * 1000)):
            with self.subTest(capacity=capacity):
                result = self.run_script(
                    script, env, f"invalid-{index}", "1", "compare",
                    f"bad={handler}@{capacity}",
                )
                self.assertEqual(result.returncode, 2)
                self.assertIn("capacity", result.stderr)
                self.assertFalse((project / "build/benchmark").exists())
                self.assertFalse(log.exists())

        common_env = {**env, "BFS_BENCH_BUFFERS": "000"}
        result = self.run_script(
            script, common_env, "invalid-common", "1", "compare", f"ok={handler}",
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("BFS_BENCH_BUFFERS", result.stderr)
        self.assertFalse((project / "build/benchmark").exists())
        self.assertFalse(log.exists())

    def test_series_refuses_to_overwrite_existing_run(self):
        project, env, log, handler, _ = self.make_series_project("series-overwrite")
        run_dir = project / "build/benchmark/preserve-plain-1-bfs-first"
        run_dir.mkdir(parents=True)
        marker = run_dir / "marker.txt"
        marker.write_text("preserve\n", encoding="ascii")
        result = self.run_script(
            project / "emulator-test/bench-series.sh",
            env,
            "preserve", "1", "compare", f"plain={handler}",
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("exists", result.stderr)
        self.assertEqual(marker.read_text(encoding="ascii"), "preserve\n")
        self.assertFalse(log.exists())

    def test_series_rejects_duplicate_names_before_creating_run_directories(self):
        project, env, log, handler, _ = self.make_series_project("series-duplicate")
        result = self.run_script(
            project / "emulator-test/bench-series.sh",
            env,
            "duplicate", "1", "compare",
            f"same={handler}", f"same={handler}@5",
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("duplicate benchmark name: same", result.stderr)
        self.assertFalse((project / "build/benchmark").exists())
        self.assertFalse(log.exists())


if __name__ == "__main__":
    unittest.main()
