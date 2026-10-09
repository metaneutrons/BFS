#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Validate all four write-attribution runs and retain every raw field."""
import importlib.util
import json
from pathlib import Path, PurePosixPath
import sys
from types import SimpleNamespace

BUNDLE = Path(__file__).resolve().parent
COMMON_PATH = BUNDLE.parent / "bfs-read-flush-cachy-2026-10-09/summarize_profile.py"
SPEC = importlib.util.spec_from_file_location("write_profile_common", COMMON_PATH)
common = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = common
SPEC.loader.exec_module(common)

HANDLER = "e2f99ecc68e411358f96e5f85a9d452518af0d8c127b4cec4a3d3319d191d39c"
GUEST = "20e751e8c558af9ca80ed4c597162fad272326fb8cf0265c3f4a7ad8c2395bf2"
PFS3 = "bc72facfdddf32e312ed3ea136849da195f2a2300f179e69599b853fe8974df7"
SCHEDULE = (
    ("split-write-compare", "bfs-first"),
    ("split-write-durable-compare", "bfs-first"),
    ("split-write-durable-compare", "pfs3-first"),
    ("split-write-compare", "pfs3-first"),
)


def summarize(results_root):
    expected_lines = ["sequence\tmode\torder\trun_name"]
    for index, (mode, order) in enumerate(SCHEDULE, 1):
        expected_lines.append(f"{index}\t{mode}\t{order}\twrite-{mode}-{order}")
    if (results_root / "schedule.tsv").read_bytes() != ("\n".join(expected_lines) + "\n").encode("ascii"):
        raise ValueError("schedule differs from the fixed four-start inventory")
    names = {f"write-{mode}-{order}" for mode, order in SCHEDULE}
    if {path.name for path in results_root.iterdir() if path.is_dir()} != names:
        raise ValueError("missing or additional run directory")
    expected_runtime = [
        (HANDLER, "system/L/bfshandler"),
        (PFS3, "system/L/pfs3aio"),
        (GUEST, "system/C/fs-compare-bench"),
    ]
    shared_assets = None
    runs = {}
    for index, (mode, order) in enumerate(SCHEDULE, 1):
        name = f"write-{mode}-{order}"
        run_dir = results_root / name
        entry = SimpleNamespace(run_name=name)
        for receipt in ("runtime-inputs.sha256", "runtime-inputs.post.sha256"):
            if common.read_receipt(run_dir / receipt) != expected_runtime:
                raise ValueError(f"{name}: runtime inputs do not match pinned binaries")
        receipt = common.read_receipt(run_dir / "installed-assets.sha256")
        assets = {path: digest for digest, path in receipt}
        if len(assets) != 67:
            raise ValueError(f"{name}: expected exactly 67 non-handler assets")
        for path in assets:
            pure = PurePosixPath(path)
            if pure.is_absolute() or ".." in pure.parts or pure.parts[0] not in {"C", "L", "Libs"}:
                raise ValueError(f"{name}: invalid asset path")
        if ("L/bfshandler" in assets or assets.get("C/fs-compare-bench") != GUEST
                or assets.get("L/pfs3aio") != PFS3):
            raise ValueError(f"{name}: asset identities do not match")
        if shared_assets is not None and assets != shared_assets:
            raise ValueError(f"{name}: installed non-handler assets differ")
        shared_assets = assets
        common.check_rdb(entry, run_dir)
        common.check_fs_uae_config(entry, run_dir)
        startup = (run_dir / "system/S/Startup-Sequence").read_text(encoding="ascii").splitlines()
        if [line.strip() for line in startup if line.strip().lower().startswith("stack")] != ["Stack 32768"]:
            raise ValueError(f"{name}: split stack guard differs")
        args = "split-write-durable" if mode.endswith("-durable-compare") else "split-write"
        sequence = [("DH1:", "bfs"), ("DH2:", "pfs3")]
        if order == "pfs3-first":
            sequence.reverse()
        commands = [f"C:fs-compare-bench {drive} {args} >SYS:Results/{fs}.split.tsv"
                    for drive, fs in sequence]
        if [line.strip() for line in startup if "C:fs-compare-bench" in line] != commands:
            raise ValueError(f"{name}: startup mode/order differs")
        result_dir = run_dir / "system/Results"
        common.VERIFIER.verify(result_dir, mode)
        filesystems = {}
        for fs in ("bfs", "pfs3"):
            values = common.VERIFIER._read_tsv(result_dir / f"{fs}.split.tsv",
                                               common.VERIFIER.build_schema(fs, mode))
            filesystems[fs] = common._phase_rows(values, fs)
        runs[name] = {"sequence": index, "mode": mode, "order": order,
                      "filesystems": filesystems}
    return {
        "scope": "Four single-candidate attribution starts, not paired handler comparisons or production speedup qualification. All probe timings are inclusive and nested; no exclusive-time decomposition or sampling extrapolation.",
        "verified_schedule_count": 4,
        "verified_non_handler_asset_count": len(shared_assets),
        "non_handler_assets_identical_across_runs": True,
        "runs": runs,
    }


if __name__ == "__main__":
    try:
        result = summarize(Path(sys.argv[1]) if len(sys.argv) == 2 else BUNDLE / "results")
    except (OSError, ValueError) as error:
        raise SystemExit(f"ERROR: {error}") from error
    print(json.dumps(result, indent=2, sort_keys=True))
