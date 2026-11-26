# SPDX-License-Identifier: GPL-3.0-or-later
"""What the package may not be built with (SPEC-253, SPEC-220).

The package holds the program as released: without the hooks the tests drive it by (a program that takes
`--script` and `--fakes`), and without a sanitizer. tests/package/test_package.py checks the package that
was made; this checks that the build refuses to make such a package at all.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REFUSAL = "SIDPLAYZERO_PACKAGE=ON cannot be built"


class Refused(unittest.TestCase):
    def configure(self, *options):
        with tempfile.TemporaryDirectory() as folder:
            env = {**os.environ, "SIDPLAYZERO_MAINTAINER": os.environ.get("SIDPLAYZERO_MAINTAINER") or "Test <test@example.org>"}
            return subprocess.run(["cmake", "-S", str(ROOT), "-B", folder, "-G", "Ninja", "--preset", "cp0-cross", *options],
                                  env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT,
                                  timeout=1800)

    def test_package_with_test_hooks_or_a_sanitizer_is_refused(self):
        """SPEC-253 SPEC-220 a package build with SIDPLAYZERO_TEST_HOOKS=ON or with a sanitizer fails at once and says why"""
        for options in (["-DSIDPLAYZERO_TEST_HOOKS=ON"], ["-DSIDPLAYZERO_SANITIZE=address"],
                        ["-DSIDPLAYZERO_SANITIZE=thread"], ["-DSIDPLAYZERO_TEST_HOOKS=ON", "-DSIDPLAYZERO_SANITIZE=address"]):
            with self.subTest(options=options):
                done = self.configure(*options)
                self.assertNotEqual(done.returncode, 0, f"{options}: the package build was configured")
                self.assertIn(REFUSAL, done.stdout, done.stdout[-1500:])


if __name__ == "__main__":
    unittest.main()
