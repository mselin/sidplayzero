# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the tests of what the app keeps between runs (SPEC-130 to SPEC-135, SPEC-207): settings
and state files, as a person or an accident might leave them. They are under tests/fixtures/keep/."""

import hashlib

# A settings file written by hand from SPEC-130's table and from nothing else: every key, each with a
# value that is allowed and is not its default.
SETTINGS_BY_HAND = """\
# SidPlayZero settings, typed in by hand

collection = /media/card/My SID Music
engine = light
sample_rate = 44100
when_song_ends = next_tune
repeat_all = on
shuffle = on
default_length = 4:05
sid_model = 8580
clock = ntsc
rom_kernal = /media/card/roms/kernal.bin
rom_basic = /media/card/roms/basic.bin
rom_chargen = /media/card/roms/chargen.bin
resume_on_start = on
screen_off_after = 120
url_files = https://mirror.example.org/hvsc/C64Music
url_archive = https://mirror.example.org/hvsc/HVSC_{release}-all-of-them.7z
"""

# The same for a build with none of the parts of SPEC-115: only the settings that are always there.
SETTINGS_BY_HAND_MUST = """\
# SidPlayZero settings, typed in by hand

collection = /media/card/My SID Music
engine = light
default_length = 4:05
url_files = https://mirror.example.org/hvsc/C64Music
"""

# Nothing the app understands, and nothing that is a fault: comments and a key of some later version.
SETTINGS_COMMENTS = """\
# SidPlayZero settings
# a comment of the user's own
future_key = something a newer version wrote
"""


def noise():
    """Three hundred bytes that are not text of any kind: the same on every run."""
    data = b""
    block = b"SidPlayZero fixture: a settings file of random bytes"
    while len(data) < 300:
        block = hashlib.sha256(block).digest()
        data += block
    data = data[:300]
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return data
    raise SystemExit("the noise fixture came out as valid UTF-8; change its seed")


def make(out, kit):
    out["keep/settings_noise.ini"] = noise()
    out["keep/settings_empty.ini"] = b""
    out["keep/settings_comments.ini"] = SETTINGS_COMMENTS.encode("utf-8")
    out["keep/settings_by_hand.ini"] = SETTINGS_BY_HAND.encode("utf-8")
    out["keep/settings_by_hand_must.ini"] = SETTINGS_BY_HAND_MUST.encode("utf-8")
    out["keep/settings_screen_off_10.ini"] = b"screen_off_after = 10\n"
    out["keep/settings_resume_on.ini"] = b"resume_on_start = on\n"
    out["keep/state_backlight_70.ini"] = b"backlight_saved = 70\n"
