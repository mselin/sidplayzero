# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the tests of the collection, the browser and searching (SPEC-040 to SPEC-049, SPEC-095,
SPEC-100, SPEC-160 to SPEC-164). Called by tools/make_fixtures.py; nothing here is taken from HVSC.

    tunes/browse_year_1985.sid        a tune whose released line starts with 1985
    tunes/browse_no_year.sid          a tune whose released line holds no year
    collections/browse_pair/C64Music  a collection whose one folder has a present tune and an absent one
    collections/browse_big/C64Music   a collection that is only a listing: a folder of 1,298 tunes, one of
                                      six, and a folder on the device with nothing to show in it
"""

BIG_FOLDER_TUNES = 1298     # SPEC-043, SPEC-045: "a folder of 1,298 entries"
SMALL_FOLDER_TUNES = 6      # SPEC-043: "the same for 6 entries and for 1,298"


def big_listing(kit):
    """A song length database for /BIG, with 1,298 tunes, and /SIX, with six. None of the tunes exists:
    each is an absent tune, and the MD5 beside it is the MD5 of its path."""
    entries = [(f"/BIG/Tune_{number:04d}.sid", f"/BIG/Tune_{number:04d}.sid".encode("ascii"), "0:02")
               for number in range(1, BIG_FOLDER_TUNES + 1)]
    entries += [(f"/SIX/Tune_{number}.sid", f"/SIX/Tune_{number}.sid".encode("ascii"), "0:02")
                for number in range(1, SMALL_FOLDER_TUNES + 1)]
    return kit.song_lengths(entries)


def make(out, kit):
    tunes = kit.good_tunes()

    out["tunes/browse_year_1985.sid"] = kit.psid("Year Test", released="1985 Test Label")
    out["tunes/browse_no_year.sid"] = kit.psid("No Year", released="Unknown label")

    # SPEC-049: one folder, its first tune on the device and its second not. The second is what
    # tunes/two_sid.sid holds, so a fake server can answer for it with that file.
    pair = "collections/browse_pair/C64Music"
    out[f"{pair}/PAIR/First_Here.sid"] = tunes["commando_test"]
    out[f"{pair}/DOCUMENTS/Songlengths.md5"] = kit.song_lengths([
        ("/PAIR/First_Here.sid", tunes["commando_test"], "0:03"),
        ("/PAIR/Second_Away.sid", tunes["two_sid"], "0:02"),
    ])

    # SPEC-043, SPEC-045: big folders without big fixtures: the listing alone gives the browser its rows.
    big = "collections/browse_big/C64Music"
    out[f"{big}/DOCUMENTS/Songlengths.md5"] = big_listing(kit)
    out[f"{big}/EMPTY/readme.txt"] = b"A folder with no tune and no folder in it.\n"
