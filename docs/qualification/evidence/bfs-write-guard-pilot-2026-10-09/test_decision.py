#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Tests of the retention rule in decide_pilot.py on synthetic summaries."""
import copy
import importlib.util
from pathlib import Path
import unittest

BUNDLE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("write_guard_decide", BUNDLE / "decide_pilot.py")
decide_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decide_module)
decide = decide_module.decide


def pair(base, candidate):
    ratio = None if base == 0 else candidate / base
    return {"denominator": base, "numerator": candidate, "ratio": ratio}


def passing_summary():
    growth = {
        "WORK_PACKET_WRITE_CALLS": pair(256, 256),
        "WORK_INODE_READ_CALLS": pair(515, 259),
        "WORK_WRITE_DETAIL_INODE_READ_CALLS": pair(515, 259),
        "WORK_CORE_FILE_WRITE_CALLS": pair(256, 256),
        "WORK_WRITE_DETAIL_INODE_WRITE_CALLS": pair(257, 257),
        "WORK_DETAIL_BUFFER_ALLOC_CALLS": pair(517, 261),
        "WORK_WRITE_DETAIL_INODE_READ_SAMPLES": pair(515, 259),
        "WORK_US": pair(124000, 118000),
    }
    diagnostic = {"APPEND_4K_1M": growth,
                  "LOOKUP_400": {"WORK_INODE_READ_CALLS": pair(400, 400)}}
    production = []
    for family in ("normal", "durable"):
        for repeat, ratio in ((1, 0.97), (2, 1.04)):
            production.append({
                "family": family, "repeat": repeat,
                "bfs_ratios": {
                    "APPEND_4K_1M": {"US": pair(70000, int(70000 * ratio))},
                    "SMALL_READ_40": {"US": pair(1000, 1050)},
                },
            })
    return {"comparisons": {
        "diagnostic_candidate_over_base": [{"bfs_ratios": diagnostic}],
        "production_candidate_over_base": production,
    }}


class DecisionTests(unittest.TestCase):
    def test_passing_summary_is_retained(self):
        result = decide(passing_summary())
        self.assertTrue(result["retain_locally"], result["checks"])
        self.assertEqual(result["reported_4k_pairs_not_slower"], 2)

    def test_medians_do_not_decide(self):
        summary = passing_summary()
        for row in summary["comparisons"]["production_candidate_over_base"]:
            row["bfs_ratios"]["APPEND_4K_1M"]["US"] = pair(70000, 75000)
        result = decide(summary)
        self.assertTrue(result["retain_locally"])
        self.assertGreater(result["reported_4k_growth"]["normal"]["median"], 1.0)

    def test_read_saving_must_be_exactly_one_per_write(self):
        for reads in (260, 258, 515):
            summary = passing_summary()
            growth = summary["comparisons"]["diagnostic_candidate_over_base"][0][
                "bfs_ratios"]["APPEND_4K_1M"]
            growth["WORK_INODE_READ_CALLS"] = pair(515, reads)
            result = decide(summary)
            self.assertFalse(result["checks"][
                "diagnostic_4k_growth_saves_one_inode_read_per_write"], reads)
            self.assertFalse(result["retain_locally"])

    def test_write_counts_must_not_change(self):
        summary = passing_summary()
        growth = summary["comparisons"]["diagnostic_candidate_over_base"][0][
            "bfs_ratios"]["APPEND_4K_1M"]
        growth["WORK_WRITE_DETAIL_INODE_WRITE_CALLS"] = pair(257, 256)
        self.assertFalse(decide(summary)["retain_locally"])

    def test_any_count_increase_blocks_but_samples_do_not(self):
        summary = passing_summary()
        phases = summary["comparisons"]["diagnostic_candidate_over_base"][0]["bfs_ratios"]
        phases["LOOKUP_400"]["WORK_INODE_READ_SAMPLES"] = pair(10, 20)
        phases["LOOKUP_400"]["WORK_DETAIL_INODE_READ_SAMPLE_TICKS"] = pair(10, 20)
        self.assertTrue(decide(summary)["retain_locally"])
        phases["LOOKUP_400"]["VOLUME_FLUSH_CACHE_READ_MISSES"] = pair(0, 1)
        result = decide(summary)
        self.assertFalse(result["retain_locally"])
        self.assertEqual(result["diagnostic_count_increases"][0]["field"],
                         "VOLUME_FLUSH_CACHE_READ_MISSES")

    def test_repeated_ten_percent_slowdown_blocks(self):
        summary = passing_summary()
        for row in summary["comparisons"]["production_candidate_over_base"]:
            if row["family"] == "durable":
                row["bfs_ratios"]["SMALL_READ_40"]["US"] = pair(1000, 1100)
        result = decide(summary)
        self.assertFalse(result["retain_locally"])
        self.assertEqual(result["repeated_slowdowns"][0]["phase"], "SMALL_READ_40")

    def test_single_slow_pair_does_not_block(self):
        summary = passing_summary()
        rows = summary["comparisons"]["production_candidate_over_base"]
        rows[0]["bfs_ratios"]["SMALL_READ_40"]["US"] = pair(1000, 1500)
        self.assertTrue(decide(summary)["retain_locally"])

    def test_missing_pairs_are_rejected(self):
        summary = copy.deepcopy(passing_summary())
        summary["comparisons"]["production_candidate_over_base"].pop()
        with self.assertRaises(ValueError):
            decide(summary)


if __name__ == "__main__":
    unittest.main()
