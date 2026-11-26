# SPDX-License-Identifier: GPL-3.0-or-later
"""Playing, with the real parts: what the app renders against what the reference player renders (SPEC-067,
SPEC-071, SPEC-074, SPEC-075, SPEC-077), and the real volume control against a stand-in `pactl` (SPEC-076).

Sound. Each comparison runs the real program, without a window and on the fake clock, with a script made
here: it plays one tune from its start with the real engine, moves the clock on by ten seconds, and saves
what the recording sink was given. That must equal, value for value, the first ten seconds of the WAV the
pinned `sidplayfp` program writes for the same tune with SPEC-071's command. Nothing is compared with
stored samples.

Volume. `spz_tool volume ...` is the real SystemVolume from the command line:

    spz_tool volume get           reads the level and the mute; prints "ok = yes", "level = N" (per cent)
                                  and "muted = yes" or "muted = no"; or "ok = no" if it could not
    spz_tool volume set N         sets the level to N per cent; prints "ok = yes" or "ok = no"
    spz_tool volume toggle-mute   toggles the mute; prints "ok = yes" or "ok = no"

Each prints one `key = value` to a line and ends with status 0 whenever it could say what happened.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import reference, simulator  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures"
BUILD = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator"))
PROGRAM = BUILD / "src" / "sidplayzero"
TOOL = BUILD / "src" / "spz_tool"
STANDINS = ROOT / "tests" / "integration" / "standins"
SECONDS = 10

# The fixture tunes that play as they are. The one that needs BASIC is left out: without a ROM the app
# does not load it (SPEC-068), and no ROM is in the repository.
TUNES = sorted(path.relative_to(FIXTURES).as_posix() for path in (FIXTURES / "tunes").glob("*.sid")
               if not path.name.startswith("bad_") and path.name != "needs_basic.sid")
VOICES = "play/C64Music/VOICES/Voices.sid"      # three voices and the filter in use; three subtunes, default 1


def stopped_at(output):
    """One line saying where a script stopped, from what the program printed."""
    lines = [line.strip() for line in output.split("\n") if line.strip()]
    line = next((text for text in lines if text.startswith("line: ")), lines[-1] if lines else "no output")
    return line.replace("line: ", "")


class Rendering(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-play-"))
        self.runs = 0

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def app_render(self, tune, settings=(), keys=()):
        """What the app gives the sink in the first ten seconds of a tune, as the bytes of 16-bit stereo
        frames. `tune` is a path under tests/fixtures; `settings` are lines of settings.ini; `keys` are
        script lines to run once the tune plays, such as a mute and a step to the next subtune, after
        which the recording starts from the sample that is then first."""
        self.runs += 1
        folder = self.work / f"run{self.runs}"
        settings_file = folder / "fakes" / "home" / ".config" / "sidplayzero" / "settings.ini"
        settings_file.parent.mkdir(parents=True)
        settings_file.write_text("".join(line + "\n" for line in settings), encoding="utf-8")
        script = folder / "render.script"
        script.write_text("\n".join([
            "# SPEC-071 ten seconds of a fixture tune, as the app renders them with the real engine",
            f"# fixture: {tune} C64Music/tune.sid",
            f"# fakes: {folder / 'fakes'}",
            "# args: --engine real",
            "expect screen = browser",
            "key Enter",
            "expect player.state = playing",
            *keys,
            "expect player.state = playing",
            "save-samples before",
            f"advance {SECONDS * 1000}",
            "expect player.underrun_frames = 0",
            "save-samples render",
            "",
        ]), encoding="utf-8")
        out = folder / "out"
        status, output, _ = simulator.run_script(PROGRAM, script, out)
        if status != 0:
            self.fail(f"the app did not render {tune}: its script ended with status {status} at: {stopped_at(output)}")
        return (out / "render.raw").read_bytes()

    def difference(self, tune, accurate, rate=48000, subtune=None, extra=(), settings=(), keys=()):
        """"" if the app's ten seconds equal the reference player's; otherwise what differs, in a few words."""
        lines = [f"engine = {'accurate' if accurate else 'light'}", f"sample_rate = {rate}", *settings]
        ours = self.app_render(tune, lines, keys)
        theirs = reference.render(FIXTURES / tune, seconds=SECONDS, accurate=accurate, rate=rate, subtune=subtune,
                                  extra=list(extra))
        # The reference player writes a little more than it is asked for: its first ten seconds are compared.
        if len(theirs) < SECONDS * rate * 4:
            self.fail(f"the reference player gave only {len(theirs) // 4} frames for {tune}")
        theirs = theirs[:SECONDS * rate * 4]
        if len(ours) != len(theirs):
            return f"{tune}: {len(ours) // 4} frames, not {len(theirs) // 4}"
        frame = reference.first_difference(ours, theirs)
        return "" if frame is None else f"{tune}: differs from frame {frame}"

    def every_tune(self, accurate, rate):
        wrong = [found for found in (self.difference(tune, accurate, rate) for tune in TUNES) if found]
        self.assertEqual(wrong, [], f"{len(wrong)} of {len(TUNES)} tunes do not render as the reference player's")

    # ---- SPEC-071 ----------------------------------------------------------------------------------------

    def test_same_twice(self):
        """SPEC-071 a fixture rendered twice by the app gives the same samples, with each engine"""
        for engine in ("accurate", "light"):
            first = self.app_render("tunes/commando_test.sid", [f"engine = {engine}"])
            second = self.app_render("tunes/commando_test.sid", [f"engine = {engine}"])
            self.assertEqual(len(first), SECONDS * 48000 * 4, engine)
            self.assertGreater(len(set(first)), 16, f"{engine}: the render is silence")
            self.assertIsNone(reference.first_difference(first, second), f"{engine}: two renders differ")

    def test_reference_accurate(self):
        """SPEC-071 SPEC-067 with reSIDfp at 48,000 Hz, ten seconds of every fixture tune equal the sidplayfp program's, value for value"""
        self.every_tune(accurate=True, rate=48000)

    def test_reference_light(self):
        """SPEC-071 SPEC-067 with sidlite at 48,000 Hz, ten seconds of every fixture tune equal the sidplayfp program's, value for value"""
        self.every_tune(accurate=False, rate=48000)

    def test_reference_accurate_44100(self):
        """SPEC-071 SPEC-073 with reSIDfp at 44,100 Hz, ten seconds of every fixture tune equal the sidplayfp program's [parts: rate]"""
        self.every_tune(accurate=True, rate=44100)

    def test_reference_light_44100(self):
        """SPEC-071 SPEC-073 with sidlite at 44,100 Hz, ten seconds of every fixture tune equal the sidplayfp program's [parts: rate]"""
        self.every_tune(accurate=False, rate=44100)

    def test_subtunes(self):
        """SPEC-071 SPEC-061 each subtune of the three-subtune fixture equals the sidplayfp program's render of that subtune"""
        tune = "tunes/three_subtunes.sid"       # its default subtune is 2
        wrong = []
        for accurate in (True, False):
            wrong.append(self.difference(tune, accurate, subtune=2))
            wrong.append(self.difference(tune, accurate, subtune=3, keys=["key Right", "expect player.subtune = 3"]))
            wrong.append(self.difference(tune, accurate, subtune=1, keys=["key Left", "expect player.subtune = 1"]))
        self.assertEqual([found for found in wrong if found], [])

    # ---- SPEC-075 ----------------------------------------------------------------------------------------

    def test_two_and_three_chips(self):
        """SPEC-075 ten seconds of the two-SID and three-SID fixtures equal the sidplayfp program's, with each engine"""
        wrong = []
        for tune in ("tunes/two_sid.sid", "tunes/three_sid.sid"):
            for accurate in (True, False):
                wrong.append(self.difference(tune, accurate, keys=[
                    "expect player.chips = " + ("2" if "two" in tune else "3")]))
        self.assertEqual([found for found in wrong if found], [])

    def test_one_chip_is_the_same_in_both_channels(self):
        """SPEC-075 a one-chip tune sounds the same in both channels, and a two-chip tune does not"""
        one = self.app_render("tunes/commando_test.sid", ["engine = accurate"])
        self.assertTrue(all(one[i:i + 2] == one[i + 2:i + 4] for i in range(0, len(one), 4)),
                        "left and right differ for a tune with one SID chip")
        self.assertGreater(len(set(one)), 16, "the render is silence")
        two = self.app_render("tunes/two_sid.sid", ["engine = accurate"])
        self.assertFalse(all(two[i:i + 2] == two[i + 2:i + 4] for i in range(0, len(two), 4)),
                         "left and right are equal for a tune with two SID chips")

    # ---- SPEC-074 ----------------------------------------------------------------------------------------

    def test_muted_voice(self):
        """SPEC-074 with voice 1 muted from the first sample, ten seconds of a fixture equal the sidplayfp program's WAV made with -u1 [parts: voices]"""
        # Muted while subtune 1 plays; the mute holds for the tune, so subtune 2 has it from its first sample.
        keys = ["key 1", "key Right", "expect player.subtune = 2"]
        wrong = [self.difference(VOICES, accurate, subtune=2, extra=["-u1"], keys=keys) for accurate in (True, False)]
        self.assertEqual([found for found in wrong if found], [])
        plain = reference.render(FIXTURES / VOICES, seconds=1, subtune=2)
        muted = reference.render(FIXTURES / VOICES, seconds=1, subtune=2, extra=["-u1"])
        self.assertIsNotNone(reference.first_difference(plain, muted), "muting voice 1 changes nothing in this fixture")

    def test_filter_off(self):
        """SPEC-074 with the filter off, ten seconds of a fixture equal the sidplayfp program's WAV made with -nf [parts: voices]"""
        keys = ["key 9", "key Right", "expect player.subtune = 2"]
        wrong = [self.difference(VOICES, accurate, subtune=2, extra=["-nf"], keys=keys) for accurate in (True, False)]
        self.assertEqual([found for found in wrong if found], [])
        plain = reference.render(FIXTURES / VOICES, seconds=1, subtune=2)
        unfiltered = reference.render(FIXTURES / VOICES, seconds=1, subtune=2, extra=["-nf"])
        self.assertIsNotNone(reference.first_difference(plain, unfiltered), "the filter changes nothing in this fixture")

    # ---- SPEC-077 ----------------------------------------------------------------------------------------

    def test_forced_model(self):
        """SPEC-077 with sid_model = 8580, ten seconds of a fixture marked 6581 equal the sidplayfp program's WAV made with -mnf [parts: force]"""
        tune = "tunes/model_6581.sid"
        wrong = [self.difference(tune, accurate, extra=["-mnf"], settings=["sid_model = 8580"]) for accurate in (True, False)]
        self.assertEqual([found for found in wrong if found], [])
        plain = reference.render(FIXTURES / tune, seconds=1)
        forced = reference.render(FIXTURES / tune, seconds=1, extra=["-mnf"])
        self.assertIsNotNone(reference.first_difference(plain, forced), "forcing the model changes nothing in this fixture")

    def test_forced_clock(self):
        """SPEC-077 with clock = ntsc, ten seconds of a fixture marked PAL equal the sidplayfp program's WAV made with -vnf [parts: force]"""
        tune = "tunes/model_6581.sid"           # marked 6581 and PAL
        wrong = [self.difference(tune, accurate, extra=["-vnf"], settings=["clock = ntsc"]) for accurate in (True, False)]
        self.assertEqual([found for found in wrong if found], [])
        plain = reference.render(FIXTURES / tune, seconds=1)
        forced = reference.render(FIXTURES / tune, seconds=1, extra=["-vnf"])
        self.assertIsNotNone(reference.first_difference(plain, forced), "forcing the clock changes nothing in this fixture")


class RealVolume(unittest.TestCase):
    """SPEC-076's real SystemVolume, with the stand-in `pactl` of tests/integration/standins first on the
    PATH. The stand-in writes its name and each argument to a file and gives every call the same answer,
    so the answer here has what `pactl get-sink-volume` and `pactl get-sink-mute` each print."""

    VOLUME_42 = ("Volume: front-left: 27525 /  42% / -22.61 dB,   front-right: 27525 /  42% / -22.61 dB\n"
                 "        balance 0.00\n")

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-volume-"))
        self.log = self.work / "log"

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def tool(self, *arguments, answer="", status=0, path=None):
        """Runs `spz_tool volume ARGUMENTS`. Returns what it printed, as {key: value}, and the argument
        list of each program the stand-ins saw run, in order."""
        env = {**os.environ, "PATH": path if path is not None else f"{STANDINS}:{os.environ['PATH']}",
               "SPZ_STANDIN_LOG": str(self.log), "SPZ_STANDIN_ANSWER": answer, "SPZ_STANDIN_STATUS": str(status)}
        process = subprocess.run([str(TOOL), "volume", *arguments], env=env, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=30)
        said = " ".join(process.stdout.split())
        if process.returncode != 0:
            self.fail(f"spz_tool volume {' '.join(arguments)} ended with status {process.returncode}: {said}")
        printed = {}
        for line in process.stdout.split("\n"):
            if " = " in line:
                key, value = line.split(" = ", 1)
                printed[key.strip()] = value.strip()
        calls = []
        for line in (self.log.read_text().split("\n") if self.log.exists() else []):
            if line.startswith("  "):
                calls[-1].append(line[2:])
            elif line:
                calls.append([line])
        return printed, calls

    def test_get(self):
        """SPEC-076 the real SystemVolume reads the level and the mute with `pactl get-sink-volume @DEFAULT_SINK@` and `pactl get-sink-mute @DEFAULT_SINK@`"""
        printed, calls = self.tool("get", answer=self.VOLUME_42 + "Mute: no")
        self.assertEqual(sorted(calls), [["pactl", "get-sink-mute", "@DEFAULT_SINK@"],
                                         ["pactl", "get-sink-volume", "@DEFAULT_SINK@"]])
        self.assertEqual(printed, {"ok": "yes", "level": "42", "muted": "no"})

    def test_get_muted(self):
        """SPEC-076 the real SystemVolume reads a muted sink as muted"""
        printed, _ = self.tool("get", answer=self.VOLUME_42 + "Mute: yes")
        self.assertEqual(printed, {"ok": "yes", "level": "42", "muted": "yes"})

    def test_set(self):
        """SPEC-076 the real SystemVolume sets the level with `pactl set-sink-volume @DEFAULT_SINK@ N%`, as an argument list and without a shell"""
        printed, calls = self.tool("set", "45")
        self.assertEqual(calls, [["pactl", "set-sink-volume", "@DEFAULT_SINK@", "45%"]])
        self.assertEqual(printed, {"ok": "yes"})

    def test_toggle_mute(self):
        """SPEC-076 the real SystemVolume toggles the mute with `pactl set-sink-mute @DEFAULT_SINK@ toggle`"""
        printed, calls = self.tool("toggle-mute")
        self.assertEqual(calls, [["pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle"]])
        self.assertEqual(printed, {"ok": "yes"})

    def test_pactl_fails(self):
        """SPEC-076 if pactl fails, the real SystemVolume says it could not, for reading, setting and muting"""
        for arguments in (["get"], ["set", "45"], ["toggle-mute"]):
            printed, calls = self.tool(*arguments, answer="Failure: Connection refused", status=1)
            self.assertEqual(printed, {"ok": "no"}, arguments)
            self.assertTrue(calls and all(call[0] == "pactl" for call in calls), arguments)

    def test_pactl_missing(self):
        """SPEC-076 if pactl is missing, the real SystemVolume says it could not, and nothing else is run in its place"""
        empty = self.work / "empty"
        empty.mkdir()
        for arguments in (["get"], ["set", "45"], ["toggle-mute"]):
            printed, calls = self.tool(*arguments, path=str(empty))
            self.assertEqual(printed, {"ok": "no"}, arguments)
            self.assertEqual(calls, [], arguments)
