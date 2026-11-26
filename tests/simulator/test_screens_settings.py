# SPDX-License-Identifier: GPL-3.0-or-later
"""The Settings screen (SPEC-101): its rows, each changed by keys alone (SPEC-110) and read back after a
restart; a value that is not allowed; and the most a prompt's field holds (SPEC-099). Which rows there are
depends on the parts of the build (SPEC-115), so the tests find their way by the rows' names."""

from test_keys_support import OWN, Case, built

# SPEC-101's table: the row, its setting, its part ("" for Always), how this test changes it, and what
# settings.ini holds afterwards (SPEC-130). A setting with a fixed set of values is stepped once, in the
# direction that leaves its default whatever happens at the ends of the set: Right from the first value,
# Left from the last. The others are typed into their prompt.
TABLE = [
    ("Collection folder", "collection", "", ("type", "~/Other"), "~/Other"),
    ("Engine", "engine", "", ("key", "Right"), "accurate"),
    ("When a song ends", "when_song_ends", "modes", ("key", "Right"), "next_tune"),
    ("Default length", "default_length", "", ("type", "1:00"), "1:00"),
    ("Repeat all", "repeat_all", "modes", ("key", "Left"), "on"),
    ("Shuffle", "shuffle", "shuffle", ("key", "Left"), "on"),
    ("Sample rate", "sample_rate", "rate", ("key", "Left"), "44100"),
    ("SID model", "sid_model", "force", ("key", "Right"), "6581"),
    ("Clock", "clock", "force", ("key", "Right"), "pal"),
    ("KERNAL ROM", "rom_kernal", "roms", ("type", "~/roms/kernal.bin"), "~/roms/kernal.bin"),
    ("BASIC ROM", "rom_basic", "roms", ("type", "~/roms/basic.bin"), "~/roms/basic.bin"),
    ("Character ROM", "rom_chargen", "roms", ("type", "~/roms/chargen.bin"), "~/roms/chargen.bin"),
    ("Resume on start", "resume_on_start", "resume", ("key", "Left"), "on"),
    ("Screen off while playing", "screen_off_after", "screen-off", ("type", "60"), "60"),
    ("Address for single files", "url_files", "", ("type", "https://example.test/C64Music"),
     "https://example.test/C64Music"),
    ("Address for the complete archive", "url_archive", "download-all",
     ("type", "https://example.test/HVSC_{release}-all.7z"), "https://example.test/HVSC_{release}-all.7z"),
]
URL_FILES = "https://hvsc.c64.org/download/C64Music"                          # SPEC-130's defaults
URL_ARCHIVE = "https://hvsc.brona.dk/HVSC/HVSC_{release}-all-of-them.7z"
CLEAR = ["key End"] + ["key Backspace"] * 60                                   # empties a prompt holding a default


def rows_built():
    return [row for row in TABLE if built(row[2])]


def scan(tag, count):
    """Script lines that walk the list from its first row, dumping after each step as TAG0, TAG1, ..."""
    lines = ["key Home", f"dump {tag}0"]
    for number in range(1, count + 1):
        lines += ["key Down", f"dump {tag}{number}"]
    return lines


def to_row(name):
    """Script lines that put the highlight on a row, from anywhere in the list."""
    names = [row[0] for row in rows_built()]
    return ["key Home"] + ["key Down"] * names.index(name)


def field(description):
    """The text of the one field of the open prompt."""
    fields = (description["panel"] or {}).get("fields") or {}
    return next(iter(fields.values()))["text"] if len(fields) == 1 else None


class Settings(Case):
    def highlighted(self, description, what):
        self.assertEqual(description["screen"], "settings", "the keys did not lead to Settings")
        rows = [row for row in description["rows"] if row["highlighted"]]
        self.assertEqual(len(rows), 1, f"{what}: one row of Settings is highlighted")
        self.assertEqual(rows[0]["kind"], "setting")
        return rows[0]

    def scanned(self, outcome, tag, count):
        return [self.highlighted(outcome.dump(f"{tag}{number}"), f"{tag}{number}") for number in range(count + 1)]

    def test_rows(self):
        """SPEC-101 SPEC-115 the Settings screen has the rows of the table whose part is built, in the table's order, each with a value"""
        expected = [row[0] for row in rows_built()]
        outcome = self.ran(self.run_lines(OWN, ["key Tab"] + scan("r", len(expected))))
        rows = self.scanned(outcome, "r", len(expected))
        self.assertEqual([row["text"] for row in rows[:len(expected)]], expected)
        self.assertEqual(rows[-1]["text"], expected[-1], "Down from the last row found a row the table does not have")
        for row in rows:
            self.assertIsInstance(row["value"], str, f"the row {row['text']} shows no value")
        self.assertLessEqual(len(outcome.dump("r0")["rows"]), 6, "more than six rows of 20 pixels in the content band")
        for name in ("Collection folder", "Engine", "Default length", "Address for single files"):
            self.assertIn(name, expected)                 # the four that are always built

    def test_change_every_row(self):
        """SPEC-101 SPEC-110 every row that is present is changed by keys alone, by Left, Right or the prompt as its kind needs, is saved at once, and shows the same value after a restart"""
        present = rows_built()
        lines = ["key Tab"] + scan("before", len(present) - 1) + ["key Home"]
        for _name, _key, _part, (how, what), _value in present:
            if how == "key":
                lines += [f"key {what}"]
            else:
                lines += ["key Enter"] + CLEAR + [f"type {what}", "key Enter"]
            lines += ["key Down"]
        lines += scan("changed", len(present) - 1) + ["restart", "key Tab"] + scan("again", len(present) - 1)
        # Without HVSC_BASE, so that the row for the collection can be changed (SPEC-040).
        outcome = self.ran(self.run_lines(OWN, lines, keep=[".config"], hvsc_base=False))
        before = self.scanned(outcome, "before", len(present) - 1)
        changed = self.scanned(outcome, "changed", len(present) - 1)
        again = self.scanned(outcome, "again", len(present) - 1)
        saved = (outcome.out / "home" / ".config" / "sidplayzero" / "settings.ini")
        self.assertTrue(saved.is_file(), "no settings.ini was written")
        file_lines = saved.read_text(encoding="utf-8").split("\n")
        for number, (name, key, _part, _change, value) in enumerate(present):
            self.assertEqual(changed[number]["text"], name)
            self.assertNotEqual(changed[number]["value"], before[number]["value"], f"the row {name} did not change")
            self.assertEqual(again[number]["text"], name)
            self.assertEqual(again[number]["value"], changed[number]["value"], f"the row {name} after a restart")
            self.assertIn(f"{key} = {value}", file_lines, f"settings.ini after the row {name} was changed")

    def test_default_length_refused(self):
        """SPEC-101 0:02 for the default length is refused with the notice MSG_INVALID_VALUE, the prompt stays open, and 0:05 is taken"""
        lines = ["key Tab"] + to_row("Default length") + ["dump row", "key Enter", "dump open"] + CLEAR + [
            "type 0:02", "key Enter", "dump refused", "key Backspace", "type 5", "key Enter", "dump taken"]
        outcome = self.ran(self.run_lines(OWN, lines, keep=[".config"]))
        row = self.highlighted(outcome.dump("row"), "row")
        self.assertEqual(row["text"], "Default length")
        opened, refused, taken = outcome.dump("open"), outcome.dump("refused"), outcome.dump("taken")
        self.assertEqual(opened["panel"]["id"], "prompt", "Enter on a setting that is a time opened no prompt")
        self.assertEqual(field(opened), "3:30", "the prompt does not hold the current value")
        self.assertIsNotNone(refused["notice"], "no notice for a value that is not allowed")
        self.assertEqual(refused["notice"]["id"], "MSG_INVALID_VALUE")
        self.assertEqual(refused["panel"]["id"], "prompt", "the prompt closed on a value that is not allowed")
        self.assertEqual(field(refused), "0:02")
        self.assertIsNone(taken["panel"], "the prompt stayed open on 0:05")
        self.assertNotEqual(self.highlighted(taken, "taken")["value"], row["value"])
        saved = (outcome.out / "home" / ".config" / "sidplayzero" / "settings.ini").read_text(encoding="utf-8")
        self.assertIn("default_length = 0:05", saved.split("\n"))
        self.assertNotIn("0:02", saved)

    def test_fields_hold_their_limits(self):
        """SPEC-099 SPEC-101 a prompt opens holding the current value; the field for a length holds 5 characters and the field for an address 255"""
        last = rows_built()[-1]
        default = URL_ARCHIVE if last[1] == "url_archive" else URL_FILES
        long_address = "https://example.test/" + "a" * (255 - len("https://example.test/"))
        lines = (["key Tab"] + to_row("Default length") + ["key Enter"] + CLEAR + ["type 12:345", "dump length", "key Esc"]
                 + ["key End", "key Enter", "dump address"] + CLEAR + [f"type {long_address}", "dump full", "key B",
                                                                       "type 12", "dump still"])
        outcome = self.ran(self.run_lines(OWN, lines))
        length, address = outcome.dump("length"), outcome.dump("address")
        self.assertEqual(length["screen"], "settings", "the keys did not lead to Settings")
        self.assertEqual(field(length), "12:34", "the field for a length took a sixth character")
        self.assertEqual(address["panel"]["id"], "prompt")
        self.assertEqual(field(address), default, "the prompt of the last row does not hold the setting's value")
        self.assertEqual(field(outcome.dump("full")), long_address)
        self.assertEqual(field(outcome.dump("still")), long_address, "the field for an address took a 256th character")

    def test_collection_prompt(self):
        """SPEC-101 SPEC-110 with HVSC_BASE unset, Enter on the row of the collection opens a prompt holding the setting's value"""
        outcome = self.ran(self.run_lines(OWN, ["key Tab", "key Home", "dump row", "key Enter", "dump open"],
                                          hvsc_base=False))
        row = self.highlighted(outcome.dump("row"), "row")
        self.assertEqual(row["text"], "Collection folder")
        opened = outcome.dump("open")
        self.assertEqual((opened["panel"] or {}).get("id"), "prompt", "Enter on the row of the collection opened no prompt")
        self.assertEqual(field(opened), "~/C64Music")
        self.assertTrue(opened["typing"])
