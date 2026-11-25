# SPDX-License-Identifier: GPL-3.0-or-later
"""Simulator tests of the browser that one script cannot make: SPEC-040 without the variable HVSC_BASE,
which the runner sets for every script; and SPEC-043 and SPEC-045 where two screen descriptions have to
be compared with each other. Each runs a script as the runner does and reads what it left behind."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import simulator  # noqa: E402

PROGRAM = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "sidplayzero"


def failed_line(output):
    """The line of a script that did not hold, from the program's report; else the output's last line."""
    for line in output.split("\n"):
        if line.startswith("line: "):
            return line[len("line: "):].strip()
    return (output.strip().split("\n") or [""])[-1]


class Browser(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-browse-"))
        self.out = self.work / "out"
        self.out.mkdir()

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def script(self, text):
        path = self.work / "test.script"
        path.write_text(text, encoding="utf-8")
        return path

    def passed(self, status, output):
        """Fails the test, on one line, unless the script ran to its end."""
        if status != 0:
            self.fail(f"the script ended with status {status} at: {failed_line(output)}")

    def dump(self, name):
        return json.loads((self.out / f"{name}.json").read_text(encoding="utf-8"))

    def test_setting_changes_the_root(self):
        """SPEC-040 with HVSC_BASE unset, changing the collection setting while the app runs makes the browser show the new root at once"""
        script = self.script("\n".join([
            # The root is ~/C64Music, the third of SPEC-040's three: neither the variable nor the setting is set.
            "expect screen = browser",
            "expect title = /",
            "expect rows[0].text = DEMOS",
            "expect rows[2].text = MUSICIANS",
            "key Tab",
            "expect screen = settings",
            "key Home",
            'expect rows[0].text = "Collection folder"',
            "expect rows[0].highlighted = true",
            "expect rows[0].value contains C64Music",
            'expect rows[0].value matches ^(?!.*HVSC_BASE)',
            # Enter opens a prompt holding the value. It is replaced by the folder "Other" in the home folder.
            "key Enter",
            "expect panel.id = prompt",
            "expect typing = true",
            "key End",
            *["key Backspace"] * 64,
            "type ~/Other",
            "key Enter",
            "expect panel = null",
            "expect rows[0].value contains Other",
            "expect-file .config/sidplayzero/settings.ini contains Other",
            # The browser, under Settings all the while, shows the new root: the user's own folder, not HVSC's.
            "key Esc",
            "expect screen = browser",
            "expect title = /",
            'expect rows[0].text = "My Rips"',
            "expect position = 1/1",
            "key Enter",
            "expect title = /My Rips",
            'expect rows[0].text = "Cool Söng.sid"',
            "",
        ]))
        with simulator.new_home() as home:
            shutil.copytree(simulator.FIXTURES / "collections" / "85" / "C64Music", Path(home) / "C64Music")
            shutil.copytree(simulator.FIXTURES / "own", Path(home) / "Other")
            env = {name: value for name, value in os.environ.items() if name != "HVSC_BASE"}
            env.update({name: value for name, value in simulator.home_environment(home).items() if name != "HVSC_BASE"})
            env["SPZ_FIXTURES"] = str(simulator.FIXTURES)
            process = subprocess.run(
                [str(PROGRAM), "--headless", "--script", str(script), "--out", str(self.out), "--backend", "fake"],
                cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180,
                errors="replace")
        self.passed(process.returncode, process.stdout)

    def test_same_objects_for_6_and_1298(self):
        """SPEC-043 the list holds the same number of LVGL objects for a folder of 6 entries and for one of 1,298"""
        script = self.script("\n".join([
            "# fixture: collections/browse_big/C64Music C64Music",
            "",
            "expect screen = browser",
            "expect rows[0].text = BIG",
            "expect rows[2].text = SIX",
            "key End",
            "key Enter",
            "expect title = /SIX",
            "expect list.total = 6",
            "dump six",
            "key Left",
            "key Home",
            "key Enter",
            "expect title = /BIG",
            "expect list.total = 1298",
            "dump big",
            "key End",
            "expect position = 1298/1298",
            "dump big_end",
            "",
        ]))
        status, output, _ = simulator.run_script(PROGRAM, script, self.out)
        self.passed(status, output)
        six, big, big_end = self.dump("six"), self.dump("big"), self.dump("big_end")
        self.assertGreater(six["list"]["objects"], 0)
        self.assertEqual(big["list"]["objects"], six["list"]["objects"])
        self.assertEqual(big_end["list"]["objects"], six["list"]["objects"])
        self.assertLessEqual(len(six["rows"]), 6)
        self.assertEqual(len(big["rows"]), len(big_end["rows"]))
        self.assertIn(len(big["rows"]), (5, 6))

    def test_page_is_the_rows_on_the_screen(self):
        """SPEC-045 Page Down and Page Up move by exactly the number of rows on the screen"""
        script = self.script("\n".join([
            "# fixture: collections/browse_big/C64Music C64Music",
            "",
            "expect screen = browser",
            "key Enter",
            "expect title = /BIG",
            "expect position = 1/1298",
            "dump start",
            "key PageDown",
            "dump one_page",
            "key PageDown",
            "key PageDown",
            "dump three_pages",
            "key PageUp",
            "dump two_pages",
            "key End",
            "key PageUp",
            "dump page_from_end",
            "",
        ]))
        status, output, _ = simulator.run_script(PROGRAM, script, self.out)
        self.passed(status, output)
        rows = len(self.dump("start")["rows"])
        self.assertIn(rows, (5, 6), "the rows on the screen: six, or five and the detail line (SPEC-091)")
        self.assertEqual(self.dump("one_page")["position"], f"{1 + rows}/1298")
        self.assertEqual(self.dump("three_pages")["position"], f"{1 + 3 * rows}/1298")
        self.assertEqual(self.dump("two_pages")["position"], f"{1 + 2 * rows}/1298")
        self.assertEqual(self.dump("page_from_end")["position"], f"{1298 - rows}/1298")
        for name in ("one_page", "three_pages", "two_pages", "page_from_end"):
            description = self.dump(name)
            self.assertEqual(len(description["rows"]), rows, name)
            self.assertEqual(sum(1 for row in description["rows"] if row["highlighted"]), 1, name)


if __name__ == "__main__":
    unittest.main()
