# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for playlists, favourites and importing (SPEC-170 to SPEC-179), and a few for the tests of how
the app is built (sections 2 and 3 of the specification). Called by tools/make_fixtures.py.

Everything here points at the tunes of the fixture collection, tests/fixtures/collections/85/C64Music:

    lists/playlists/    playlists written by hand from SPEC-170's description
    lists/expected/     what the app must have written after the steps the tests take, byte for byte
    lists/import/       one file for each row of SPEC-175's table, written as that player writes it, and the
                        samples of SPEC-176 and SPEC-177 with the fixture tunes' names
    lists/small/        a collection of two tunes, each alone in its folder, for the scripts
    structure/          a settings file written by hand from SPEC-130's table (SPEC-009), and one that only
                        chooses the engine (SPEC-003)

The paths of other people's machines in the import files are /home/u/... and /Users/u/..., as the
specification's own examples are.
"""

COMMANDO = "/MUSICIANS/H/Hubbard_Rob/Commando.sid"
THREE_SONGS = "/MUSICIANS/T/Tester_Tom/Three_Songs.sid"
CLOCK_DEMO = "/DEMOS/A-F/Clock_Demo.sid"
EITHER_CHIP = "/GAMES/A-F/Either_Chip.sid"
HUELSBECK = "/MUSICIANS/H/Huelsbeck_Chris/Huelsbeck_Test.sid"


def lines(*rows, ending="\n", encoding="utf-8"):
    """Text lines as a file's bytes, each line ended."""
    return "".join(row + ending for row in rows).encode(encoding)


def entry(path, extinf=None, subtune=None, md5=None):
    """One entry of a playlist file as the app writes it (SPEC-170): #EXTINF, #SIDTUNE, then the tune."""
    rows = []
    if extinf:
        rows.append(f"#EXTINF:{extinf}")
    tags = ([f"subtune={subtune}"] if subtune else []) + ([f"md5={md5}"] if md5 else [])
    if tags:
        rows.append("#SIDTUNE:" + " ".join(tags))
    return rows + [path]


def playlist(name, *entries):
    rows = ["#EXTM3U", f"#PLAYLIST:{name}"]
    for rows_of_entry in entries:
        rows += rows_of_entry
    return lines(*rows)


def make_playlists(out, kit):
    tunes = kit.good_tunes()
    commando, three = kit.md5(tunes["commando_test"]), kit.md5(tunes["three_subtunes"])

    # SPEC-170: written by hand, with every kind of line the item names.
    out["lists/playlists/by_hand.m3u8"] = lines(
        "#EXTM3U",
        "#PLAYLIST:Loading tunes",
        "#EXTINF:3,Test Author - Commando Test",
        f"#SIDTUNE:subtune=1 md5={commando}",
        COMMANDO,
        "#EXTINF:3,Test Author - Three Subtunes",
        "#SIDTUNE:subtune=3",
        THREE_SONGS,
        f"#SIDTUNE:md5={three}",
        THREE_SONGS,
        "#EXTVLCOPT:network-caching=1000",
        CLOCK_DEMO,
        "# a note that travels with the tune below",
        "#EXTGRP:games",
        EITHER_CHIP,
        "file:///home/u/Music/My Rips/Cool Söng.sid",
    )
    # SPEC-170: the same form with none of the optional lines.
    out["lists/playlists/plain.m3u8"] = lines("#EXTM3U", "#PLAYLIST:Plain", COMMANDO, THREE_SONGS)
    # SPEC-173: present, missing, present.
    out["lists/playlists/gap.m3u8"] = lines("#EXTM3U", "#PLAYLIST:Gap", CLOCK_DEMO,
                                            "/MUSICIANS/N/Nobody/Nothing.sid", EITHER_CHIP)

    # What the app writes (SPEC-170, SPEC-172). The length is the listing's, in whole seconds; an entry
    # without a subtune number has the length of the tune's default subtune.
    clock = entry(CLOCK_DEMO, "2,Test Author - Sixty Hertz", md5=kit.md5(tunes["clock_ntsc"]))
    either = entry(EITHER_CHIP, "2,Test Author - Either Chip", md5=kit.md5(tunes["model_either"]))
    huelsbeck = entry(HUELSBECK, "2,Thirty-two characters of author! - Hülsbeck Tëst",
                      md5=kit.md5(tunes["huelsbeck_test"]))
    out["lists/expected/empty.m3u8"] = playlist("Loading")
    out["lists/expected/three.m3u8"] = playlist("Loading", clock, either, huelsbeck)
    out["lists/expected/three_moved.m3u8"] = playlist("Loading", huelsbeck, clock, either)
    out["lists/expected/three_renamed.m3u8"] = playlist("Turbo", huelsbeck, clock, either)
    # SPEC-171: subtune 3 marked on the now-playing screen, then the tune marked in the browser.
    out["lists/expected/favourites_two.m3u8"] = playlist(
        "Favourites",
        entry(THREE_SONGS, "3,Test Author - Three Subtunes", subtune=3, md5=three),
        entry(THREE_SONGS, "2,Test Author - Three Subtunes", md5=three))
    out["lists/expected/favourites_one.m3u8"] = playlist(
        "Favourites", entry(COMMANDO, "3,Test Author - Commando Test", md5=commando))
    # SPEC-179: an export has no #SIDTUNE line; the subtune number follows the path.
    out["lists/expected/export.m3u8"] = lines(
        "#EXTM3U",
        "#PLAYLIST:Loading tunes",
        "#EXTINF:3,Test Author - Commando Test",
        COMMANDO,
        "#EXTINF:3,Test Author - Three Subtunes",
        THREE_SONGS + "?3",
        "#EXTINF:2,Test Author - Sixty Hertz",
        CLOCK_DEMO,
    )


def make_imports(out, kit):
    home = "/home/u/C64Music"

    # SPEC-175's table, a file for each row.
    out["lists/import/vsid.m3u"] = lines(
        "#EXTM3U",
        "# A playlist saved by VSID",
        "#",
        home + COMMANDO,
        home + THREE_SONGS,
    )
    out["lists/import/sidplaywx.m3u8"] = lines(
        "#",
        home + CLOCK_DEMO,
        home + EITHER_CHIP,
        home + COMMANDO,
    )
    out["lists/import/phosphor.m3u"] = lines(
        "#PHOSPHOR:song=3",
        THREE_SONGS[1:],
        "#PHOSPHOR:song=1",
        COMMANDO[1:],
    )
    # Latin-1, not UTF-8: the u with dots is one byte, 0xFC, and the o with dots one byte, 0xF6.
    out["lists/import/sidplay_mac.m3u"] = lines(
        "#EXTM3U",
        "#EXTINF:2,Thirty-two characters of author! - Hülsbeck Tëst",
        "/Users/u/Music/C64Music" + HUELSBECK,
        "#EXTINF:210,Somebody - The User's Own",
        "/Users/u/Music/C64Music/My Rips/Cool Söng.sid",
        encoding="latin-1",
    )
    out["lists/import/audacious.m3u"] = lines(
        "file://" + home + THREE_SONGS + "?2",
        "file://" + home + "/My%20Rips/Cool%20S%C3%B6ng.sid",
        "file://" + home + COMMANDO,
    )
    out["lists/import/mpd.m3u"] = lines(
        THREE_SONGS[1:] + "/tune_002.sid",
        COMMANDO[1:],
        THREE_SONGS[1:] + "/tune_003.sid",
    )
    out["lists/import/winamp.m3u"] = lines(
        "{3}C:\\C64Music" + THREE_SONGS.replace("/", "\\"),
        "{1}C:\\C64Music" + COMMANDO.replace("/", "\\"),
        ending="\r\n",
    )

    # SPEC-175's other clauses.
    out["lists/import/twice.m3u"] = lines("#EXTM3U", home + COMMANDO, home + CLOCK_DEMO, home + COMMANDO)
    out["lists/import/named/mix.m3u"] = lines("#EXTM3U", "#PLAYLIST:Favourites", home + COMMANDO)
    out["lists/import/plain/mix.m3u"] = lines("#EXTM3U", home + COMMANDO)
    out["lists/import/long_name.m3u"] = lines(
        "#PLAYLIST:A name that is a good deal longer than forty characters allow", home + COMMANDO)
    out["lists/import/control_name.m3u"] = lines("#PLAYLIST:Tab\there and a bell\x07 there", home + COMMANDO)
    # Each way the subtune can be given, and which wins when two are there (SPEC-175: the first that applies).
    out["lists/import/subtunes.m3u"] = lines(
        "#SIDTUNE:subtune=3",
        home + THREE_SONGS,
        "#SIDTUNE:subtune=1",
        "#PHOSPHOR:song=2",
        "{3}" + home + THREE_SONGS + "?2",
        "#PHOSPHOR:song=2",
        "{3}" + home + THREE_SONGS,
        "{3}" + home + THREE_SONGS + "?2",
        home + THREE_SONGS + "?2",
        home + THREE_SONGS + "/tune_001.sid",
        home + THREE_SONGS,
    )

    # SPEC-178: one entry for each form, and one that matches nothing. The fourth form needs the path of
    # the folder the file is imported from, which only the test knows: the test adds those two lines.
    out["lists/import/forms.m3u"] = lines(
        home + COMMANDO,                                              # the part after /C64Music/
        CLOCK_DEMO,                                                   # as it stands
        EITHER_CHIP[1:],                                              # with a / put in front
        "/home/u/c64music" + HUELSBECK.lower(),                       # the same three, whatever the case
        "/musicians/t/tester_tom/two_chips_2sid.sid",
        "musicians/t/tester_tom/three_chips_3sid.sid",
        "D:\\Old\\Stuff\\Renamed.sid",                                # the file's name alone
        home + "/MUSICIANS/N/Nobody/Nothing.sid",                     # nothing
    )

    # SPEC-176.
    out["lists/import/jsidplay2.js2"] = lines(
        "<HVSC>" + COMMANDO,
        "<HVSC>" + THREE_SONGS,
        home + CLOCK_DEMO,
    )
    out["lists/import/jsidplay2.json"] = (
        '[{"filename":"/C64Music' + COMMANDO + '"},\n'
        ' {"filename":"/C64Music' + THREE_SONGS + '","rating":5,"more":{"filename":"/C64Music/NOT/This.sid","list":[1,2,{}]}},\n'
        ' {"title":"no file name here"},\n'
        ' {"filename":"/C64Music' + CLOCK_DEMO + '"}]\n').encode("utf-8")
    out["lists/import/not_a_list.json"] = ('{"filename":"/C64Music' + COMMANDO + '"}\n').encode("utf-8")
    out["lists/import/not_objects.json"] = ('["/C64Music' + COMMANDO + '", 7, null]\n').encode("utf-8")

    # SPEC-177: the item's two samples, with the fixture tunes' names in place of its own.
    out["lists/import/format1.spl"] = lines(
        "[SIDPLAY/Windows Playlist]",
        "Format=1",
        "BaseDir=C:\\c64music\\",
        "[1]",
        "Filename=" + THREE_SONGS[1:].replace("/", "\\"),
        "Subsong=3",
        "Time=79",
        "[2]",
        "Filename=" + COMMANDO[1:].replace("/", "\\"),
        "Subsong=1",
        "Time=3",
        ending="\r\n",
    )
    out["lists/import/format2.spl"] = lines(
        "[SIDPLAY/Windows Playlist]",
        "Format=2",
        "Basedir=MUSICIANS\\T\\Tester_Tom",
        "file=Three_Songs.sid",
        "name=Three Subtunes",
        "subtune=1",
        "time=6",
        "file=New_Chip.sid",
        "name=New Chip",
        "subtune=1",
        "time=2",
        ending="\r\n",
    )


def make_small_collection(out, kit):
    """Two tunes, each alone in its folder, so that a script finds each at row 0."""
    tunes = kit.good_tunes()
    listed = [(COMMANDO, tunes["commando_test"], "0:03.500"), (THREE_SONGS, tunes["three_subtunes"], "0:02 0:02.500 0:03")]
    root = "lists/small/C64Music"
    for path, data, _ in listed:
        out[root + path] = data
    out[root + "/DOCUMENTS/Songlengths.md5"] = kit.song_lengths(listed)
    out[root + "/DOCUMENTS/hv_sids.txt"] = f"# stub for old updaters{kit.CRLF}Release 85{kit.CRLF}".encode("ascii")


def make_structure(out, kit):
    # SPEC-009: a settings file written by hand from SPEC-130's table, with a comment and an empty line.
    out["structure/settings.ini"] = lines(
        "# written by hand",
        "",
        "engine = light",
        "default_length = 0:07",
        "sample_rate = 44100",
    )
    # SPEC-003: one setting, so that a script's tune is rendered by the engine the reference program uses.
    out["structure/engine_accurate.ini"] = lines("engine = accurate")


def make(out, kit):
    make_playlists(out, kit)
    make_imports(out, kit)
    make_small_collection(out, kit)
    make_structure(out, kit)
