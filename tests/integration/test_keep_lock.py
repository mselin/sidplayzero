# SPDX-License-Identifier: GPL-3.0-or-later
"""The lock of SPEC-200 is in the state folder of SPEC-134, with the real program.

SPEC-134 has a relative XDG_STATE_HOME ignored, as the XDG Base Directory Specification says: the state
folder is then ~/.local/state/sidplayzero, and state.ini is there. The lock that keeps a second copy from
running beside the first must be in that same folder; if it followed the relative path, two copies started
from different folders would both run and both write the one state.ini.
"""

import os
import shutil
import signal
import stat
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

BUILD = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator"))
PROGRAM = BUILD / "src" / "sidplayzero"


def one_line(text, most=600):
    return " | ".join(line.strip() for line in (text or "").strip().split("\n") if line.strip())[-most:]


class Lock(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-lock-"))
        self.home = self.work / "home"
        self.home.mkdir()
        self.started = []

    def tearDown(self):
        for process in self.started:
            if process.poll() is None:
                try:
                    os.killpg(os.getpgid(process.pid), signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.communicate()
        shutil.rmtree(self.work, ignore_errors=True)

    def start(self, name, cwd):
        """The program, with its home folder's environment but XDG_STATE_HOME relative, started in `cwd`."""
        script = self.work / f"{name}.script"
        script.write_text("settle\ndump up\nadvance 30000\n", encoding="utf-8")
        out = self.work / f"out-{name}"
        out.mkdir()
        cwd.mkdir(parents=True, exist_ok=True)
        env = {**os.environ, **home_environment(self.home), "XDG_STATE_HOME": "relative/state"}
        process = subprocess.Popen([str(PROGRAM), "--headless", "--clock", "real", "--script", str(script),
                                    "--out", str(out)], cwd=cwd, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, start_new_session=True)
        self.started.append(process)
        return process, out / "up.json"

    def wait_for(self, process, dump, seconds=20.0):
        limit = time.monotonic() + seconds
        while time.monotonic() < limit:
            if dump.exists():
                return
            if process.poll() is not None:
                output, _ = process.communicate()
                self.fail(f"the first copy ended with status {process.returncode}: {one_line(output)}")
            time.sleep(0.01)
        self.fail("the first copy did not start")

    def test_relative_state_home(self):
        """SPEC-200 SPEC-134 with XDG_STATE_HOME relative, the lock is in ~/.local/state/sidplayzero beside state.ini, made for the user only, and a second copy started from another folder exits with status 1"""
        first, up = self.start("first", self.work / "a")
        self.wait_for(first, up)
        state = self.home / ".local" / "state" / "sidplayzero"
        self.assertTrue((state / "lock").is_file(), "there is no lock in the state folder of SPEC-134")
        self.assertEqual(stat.S_IMODE(state.stat().st_mode), 0o700, "the state folder is not for the user only")
        self.assertFalse((self.work / "a" / "relative").exists(), "a folder was made at the relative XDG_STATE_HOME")

        second, second_up = self.start("second", self.work / "b")
        try:
            output, _ = second.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            self.fail("the second copy did not exit: it runs beside the first")
        self.assertEqual(second.returncode, 1, "the second copy's exit status; it said: " + one_line(output))
        self.assertIn("SidPlayZero is already running", output)
        self.assertFalse(second_up.exists(), "the second copy ran its script")

        self.assertIsNone(first.poll(), "the first copy ended when the second was started")
        ended = program.stop(first, signal.SIGTERM)
        self.assertEqual(ended.status, 0, "the first copy's exit status; it said: " + one_line(ended.output))


if __name__ == "__main__":
    unittest.main()
