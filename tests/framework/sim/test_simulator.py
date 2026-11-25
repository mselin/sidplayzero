# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-041 to TF-045: the simulator level's own machinery. Tests of the test framework, not of the app."""

import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from testing import png, screen, simulator  # noqa: E402

PROGRAM = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "sidplayzero"
SCRIPTS = ROOT / "tests" / "framework" / "sim"


class Scripts(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp(prefix="spz-sim-"))

    def tearDown(self):
        shutil.rmtree(self.out, ignore_errors=True)

    def run_script(self, name, out, extra=(), prefix=()):
        out.mkdir(parents=True, exist_ok=True)
        return simulator.run_script(PROGRAM, SCRIPTS / name, out, extra_args=extra, prefix=prefix)

    def test_same_bytes_twice(self):
        """TF-042 SPEC-253 two runs of one script give the same dumps and the same pictures, byte for byte"""
        for run in ("one", "two"):
            status, output, _ = self.run_script("canary_passes.script", self.out / run)
            self.assertEqual(status, 0, output)
        for name in ("empty.json", "empty.png"):
            self.assertEqual((self.out / "one" / name).read_bytes(), (self.out / "two" / name).read_bytes(), name)

    def test_failure_report(self):
        """TF-042 SPEC-253 a failed expect writes the line, what was expected and found, the description and the screen"""
        status, output, _ = self.run_script("status_1.script", self.out)
        self.assertEqual(status, 1, output)
        report = (self.out / "failure.txt").read_text()
        self.assertIn("status_1.script:4", report)
        self.assertIn('expected: = Some other app', report)
        self.assertIn("found: SidPlayZero", report)
        self.assertEqual(json.loads((self.out / "failure.json").read_text())["screen"], "empty")
        self.assertEqual(png.read(self.out / "failure.png")[:2], (320, 170))

    def test_new_home_each_time(self):
        """TF-041 SPEC-253 every script starts in a new, empty home folder"""
        status, output, _ = self.run_script("restart_and_files.script", self.out / "one")
        self.assertEqual(status, 0, output)
        status, output, _ = self.run_script("restart_and_files.script", self.out / "two")
        self.assertEqual(status, 0, output)   # its first line expects the file of the run before to be absent

    def test_no_fakes_means_no_network(self):
        """TF-041 SPEC-253 without an http.tsv the fake network is forbidden, and a run that used it would fail"""
        status, output, _ = self.run_script("canary_passes.script", self.out)
        self.assertEqual(status, 0, output)

    def test_window(self):
        """TF-045 SPEC-253 the windowed build draws a frame under a virtual display and leaves on a held ESC"""
        if shutil.which("xvfb-run") is None:
            self.skipTest("xvfb-run is not installed")
        status, output, _ = self.run_script("window.script", self.out, extra=("--window", "--clock", "real"),
                                            prefix=("xvfb-run", "-a"))
        self.assertEqual(status, 0, output)
        width, height, rows = png.read(self.out / "window.png")
        self.assertEqual((width, height), (320, 170))
        self.assertGreater(len({bytes(row[x:x + 3]) for row in rows for x in range(0, width * 3, 3)}), 2,
                           "the picture of the window is one flat colour")


class Descriptions(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp(prefix="spz-sim-"))
        status, output, _ = simulator.run_script(PROGRAM, SCRIPTS / "canary_passes.script", self.out)
        self.assertEqual(status, 0, output)
        self.description = json.loads((self.out / "empty.json").read_text())

    def tearDown(self):
        shutil.rmtree(self.out, ignore_errors=True)

    def test_stub_passes(self):
        """TF-043 SPEC-254 the stub's description has every field of the table and nothing else"""
        self.assertEqual(screen.problems(self.description), [])
        self.assertEqual(list(self.description), screen.TOP)

    def test_unknown_field(self):
        """TF-043 SPEC-254 a description with a field the table does not name is refused"""
        self.description["colour"] = "blue"
        self.assertIn("has the field 'colour'", " ".join(screen.problems(self.description)))

    def test_wrong_form(self):
        """TF-043 SPEC-254 a value in a form the table does not give is refused: MOS6581 for the chip model"""
        self.description["player"] = {
            "state": "playing", "message": None, "path": "/a.sid", "subtune": 1, "subtunes": 1, "elapsed_ms": 0,
            "length_ms": 1000, "engine": "light", "chips": 1, "model": "MOS6581", "clock": "PAL",
            "muted": [[False, False, False]], "filter": [True], "chip": 1, "mode": "subtunes", "underrun_frames": 0}
        problems = " ".join(screen.problems(self.description))
        self.assertIn("player.model", problems)
        self.description["player"]["model"] = "6581"
        self.assertEqual(screen.problems(self.description), [])

    def test_unknown_message(self):
        """TF-043 SPEC-254 SPEC-116 a notice whose identifier is not in the message table is refused"""
        self.description["notice"] = {"id": "MSG_MADE_UP", "text": "x"}
        self.assertIn("not an identifier of SPEC-116", " ".join(screen.problems(self.description)))


class Pictures(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp(prefix="spz-sim-"))
        self.golden = Path(tempfile.mkdtemp(prefix="spz-golden-"))
        status, output, _ = simulator.run_script(PROGRAM, SCRIPTS / "canary_passes.script", self.out)
        self.assertEqual(status, 0, output)
        self.saved, simulator.GOLDEN = simulator.GOLDEN, self.golden

    def tearDown(self):
        simulator.GOLDEN = self.saved
        shutil.rmtree(self.out, ignore_errors=True)
        shutil.rmtree(self.golden, ignore_errors=True)

    def test_no_reference(self):
        """TF-044 SPEC-255 a shot with no stored image is reported as "no reference yet" and does not fail"""
        problems, notes = simulator.judge_output(self.out, "x")
        self.assertEqual(problems, [])
        self.assertEqual(notes, ["empty.png: no reference yet"])

    def test_equal_reference(self):
        """TF-044 SPEC-255 a shot equal to its stored image passes"""
        shutil.copy(self.out / "empty.png", self.golden / "empty.png")
        self.assertEqual(simulator.judge_output(self.out, "x"), ([], []))

    def test_one_pixel(self):
        """TF-044 SPEC-255 a shot that differs from its stored image in one pixel fails, and the difference is written"""
        width, height, rows = png.read(self.out / "empty.png")
        changed = [bytearray(row) for row in rows]
        changed[100][30] ^= 0xFF
        png.write(self.golden / "empty.png", width, height, changed)
        problems, _ = simulator.judge_output(self.out, "x")
        self.assertEqual(problems, ["empty.png: 1 pixel(s) differ from tests/golden/empty.png"])
        self.assertTrue((self.out / "empty.diff.png").exists())
