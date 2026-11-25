# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-107: for every place, the lines of help are exactly those the item's rule gives from docs/keymap.tsv,
which this test reads itself."""

import re

from test_keys_support import (PANEL_PLACES, SCREEN_PLACES, TYPING_PLACES, WAYS, Case, built, groups_of, keymap)

# SPEC-111: what each key is called on the device. A key that is its own key is called by its name.
ON_THE_DEVICE = {
    "Up": "fn+F", "Down": "fn+X", "Left": "fn+Z", "Right": "fn+C",
    "Home": "fn+K", "End": "fn+N", "PageUp": "fn+L", "PageDown": "fn+M",
    "Delete": "fn+Backspace",
    "PlayPause": "fn+Q", "Previous": "fn+W", "Next": "fn+E",
    "Mute": "fn+A", "VolumeDown": "fn+S", "VolumeUp": "fn+D",
    "BrightnessDown": "fn+U", "BrightnessUp": "fn+I",
    "Help": "fn+H", "PrintScreen": "fn+J",
    "Shift": "shift",
}
HELD = {"EscHold": "Esc", "RightHold": "Right"}     # SPEC-111: a key held, as SPEC-106 and SPEC-096 say
NO_ONE_KEY = ("Char", "Any")                        # any key that types a character; any key at all


def rule(place, replaced_too=False):
    """SPEC-107's rule: the place's own rows; then the `screens` rows, if the place is a screen, without a
    key the place has switched off; then the `lists` rows, if no text is typed there; then the `always`
    rows. Without a row whose action is "nothing" or whose part is not built. Returns [(key, words)].

    With `replaced_too`, a `lists` row is also left out when the place has a row of its own for that key."""
    rows = keymap()
    own = {row["key"] for row in rows if row["where"] == place}
    chosen = [row for row in rows if row["where"] == place]
    for group in groups_of(place):
        for row in rows:
            if row["where"] != group:
                continue
            if row["key"] in own and (group == "screens" or replaced_too):
                continue
            chosen.append(row)
    return [(row["key"], row["help"]) for row in chosen if row["action"] != "nothing" and built(row["part"])]


def fault(line, key, words):
    """Why a line of help is not the line for this key and these words; None if it is. The line is the key's
    name on the device and the row's help words (SPEC-107); how the two are set apart is the screen's
    choice, so long as nothing else is written between them."""
    if not line.endswith(words):
        return f"does not end with the words {words!r}"
    before = line[:len(line) - len(words)]
    if key in NO_ONE_KEY:
        return None                                     # the specification gives these no name on the device
    if key in HELD:
        name = ON_THE_DEVICE.get(HELD[key], HELD[key])
        return None if name.lower() in before.lower() else f"does not name the key {name}"
    name = ON_THE_DEVICE.get(key, key)
    if key in ON_THE_DEVICE:
        if not before.startswith(name):
            return f"does not begin with the key's name on the device, {name}"
    elif not before.lower().startswith(name.lower()):
        return f"does not begin with the key's name, {name}"
    if re.search(r"[A-Za-z0-9]", before[len(name):]):
        return f"has more than the key's name before the words: {before!r}"
    return None


def names(lines, key):
    """Whether one of the lines begins with the name of a key that is its own key, such as TAB or P."""
    return any(re.match(re.escape(key.lower()) + r"[^a-z0-9+]", line.lower()) for line in lines)


class Help(Case):
    def help_over(self, place):
        outcome = self.at(place, ["key Help", "dump help"])
        description = outcome.dump("help")
        self.assertEqual(description["place"], "help", f"fn+H did not open help over {place}")
        self.assertIsInstance(description["help"], list, f"no help lines over {place}")
        return description["help"]

    def compare(self, place, lines, expected):
        self.assertEqual(len(lines), len(expected), f"help over {place} has {len(lines)} lines, and the rule gives "
                         f"{len(expected)}: {lines}")
        for line, (key, words) in zip(lines, expected):
            why = fault(line, key, words)
            self.assertIsNone(why, f"help over {place}: the line {line!r} {why}")

    def check(self, place):
        self.compare(place, self.help_over(place), rule(place))

    def test_browser(self):
        """SPEC-107 SPEC-115 help over the browser has exactly the lines the rule gives from the key map"""
        self.check("browser")

    def test_empty(self):
        """SPEC-107 help over the empty screen has exactly the lines the rule gives from the key map"""
        self.check("empty")

    def test_playing(self):
        """SPEC-107 SPEC-115 help over the now-playing screen has the lines the rule gives, with or without the two `lists` rows the screen replaces"""
        # The now-playing screen is the one place with rows of its own for two keys of `lists`, Up and Down
        # (SPEC-094). SPEC-107 says which `screens` rows a place's own rows take out of help, and does not
        # say it of `lists` rows: so the `lists` rows "Up" and "Down" may be there or not. All else is fixed.
        lines = self.help_over("playing")
        as_written, without = rule("playing"), rule("playing", replaced_too=True)
        self.assertEqual(len(as_written), len(without) + 2)
        self.compare("playing", lines, as_written if len(lines) == len(as_written) else without)
        self.assertFalse(names(lines, "P"), "help over now playing has a line for P, which the screen switches off")

    def test_search_field(self):
        """SPEC-107 help over the Search field has its rows and the `always` rows, and none of `lists` or `screens`"""
        lines = self.help_over("search-field")
        self.compare("search-field", lines, rule("search-field"))
        for words in ("Settings", "Now playing", "A screenful up", "First entry"):
            self.assertFalse(any(line.endswith(words) for line in lines), f"a line for {words!r} where text is typed")

    def test_search_results(self):
        """SPEC-107 SPEC-115 help over the results of Search has exactly the lines the rule gives from the key map"""
        self.check("search-results")

    def test_settings(self):
        """SPEC-107 help in Settings has exactly the lines the rule gives, so none for TAB"""
        lines = self.help_over("settings")
        self.compare("settings", lines, rule("settings"))
        self.assertFalse(names(lines, "Tab"), "help in Settings has a line for TAB")
        self.assertFalse(any(line.endswith("Settings") for line in lines), "help in Settings has the line of TAB")
        self.assertTrue(any(line.endswith("Now playing") for line in lines), "the other `screens` rows are there")

    def test_hvsc(self):
        """SPEC-107 help over the HVSC screen has exactly the lines the rule gives from the key map"""
        self.check("hvsc")

    def test_about(self):
        """SPEC-107 help over the About screen has exactly the lines the rule gives from the key map"""
        self.check("about")

    def test_prompt(self):
        """SPEC-107 help over a text prompt has its rows and the `always` rows, and none of `lists` or `screens`"""
        lines = self.help_over("prompt")
        self.compare("prompt", lines, rule("prompt"))
        self.assertTrue(lines[0].endswith("Type"))
        for words in ("Settings", "Now playing", "Up", "Down", "Last entry"):
            self.assertFalse(any(line.endswith(words) for line in lines), f"a line for {words!r} where text is typed")

    def test_text(self):
        """SPEC-107 help over a text panel has its row, the `lists` rows and the `always` rows"""
        self.check("text")

    def test_notice_panel(self):
        """SPEC-107 help over the notice panel has its row, the `lists` rows and the `always` rows"""
        self.check("notice-panel")

    def test_confirm(self):
        """SPEC-107 help over a confirm panel has exactly the lines the rule gives, so none for TAB, Space, P or L"""
        lines = self.help_over("confirm")
        self.compare("confirm", lines, rule("confirm"))
        for key in ("Tab", "Space", "P", "L"):
            self.assertFalse(names(lines, key), f"help over a confirm panel has a line for {key}")
        self.assertTrue(lines[0].endswith("Yes"))
        self.assertTrue(any(line.endswith("A screenful down") for line in lines), "the `lists` rows are there")

    def test_playlists(self):
        """SPEC-107 help over the Playlists screen has exactly the lines the rule gives [parts: playlists]"""
        self.check("playlists")

    def test_playlist(self):
        """SPEC-107 help over the Playlist screen has exactly the lines the rule gives [parts: playlists]"""
        self.check("playlist")

    def test_panels_of_playlists(self):
        """SPEC-107 help over the pick-playlist, more-playlist and import panels has exactly the lines the rule gives [parts: playlists]"""
        for place in ("pick-playlist", "more-playlist", "import"):
            self.check(place)

    def test_more_playing(self):
        """SPEC-107 help over the "More" panel of now playing has exactly the lines the rule gives [parts: playlists]"""
        self.check("more-playing")

    def test_filter(self):
        """SPEC-107 help over the filter panel has exactly the lines the rule gives [parts: filters]"""
        self.check("filter")

    def test_every_place_is_covered(self):
        """SPEC-107 every place of the key map where help can be opened has a test here"""
        rows = keymap()
        places = {row["where"] for row in rows} - {"always", "lists", "screens"}
        # Help is not opened over itself; the key step records every key, fn+H too (SPEC-260).
        self.assertEqual(places - {"help", "key-step"}, set(WAYS))
        self.assertEqual(set(SCREEN_PLACES + TYPING_PLACES + PANEL_PLACES), set(WAYS))
