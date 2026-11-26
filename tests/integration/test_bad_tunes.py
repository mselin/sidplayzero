# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-203, at the integration level: bad and cut tune files, given to the real backend under
AddressSanitizer.

The way in is a command of `spz_tool`, built with the sanitizer:

    spz_tool load-tunes [--cut] [--unchecked] FILE...

For each FILE it does what the player does with a tune file: SPEC-203's check, and, if the file passes,
`load` of the real software backend (SPEC-025) with the real engine and a render of 100 ms. With `--cut`
it does that for the first n bytes of the file, for every n from 0 to the file's size. With `--unchecked` it
leaves the player's check out, and gives the file to the backend as it is. It prints one line
for each file,

    NAME = played                      or    NAME = refused MSG_INVALID_SID    or    NAME = failed MSG_...
    NAME = N lengths, C refused, F failed, P played                            (with --cut)

then `engine_sanitizer = address`, which says the engine library inside the tool was compiled with the
sanitizer too, and `done = FILES`. Its status is 0 unless it could not read a file."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get("SPZ_ASAN_TOOL", ROOT / "build" / "tool-asan" / "src" / "spz_tool"))
TUNES = ROOT / "tests" / "fixtures" / "tunes"
HEADER = 0x7C          # every fixture is version 2 or later


def load_tunes(*arguments):
    done = subprocess.run([str(TOOL), "load-tunes", *map(str, arguments)], text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=900,
                          env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=0"})
    return done.returncode, done.stdout


class BadTunes(unittest.TestCase):
    def lines(self, *arguments):
        status, output = load_tunes(*arguments)
        said = " ".join(output.split())
        self.assertNotIn("AddressSanitizer", output, "the sanitizer reported: " + said[-600:])
        self.assertNotIn("runtime error", output, said[-600:])
        self.assertEqual(status, 0, "spz_tool said: " + said[:160])
        found = dict(line.split(" = ", 1) for line in output.strip().split("\n"))
        self.assertEqual(found.pop("engine_sanitizer"), "address")
        return found

    def test_bad_fixtures(self):
        """SPEC-203 every bad fixture is refused with MSG_INVALID_SID by the real backend's way in, under AddressSanitizer with no report"""
        bad = sorted(TUNES.glob("bad_*.sid"))
        self.assertGreaterEqual(len(bad), 8)       # one for each row of the table, an empty file and noise
        found = self.lines(*bad)
        self.assertEqual(found.pop("done"), str(len(bad)))
        self.assertEqual(found, {path.name: "refused MSG_INVALID_SID" for path in bad})

    def test_good_fixtures_cut_short(self):
        """SPEC-203 every good fixture, cut at every length from 0 to its size, goes to the real backend under AddressSanitizer with no report"""
        good = sorted(path for path in TUNES.glob("*.sid") if not path.name.startswith("bad_"))
        self.assertGreaterEqual(len(good), 15)
        found = self.lines("--cut", *good)
        self.assertEqual(found.pop("done"), str(len(good)))
        self.assertEqual(sorted(found), [path.name for path in good])
        for path in good:
            counts = re.fullmatch(r"(\d+) lengths, (\d+) refused, (\d+) failed, (\d+) played", found[path.name])
            self.assertIsNotNone(counts, f"{path.name} = {found[path.name]}")
            lengths, refused, failed, played = map(int, counts.groups())
            self.assertEqual(lengths, path.stat().st_size + 1, path.name)
            # A file no longer than its header is refused, and the check looks at nothing after the header.
            self.assertEqual(refused, HEADER + 1, path.name)
            self.assertEqual(refused + failed + played, lengths, path.name)
            if path.name != "needs_basic.sid":     # that one plays only with the C64's ROMs, which nobody has here
                self.assertGreaterEqual(played, 1, f"{path.name} did not play even whole")

    def test_engine_checks_on_its_own(self):
        """SPEC-203 the real engine makes the check itself: every bad fixture, given to the backend without the player's check (load-tunes --unchecked), is refused by the engine with MSG_INVALID_SID, under AddressSanitizer with no report"""
        bad = sorted(TUNES.glob("bad_*.sid"))
        self.assertGreaterEqual(len(bad), 8)
        found = self.lines("--unchecked", *bad)
        self.assertEqual(found.pop("done"), str(len(bad)))
        self.assertEqual(found, {path.name: "failed MSG_INVALID_SID" for path in bad})
        # A good tune still plays that way.
        found = self.lines("--unchecked", TUNES / "commando_test.sid")
        self.assertEqual(found.get("commando_test.sid"), "played")


class NotAFile(unittest.TestCase):
    """SPEC-203, SPEC-204: what the real Files reads is a regular file. A FIFO in a tune's place, or a link to
    one, would hold whoever opened it until something wrote to it: it is refused at once instead. The way in
    is `spz_tool load-tunes FILE`, which reads FILE with the real Files and ends with status 1, saying
    "cannot read", when it cannot; and `spz_tool listing FILE`, which builds the listing from FILE as the
    song length database and says "ok = no" when it cannot."""

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-fifo-"))
        self.fifo = self.work / "Commando.sid"
        os.mkfifo(self.fifo)
        self.link = self.work / "Link.sid"
        self.link.symlink_to(self.fifo)

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def run_tool(self, *arguments):
        try:
            done = subprocess.run([str(TOOL), *map(str, arguments)], text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=60,
                                  env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=0"})
        except subprocess.TimeoutExpired:
            self.fail(f"spz_tool {' '.join(map(str, arguments))} was still waiting after a minute")
        self.assertNotIn("AddressSanitizer", done.stdout, done.stdout[-600:])
        return done.returncode, done.stdout

    def test_fifo_is_refused_at_once(self):
        """SPEC-203 SPEC-204 a FIFO in a tune's place, or a link to one, is refused by the real Files at once: nothing waits for it to be written"""
        for path in (self.fifo, self.link):
            with self.subTest(path=path.name):
                status, output = self.run_tool("load-tunes", path)
                self.assertEqual(status, 1, output)
                self.assertIn("cannot read", output)

    def test_fifo_as_song_lengths_is_refused_at_once(self):
        """SPEC-204 SPEC-142 a FIFO in the song length database's place, or a link to one, gives no listing, at once"""
        for path in (self.fifo, self.link):
            with self.subTest(path=path.name):
                status, output = self.run_tool("listing", path)
                self.assertEqual(status, 0, output)
                self.assertIn("ok = no", output)
