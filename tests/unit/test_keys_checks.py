# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that read the sources and the tests, for three items of keys and messages:

SPEC-115: a script without a `parts:` line does not use a key, a label, a row or a panel that the key map
          or the specification gives to a part. The check is here, with tests of the check itself.
SPEC-116: no source file of the app names a message that is not in the specification's table.
SPEC-111: the source never grabs the keyboard.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GROUPS = ("always", "lists", "screens")
# SPEC-101 and SPEC-102: the rows of Settings and of the HVSC screen that belong to a part.
ROWS_OF_PARTS = {
    "When a song ends": "modes", "Repeat all": "modes", "Shuffle": "shuffle", "Sample rate": "rate",
    "SID model": "force", "Clock": "force", "KERNAL ROM": "roms", "BASIC ROM": "roms", "Character ROM": "roms",
    "Resume on start": "resume", "Screen off while playing": "screen-off",
    "Address for the complete archive": "download-all",
    "Check for a newer release": "releases", "Download everything": "download-all",
    "Finish the download": "download-all",
}


def keymap():
    rows, columns = [], None
    for line in (ROOT / "docs" / "keymap.tsv").read_text(encoding="utf-8").split("\n"):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if columns is None:
            columns = cells
            continue
        rows.append(dict(zip(columns, cells)))
    return rows


def unquote(text):
    text = text.strip()
    return text[1:-1] if len(text) >= 2 and text[0] == '"' and text[-1] == '"' else text


def part_faults(script):
    """What a script that names no part uses of a part (SPEC-115), as a list of sentences. Empty for a script
    with a `parts:` line. A script cannot be followed from place to place without running it, so this looks
    for what is a part's wherever it is: a key of the group `always` whose row has a part; a place, panel or
    screen whose every row has a part; a label over a key 4 to 8 that only rows of parts have; and a row of
    Settings or of the HVSC screen that belongs to a part."""
    rows = keymap()
    lines = script.split("\n")
    for line in lines:
        if not line.startswith("#"):
            break
        if line.lstrip("#").strip().startswith("parts:"):
            return []
    keys = {row["key"]: row["part"] for row in rows if row["where"] == "always" and row["part"] != "-"}
    places = {}
    for row in rows:
        if row["where"] not in GROUPS:
            places.setdefault(row["where"], []).append(row["part"])
    places = {place: parts[0] for place, parts in places.items() if all(part != "-" for part in parts)}
    labels = {}
    for row in rows:
        if row["label"] != "-":
            labels.setdefault((row["key"], row["label"]), []).append(row["part"])
    labels = {pair: parts[0] for pair, parts in labels.items() if all(part != "-" for part in parts)}

    faults = []
    for number, line in enumerate(lines, 1):
        words = line.strip().split(None, 1)
        if len(words) < 2 or line.startswith("#"):
            continue
        command, rest = words
        if command in ("key", "press", "release", "hold"):
            key = rest.split()[0]
            if key in keys:
                faults.append(f"line {number}: the key {key} belongs to the part {keys[key]}")
        elif command == "expect":
            cells = rest.split(None, 2)
            if len(cells) < 3 or cells[1] != "=":
                continue
            what, value = cells[0], unquote(cells[2])
            if what in ("panel.id", "place", "screen") and value in places:
                faults.append(f"line {number}: {value} belongs to the part {places[value]}")
            if what == "stack":
                for place in re.findall(r'"([a-z-]+)"', cells[2]):
                    if place in places:
                        faults.append(f"line {number}: the screen {place} belongs to the part {places[place]}")
            label = re.fullmatch(r"actions\[([0-4])\]", what)
            if label and (str(4 + int(label.group(1))), value) in labels:
                faults.append(f"line {number}: the label {value} belongs to the part "
                              f"{labels[(str(4 + int(label.group(1))), value)]}")
            if re.fullmatch(r"(panel\.)?rows\[\d+\]\.text", what) and value in ROWS_OF_PARTS:
                faults.append(f"line {number}: the row {value} belongs to the part {ROWS_OF_PARTS[value]}")
    return faults


class PartsCheck(unittest.TestCase):
    HEAD = "# SPEC-093 a made-up script\n"

    def test_scripts_name_their_parts(self):
        """SPEC-115 no simulator script without a `parts:` line uses a key, label, row or panel that belongs to a part"""
        found = []
        for folder in ("tests/simulator", "tests/framework/sim"):
            for path in sorted((ROOT / folder).glob("*.script")):
                found += [f"{path.relative_to(ROOT)}: {fault}" for fault in part_faults(path.read_text(encoding="utf-8"))]
        self.assertEqual(found, [])

    def test_key_of_a_part(self):
        """SPEC-115 the check reports a script without a `parts:` line that presses a key of a part"""
        self.assertEqual(part_faults(self.HEAD + "expect screen = browser\nkey BrightnessUp\n"),
                         ["line 3: the key BrightnessUp belongs to the part brightness"])
        self.assertEqual(part_faults(self.HEAD + "hold PrintScreen 100\n"),
                         ["line 2: the key PrintScreen belongs to the part screenshot"])
        self.assertEqual(part_faults(self.HEAD + "key VolumeUp\nkey 6\nkey L\ntype lists\n"), [])

    def test_panel_of_a_part(self):
        """SPEC-115 the check reports a script without a `parts:` line that expects a panel or a screen of a part"""
        self.assertEqual(part_faults(self.HEAD + "key 8\nexpect panel.id = pick-playlist\n"),
                         ["line 3: pick-playlist belongs to the part playlists"])
        self.assertEqual(part_faults(self.HEAD + "expect place = filter\n"), ["line 2: filter belongs to the part filters"])
        self.assertEqual(part_faults(self.HEAD + "expect screen = playlists\n"),
                         ["line 2: playlists belongs to the part playlists"])
        self.assertEqual(part_faults(self.HEAD + 'expect stack = ["browser","playlists","playlist"]\n'),
                         ["line 2: the screen playlists belongs to the part playlists",
                          "line 2: the screen playlist belongs to the part playlists"])
        self.assertEqual(part_faults(self.HEAD + "expect panel.id = more-playing\n"),
                         ["line 2: more-playing belongs to the part playlists|modes|shuffle|notes"])
        self.assertEqual(part_faults(self.HEAD + "expect panel.id = confirm\nexpect place = prompt\n"
                                     'expect stack = ["browser","settings","hvsc"]\n'), [])

    def test_label_of_a_part(self):
        """SPEC-115 the check reports a script without a `parts:` line that expects the label of a part's key"""
        self.assertEqual(part_faults(self.HEAD + "expect actions[2] = Lists\n"),
                         ["line 2: the label Lists belongs to the part playlists"])
        self.assertEqual(part_faults(self.HEAD + "expect actions[4] = More\n"),
                         ["line 2: the label More belongs to the part playlists|modes|shuffle|notes"])
        self.assertEqual(part_faults(self.HEAD + 'expect actions[0] = Back\nexpect actions[1] = Stop\nexpect actions[2] = ""\n'
                                     "expect actions[4] = About\n"), [])

    def test_row_of_a_part(self):
        """SPEC-115 the check reports a script without a `parts:` line that expects a row of Settings or HVSC that belongs to a part"""
        self.assertEqual(part_faults(self.HEAD + 'expect rows[2].text = "When a song ends"\n'),
                         ["line 2: the row When a song ends belongs to the part modes"])
        self.assertEqual(part_faults(self.HEAD + 'expect rows[1].text = "Download everything"\n'),
                         ["line 2: the row Download everything belongs to the part download-all"])
        self.assertEqual(part_faults(self.HEAD + 'expect rows[0].text = "Collection folder"\nexpect rows[1].text = Engine\n'
                                     'expect rows[0].text = "Get HVSC"\n'), [])

    def test_parts_line_answers_for_it(self):
        """SPEC-115 the check reports nothing for a script that names its parts"""
        self.assertEqual(part_faults("# SPEC-104 a made-up script\n# parts: brightness\nkey BrightnessUp\n"
                                     "expect actions[2] = Lists\n"), [])


class Sources(unittest.TestCase):
    def sources(self):
        return [path for path in sorted((ROOT / "src").rglob("*")) if path.suffix in (".cpp", ".h", ".c")]

    def test_messages_are_in_the_table(self):
        """SPEC-116 no source file of the app names a message that is not in the specification's table"""
        spec = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
        table = set(re.findall(r"MSG_[A-Z0-9_]+", spec[spec.index("**SPEC-116"):spec.index("## 7.")]))
        self.assertEqual(len(table), 55)
        self.assertTrue(self.sources())
        for path in self.sources():
            for name in re.findall(r"\bMSG_[A-Z0-9_]+", path.read_text(encoding="utf-8", errors="replace")):
                self.assertIn(name, table, f"{path.relative_to(ROOT)} names a message that SPEC-116 does not have")

    def test_keyboard_is_not_grabbed(self):
        """SPEC-111 the source never uses EVIOCGRAB: the keyboard is read without grabbing it"""
        for path in self.sources():
            self.assertNotIn("EVIOCGRAB", path.read_text(encoding="utf-8", errors="replace"), str(path.relative_to(ROOT)))
