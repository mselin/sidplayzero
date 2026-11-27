#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes tests/fixtures/: everything the tests use as input, made here and not collected (SPEC-252, TF-030).

    python3 tools/make_fixtures.py            writes tests/fixtures/ and its MANIFEST
    python3 tools/make_fixtures.py --out DIR  writes DIR instead

Every run writes the same bytes. Nothing here is taken from HVSC or from any other collection: the tunes
are a few dozen bytes of 6502 code written below, and the documents only have HVSC's forms, as observed
in release 85 (CRLF line endings; "[Database]", "; /path" and "md5=lengths" lines; STIL in Latin-1; the
release on the fifth line of HVSC.txt; hv_sids.txt as two lines).

The generator is a list of makers. A phase that needs more fixtures adds a maker beside the others.
"""

import argparse
import hashlib
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

LOAD = 0x1000
CRLF = "\r\n"


# --- tunes -------------------------------------------------------------------------------------------

def player_code(chips):
    """6502 code for a tune: init stores the song number, play changes the pitch on every call. The song
    number sets where the pitch starts, so each subtune sounds different and renders the same every time."""
    init = bytes([0x85, 0xFB,              # STA $FB       the song number, 0 for the first
                  0xA9, 0x00, 0x85, 0xFC])  # LDA #0 ; STA $FC   the call counter
    for chip in range(chips):
        base = 0xD400 + 0x20 * chip
        for address, value in ((base + 0x18, 0x0F), (base + 0x05, 0x09), (base + 0x06, 0xF0)):
            init += bytes([0xA9, value, 0x8D, address & 0xFF, address >> 8])   # volume, attack/decay, sustain/release
    init += bytes([0x60])                                                     # RTS
    play = bytes([0xE6, 0xFC])                                                # INC $FC
    for chip in range(chips):
        base = 0xD400 + 0x20 * chip
        play += bytes([0xA5, 0xFB, 0x0A, 0x0A, 0x0A, 0x0A, 0x18, 0x65, 0xFC])  # LDA $FB ; ASL x4 ; CLC ; ADC $FC
        play += bytes([0x69, 0x10 * chip])                                    # ADC #chip*16: each chip its own pitch
        play += bytes([0x8D, (base + 1) & 0xFF, (base + 1) >> 8])             # STA frequency, high byte
        play += bytes([0xA9, 0x21, 0x8D, (base + 4) & 0xFF, (base + 4) >> 8])  # sawtooth, gate on
    play += bytes([0x60])
    return init, play


def field(text):
    """A 32-byte header field in Windows-1252."""
    data = text if isinstance(text, bytes) else text.encode("cp1252")
    if len(data) > 32:
        raise ValueError(f"header field too long: {text!r}")
    return data.ljust(32, b"\0")


def psid(title, author="Test Author", released="2026 SidPlayZero", songs=1, start=1, chips=1, clock="pal",
         model="6581", magic=b"PSID", version=None, basic=False):
    """A whole tune file. `clock` is pal, ntsc or either; `model` is 6581, 8580 or either."""
    version = version or (2 if chips == 1 else 3 if chips == 2 else 4)
    init, play = player_code(chips)
    flags = {"pal": 0x04, "ntsc": 0x08, "either": 0x0C}[clock]
    model_bits = {"6581": 1, "8580": 2, "either": 3}[model]
    flags |= model_bits << 4
    if chips >= 2:
        flags |= model_bits << 6
    if chips >= 3:
        flags |= model_bits << 8
    header = bytearray(0x7C)
    header[0:4] = magic
    header[4:6] = version.to_bytes(2, "big")
    header[6:8] = (0x7C).to_bytes(2, "big")
    header[0x0E:0x10] = songs.to_bytes(2, "big")
    header[0x10:0x12] = start.to_bytes(2, "big")
    header[0x16:0x36] = field(title)
    header[0x36:0x56] = field(author)
    header[0x56:0x76] = field(released)
    if basic:
        # An RSID tune that starts from BASIC: load address in the data, no init or play address.
        flags |= 0x02
        header[0x76:0x78] = flags.to_bytes(2, "big")
        program = bytes([0x01, 0x08,                                      # loads at $0801
                         0x0B, 0x08, 0x0A, 0x00, 0x9E, 0x32, 0x30, 0x36, 0x31, 0x00, 0x00, 0x00,  # 10 SYS2061
                         0x60])                                           # RTS
        return bytes(header) + program
    header[0x08:0x0A] = LOAD.to_bytes(2, "big")
    header[0x0A:0x0C] = LOAD.to_bytes(2, "big")
    header[0x0C:0x0E] = (LOAD + len(init)).to_bytes(2, "big")
    header[0x76:0x78] = flags.to_bytes(2, "big")
    if chips >= 2:
        header[0x7A] = 0x42            # the second chip at $D420
    if chips >= 3:
        header[0x7B] = 0x44            # the third at $D440
    return bytes(header) + init + play


def cp1252_titles():
    """Seven 32-byte titles that between them hold every printable Windows-1252 character (SPEC-010)."""
    undefined = {0x81, 0x8D, 0x8F, 0x90, 0x9D}
    codes = [c for c in range(0x21, 0x100) if c != 0x7F and c not in undefined]
    codes.insert(1, 0x20)              # the space goes inside a title, not at an end of one
    assert len(codes) == 218
    return [bytes(codes[i:i + 32]) for i in range(0, len(codes), 32)]


def good_tunes():
    """name -> bytes: the tunes that play."""
    tunes = {
        "commando_test": psid("Commando Test"),
        "three_subtunes": psid("Three Subtunes", songs=3, start=2),
        "two_sid": psid("Two Chips", chips=2),
        "three_sid": psid("Three Chips", chips=3),
        "model_6581": psid("Old Chip", model="6581"),
        "model_8580": psid("New Chip", model="8580"),
        "model_either": psid("Either Chip", model="either"),
        "clock_pal": psid("Fifty Hertz", clock="pal"),
        "clock_ntsc": psid("Sixty Hertz", clock="ntsc"),
        "needs_basic": psid("Needs BASIC", magic=b"RSID", basic=True),
        "huelsbeck_test": psid("Hülsbeck Tëst", author="Thirty-two characters of author!", released="1988 Thirty-two chars released."),
        "moved": psid("Moved Between Releases"),
        "changed_84": psid("Changed Between Releases", released="1984 first version"),
        "changed_85": psid("Changed Between Releases", released="1985 second version"),
        "own_tune": psid("The User's Own", author="Somebody"),
    }
    for number, title in enumerate(cp1252_titles(), 1):
        tunes[f"cp1252_{number}"] = psid(title, author=f"Windows-1252, part {number} of 7")
    return tunes


def bad_tunes():
    """name -> bytes: one tune breaking each row of SPEC-203's table, an empty file, and noise."""
    good = psid("Broken")

    def changed(offset, data):
        tune = bytearray(good)
        tune[offset:offset + len(data)] = data
        return bytes(tune)

    noise = hashlib.sha256(b"SidPlayZero fixture noise").digest() * 8
    return {
        "bad_too_large": good + bytes(1024 * 1024),
        "bad_only_header": good[:0x7C],
        "bad_cut_in_header": good[:0x40],
        "bad_magic": changed(0, b"PSIX"),
        "bad_version_0": changed(4, (0).to_bytes(2, "big")),
        "bad_version_5": changed(4, (5).to_bytes(2, "big")),
        "bad_rsid_version_1": changed(0, b"RSID")[:4] + (1).to_bytes(2, "big") + good[6:],
        "bad_data_offset": changed(6, (0x80).to_bytes(2, "big")),
        "bad_no_songs": changed(0x0E, (0).to_bytes(2, "big")),
        "bad_too_many_songs": changed(0x0E, (257).to_bytes(2, "big")),
        "bad_default_song": changed(0x10, (2).to_bytes(2, "big")),
        "bad_empty": b"",
        "bad_noise": noise,
    }


# --- HVSC's documents, in their forms ------------------------------------------------------------------

def md5(data):
    return hashlib.md5(data).hexdigest()


def song_lengths(entries):
    """entries: [(HVSC path, tune bytes, "length length ...")], in the order HVSC would list them."""
    lines = ["[Database]"]
    for path, data, lengths in entries:
        lines += [f"; {path}", f"{md5(data)}={lengths}"]
    return (CRLF.join(lines) + CRLF).encode("ascii")


def hvsc_txt(release):
    lines = [
        "-" * 77,
        " " * 24 + "T H E   H I G H   V O L T A G E",
        " " * 26 + "S I D   C O L L E C T I O N",
        "",
        " " * 34 + f"Release {release}",
        "",
        " " * 32 + "A fixture, not HVSC",
        "",
        "This file has the form of HVSC's HVSC.txt and none of its text. It was written",
        "by tools/make_fixtures.py for SidPlayZero's tests.",
        "",
        "-------------------------------",
        "COPYRIGHT NOTICE AND DISCLAIMER",
        "-------------------------------",
        "",
        f"This is the copyright section of a test fixture for release {release}.",
        "The tunes beside it are a few bytes of code written for the tests.",
        "",
        "------------",
        "INTRODUCTION",
        "------------",
        "",
        "Nothing more.",
    ]
    return (CRLF.join(lines) + CRLF).encode("ascii")


def stil_txt():
    lines = [
        "#" * 78,
        "#  A fixture in the form of HVSC's STIL.txt, written by tools/make_fixtures.py.",
        "#" * 78,
        "",
        "/MUSICIANS/H/Hubbard_Rob/Commando.sid",
        "  TITLE: Commando Test",
        " ARTIST: Test Author",
        "COMMENT: A fixture's note, with an é in it to show that STIL is Latin-1.",
        "",
        "/MUSICIANS/T/Tester_Tom/Three_Songs.sid",
        "(#2)",
        "COMMENT: The second subtune is the one the file names as its default.",
        "",
    ]
    return (CRLF.join(lines) + CRLF).encode("latin-1")


def collection(release, tunes):
    """A small collection laid out like HVSC: {path under C64Music: bytes}. Release 84 differs from 85 in
    one tune that moved and one whose bytes changed."""
    listed = [
        ("/DEMOS/A-F/Clock_Demo.sid", tunes["clock_ntsc"], "0:02"),
        ("/GAMES/A-F/Either_Chip.sid", tunes["model_either"], "0:02"),
        ("/MUSICIANS/H/Hubbard_Rob/Commando.sid", tunes["commando_test"], "0:03.500"),
        ("/MUSICIANS/H/Huelsbeck_Chris/Huelsbeck_Test.sid", tunes["huelsbeck_test"], "0:02"),
        ("/MUSICIANS/T/Tester_Tom/Changed.sid", tunes[f"changed_{release}"], "0:02"),
        ("/MUSICIANS/T/Tester_Tom/New_Chip.sid", tunes["model_8580"], "0:02"),
        ("/MUSICIANS/T/Tester_Tom/Needs_BASIC.sid", tunes["needs_basic"], "0:02"),
        ("/MUSICIANS/T/Tester_Tom/Three_Chips_3SID.sid", tunes["three_sid"], "0:02"),
        ("/MUSICIANS/T/Tester_Tom/Three_Songs.sid", tunes["three_subtunes"], "0:02 0:02.5 0:03"),
        ("/MUSICIANS/T/Tester_Tom/Two_Chips_2SID.sid", tunes["two_sid"], "0:02"),
    ]
    moved = "/MUSICIANS/T/Tester_Tom/Old_Name.sid" if release == 84 else "/MUSICIANS/T/Tester_Tom/Renamed.sid"
    listed.append((moved, tunes["moved"], "0:02"))
    listed.sort(key=lambda entry: entry[0].lower())
    files = {path.lstrip("/"): data for path, data, _ in listed}
    files["DOCUMENTS/Songlengths.md5"] = song_lengths(listed)
    files["DOCUMENTS/STIL.txt"] = stil_txt()
    files["DOCUMENTS/HVSC.txt"] = hvsc_txt(release)
    files["DOCUMENTS/BUGlist.txt"] = (CRLF.join(["A fixture in place of HVSC's BUGlist.txt.", ""])).encode("ascii")
    files["DOCUMENTS/hv_sids.txt"] = (f"# stub for old updaters{CRLF}Release {release}{CRLF}").encode("ascii")
    return files


# --- the makers ------------------------------------------------------------------------------------------

def make_tunes(out):
    for name, data in {**good_tunes(), **bad_tunes()}.items():
        out[f"tunes/{name}.sid"] = data


def make_collections(out):
    tunes = good_tunes()
    for release in (85, 84):
        for path, data in collection(release, tunes).items():
            out[f"collections/{release}/C64Music/{path}"] = data
    # A tune of the user's own, which HVSC does not list, with a name HVSC would never use.
    out["own/My Rips/Cool Söng.sid"] = tunes["own_tune"]


MAKERS = [make_tunes, make_collections]


def more_makers():
    """The makers of tools/fixture_makers/*.py: each module has make(out, kit), where `out` is the dict of
    {relative path: bytes} to add to, and `kit` is this module, for psid(), good_tunes(), collection() and
    the rest. A fixture belongs beside the tests that use it, so each area of tests has a module there."""
    import importlib.util
    found = []
    for path in sorted((ROOT / "tools" / "fixture_makers").glob("*.py")):
        spec = importlib.util.spec_from_file_location(f"fixture_makers_{path.stem}", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        found.append(module.make)
    return found


def build():
    """Everything, as {relative path: bytes}."""
    out = {}
    kit = sys.modules[__name__]
    for maker in MAKERS:
        maker(out)
    for maker in more_makers():
        before = dict(out)
        maker(out, kit)
        changed = [name for name, data in before.items() if out.get(name) != data]
        if changed:
            raise SystemExit(f"a fixture maker changed fixtures that were there already: {changed}")
    return out


def write(folder):
    folder = Path(folder)
    files = build()
    shutil.rmtree(folder, ignore_errors=True)
    for name in sorted(files):
        path = folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(files[name])
    manifest = "".join(f"{name}\t{hashlib.sha256(files[name]).hexdigest()}\n" for name in sorted(files))
    (folder / "MANIFEST").write_text(manifest, encoding="utf-8")
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=str(ROOT / "tests" / "fixtures"))
    arguments = parser.parse_args()
    count = write(arguments.out)
    print(f"wrote {count} files to {arguments.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
