# SPDX-License-Identifier: GPL-3.0-or-later
"""What must hold of the boxes of every screen: the three bands and the action bar (SPEC-091), and text that
does not fit its box (SPEC-098)."""

from test_keys_support import FAKE_BACKEND, SCREEN_PLACES, WAYS, Case, built

# SPEC-091: the bands, as first and last row of pixels, and the centres of the five action labels.
BANDS = {"top": (0, 15), "content": (16, 135), "actions": (136, 169)}
CENTRES = [49, 105, 161, 217, 273]
WIDTH = 320

LONG = "A_tune_whose_file_name_is_far_too_long_to_fit_on_one_line_of_a_screen_that_is_320_pixels_wide_and_so_is_cut"
DEEP = "A_folder_with_a_long_name_number_one/Another_folder_with_a_long_name_two/And_a_third_folder_with_a_long_name"


def layout_faults(description):
    """What breaks SPEC-091's oracle in one description: a box outside its band, two boxes of a band that
    overlap, an action label that is not centred on its key."""
    faults = []
    boxes = description["boxes"] or []
    for box in boxes:
        low, high = BANDS[box["band"]]
        if box["y"] < low or box["y"] + box["h"] - 1 > high or box["x"] < 0 or box["x"] + box["w"] > WIDTH:
            faults.append(f"the box {box['id']} ({box['x']},{box['y']} {box['w']}x{box['h']}) is not inside the band {box['band']}")
    for band in BANDS:
        inside = [box for box in boxes if box["band"] == band and box["w"] > 0 and box["h"] > 0]
        for i, a in enumerate(inside):
            for b in inside[i + 1:]:
                if a["x"] < b["x"] + b["w"] and b["x"] < a["x"] + a["w"] and a["y"] < b["y"] + b["h"] and b["y"] < a["y"] + a["h"]:
                    faults.append(f"the boxes {a['id']} and {b['id']} of the band {band} overlap")
    for index, label in enumerate(description["actions"] or []):
        if not label:
            continue
        found = [box for box in boxes if box["band"] == "actions" and box["text"] == label]
        if len(found) != 1:
            faults.append(f"the label {label} has {len(found)} boxes in the action bar")
            continue
        box = found[0]
        # "Centred on its x (within one pixel)", whichever way the middle of an even width is counted.
        if min(abs(box["x"] + box["w"] / 2 - CENTRES[index]), abs(box["x"] + (box["w"] - 1) / 2 - CENTRES[index])) > 1:
            faults.append(f"the label {label} is centred on {box['x'] + box['w'] / 2}, not on {CENTRES[index]}")
    return faults


def fit_faults(description):
    """What breaks SPEC-098's oracle in one description: a box whose text needs more width than the box has,
    and that is not marked as cut to fit."""
    return [f"the text of the box {box['id']} needs {box['text_w']} pixels, the box has {box['w']}, and it is not clipped"
            for box in description["boxes"] or [] if box["text_w"] > box["w"] and not box["clipped"]]


class Layout(Case):
    def screens(self):
        """The description of every screen this build has, by the place its keys are: {place: description}."""
        found = {}
        for place in SCREEN_PLACES + ["search-field"]:
            if built(WAYS[place][2]):
                found[place] = self.at(place).dump("arrived")
        return found

    def test_bands_and_action_bar(self):
        """SPEC-091 on every screen each action label has a box centred on its x, no two boxes of one band overlap, and every box lies inside its band"""
        for place, description in self.screens().items():
            self.assertTrue(description["boxes"], f"the screen {place} has no boxes")
            self.assertEqual(layout_faults(description), [], f"the layout of the screen {place}")

    def test_playing_with_a_tune(self):
        """SPEC-091 SPEC-098 the now-playing screen while a tune plays, and the browser in a folder three levels down, keep to the bands and the boxes"""
        playing = self.at("playing", ["advance 1500", "dump later"]).dump("later")
        self.assertEqual(playing["player"]["state"], "playing")
        self.assertEqual(layout_faults(playing) + fit_faults(playing), [], "the now-playing screen, 1.5 s into a tune")
        deep = self.ran(self.run_lines([f"# fixture: tunes/commando_test.sid C64Music/{DEEP}/Tune.sid"],
                                       ["key Enter", "key Enter", "key Enter", "dump deep"])).dump("deep")
        self.assertEqual(deep["screen"], "browser", "the keys did not lead to the browser")
        self.assertEqual(deep["title"], "/" + DEEP)
        self.assertEqual(layout_faults(deep) + fit_faults(deep), [], "the browser with a path too long for the top line")
        self.assertTrue(any(box["band"] == "top" and box["clipped"] for box in deep["boxes"]),
                        "a path this long fits no top line, and no box of the top line is marked as cut")

    def test_action_bar_while_typing(self):
        """SPEC-091 while text is typed the action bar shows "Enter: OK" and "ESC: cancel" instead of its five labels"""
        for place in ("search-field", "prompt"):
            description = self.at(place).dump("arrived")
            self.assertTrue(description["typing"], f"no text is being typed in {place}")
            self.assertEqual(description["actions"], [""] * 5, f"the five labels while text is typed in {place}")
            shown = [box["text"] for box in description["boxes"] if box["band"] == "actions" and box["text"]]
            self.assertEqual(sorted(shown), ["ESC: cancel", "Enter: OK"], f"the action bar while text is typed in {place}")
            self.assertEqual(layout_faults(description), [], f"the layout while text is typed in {place}")

    def test_text_fits_or_is_clipped(self):
        """SPEC-098 for every box of every screen, the text is no wider than the box or the box is marked as clipped"""
        for place, description in self.screens().items():
            self.assertTrue(description["boxes"], f"the screen {place} has no boxes")
            self.assertEqual(fit_faults(description), [], f"the boxes of the screen {place}")

    def test_long_row(self):
        """SPEC-098 a list row whose file name is too long for its box is cut to fit, and the description still has the whole name"""
        outcome = self.ran(self.run_lines([f"# fixture: tunes/commando_test.sid C64Music/{LONG}.sid"], ["dump long"]))
        description = outcome.dump("long")
        self.assertEqual(description["screen"], "browser", "the keys did not lead to the browser")
        self.assertEqual(description["rows"][0]["text"], LONG + ".sid")
        self.assertEqual(fit_faults(description), [], "the browser with a file name too long for a row")
        self.assertTrue(any(box["band"] == "content" and box["clipped"] for box in description["boxes"]),
                        "a name this long fits no row, and no box of the list is marked as cut")
        self.assertEqual(layout_faults(description), [])

    def test_long_line_scrolls(self):
        """SPEC-098 on the now-playing screen a line 1 that is too long scrolls: its text_x differs between two dumps a second apart"""
        outcome = self.ran(self.run_lines([f"# fixture: tunes/bad_magic.sid C64Music/{LONG}.sid"] + FAKE_BACKEND,
                                          ["key Enter", "dump shown", "advance 500", "dump first", "advance 1000",
                                           "dump second"]))
        shown, first, second = outcome.dump("shown"), outcome.dump("first"), outcome.dump("second")
        self.assertEqual(shown["screen"], "playing", "the keys did not lead to the now-playing screen")
        # A file that is no tune leaves the player Failed, and line 1 is then the file's name (SPEC-096).
        self.assertEqual(shown["player"]["state"], "failed")
        self.assertEqual(shown["lines"][0], LONG + ".sid")

        def line_1(description):
            found = [box for box in description["boxes"] if box["text"] == LONG + ".sid"]
            self.assertEqual(len(found), 1, "one box holds line 1")
            return found[0]

        a, b = line_1(first), line_1(second)
        self.assertGreater(a["text_w"], a["w"], "the name is not too long for its box")
        self.assertTrue(a["clipped"] and b["clipped"], "no text is drawn outside its box")
        self.assertNotEqual(a["text_x"], b["text_x"], "a line that is too long did not move in one second")
        self.assertEqual((a["x"], a["y"], a["w"], a["h"]), (b["x"], b["y"], b["w"], b["h"]), "the box itself moved")
        for description in (shown, first, second):
            self.assertEqual(fit_faults(description) + layout_faults(description), [])
