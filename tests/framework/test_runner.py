# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-010, TF-012: tools/test and the waiting list. Tests of the test framework, not of the app."""

import os
import shutil
import tempfile
import unittest
from pathlib import Path

from support import ROOT, run

from testing import common


class WaitingList(unittest.TestCase):
    def setUp(self):
        self.waiting = {"SPEC-900 waits": common.Waiting("SPEC-900 waits", "code", "not built yet", 1)}

    def test_failing_as_expected(self):
        """TF-012 a listed test that fails with the expected words is in order"""
        verdict = common.judge([common.Result("SPEC-900 waits", common.FAILED, "player: not built yet")], self.waiting)
        self.assertEqual((verdict.waiting, verdict.faults), (1, []))

    def test_failing_otherwise(self):
        """TF-012 a listed test that fails with other words is a fault"""
        verdict = common.judge([common.Result("SPEC-900 waits", common.FAILED, "segmentation fault")], self.waiting)
        self.assertEqual(len(verdict.faults), 1)
        self.assertIn("on the waiting list", verdict.faults[0][1])

    def test_passing_while_listed(self):
        """TF-012 a listed test that passes is a fault, so it is taken off the list"""
        verdict = common.judge([common.Result("SPEC-900 waits", common.PASSED)], self.waiting)
        self.assertEqual(len(verdict.faults), 1)
        self.assertIn("take it off", verdict.faults[0][1])

    def test_unlisted(self):
        """TF-012 a test that is not listed passes or fails as usual"""
        verdict = common.judge([common.Result("SPEC-901 a", common.PASSED), common.Result("SPEC-902 b", common.FAILED, "x"),
                                common.Result("SPEC-903 c", common.SKIPPED, "part off")], self.waiting)
        self.assertEqual((verdict.passed, verdict.skipped, len(verdict.faults)), (1, 1, 1))

    def test_report_status(self):
        """TF-012 the level's status is 0 with a waiting test and 1 with a fault"""
        waiting = [common.Result("SPEC-900 waits", common.FAILED, "not built yet")]
        self.assertEqual(common.report("framework", waiting, self.waiting), 0)
        self.assertEqual(common.report("framework", [common.Result("SPEC-900 waits", common.PASSED)], self.waiting), 1)

    def test_file_format(self):
        """TF-012 the waiting list's own form is checked: a reason must be code or images"""
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "waiting.tsv"
            path.write_text("# comment\ntest\treason\twords\nSPEC-900 a\tcode\tnot built\nSPEC-901 b\tlater\tx\n")
            entries, faults = common.load_waiting(path)
            self.assertEqual(sorted(entries), ["SPEC-900 a", "SPEC-901 b"])
            self.assertEqual(len(faults), 1)
            self.assertIn("'code' or 'images'", faults[0])


class Command(unittest.TestCase):
    def test_says_what_is_missing(self):
        """TF-010 SPEC-250 a level stops with the list of programs that are not installed"""
        with tempfile.TemporaryDirectory() as folder:
            for program in ("python3", "cmake", "git", "c++"):
                found = shutil.which(program)
                self.assertIsNotNone(found, program)
                os.symlink(found, Path(folder) / program)
            process = run([shutil.which("python3"), ROOT / "tools" / "test", "unit"], env={"PATH": folder})
            self.assertEqual(process.returncode, 3, process.stdout)
            self.assertIn("Not installed: ninja", process.stdout)

    def test_unknown_word(self):
        """TF-010 SPEC-250 an unknown word gives the usage and a status other than 0"""
        process = run([ROOT / "tools" / "test", "everything"])
        self.assertEqual(process.returncode, 2)
        self.assertIn("tools/test unit", process.stdout)
