# SPDX-License-Identifier: GPL-3.0-or-later
"""The settings and the workers under ThreadSanitizer (SPEC-022, SPEC-130).

The simulator level's C++ test tests/simulator/keep_settings_threads_test.cpp has a set-up read
`url_files` on a worker thread while the UI thread sets it. A race there is something only
ThreadSanitizer sees: this runs that test in the ThreadSanitizer build the integration level makes, and
fails on any report.
"""

import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import common  # noqa: E402

# The build of the ThreadSanitizer run, as tools/testing/integration.py makes it, so that one build serves both.
TSAN_OPTIONS = {"SIDPLAYZERO_TEST_HOOKS": "ON", "SIDPLAYZERO_DISPLAY": "window", "SIDPLAYZERO_BUILD_SIM_TESTS": "ON",
                "SIDPLAYZERO_SANITIZE": "thread", "CMAKE_BUILD_TYPE": "Debug", "SIDPLAYZERO_BUILD_RACE_CANARY": "ON"}
CASE = "SPEC-022 SPEC-130 SPEC-140 a set-up reads url_files on the long worker*"


class SettingsAndWorkers(unittest.TestCase):
    def test_no_race_on_a_setting(self):
        """SPEC-022 SPEC-130 a setting the UI thread changes while a job on a worker reads it gives no ThreadSanitizer report"""
        build = common.configure_and_build(common.BUILD / "simulator-tsan", TSAN_OPTIONS)
        program = build / "tests" / "simulator" / "spz_sim_tests"
        done = subprocess.run([str(program), f"--test-case={CASE}", "--no-colors=true"], text=True, errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600,
                              env={**os.environ, "TSAN_OPTIONS": "halt_on_error=0"})
        tail = " | ".join(done.stdout.strip().split("\n")[-6:])
        self.assertIn("1 passed", done.stdout, "the test did not run and pass: " + tail)
        self.assertNotIn("ThreadSanitizer", done.stdout, "a report: " + done.stdout[:3000])
        self.assertEqual(done.returncode, 0, tail)


if __name__ == "__main__":
    unittest.main()
