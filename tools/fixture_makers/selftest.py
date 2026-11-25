# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the self-test (SPEC-260): a collection with the four paths of the benchmark set (SPEC-242)
and the two folders the key script opens (SPEC-240). The tunes are this project's own few bytes; only the
paths are HVSC's."""


def make(out, kit):
    tunes = kit.good_tunes()
    listed = [
        ("/DEMOS/A-F/Devils_ReSIDence_3SID.sid", tunes["three_sid"], "0:05"),
        ("/MUSICIANS/B/Bayliss_Richard/First_Test.sid", kit.psid("First Test", author="Bench Author One"), "0:05"),
        ("/MUSICIANS/B/Bayliss_Richard/Second_Test.sid", kit.psid("Second Test", author="Bench Author One"), "0:05"),
        ("/MUSICIANS/B/Bayliss_Richard/Third_Test.sid", kit.psid("Third Test", author="Bench Author One"), "0:05"),
        ("/MUSICIANS/G/Galway_Martin/Arkanoid.sid",
         kit.psid("Arkanoid Test", author="Bench Galway", released="1987 SidPlayZero"), "0:05"),
        ("/MUSICIANS/H/Hubbard_Rob/Commando.sid",
         kit.psid("Commando Bench", author="Bench Hubbard", released="1985 SidPlayZero"), "0:05"),
        ("/MUSICIANS/J/Jammer/Antipop_2SID.sid", tunes["two_sid"], "0:05"),
    ]
    for number in range(1, 31):
        name = f"/MUSICIANS/S/Sample_{number:02d}/Tune_{number:02d}.sid"
        listed.append((name, kit.psid(f"Sample Tune {number}", author=f"Sample Author {number}"), "0:05"))
    listed.sort(key=lambda entry: entry[0].lower())
    base = "collections/bench/C64Music/"
    for path, data, _ in listed:
        out[base + path.lstrip("/")] = data
    out[base + "DOCUMENTS/Songlengths.md5"] = kit.song_lengths(listed)
    out[base + "DOCUMENTS/STIL.txt"] = kit.stil_txt()
    out[base + "DOCUMENTS/HVSC.txt"] = kit.hvsc_txt(85)
    out[base + "DOCUMENTS/BUGlist.txt"] = b"A fixture in place of HVSC's BUGlist.txt.\r\n"
    out[base + "DOCUMENTS/hv_sids.txt"] = b"# stub for old updaters\r\nRelease 85\r\n"
