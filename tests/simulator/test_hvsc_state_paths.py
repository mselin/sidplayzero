# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-154, the state reader's side: a path in state.ini that is not a collection path is dropped when the
app starts, and one with a space or an accent in it is kept.

Each row of the specification's table of paths is one start of the app with a state.ini of the test's own,
written here because a control character and a backslash do not sit well in a file kept in the repository.
"""

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
COMMANDO = "/MUSICIANS/H/Hubbard_Rob/Commando.sid"


def start_with(current_path, expects):
    """Starts the app in a home that holds the fixture collection, a tune of the user's own, and a state.ini
    naming `current_path` as the current entry; runs the `expects` lines. Returns "" if they all held, else
    one line saying which did not."""
    work = Path(tempfile.mkdtemp(prefix="spz-state-paths-"))
    try:
        state = work / "fakes" / "home" / ".local" / "state" / "sidplayzero"
        state.mkdir(parents=True)
        (state / "state.ini").write_bytes(
            b"browser_folder = /\n"
            b"current_path = " + current_path.encode("utf-8") + b"\n"
            b"current_subtune = 1\n")
        # The user's own tune, where SPEC-154's example has it.
        own = work / "fakes" / "home" / "C64Music" / "My Rips"
        own.mkdir(parents=True)
        shutil.copy(ROOT / "tests" / "fixtures" / "own" / "My Rips" / "Cool Söng.sid", own / "Cool Söng.sid")
        script = work / "state_path.script"
        script.write_text(
            "# SPEC-154 a start with a state.ini of the test's own\n"
            "# fakes: fakes\n"
            "# fixture: collections/85/C64Music C64Music\n"
            "# args: --backend fake\n"
            "expect screen = browser\n" + "".join(line + "\n" for line in expects), encoding="utf-8")
        status, output, _ = simulator.run_script(PROGRAM, script, work / "out")
        if status == 0:
            return ""
        failed = next((line[len("line: "):] for line in output.split("\n") if line.startswith("line: ")), "")
        found = next((line for line in output.split("\n") if line.startswith("found: ")), "")
        return f"the app, started with current_path = {current_path!r}, ended with status {status} at: {failed} ({found})"
    finally:
        shutil.rmtree(work, ignore_errors=True)


DROPPED = ["expect player.state = idle", "expect player.path = null", "expect-requests 0"]


class StatePaths(unittest.TestCase):
    def holds(self, current_path, expects):
        problem = start_with(current_path, expects)
        if problem:
            self.fail(problem)

    def test_kept(self):
        """SPEC-154 the state reader keeps a path that the listing names: the current entry is restored"""
        self.holds(COMMANDO, ["expect player.state = idle", f"expect player.path = {COMMANDO}"])

    def test_space_and_accent(self):
        """SPEC-154 the state reader accepts a path with a space and an accent: the user's own tune is the current entry again"""
        path = "/My Rips/Cool Söng.sid"
        self.holds(path, ["expect player.state = idle", f'expect player.path = "{path}"', "expect-requests 0"])

    def test_dot_dot(self):
        """SPEC-154 the state reader drops a path with a .. part, though it leads to a tune that is there"""
        self.holds("/MUSICIANS/T/../H/Hubbard_Rob/Commando.sid", DROPPED)

    def test_out_of_the_collection(self):
        """SPEC-154 the state reader drops a path that would lead out of the collection"""
        self.holds("/../C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid", DROPPED)

    def test_doubled_slash(self):
        """SPEC-154 the state reader drops a path with a doubled slash"""
        self.holds("/MUSICIANS//H/Hubbard_Rob/Commando.sid", DROPPED)

    def test_backslash(self):
        """SPEC-154 the state reader drops a path with a backslash"""
        self.holds("/MUSICIANS\\H\\Hubbard_Rob\\Commando.sid", DROPPED)

    def test_control_character(self):
        """SPEC-154 the state reader drops a path with a control character"""
        self.holds("/MUSICIANS/H/Hubbard_Rob/Comm\x01ando.sid", DROPPED)

    def test_no_sid_ending(self):
        """SPEC-154 the state reader drops a path that does not end in .sid, though the file is there"""
        self.holds("/DOCUMENTS/STIL.txt", DROPPED)

    def test_not_from_the_root(self):
        """SPEC-154 the state reader drops a path that does not start with a slash"""
        self.holds("MUSICIANS/H/Hubbard_Rob/Commando.sid", DROPPED)


if __name__ == "__main__":
    unittest.main()
