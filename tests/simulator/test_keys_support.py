# SPDX-License-Identifier: GPL-3.0-or-later
"""What the Python tests of keys and screens share. It has no test of its own.

These tests are for what one script cannot say: an expectation worked out from docs/keymap.tsv, two dumps
compared with each other, a file the app wrote. Each writes a script into a folder of its own, runs it
through tools/testing/simulator.py as the level does, and reads the dumps.

They ask the build they are given which parts it has (SIDPLAYZERO_PARTS in its CMakeCache.txt), so the
same tests hold for a build with every part and for one with only the Must parts (SPEC-115).
"""

import contextlib
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import screen, simulator  # noqa: E402

BUILD = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator"))
PROGRAM = BUILD / "src" / "sidplayzero"
FAKES = ROOT / "tests" / "simulator" / "fakes"

# SPEC-115's table, in its order.
ALL_PARTS = ("detail mini modes shuffle restart skip voices bars notes rate force roms too-slow brightness screenshot "
             "playlists import-more export filters resume screen-off releases download-all usbsid").split()

# A collection of the user's own, with three tunes at its root: A_Commando.sid, B_Three.sid, C_Two.sid.
OWN = ["# fixture: tunes/commando_test.sid C64Music/A_Commando.sid",
       "# fixture: tunes/three_subtunes.sid C64Music/B_Three.sid",
       "# fixture: tunes/two_sid.sid C64Music/C_Two.sid"]
# The fixture collection laid out like HVSC, set up.
HVSC = ["# fixture: collections/85/C64Music C64Music"]
# No engine: the player drives the fake sound backend.
FAKE_BACKEND = ["# args: --backend fake"]
# The documents of "Get HVSC", each held back: a job that stays running.
STALLED = [f"# fakes: {FAKES / 'keys_setup_stalled'}"]
# B_Three.sid plays, with the folder's three tunes as the queue, and now playing is on top.
PLAY = ["key Down", "key Enter"]

# How the keys lead to each place from a fresh start: (header lines, script lines, the part it needs).
WAYS = {
    "browser": (OWN + FAKE_BACKEND, PLAY + ["key Esc"], ""),
    "empty": ([], [], ""),
    "playing": (OWN + FAKE_BACKEND, PLAY, ""),
    "search-field": (OWN, ["key 5"], ""),
    "search-results": (OWN, ["key 5", "type test", "key Enter"], ""),
    "settings": (OWN, ["key Tab"], ""),
    "hvsc": (OWN, ["key Tab", "key 5"], ""),
    "about": (OWN, ["key Tab", "key 8"], ""),
    "prompt": (OWN, ["key Tab", "key End", "key Enter"], ""),
    "text": (OWN, ["key Tab", "key 8", "key 5"], ""),
    "notice-panel": (OWN, ["key Tab", "key 5", "key Enter"], ""),
    "confirm": (OWN + STALLED, ["key Tab", "key 5", "key Enter", "key Enter", "key 5"], ""),
    "playlists": (OWN, ["key 6"], "playlists"),
    "playlist": (OWN, ["key 7", "key 6", "key Enter"], "playlists"),
    "pick-playlist": (OWN, ["key 8"], "playlists"),
    "more-playlist": (OWN, ["key 7", "key 6", "key Enter", "key 8"], "playlists"),
    "import": (OWN, ["key 6", "key 8"], "playlists"),
    "more-playing": (OWN + FAKE_BACKEND, PLAY + ["key 8"], "playlists|modes|shuffle|notes"),
    "filter": (OWN, ["key 5", "type test", "key Enter", "key 6"], "filters"),
}
SCREEN_PLACES = ["browser", "empty", "playing", "search-results", "playlists", "playlist", "settings", "hvsc", "about"]
TYPING_PLACES = ["search-field", "prompt"]
PANEL_PLACES = ["confirm", "text", "notice-panel", "more-playing", "pick-playlist", "more-playlist", "import", "filter"]
# The screen each place is on (SPEC-254's `screen`).
SCREEN_OF = {"search-field": "search", "search-results": "search", "prompt": "settings", "text": "about",
             "notice-panel": "hvsc", "confirm": "hvsc", "pick-playlist": "browser", "more-playlist": "playlist",
             "import": "playlists", "more-playing": "playing", "filter": "search"}


def parts_built():
    """The parts of the build under test, from its SIDPLAYZERO_PARTS (SPEC-115): "all" is every part but
    usbsid, "must" is none, else a list of names."""
    asked = "all"
    cache = BUILD / "CMakeCache.txt"
    if cache.exists():
        found = re.search(r"(?m)^SIDPLAYZERO_PARTS:[A-Z]+=(.*)$", cache.read_text(encoding="utf-8", errors="replace"))
        if found:
            asked = found.group(1).strip()
    if asked == "all":
        return [part for part in ALL_PARTS if part != "usbsid"]
    if asked == "must":
        return []
    return [part for part in re.split(r"[;,]", asked) if part]


def built(part):
    """Whether what the key map's part column names is built: "" or "-" always, "a|b" with any of them."""
    if part in ("", "-"):
        return True
    have = parts_built()
    return any(name in have for name in part.split("|"))


def keymap():
    """docs/keymap.tsv as this test reads it: a list of rows, each a dict of the six columns, "-" as ""."""
    rows, columns = [], None
    for line in (ROOT / "docs" / "keymap.tsv").read_text(encoding="utf-8").split("\n"):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if columns is None:
            columns = cells
            continue
        rows.append({name: ("" if cell == "-" else cell) for name, cell in zip(columns, cells)})
    return rows


def groups_of(place):
    """The groups of the key map that apply in a place, in the order help lists them (SPEC-092, SPEC-107)."""
    if place in SCREEN_PLACES:
        return ["screens", "lists", "always"]
    if place in PANEL_PLACES or place == "help":
        return ["lists", "always"]
    if place in TYPING_PLACES:
        return ["always"]
    return []


def row_for(rows, place, key):
    """The row of the key map that decides what a key does in a place, or None (SPEC-092)."""
    for where in [place] + groups_of(place):
        for row in rows:
            if row["where"] == where and row["key"] == key:
                return row
    return None


class Outcome:
    """How a script ended, and what it left behind."""

    def __init__(self, status, output, out):
        self.status, self.output, self.out = status, output, Path(out)

    def dump(self, name):
        return json.loads((self.out / f"{name}.json").read_text(encoding="utf-8"))

    def has(self, name):
        return (self.out / f"{name}.json").exists()


@contextlib.contextmanager
def _changed(keep, out, hvsc_base):
    """While a script runs: copies what it is asked to keep out of the home folder before the runner removes
    it, and, where asked, leaves HVSC_BASE unset so that the setting `collection` decides the root."""
    new_home, home_environment = simulator.new_home, simulator.home_environment
    saved = os.environ.pop("HVSC_BASE", None) if not hvsc_base else None

    @contextlib.contextmanager
    def keeping_home():
        with new_home() as home:
            try:
                yield home
            finally:
                for name in keep:
                    source = Path(home) / name
                    target = Path(out) / "home" / name
                    if source.is_dir():
                        shutil.copytree(source, target, dirs_exist_ok=True)
                    elif source.is_file():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy(source, target)

    def environment(home):
        values = home_environment(home)
        if not hvsc_base:
            values.pop("HVSC_BASE", None)
        return values

    simulator.new_home, simulator.home_environment = keeping_home, environment
    try:
        yield
    finally:
        simulator.new_home, simulator.home_environment = new_home, home_environment
        if saved is not None:
            os.environ["HVSC_BASE"] = saved


class Case(unittest.TestCase):
    """A test that writes scripts and runs them."""

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-keys-"))
        self.runs = 0

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def need(self, part):
        """Skips the test in a build without a part it needs (SPEC-115)."""
        if not built(part):
            self.skipTest("needs the part " + part)

    def run_lines(self, header, lines, keep=(), hvsc_base=True):
        """Runs a script made of header lines and command lines, in a new home folder. `keep` names files or
        folders of the home folder to copy to the outcome's `out / "home"` before the home is removed."""
        self.runs += 1
        folder = self.work / f"run{self.runs}"
        folder.mkdir()
        script = folder / "test.script"
        script.write_text("\n".join(["# a script written by " + self.id().split(".")[-1], *header, *lines]) + "\n",
                          encoding="utf-8")
        out = folder / "out"
        out.mkdir()
        with _changed(keep, out, hvsc_base):
            status, output, _ = simulator.run_script(PROGRAM, script, out)
        return Outcome(status, output, out)

    def ran(self, outcome):
        """The script ran to its end, and every dump it wrote has the form of SPEC-254, as the level asks of
        the dumps of its own scripts."""
        self.assertEqual(outcome.status, 0, "the script did not run to its end: " + " | ".join(
            outcome.output.strip().split("\n")[-4:]))
        for dump in sorted(outcome.out.glob("*.json")):
            problems = screen.problems(json.loads(dump.read_text(encoding="utf-8")))
            self.assertEqual(problems, [], f"the dump {dump.stem} does not have the form of SPEC-254")
        return outcome

    def at(self, place, more=(), header=(), keep=(), hvsc_base=True):
        """Leads the app to a place by keys, runs `more` there, and returns the outcome. The script dumps
        the description on arrival as "arrived"; the test fails if that is not the place."""
        way_header, way, part = WAYS[place]
        self.need(part)
        outcome = self.ran(self.run_lines(list(way_header) + list(header), list(way) + ["dump arrived", *more],
                                          keep=keep, hvsc_base=hvsc_base))
        arrived = outcome.dump("arrived")
        self.assertEqual(arrived["place"], place, f"the keys did not lead to the place {place}")
        self.assertEqual(arrived["screen"], SCREEN_OF.get(place, place), f"the place {place} is on another screen")
        return outcome
