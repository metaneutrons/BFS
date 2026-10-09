#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Explicit interim production-family result; never a full-inventory claim."""

import argparse
from pathlib import Path

import summarize

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("family", choices=("normal", "durable"))
args = parser.parse_args()
selected = [run for run in summarize.PRODUCTION_RUNS if run.family == args.family]
assert len(selected) == 16
rows = summarize.verify_inventory(Path(__file__).resolve().parent, selected)
for variant in ("m5", "sparse"):
    summarize.summarize_series(f"{args.family}, variant={variant}",
                               summarize.group_rows(rows, args.family, variant))
summarize.summarize_paired_sparse_m5(args.family, rows)
print(f"\nINTERIM {args.family.upper()}-ONLY RESULT: all16 selected runs verified; "
      "NOT FULL QUALIFICATION. Use summarize.py for all40 required runs.")
