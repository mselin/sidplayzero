# SPDX-License-Identifier: GPL-3.0-or-later
"""The About screen (SPEC-103): its lines, with the version that app-builder.json gives, what
`sidplayzero --version` prints of them, and the notice panel with SPEC-152's text."""

import json
import re
import subprocess

from test_keys_support import HVSC, PROGRAM, ROOT, Case


def version():
    """The app's version, from the store's manifest (SPEC-223)."""
    return json.loads((ROOT / "app-builder.json").read_text(encoding="utf-8"))["version"]


def notice():
    """SPEC-152's notice, read from the specification: the quoted paragraph under the item, as one line."""
    spec = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
    item = spec[spec.index("**SPEC-152 "):spec.index("**SPEC-153 ")]
    return " ".join(line[2:].strip() for line in item.split("\n") if line.startswith("> "))


def one_line(text):
    return " ".join(text.split())


class About(Case):
    def lines(self):
        """Every line of the About screen: those it shows first and those that Down scrolls to."""
        more = []
        for number in range(1, 13):
            more += ["key Down", f"dump l{number}"]
        outcome = self.at("about", more)
        seen = []
        for name in ["arrived"] + [f"l{number}" for number in range(1, 13)]:
            description = outcome.dump(name)
            self.assertEqual(description["screen"], "about", "Down left the About screen")
            for line in description["lines"] or []:
                if line not in seen:
                    seen.append(line)
        return seen

    def test_lines(self):
        """SPEC-103 the About screen shows SidPlayZero and its version, the two libraries, the licence and where it is installed, and the address of the source"""
        lines = self.lines()
        text = "\n".join(lines)
        self.assertTrue(any("SidPlayZero" in line and version() in line for line in lines),
                        f"no line with SidPlayZero and the version {version()} of app-builder.json")
        self.assertIn("libsidplayfp 3.1.2", text)
        self.assertIn("libresidfp 1.3.0", text)
        self.assertIn("GPL-3.0-or-later", text)
        self.assertIn("/usr/share/doc/sidplayzero", text)          # where the package puts LICENSE (SPEC-011, SPEC-220)
        # The requirements name the place of the source as github.com, owner, sidplayzero.
        self.assertRegex(text, r"github\.com/[A-Za-z0-9_.-]+/sidplayzero")

    def test_version_option(self):
        """SPEC-103 `sidplayzero --version` prints the first two of the About screen's lines: the name with the version, and the libraries"""
        lines = self.lines()
        done = subprocess.run([str(PROGRAM), "--version"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(done.returncode, 0, done.stdout)
        printed = [line for line in done.stdout.split("\n") if line.strip()]
        self.assertEqual(printed[0], f"SidPlayZero {version()}")
        self.assertIn("libsidplayfp 3.1.2", done.stdout)
        self.assertIn("libresidfp 1.3.0", done.stdout)
        self.assertNotIn("GPL", done.stdout)
        for line in printed:
            self.assertIn(line, lines, "a line that --version prints is not a line of the About screen")

    def test_notice(self):
        """SPEC-103 SPEC-152 "Notice" opens a text panel whose text is SPEC-152's notice, and no more than that while HVSC is not set up"""
        self.assertTrue(notice().startswith("SidPlayZero can fetch music from the High Voltage SID Collection (HVSC)"))
        self.assertTrue(notice().endswith("It does not contain HVSC's files and does not pass them on."))
        outcome = self.at("about", ["key 5", "dump panel", "key Esc", "dump closed"])
        panel = outcome.dump("panel")["panel"]
        self.assertIsNotNone(panel, "the key 5, \"Notice\", opened no panel")
        self.assertEqual(panel["id"], "text")
        self.assertEqual(one_line(panel["text"]), notice())
        self.assertIsNone(outcome.dump("closed")["panel"])
        self.assertEqual(outcome.dump("closed")["screen"], "about")

    def test_notice_with_hvsc(self):
        """SPEC-103 once HVSC is set up, the notice is followed by the copyright section of the root's DOCUMENTS/HVSC.txt"""
        outcome = self.ran(self.run_lines(HVSC, ["key Tab", "key 8", "dump about", "key 5", "dump panel"]))
        self.assertEqual(outcome.dump("about")["screen"], "about", "the keys did not lead to the About screen")
        panel = outcome.dump("panel")["panel"]
        self.assertIsNotNone(panel, "the key 5, \"Notice\", opened no panel")
        text = one_line(panel["text"])
        self.assertTrue(text.startswith(notice()), "the panel does not begin with SPEC-152's notice")
        rest = text[len(notice()):]
        # The fixture's HVSC.txt (tools/make_fixtures.py): the two lines under its copyright heading.
        self.assertIn("This is the copyright section of a test fixture for release 85. The tunes beside it are a few "
                      "bytes of code written for the tests.", rest)
        self.assertNotIn("Nothing more.", rest)                     # the section after it
        self.assertNotIn("A fixture, not HVSC", rest)               # the file's head
        self.assertIsNone(re.search(r"T H E\s+H I G H", rest))
