# SPDX-License-Identifier: GPL-3.0-or-later
"""Simulator tests of a change of the collection root while the app runs (SPEC-040): everything that depends
on the root follows it, so that the new root's listing is shown, a tune absent there is fetched into it and
played, and the old root is shown again when the setting names it again. HVSC_BASE is left unset, so that
the setting decides the root; the runner sets it for every script, so these are Python tests."""

import unittest

from test_keys_support import FAKES, Case

# The setting `collection`, changed in the Settings screen to `path`, and back to the browser.
def change_root(path):
    return [
        "key Tab",
        "expect screen = settings",
        "key Home",
        'expect rows[0].text = "Collection folder"',
        "key Enter",
        "expect panel.id = prompt",
        "key End",
        *["key Backspace"] * 64,
        "type " + path,
        "key Enter",
        "expect panel = null",
        "expect rows[0].value contains " + path.lstrip("~/"),
        "key Esc",
        "expect screen = browser",
        "expect title = /",
    ]


class RootScope(Case):
    def test_new_root_fetches_and_plays_there(self):
        """SPEC-040 SPEC-141 SPEC-142 with the setting changed to a root that HVSC was set up in, the browser shows that root's listing, an absent tune is fetched into the new root and plays, and changing it back shows and plays the first root's tune"""
        header = [
            f"# fakes: {FAKES / 'hvsc_accepted'}",
            "# fixture: tunes/commando_test.sid C64Music/A_Commando.sid",
            "# fixture: collections/85/C64Music/DOCUMENTS Other/DOCUMENTS",
            "# args: --backend fake",
        ]
        lines = [
            # The first root, ~/C64Music, holds one tune of the user's own and no listing.
            "expect screen = browser",
            "expect title = /",
            "expect rows[0].text = A_Commando.sid",
            *change_root("~/Other"),
            # The new root's listing: HVSC's folders, though none of its tunes is on the device.
            "expect rows[0].text = DEMOS",
            "expect rows[2].text = MUSICIANS",
            "key Down",
            "key Down",
            "key Enter",
            "expect title = /MUSICIANS",
            "key Enter",
            "key Enter",
            "expect title = /MUSICIANS/H/Hubbard_Rob",
            "expect rows[0].text = Commando.sid",
            "expect rows[0].marks contains absent",
            # Enter fetches it, into the new root, and it plays from there.
            "key Enter",
            "expect-requests 1",
            "expect-request 1 https://hvsc.c64.org/download/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid",
            "expect-file Other/MUSICIANS/H/Hubbard_Rob/Commando.sid exists",
            "expect-file C64Music/MUSICIANS absent",
            "expect player.state = playing",
            "expect player.path = /MUSICIANS/H/Hubbard_Rob/Commando.sid",
            "key Esc",
            # Back to the first root: its tune is there again, and plays.
            *change_root("~/C64Music"),
            "expect rows[0].text = A_Commando.sid",
            "expect position = 1/1",
            "key Enter",
            "expect player.state = playing",
            "expect player.path = /A_Commando.sid",
            "expect-requests 1",
            "",
        ]
        self.ran(self.run_lines(header, lines, hvsc_base=False))


if __name__ == "__main__":
    unittest.main()
