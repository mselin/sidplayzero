# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of playing that a script alone cannot settle: they run a script of tests/simulator/ and then look
at what it left in its --out folder, the samples the recording sink was given or two dumps of the screen.

The scripts here run with `--engine fake`. The fake engine's sound is a ramp: the left sample of frame i of
subtune s is (s x 1000 + i) modulo 32000, and the right one its negative. So what the sink received says
exactly which frames of the tune it was given, and a frame of silence is 0, 0.
"""

import array
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import simulator  # noqa: E402

PROGRAM = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "sidplayzero"
SCRIPTS = ROOT / "tests" / "simulator"
RATE = 48000


def stopped_at(output):
    """One line saying where a script stopped, from what the program printed."""
    lines = [line.strip() for line in output.split("\n") if line.strip()]
    line = next((text for text in lines if text.startswith("line: ")), lines[-1] if lines else "no output")
    return line.replace("line: ", "")


def frames_of(path):
    """A saved recording as a list of (left, right) frames."""
    samples = array.array("h")
    samples.frombytes(Path(path).read_bytes())
    if sys.byteorder != "little":
        samples.byteswap()
    return list(zip(samples[0::2], samples[1::2]))


def ramp(subtune, count):
    """The first `count` frames the fake engine renders for a subtune."""
    frames = []
    for index in range(count):
        left = (subtune * 1000 + index) % 32000
        frames.append((left, -left))
    return frames


def without_silence(frames):
    return [frame for frame in frames if frame != (0, 0)]


class Played(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp(prefix="spz-play-"))

    def tearDown(self):
        shutil.rmtree(self.out, ignore_errors=True)

    def run_script(self, name):
        """Runs a script to its end. Fails, in one line, if an expectation of its own did not hold."""
        status, output, _ = simulator.run_script(PROGRAM, SCRIPTS / name, self.out)
        if status != 0:
            self.fail(f"{name} ended with status {status} at: {stopped_at(output)}")

    def assert_start_of_the_tune(self, recording, subtune, at_least_seconds):
        """What was heard is the subtune from its first frame on, with nothing lost, repeated or out of
        place. Silence, which is what the sink gets while the device is away or opening, is left out."""
        heard = without_silence(frames_of(recording))
        expected = without_silence(ramp(subtune, len(heard) + 64))[:len(heard)]
        wrong = next((i for i, (found, wanted) in enumerate(zip(heard, expected)) if found != wanted), None)
        if wrong is not None:
            self.fail(f"frame {wrong} of what was heard is {heard[wrong]}, not {expected[wrong]}: "
                      "part of the tune was lost, repeated or out of place")
        self.assertGreaterEqual(len(heard), int(at_least_seconds * RATE),
                                f"only {len(heard)} frames of the tune were heard")

    def test_device_dropped_for_300_ms(self):
        """SPEC-078 with the sink dropping the device for 300 ms mid-tune, the frames received are complete and in order"""
        self.run_script("play_device_drop.script")
        # A second before the drop and two after it, less the time the device was away and being opened.
        self.assert_start_of_the_tune(self.out / "dropped.raw", subtune=1, at_least_seconds=2.0)

    def test_device_gone_and_back(self):
        """SPEC-078 after the device was dropped for good and came back, Play/Pause carries on from the same sample"""
        self.run_script("play_device_gone.script")
        # A second before the drop and a second after Play/Pause.
        self.assert_start_of_the_tune(self.out / "gone.raw", subtune=1, at_least_seconds=1.9)

    def test_bars_stand_still_while_paused(self):
        """SPEC-080 while paused, two screen descriptions a second apart are equal [parts: bars]"""
        self.run_script("play_bars.script")
        first = json.loads((self.out / "bars_paused_1.json").read_text(encoding="utf-8"))
        second = json.loads((self.out / "bars_paused_2.json").read_text(encoding="utf-8"))
        self.assertEqual(first["player"]["state"], "paused")
        self.assertEqual(len(first["voices"]), 3)
        different = sorted(key for key in set(first) | set(second) if first.get(key) != second.get(key))
        self.assertEqual(different, [], "these fields of the screen description changed while paused")
