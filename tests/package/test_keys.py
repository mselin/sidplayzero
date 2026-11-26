# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-103 at the package level: the version the About screen shows is the package's.

The About screen cannot be drawn from a package, which has no test hooks. What ties the two is
`sidplayzero --version`, which prints the first two of the About screen's lines (SPEC-103): the simulator
level checks that those lines are the About screen's, and this test checks that the packaged program
prints them with the version of the control file and of app-builder.json."""

import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import deb  # noqa: E402

DEB = Path(os.environ.get("SPZ_DEB", ""))


class About(unittest.TestCase):
    def test_version(self):
        """SPEC-103 the packaged program gives the version of the package's control file and of app-builder.json, with the two libraries, as the About screen's first lines"""
        control = deb.control(DEB)
        self.assertEqual(control["Version"], deb.version() + "-1")
        done = deb.in_container(DEB, f"""
            apt-get update -q >/dev/null
            apt-get install -y -q --no-install-recommends /dist/{DEB.name} >/dev/null
            sidplayzero --version
        """)
        self.assertEqual(done.returncode, 0, done.stdout[-1500:])
        printed = [line.strip() for line in done.stdout.split("\n") if line.strip()]
        first = f"SidPlayZero {deb.version()}"
        self.assertTrue(first in printed, f"--version does not print the line {first}")
        after = " ".join(printed[printed.index(first) + 1:])        # what apt said comes before; this is the program's
        self.assertTrue("libsidplayfp 3.1.2" in after, "--version does not print libsidplayfp 3.1.2 after the app's own line")
        self.assertTrue("libresidfp 1.3.0" in after, "--version does not print libresidfp 1.3.0 after the app's own line")
