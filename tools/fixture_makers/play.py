# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the tests of playing (SPEC-060 to SPEC-080, SPEC-096, SPEC-097, SPEC-109): tests/fixtures/play/.

    play/C64Music/     a small collection laid out like HVSC, with its own song length database and STIL:
        QUEUE/         three tunes to step through: one subtune; three subtunes, default 2; two SID chips
        CHIPS/         a tune for one, for two and for three SID chips
        LENGTHS/       lengths that are not whole seconds, a 30-second song, and a tune the database leaves out
        BAD/           a good tune, five truncated ones and another good one (SPEC-069)
        FAIL/          a truncated tune, one the engine refuses, one that needs BASIC, and a good one
        FETCH/         two present tunes, and around them five that the database lists and the folder lacks
        VOICES/        a tune that uses all three voices and the filter (SPEC-074)
    play/absent/       the files of FETCH's absent tunes, at their paths, for what stands in for HVSC's site

Nothing here is music from anywhere: the tunes are the generator's few bytes of 6502 code, or the few more
written below.
"""

CRLF = "\r\n"
LOAD = 0x1000


def voices_tune(kit):
    """A tune of three subtunes that sounds all three voices, each with its own waveform and pitch, and
    sends the first through the filter: so that a muted voice and a filter switched off each change what
    is rendered (SPEC-074). The song number sets where the pitch starts, as in the generator's own tune."""
    def store(value, address):
        return bytes([0xA9, value, 0x8D, address & 0xFF, address >> 8])        # LDA #value ; STA address

    init = bytes([0x85, 0xFB,                    # STA $FB       the song number, 0 for the first
                  0xA9, 0x00, 0x85, 0xFC])       # LDA #0 ; STA $FC   the call counter
    for value, address in ((0x1F, 0xD418),       # full volume, low-pass
                           (0xF1, 0xD417),       # resonance 15, voice 1 through the filter
                           (0x40, 0xD416),       # the cut-off's high byte
                           (0x09, 0xD405), (0xF0, 0xD406),     # voice 1: attack/decay, sustain/release
                           (0x09, 0xD40C), (0xF0, 0xD40D),     # voice 2
                           (0x08, 0xD40A),                     # voice 2: a square pulse
                           (0x09, 0xD413), (0xF0, 0xD414)):    # voice 3
        init += store(value, address)
    init += bytes([0x60])                        # RTS
    play = bytes([0xE6, 0xFC,                    # INC $FC
                  0xA5, 0xFB, 0x0A, 0x0A, 0x0A, 0x0A, 0x18, 0x65, 0xFC,   # LDA $FB ; ASL x4 ; CLC ; ADC $FC
                  0x8D, 0x01, 0xD4,              # STA $D401     voice 1's frequency, high byte
                  0x18, 0x69, 0x25, 0x8D, 0x08, 0xD4,          # CLC ; ADC #$25 ; STA $D408   voice 2's
                  0x18, 0x69, 0x25, 0x8D, 0x0F, 0xD4])         # CLC ; ADC #$25 ; STA $D40F   voice 3's
    play += store(0x21, 0xD404)                  # voice 1: sawtooth, gate on
    play += store(0x41, 0xD40B)                  # voice 2: pulse, gate on
    play += store(0x11, 0xD412)                  # voice 3: triangle, gate on
    play += bytes([0x60])
    header = bytearray(kit.psid("Three Voices", songs=3, start=1)[:0x7C])
    header[0x0C:0x0E] = (LOAD + len(init)).to_bytes(2, "big")          # where play starts
    return bytes(header) + init + play


def refused_tune(kit):
    """A file that passes SPEC-203's check and that the engine refuses: its data says it loads at $FFF0,
    and is longer than the sixteen bytes there are from there to the end of the C64's memory."""
    header = bytearray(kit.psid("The Engine Refuses")[:0x7C])
    header[0x08:0x0A] = (0).to_bytes(2, "big")   # load address 0: it is the data's first two bytes
    return bytes(header) + bytes([0xF0, 0xFF]) + bytes(64)


def stil_txt():
    """In the form of HVSC's STIL.txt: an entry is a path's line, then the fields for the file, then a
    "(#n)" line and the fields for each subtune that has any. Latin-1, CRLF."""
    lines = [
        "#" * 78,
        "#  A fixture in the form of HVSC's STIL.txt, written by tools/fixture_makers/play.py.",
        "#" * 78,
        "",
        "/LENGTHS/Three_Lengths.sid",
        "COMMENT: A note for the whole file, with café in it.",
        "(#1)",
        "  TITLE: First Song",
        " ARTIST: Somebody",
        "(#2)",
        "COMMENT: The second subtune's note runs over",
        "         two lines.",
        "(#3)",
        "  TITLE: Third Song",
        "",
        "/QUEUE/A_One.sid",
        "  TITLE: Queue One",
        "COMMENT: Only a part for the file.",
        "",
        "/QUEUE/B_Three.sid",
        "(#2)",
        "COMMENT: Only a part for subtune 2.",
        "",
    ]
    return (CRLF.join(lines) + CRLF).encode("latin-1")


def make(out, kit):
    psid = kit.psid
    basic = kit.good_tunes()["needs_basic"]
    cut = psid("Cut Short")
    # (path, bytes, lengths): the lengths as the database gives them, or None for a file it leaves out.
    present = [
        ("/BAD/A_Good.sid", psid("Bad Folder, Good A"), "0:02"),
        ("/BAD/B_Bad_1.sid", cut[:0x10], None),
        ("/BAD/C_Bad_2.sid", cut[:0x40], None),
        ("/BAD/D_Bad_3.sid", cut[:0x76], None),
        ("/BAD/E_Bad_4.sid", cut[:0x7B], None),
        ("/BAD/F_Bad_5.sid", cut[:0x7C], None),
        ("/BAD/G_Good.sid", psid("Bad Folder, Good G"), "0:02"),
        ("/CHIPS/One_Chip.sid", psid("One Chip"), "0:02"),
        ("/CHIPS/Three_Chips_3SID.sid", psid("Three Chips Here", chips=3), "0:02"),
        ("/CHIPS/Two_Chips_2SID.sid", psid("Two Chips Here", chips=2), "0:02"),
        ("/FAIL/A_Invalid.sid", cut[:0x50], None),
        ("/FAIL/B_Refused.sid", refused_tune(kit), None),
        ("/FAIL/C_Needs_BASIC.sid", basic, "0:02"),
        ("/FAIL/D_Good.sid", psid("Fail Folder, Good"), "0:02"),
        ("/FETCH/A_Present.sid", psid("Fetch Folder, Present A"), "0:02"),
        ("/FETCH/E_Present.sid", psid("Fetch Folder, Present E"), "0:02"),
        ("/LENGTHS/Long.sid", psid("Long Song"), "0:30"),
        ("/LENGTHS/Odd.sid", psid("Odd Length"), "0:02.001"),
        ("/LENGTHS/Three_Lengths.sid", psid("Three Lengths", songs=3), "0:01.001 0:02.345 1:01.288"),
        ("/LENGTHS/Unlisted.sid", psid("Not In The Database"), None),
        ("/QUEUE/A_One.sid", psid("Queue One"), "0:02"),
        ("/QUEUE/B_Three.sid", psid("Queue Three", songs=3, start=2), "0:02 0:02 0:02"),
        ("/QUEUE/C_Two_Chips.sid", psid("Queue Two Chips", chips=2), "0:02"),
        ("/VOICES/Voices.sid", voices_tune(kit), "0:30 0:30 0:30"),
    ]
    absent = [
        ("/FETCH/B_Absent.sid", psid("Fetch Folder, Absent B", songs=3, start=2), "0:05 0:06 0:07"),
        ("/FETCH/C_Absent.sid", psid("Fetch Folder, Absent C"), "0:02"),
        ("/FETCH/D_Absent.sid", psid("Fetch Folder, Absent D"), "0:02"),
        ("/FETCH/F_Absent.sid", psid("Fetch Folder, Absent F"), "0:02"),
        ("/FETCH/G_Absent.sid", psid("Fetch Folder, Absent G"), "0:02"),
    ]
    for path, data, _ in present:
        out["play/C64Music" + path] = data
    for path, data, _ in absent:
        out["play/absent" + path] = data
    listed = sorted(((path, data, lengths) for path, data, lengths in present + absent if lengths),
                    key=lambda entry: entry[0].lower())
    out["play/C64Music/DOCUMENTS/Songlengths.md5"] = kit.song_lengths(listed)
    out["play/C64Music/DOCUMENTS/STIL.txt"] = stil_txt()
    out["play/C64Music/DOCUMENTS/hv_sids.txt"] = f"# stub for old updaters{CRLF}Release 85{CRLF}".encode("ascii")
