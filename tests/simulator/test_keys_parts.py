# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-115: what a build looks like with a part and without it. For each key the key map gives to a part:
with the part built it has its label and its line in help; without it, it does nothing, has no label, and
help leaves it out. The tests ask the build they are given which parts it has, so they hold for both the
builds the level makes."""

from test_keys_support import ALL_PARTS, BUILD, SCREEN_PLACES, WAYS, Case, built, keymap, parts_built

# Labels a screen takes away while their action cannot be used: "Stop" with no job running (SPEC-102),
# "Rename" and "Delete" on Favourites (SPEC-171). On the ways these tests take, those may be empty.
SOMETIMES = {("hvsc", "5"), ("playlists", "6"), ("playlists", "7")}
# Where a row of a group is tried.
GROUP_PLACES = {"always": ["browser", "settings", "prompt"], "screens": ["browser", "settings"], "lists": ["browser"]}


class Parts(Case):
    def test_parts_of_the_build(self):
        """SPEC-115 the build says which parts it has: all but usbsid by default, none for "must", or the list asked for"""
        cache = (BUILD / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
        self.assertIn("SIDPLAYZERO_PARTS:", cache)
        have = parts_built()
        self.assertTrue(set(have) <= set(ALL_PARTS))
        if "SIDPLAYZERO_PARTS:STRING=all" in cache:
            self.assertEqual(have, [part for part in ALL_PARTS if part != "usbsid"])
        if "SIDPLAYZERO_PARTS:STRING=must" in cache:
            self.assertEqual(have, [])

    def test_labels(self):
        """SPEC-115 SPEC-091 on every screen the action bar has the key map's label for each key whose part is built, and none for a key whose part is not"""
        rows = keymap()
        for place in SCREEN_PLACES:
            if not built(WAYS[place][2]):
                continue
            actions = self.at(place).dump("arrived")["actions"]
            for index, key in enumerate("45678"):
                row = next((r for r in rows if r["where"] == place and r["key"] == key), None)
                label = row["label"] if row and built(row["part"]) else ""
                if (place, key) in SOMETIMES and actions[index] == "":
                    continue
                self.assertEqual(actions[index], label, f"the label over the key {key} on the screen {place}")

    def test_keys(self):
        """SPEC-115 a key whose part is not built changes nothing and has no line in help; a key whose part is built has its line"""
        rows = [row for row in keymap() if row["part"]]
        places = []
        for row in rows:
            for place in GROUP_PLACES.get(row["where"], [row["where"]]):
                if place in WAYS and built(WAYS[place][2]) and place not in places:
                    places.append(place)
        self.assertIn("browser", places)
        for place in places:
            mine = [row for row in rows if place in GROUP_PLACES.get(row["where"], [row["where"]])]
            # RightHold is Right held: without the part skip, Right is still the next subtune (SPEC-096).
            off = [row for row in mine if not built(row["part"]) and row["key"] != "RightHold"]
            lines = ["dump before"]
            for number, row in enumerate(off):
                lines += [f"key {row['key']}", f"dump after{number}"]
            lines += ["key Help", "dump help"]
            outcome = self.at(place, lines)
            before = outcome.dump("before")
            for number, row in enumerate(off):
                self.assertEqual(outcome.dump(f"after{number}"), before,
                                 f"the key {row['key']} did something in {place} without the part {row['part']}")
            help_lines = outcome.dump("help")["help"]
            self.assertIsInstance(help_lines, list, f"fn+H did not open help over {place}")
            for row in mine:
                listed = any(line.endswith(row["help"]) for line in help_lines)
                self.assertEqual(listed, built(row["part"]),
                                 f"help over {place}, the line for {row['key']} ({row['help']}), part {row['part']}")

    def test_part_keys_have_no_notice(self):
        """SPEC-115 SPEC-104 SPEC-108 without their parts the brightness keys raise no notice and Print Screen saves no picture; with them, they do"""
        outcome = self.at("browser", ["fake backlight.level 50", "dump before", "key BrightnessUp", "key BrightnessDown",
                                      "key PrintScreen", "dump after"], keep=["Pictures"])
        before, after = outcome.dump("before"), outcome.dump("after")
        pictures = outcome.out / "home" / "Pictures" / "Screenshots"
        if built("brightness"):
            self.assertIsNotNone(after["notice"], "no notice for the brightness keys")
            self.assertEqual(after["notice"]["id"], "MSG_BRIGHTNESS")      # the first raised shows first (SPEC-113)
        elif built("screenshot"):
            self.assertIsNotNone(after["notice"], "no notice for Print Screen")
            self.assertEqual(after["notice"]["id"], "MSG_SCREENSHOT_SAVED")
        else:
            self.assertEqual(after, before, "a key of a part that is not built changed the screen description")
        if built("screenshot"):
            self.assertEqual(len(list(pictures.glob("*.png"))), 1, "Print Screen did not save one picture")
        else:
            self.assertFalse(pictures.exists(), "a picture was saved without the part screenshot")
