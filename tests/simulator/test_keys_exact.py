# SPDX-License-Identifier: GPL-3.0-or-later
"""Oracles that compare one screen description with another, or look at a file the app wrote: a key that
changes nothing (SPEC-092, SPEC-093), a held ESC (SPEC-106), and the picture Print Screen saves (SPEC-108)."""

import re

from test_keys_support import OWN, Case

from testing import png


def without_notice(description):
    """A description without what a notice adds to it: the notice, and its line among the text and the boxes."""
    return {key: value for key, value in description.items() if key not in ("notice", "text", "boxes")}


class Unchanged(Case):
    def test_unnamed_keys_on_the_browser(self):
        """SPEC-093 on the browser 9 and Q change nothing in the screen description"""
        outcome = self.at("browser", ["dump before", "key 9", "dump after_9", "key Q", "dump after_q"])
        before = outcome.dump("before")
        self.assertEqual(outcome.dump("after_9"), before, "the key 9 changed the screen description of the browser")
        self.assertEqual(outcome.dump("after_q"), before, "the key Q changed the screen description of the browser")

    def test_unnamed_keys_in_settings(self):
        """SPEC-093 in Settings the keys 6, 7, 9 and Q, which the key map does not name there, change nothing"""
        lines = ["dump before"]
        for key in ("6", "7", "9", "Q"):
            lines += [f"key {key}", f"dump after_{key.lower()}"]
        outcome = self.at("settings", lines)
        before = outcome.dump("before")
        for key in ("6", "7", "9", "q"):
            self.assertEqual(outcome.dump(f"after_{key}"), before, f"the key {key.upper()} changed the description of Settings")

    def test_screens_keys_in_a_confirm_panel(self):
        """SPEC-092 with a confirm panel open, TAB, Space, P and L leave the screen description unchanged"""
        lines = ["dump before"]
        for key in ("Tab", "Space", "P", "L"):
            lines += [f"key {key}", f"dump after_{key.lower()}"]
        outcome = self.at("confirm", lines)
        before = outcome.dump("before")
        self.assertEqual(before["panel"]["id"], "confirm")
        for key in ("tab", "space", "p", "l"):
            self.assertEqual(outcome.dump(f"after_{key}"), before, f"{key.upper()} changed the description under a confirm panel")

    def test_keys_4_to_8_in_a_text_panel(self):
        """SPEC-114 while a text panel is open the keys 4 to 8 change nothing in the screen description"""
        lines = ["dump before"]
        for key in "45678":
            lines += [f"key {key}", f"dump after_{key}"]
        outcome = self.at("text", lines)
        before = outcome.dump("before")
        for key in "45678":
            self.assertEqual(outcome.dump(f"after_{key}"), before, f"the key {key} changed the description under a text panel")


class Esc(Case):
    def test_700_ms(self):
        """SPEC-106 a 700 ms press of ESC shows and removes the notice and leaves the screen description otherwise unchanged"""
        outcome = self.at("about", ["dump before", "press Esc", "advance 700", "dump held", "release Esc", "dump after"])
        before, held, after = outcome.dump("before"), outcome.dump("held"), outcome.dump("after")
        self.assertIsNone(before["notice"])
        self.assertIsNotNone(held["notice"], "no notice while ESC is held for 700 ms")
        self.assertEqual(held["notice"]["id"], "MSG_HOLD_TO_LEAVE")
        self.assertEqual(without_notice(held), without_notice(before), "more than the notice changed while ESC was held")
        self.assertEqual(after, before, "a 700 ms press of ESC left the screen description changed")

    def test_200_ms(self):
        """SPEC-106 a 200 ms press of ESC goes back, and no notice shows on the way"""
        outcome = self.at("about", ["press Esc", "advance 200", "dump held", "release Esc", "dump after"])
        arrived, held, after = outcome.dump("arrived"), outcome.dump("held"), outcome.dump("after")
        self.assertEqual(held, arrived, "ESC did something before it was released")
        self.assertEqual(after["stack"], arrived["stack"][:-1], "a 200 ms press of ESC did not close the top screen")
        self.assertIsNone(after["notice"])

    def test_1000_ms(self):
        """SPEC-106 a 1,000 ms press of ESC ends the program with status 0"""
        there = self.ran(self.run_lines(OWN, ["key Tab", "dump there", "hold Esc 990", "dump still", "hold Esc 1000"]))
        self.assertEqual(there.dump("there")["screen"], "settings", "the keys did not lead to Settings")
        self.assertEqual(there.dump("still"), there.dump("there"), "a 990 ms press of ESC changed the screen description")
        # The script above ended with status 0 after the held ESC. This one asks for the screen afterwards,
        # which only fails, as a script error, if the app has left by itself.
        gone = self.run_lines(OWN, ["key Tab", "hold Esc 1000", "dump gone"])
        self.assertEqual(gone.status, 2, "after a 1,000 ms press of ESC the app was still there: " + gone.output[-300:])
        self.assertIn("it has left", gone.output)
        self.assertFalse(gone.has("gone"))


class Screenshot(Case):
    NAME = re.compile(r"^sidplayzero_\d{8}_\d{6}_\d{3}\.png$")

    def saved(self, place):
        self.need("screenshot")
        outcome = self.at(place, ["shot before", "key PrintScreen", "dump after"], keep=["Pictures"])
        folder = outcome.out / "home" / "Pictures" / "Screenshots"
        files = sorted(path.name for path in folder.glob("*")) if folder.is_dir() else []
        self.assertEqual(len(files), 1, f"Print Screen on {place} left {files} in Pictures/Screenshots, not one picture")
        self.assertRegex(files[0], self.NAME)
        return outcome, folder / files[0]

    def test_same_pixels(self):
        """SPEC-108 Print Screen writes a PNG of 320 by 170 named sidplayzero_YYYYMMDD_HHMMSS_mmm.png, with the same pixels as the simulator's own shot of that moment [parts: screenshot]"""
        for place in ("browser", "playing", "settings"):
            outcome, picture = self.saved(place)
            width, height, rows = png.read(picture)
            self.assertEqual((width, height), (320, 170), f"the picture taken on {place}")
            self.assertTrue(rows == png.read(outcome.out / "before.png")[2],
                            f"the picture taken on {place} differs from the simulator's shot of that moment")

    def test_notice_has_the_path(self):
        """SPEC-108 the notice MSG_SCREENSHOT_SAVED shows the path of the picture, in Pictures/Screenshots, which is created if needed [parts: screenshot]"""
        outcome, picture = self.saved("browser")
        notice = outcome.dump("after")["notice"]
        self.assertIsNotNone(notice, "no notice after Print Screen")
        self.assertEqual(notice["id"], "MSG_SCREENSHOT_SAVED")
        self.assertTrue(notice["text"].startswith("Saved"), notice["text"])
        self.assertIn("Pictures/Screenshots/" + picture.name, notice["text"])

    def test_panel_is_in_the_picture(self):
        """SPEC-108 Print Screen works with a panel open, and the picture has the panel [parts: screenshot]"""
        outcome, picture = self.saved("text")
        self.assertEqual(png.read(picture)[:2], (320, 170))
        self.assertEqual(outcome.dump("after")["panel"]["id"], "text", "Print Screen closed the panel")
        self.assertTrue(png.read(picture) == png.read(outcome.out / "before.png"),
                        "the picture taken with a panel open differs from the simulator's shot of that moment")
