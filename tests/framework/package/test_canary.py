# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-071: the package level's canaries. Tests of the test framework, not of the app."""

import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from testing import deb  # noqa: E402

DEB = Path(os.environ.get("SPZ_DEB", ""))


class Canaries(unittest.TestCase):
    def test_passes(self):
        """TF-071 canary: the package level looks into the package and finds the program"""
        self.assertIn("/usr/bin/sidplayzero", [path for _, _, path in deb.files(DEB)])

    def test_fails(self):
        """TF-071 SPEC-250 canary: the package is expected to hold a file that is not there [canary-fails]"""
        self.assertIn("/usr/bin/some-other-program", [path for _, _, path in deb.files(DEB)])
