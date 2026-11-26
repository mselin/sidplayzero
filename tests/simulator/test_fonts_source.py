# SPDX-License-Identifier: GPL-3.0-or-later
"""The sources of the app's fonts (SPEC-010) are kept in the repository, in src/ui/fonts. They are made from
DejaVu's pinned release archive by tools/make_fonts.py, which `cmake -P cmake/fonts.cmake` runs; this test
makes them again into a scratch folder, twice, and compares them with the repository's byte for byte."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEPT = ROOT / "src" / "ui" / "fonts"
NAMES = ("dejavu_sans_12.c", "dejavu_sans_14.c")


class FontSources(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-fonts-source-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def make(self, folder):
        done = subprocess.run(["cmake", f"-DSPZ_FONTS_OUT={folder}", "-P", str(ROOT / "cmake" / "fonts.cmake")],
                              cwd=self.work, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=600)
        self.assertEqual(done.returncode, 0, done.stdout[-800:])

    def test_made_again_they_are_the_same(self):
        """SPEC-010 the font sources in the repository are what the generator makes from the pinned DejaVu archive, the same bytes on every run"""
        for run in ("one", "two"):
            self.make(self.work / run)
        self.assertEqual(sorted(p.name for p in KEPT.glob("*.c")), sorted(NAMES))
        for name in NAMES:
            kept = (KEPT / name).read_bytes()
            self.assertEqual((self.work / "one" / name).read_bytes(), kept, f"src/ui/fonts/{name} is not what is made")
            self.assertEqual((self.work / "two" / name).read_bytes(), kept, f"a second run made another {name}")
