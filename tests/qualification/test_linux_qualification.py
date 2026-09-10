#!/usr/bin/env python3
"""Contract tests for the versioned M7 qualification matrix."""

import json
from pathlib import Path
import tempfile
import unittest

import linux_qualification
import fuse_soak


class LinuxQualificationTests(unittest.TestCase):
    def test_current_matrix_is_valid(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        self.assertEqual(matrix["soak"]["target_duration_seconds"], 72 * 60 * 60)

    def test_rejects_missing_block_size(self):
        matrix = json.loads(linux_qualification.DEFAULT_MATRIX.read_text(encoding="ascii"))
        matrix["fast"]["block_sizes"].pop()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"
            path.write_text(json.dumps(matrix), encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "block size"):
                linux_qualification.load_matrix(path)

    def test_rejects_short_soak(self):
        matrix = json.loads(linux_qualification.DEFAULT_MATRIX.read_text(encoding="ascii"))
        matrix["soak"]["target_duration_seconds"] = 60
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"
            path.write_text(json.dumps(matrix), encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "72 hours"):
                linux_qualification.load_matrix(path)

    def test_rejects_unapproved_short_soak(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        with self.assertRaisesRegex(RuntimeError, "explicit preflight"):
            fuse_soak.select_duration(matrix["soak"], 60, False)

    def test_marks_short_soak_as_preflight(self):
        matrix = linux_qualification.load_matrix(linux_qualification.DEFAULT_MATRIX)
        self.assertEqual(fuse_soak.select_duration(matrix["soak"], 60, True), 60)


if __name__ == "__main__":
    unittest.main()
