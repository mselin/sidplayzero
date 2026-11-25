# SPDX-License-Identifier: GPL-3.0-or-later
"""What the app keeps, with the real program: started, signalled and killed on the real clock
(SPEC-132, SPEC-200, SPEC-201, SPEC-202).

Each test writes the script it needs into its own work folder and starts the program with it, in a home
folder of its own (tools/testing/program.py). A script says where it has got to by writing a dump: the
test waits for that file and then sends its signal. The random moments come from a fixed seed, so a run
that fails can be made again.
"""

import hashlib
import json
import os
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import program  # noqa: E402
from testing.simulator import home_environment  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures"
BUILD = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator"))
PROGRAM = BUILD / "src" / "sidplayzero"
TOOL = BUILD / "src" / "spz_tool"
HOLD_DOWNLOAD = ROOT / "tests" / "simulator" / "fakes" / "keep_hold_download"

# From the root of the fixture collection: into DEMOS, into A-F, and Enter on its one tune.
PLAY = [
    "settle",
    "expect screen = browser",
    "key Enter", "settle",
    "key Enter", "settle",
    "key Enter", "settle",
    "expect player.state = playing",
    "expect player.path = /DEMOS/A-F/Clock_Demo.sid",
    "dump playing",
    "advance 30000",
]

# In a collection that is one tune and not HVSC: play the tune, then start setting HVSC up from Settings.
# The fake server holds every document back, so the download stays open while the tune plays.
PLAY_AND_HOLD_A_DOWNLOAD = [
    "settle",
    "expect screen = browser",
    "key Enter", "settle",
    "expect player.state = playing",
    "expect player.path = /Own_Tune.sid",
    "key Tab", "settle",
    "expect screen = settings",
    "key 5", "settle",
    "expect screen = hvsc",
    "key Enter", "settle",
    "expect panel.id = notice-panel",
    "key Enter", "settle",
    "expect job.kind = setup",
    "expect player.state = playing",
    "dump held",
    "advance 30000",
]


def saving_script():
    """Makes the app save, over and over: the Engine row of Settings stepped a hundred times a second, which
    writes settings.ini each time, and the browser's highlight moved every fifth of a second, which writes
    state.ini two seconds after."""
    lines = ["settle", "expect screen = browser", "key End",
             "key Tab", "settle", "expect screen = settings", "key Home", "key Down", "settle",
             "expect rows[1].text = Engine", "expect rows[1].highlighted = true",
             "dump saving"]
    for block in range(40):
        lines += ["key Enter", "advance 10"] * 20
        lines += ["key Esc", "settle", "key Home" if block % 2 == 0 else "key End",
                  "key Tab", "settle", "key Home", "key Down"]
    return lines + ["advance 30000"]


def killed_script():
    """SPEC-202's script: it plays a fixture tune and moves the highlight ten times a second, and writes a
    dump after everything that changes the browser's place, so that the dumps are its log."""
    lines = ["settle", "expect screen = browser", "dump restored"]
    for step in range(3):          # to the root, from wherever the run before left the browser
        lines += ["key Left", "settle", f"dump up{step}"]
    lines += ["key End", "dump root",                       # MUSICIANS
              "key Enter", "settle", "dump musicians",      # /MUSICIANS
              "key End", "dump letter",                     # T
              "key Enter", "settle", "dump t",              # /MUSICIANS/T
              "key Enter", "settle", "dump tom",            # /MUSICIANS/T/Tester_Tom, seven tunes
              "key Enter", "settle",                        # plays the first; the now-playing screen opens
              "expect player.state = playing",
              "key Esc", "settle",
              "expect screen = browser", "dump back"]
    moves = ["Down"] * 6 + ["Up"] * 6
    for move in range(80):         # eight seconds of it; the kill comes within four
        lines += [f"key {moves[move % 12]}", f"dump move{move:03d}", "advance 100"]
    return lines + ["advance 30000"]


def one_line(text, most=600):
    """A program's output as one line: the test runner keeps the last line of a failure's message, so that
    is where the whole story has to be."""
    return " / ".join(line.strip() for line in text.strip().split("\n") if line.strip())[-most:]


def read_dump(path):
    """A dump's screen description, or None while it is not there or not whole."""
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def place_of(description):
    """The browser's place in a screen description: (folder, highlighted entry); None on another screen."""
    if not description or description.get("screen") != "browser":
        return None
    highlighted = [row["text"] for row in description.get("rows") or [] if row.get("highlighted")]
    return (description.get("title"), highlighted[0] if highlighted else None)


def snapshot(folder):
    """Every file under a folder with its size, time of change and contents' hash."""
    found = {}
    for path in sorted(Path(folder).rglob("*")):
        if path.is_file():
            status = path.stat()
            found[str(path.relative_to(folder))] = (status.st_size, status.st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
    return found


def kept_files(home):
    """Runs `spz_tool kept-files` for a home folder. Returns (the finished process, its lines as a dict).

    spz_tool kept-files
        Takes no argument: the folders are those the environment gives (HOME and the XDG variables), as
        the app finds them (SPEC-134). It reads settings.ini and state.ini with the real Files, as a start
        of the app does (SPEC-133), but copies nothing, writes nothing and removes nothing. It prints, one
        `key = value` to a line, and ends with status 0:
            settings = ok | fault | missing     ok: read, and no fault found; fault: a fault of SPEC-133
            state = ok | fault | missing        the same for state.ini
            temp_files = N                      how many files with the temporary ending of SPEC-132 are in
                                                the app's four folders
    """
    done = subprocess.run([str(TOOL), "kept-files"], env={**os.environ, **home_environment(home)}, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    values = dict(line.split(" = ", 1) for line in done.stdout.split("\n") if " = " in line)
    return done, values


class RealProgramCase(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-keep-"))
        self.home = self.work / "home"
        self.home.mkdir()
        self.started = []
        self.fixtures_before = os.environ.get("SPZ_FIXTURES")
        os.environ["SPZ_FIXTURES"] = str(FIXTURES)      # for the "fixtures/..." bodies of an http.tsv

    def tearDown(self):
        for process in self.started:
            if process.poll() is None:
                try:
                    os.killpg(os.getpgid(process.pid), signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.communicate()
            elif process.stdout and not process.stdout.closed:
                process.stdout.close()
        if self.fixtures_before is None:
            os.environ.pop("SPZ_FIXTURES", None)
        else:
            os.environ["SPZ_FIXTURES"] = self.fixtures_before
        shutil.rmtree(self.work, ignore_errors=True)

    # ---- helpers ------------------------------------------------------------------------------------
    def fixture_collection(self):
        shutil.copytree(FIXTURES / "collections" / "85" / "C64Music", self.home / "C64Music")

    def script(self, name, lines):
        path = self.work / name
        path.write_text("# written by tests/integration/test_keep.py\n" + "\n".join(lines) + "\n", encoding="utf-8")
        return path

    def start(self, script, out, extra=()):
        process = program.start(PROGRAM, script, self.home, out, extra)
        self.started.append(process)
        return process

    def ended_by_itself(self, process, doing):
        output, _ = process.communicate()
        self.fail(f"the program ended by itself with status {process.returncode} {doing}; it said: {one_line(output)}")

    def wait_for_dump(self, process, path, seconds=20.0):
        """Waits until the script has written a dump, and returns it. Fails if the program ends first."""
        limit = time.monotonic() + seconds
        while time.monotonic() < limit:
            description = read_dump(path)
            if description is not None:
                return description
            if process.poll() is not None:
                description = read_dump(path)
                if description is not None:
                    return description
                self.ended_by_itself(process, f"before its script wrote {Path(path).name}")
            time.sleep(0.005)
        self.fail(f"the script did not get as far as {Path(path).name} in {seconds:.0f} s")

    def state_ini(self):
        path = self.home / ".local" / "state" / "sidplayzero" / "state.ini"
        self.assertTrue(path.is_file(), "there is no state.ini")
        return path.read_text(encoding="utf-8")

    def kept(self, where):
        """What `spz_tool kept-files` says of this test's home folder."""
        done, values = kept_files(self.home)
        self.assertEqual(done.returncode, 0, f"{where}: spz_tool kept-files said: {one_line(done.stdout)}")
        for key in ("settings", "state", "temp_files"):
            self.assertIn(key, values, f"{where}: spz_tool kept-files printed no {key}; it said: {one_line(done.stdout)}")
        return values


class Stopping(RealProgramCase):
    def told_to_stop(self, how):
        self.fixture_collection()
        out = self.work / "out"
        process = self.start(self.script("play.script", PLAY), out)
        self.wait_for_dump(process, out / "playing.json")
        ended = program.stop(process, how)
        self.assertEqual(ended.status, 0, "the exit status; the program said: " + one_line(ended.output))
        self.assertLess(ended.seconds, 1.0, "seconds from the signal to the end")
        self.assertEqual(ended.children_left, 0, "a process of the program's was left behind")
        self.assertIn("current_path = /DEMOS/A-F/Clock_Demo.sid\n", self.state_ini())

    def test_sigterm(self):
        """SPEC-201 playing a fixture tune and sent SIGTERM, the real program ends with status 0 within 1 s, state.ini names the tune, and no child process is left"""
        self.told_to_stop(signal.SIGTERM)

    def test_sigint(self):
        """SPEC-201 the same with SIGINT"""
        self.told_to_stop(signal.SIGINT)

    def test_sigterm_with_a_download_held_open(self):
        """SPEC-201 the same with a fake download held open: the download is cut short, and nothing of it is in the collection"""
        (self.home / "C64Music").mkdir()
        shutil.copy(FIXTURES / "tunes" / "commando_test.sid", self.home / "C64Music" / "Own_Tune.sid")
        out = self.work / "out"
        process = self.start(self.script("hold.script", PLAY_AND_HOLD_A_DOWNLOAD), out, extra=("--fakes", str(HOLD_DOWNLOAD)))
        held = self.wait_for_dump(process, out / "held.json")
        self.assertEqual((held.get("job") or {}).get("kind"), "setup", "the job that holds the download")
        ended = program.stop(process, signal.SIGTERM)
        self.assertEqual(ended.status, 0, "the exit status; the program said: " + one_line(ended.output))
        self.assertLess(ended.seconds, 1.0, "seconds from the signal to the end")
        self.assertEqual(ended.children_left, 0, "a process of the program's was left behind")
        self.assertIn("current_path = /Own_Tune.sid\n", self.state_ini())
        self.assertFalse((self.home / "C64Music" / "DOCUMENTS").exists(), "a set-up that was cut short left documents behind")


class Starting(RealProgramCase):
    def test_second_copy(self):
        """SPEC-200 a second copy of the real program, started while the first runs, says that SidPlayZero is already running, exits with status 1 and touches nothing; the first carries on"""
        first = self.start(self.script("first.script", ["settle", "expect screen = empty", "dump up", "advance 30000"]),
                           self.work / "out1")
        self.wait_for_dump(first, self.work / "out1" / "up.json")
        before = snapshot(self.home)

        second = self.start(self.script("second.script", ["settle", "dump up", "advance 30000"]), self.work / "out2")
        try:
            output, _ = second.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            self.fail("the second copy did not exit: it runs beside the first")
        self.assertEqual(second.returncode, 1, "the second copy's exit status; it said: " + one_line(output))
        self.assertIn("SidPlayZero is already running", one_line(output))
        self.assertFalse((self.work / "out2" / "up.json").exists(), "the second copy ran its script")
        self.assertEqual(snapshot(self.home), before, "the second copy changed a file in the home folder")

        self.assertIsNone(first.poll(), "the first copy ended when the second was started")
        ended = program.stop(first, signal.SIGTERM)
        self.assertEqual(ended.status, 0, "the first copy's exit status; it said: " + one_line(ended.output))


class Killed(RealProgramCase):
    def check_the_checker(self):
        """`spz_tool kept-files` is what says whether a kept file loads, so first it is shown to tell a file
        that loads from one that does not."""
        probe = self.work / "probe-home"
        config = probe / ".config" / "sidplayzero"
        state = probe / ".local" / "state" / "sidplayzero"
        config.mkdir(parents=True)
        state.mkdir(parents=True)

        def says():
            done, values = kept_files(probe)
            self.assertEqual(done.returncode, 0, f"spz_tool kept-files said: {one_line(done.stdout)}")
            return values.get("settings"), values.get("state"), values.get("temp_files")

        self.assertEqual(says(), ("missing", "missing", "0"))
        shutil.copy(FIXTURES / "keep" / "settings_by_hand_must.ini", config / "settings.ini")
        (state / "state.ini").write_text("browser_folder = /MUSICIANS\nbrowser_entry = H\n", encoding="utf-8")
        self.assertEqual(says(), ("ok", "ok", "0"))
        (config / "settings.ini").write_text("engine = li", encoding="utf-8")            # cut short in a value
        shutil.copy(FIXTURES / "keep" / "settings_noise.ini", state / "state.ini")       # not text at all
        (config / ".settings.ini.tmp-sidplayzero").write_text("engine = light\n", encoding="utf-8")
        self.assertEqual(says(), ("fault", "fault", "1"))
        self.assertEqual(sorted(p.name for p in config.iterdir()), [".settings.ini.tmp-sidplayzero", "settings.ini"],
                         "looking at the files changed them")

    def test_killed_while_saving(self):
        """SPEC-132 the real program is killed with SIGKILL at 20 random moments while a script makes it save, and after each kill every kept file loads"""
        self.check_the_checker()
        self.fixture_collection()
        script = self.script("saving.script", saving_script())
        moments = random.Random(132)
        config = self.home / ".config" / "sidplayzero"
        values = {}
        for kill in range(1, 21):
            out = self.work / f"out{kill:02d}"
            process = self.start(script, out)
            self.wait_for_dump(process, out / "saving.json")
            time.sleep(moments.uniform(0.0, 3.5))
            if process.poll() is not None:
                self.ended_by_itself(process, f"while it was saving (kill {kill})")
            ended = program.stop(process, signal.SIGKILL)
            self.assertEqual(ended.status, -signal.SIGKILL, "how the killed program ended")

            values = self.kept(f"after kill {kill}")
            self.assertIn(values["settings"], ("ok", "missing"), f"after kill {kill}: settings.ini does not load")
            self.assertIn(values["state"], ("ok", "missing"), f"after kill {kill}: state.ini does not load")
            self.assertEqual(sorted(p.name for p in config.glob("settings.ini.bad*")), [],
                             f"after kill {kill}: a start found a fault in settings.ini")
        # The script did make it save: by now both files are there, and whole.
        self.assertEqual((values["settings"], values["state"]), ("ok", "ok"), "after 20 kills, what is kept")
        self.assertTrue((config / "settings.ini").is_file(), "the script never made the app save its settings")
        self.assertIn("engine = ", (config / "settings.ini").read_text(encoding="utf-8"))

    def test_killed_fifty_times(self):
        """SPEC-202 killed with SIGKILL at a random moment in its first four seconds and started again, 50 times: every start reaches the browser, and the folder and entry restored were highlighted in the 10 s before the kill"""
        self.fixture_collection()
        script = self.script("killed.script", killed_script())
        moments = random.Random(202)
        log = []            # (from, until, place): every highlight the script's dumps show, over all the runs
        last_kill = None    # when the run before was killed

        def check_restored(run, place):
            self.assertIsNotNone(place, f"start {run} did not restore the browser")
            if last_kill is None:
                self.assertEqual(place, ("/", "DEMOS"), "the first start, with nothing remembered")
                return
            recent = {where for since, until, where in log if until >= last_kill - 10.0 and since <= last_kill}
            self.assertIn(place, recent, f"start {run} restored a place that the script's log does not show as "
                                         f"highlighted in the 10 s before the kill")

        def add_to_log(out, until):
            dumps = sorted((path.stat().st_mtime, path) for path in Path(out).glob("*.json"))
            places = [(when, place_of(read_dump(path))) for when, path in dumps]
            places = [(when, place) for when, place in places if place is not None]
            for index, (when, place) in enumerate(places):
                end = places[index + 1][0] if index + 1 < len(places) else until
                log.append((when, end, place))

        for run in range(1, 51):
            out = self.work / f"run{run:02d}"
            begun = time.monotonic()
            process = self.start(script, out)
            moment = moments.uniform(0.0, 4.0)
            restored = None
            while time.monotonic() - begun < moment:
                if restored is None:
                    restored = read_dump(out / "restored.json")
                    if restored is not None:
                        check_restored(run, place_of(restored))
                if process.poll() is not None:
                    self.ended_by_itself(process, f"in start {run}, {time.monotonic() - begun:.2f} s after it began")
                time.sleep(0.002)
            if restored is None and moment >= 2.0:
                self.fail(f"start {run} had {moment:.2f} s and did not reach the browser (SPEC-200 gives it 2 s)")
            ended = program.stop(process, signal.SIGKILL)
            self.assertEqual(ended.status, -signal.SIGKILL, "how the killed program ended")
            killed = time.time()
            add_to_log(out, killed)
            if log:                # with nothing ever shown, the next start is again a first one
                last_kill = killed

        # The start after the fiftieth kill, left to reach the browser and then told to leave.
        out = self.work / "run51"
        process = self.start(script, out)
        check_restored(51, place_of(self.wait_for_dump(process, out / "restored.json")))
        ended = program.stop(process, signal.SIGTERM)
        self.assertEqual(ended.status, 0, "the exit status of the last start; it said: " + one_line(ended.output))
        self.assertEqual(self.kept("after the last start")["temp_files"], "0", "temporary files left after a start")


if __name__ == "__main__":
    unittest.main()
