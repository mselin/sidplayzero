# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-191, the first clause of its oracle: the player's own unit tests pass against a fake SoundBackend
whose can() is the hardware's.

The player's tests belong to another part of the test suite and make their fake backend themselves, so
this test does not repeat them: it runs them again, in the unit test program as it is built, with the
environment variable SPZ_FAKE_BACKEND_CAN set to "hardware". The fake SoundBackend (tests/fakes/fakes.h)
is to start with the abilities of spz::usbsid_abilities() when it finds that variable so. One test of
tests/unit/lists_usbsid_test.cpp is this run's proof that it did.

The part `usbsid` is not built unless asked for (SPEC-115); without it this test is skipped.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("SPZ_UNIT_BUILD", ROOT / "build" / "unit"))
PROGRAM = BUILD / "tests" / "unit" / "spz_unit_tests"
PROOF = "SPEC-191 in a run that asks for the hardware's can(), the fake backend reports it [parts: usbsid]"
PLAYER_ITEMS = [f"SPEC-{number:03d}" for number in range(60, 70)]


def parts_built(build):
    """The parts a build folder was configured with, from its cache (cmake/parts.cmake)."""
    cache = Path(build) / "CMakeCache.txt"
    if not cache.exists():
        return set()
    asked = re.search(r"(?m)^SIDPLAYZERO_PARTS:[A-Z]+=(.*)$", cache.read_text(encoding="utf-8"))
    return set(re.split(r"[;,]", asked.group(1))) if asked else set()


def run_cases(pattern):
    """Runs the test cases whose names fit a doctest pattern with the hardware's abilities asked for.
    Returns {name: "" if it passed, else why it failed}."""
    with tempfile.TemporaryDirectory() as folder:
        report = Path(folder) / "junit.xml"
        subprocess.run([str(PROGRAM), f"--test-case={pattern}", "--test-case-exclude=*[canary-fails*",
                        "--reporters=junit", f"--out={report}", "--no-colors=true"],
                       env=dict(os.environ, SPZ_FAKE_BACKEND_CAN="hardware"), text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        outcomes = {}
        if report.exists():
            for case in ET.parse(report).getroot().iter("testcase"):
                bad = case.find("failure")
                bad = case.find("error") if bad is None else bad
                outcomes[case.get("name")] = "" if bad is None else (bad.get("message", "") + " " + (bad.text or "")).strip()
        return outcomes


class HardwareAbilities(unittest.TestCase):
    def test_player_tests_pass_with_the_hardwares_abilities(self):
        """SPEC-191 the player's own unit tests pass against a fake backend whose can() is the hardware's [parts: usbsid]"""
        if "usbsid" not in parts_built(BUILD):
            self.skipTest("needs the part usbsid")
        # First, that the fake backend took the hardware's abilities in such a run.
        proof = run_cases("*SPEC-191 in a run that asks for the hardware*")
        self.assertEqual(proof.get(PROOF), "", f"the fake backend did not take the hardware's can(): {proof.get(PROOF)!r}")
        # Then the player's tests: every one that names an item of the player, 060 to 069, as SPEC-191 says.
        outcomes = run_cases(",".join(f"*{item}*" for item in PLAYER_ITEMS))
        self.assertGreater(len(outcomes), 0, "no test of the player ran: none names SPEC-060 to SPEC-069")
        failed = sorted(f"{name}: {' '.join(why.split())[:200]}" for name, why in outcomes.items() if why)
        self.assertFalse(failed, f"{len(failed)} of the player's tests fail with the hardware's can(): " + "; ".join(failed))


if __name__ == "__main__":
    sys.exit(unittest.main())
