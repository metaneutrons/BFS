# SPDX-License-Identifier: MPL-2.0
"""Oracles for the predeclared retention rule, not performance evidence."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("floor_decision", Path(__file__).with_name("decide_pilot.py"))
decision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decision)


def fixture(normal=(0.97, 0.96), durable=(0.98, 0.99), requests=260, slow=(1.0, 1.0)):
    pairs = []
    for mode, ratios in (("normal", normal), ("durable", durable)):
        for repeat, ratio in enumerate(ratios, 1):
            pairs.append({"family": mode, "repeat": repeat, "bfs_ratios": {
                "APPEND_4K_1M": {"US": {"ratio": ratio}},
                "OTHER": {"US": {"ratio": slow[repeat - 1] if mode == "normal" else 1.0}},
            }})
    return {"comparisons": {
        "production_candidate_over_base": pairs,
        "diagnostic_candidate_over_base": [{"bfs_ratios": {"APPEND_4K_1M": {
            "WORK_DETAIL_BUFFER_ALLOC_CALLS": {"numerator": requests, "denominator": 517},
        }}}],
    }}


class RetentionRuleTests(unittest.TestCase):
    def test_consistent_gain_and_requests_pass(self):
        self.assertTrue(decision.decide(fixture())["retain_locally"])

    def test_one_mode_without_lower_median_fails(self):
        self.assertFalse(decision.decide(fixture(durable=(1.04, 0.97)))["retain_locally"])

    def test_two_better_medians_do_not_replace_three_better_pairs(self):
        result = decision.decide(fixture(normal=(0.8, 1.1), durable=(0.8, 1.1)))
        self.assertTrue(result["checks"]["both_primary_mode_medians_lower"])
        self.assertFalse(result["retain_locally"])

    def test_equal_buffer_requests_fail(self):
        self.assertFalse(decision.decide(fixture(requests=517))["retain_locally"])

    def test_repeated_ten_percent_threshold_is_inclusive(self):
        result = decision.decide(fixture(slow=(1.10, 1.10)))
        self.assertFalse(result["retain_locally"])
        self.assertEqual(result["repeated_slowdowns"][0]["phase"], "OTHER")

    def test_incomplete_inventory_is_not_a_decision(self):
        data = fixture()
        data["comparisons"]["production_candidate_over_base"].pop()
        with self.assertRaises(ValueError):
            decision.decide(data)


if __name__ == "__main__":
    unittest.main()
