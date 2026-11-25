# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-025: only the software backend's own files, and the one place that creates it, name its type."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OWN_FILES = {"src/core/spz/software_backend.h", "src/core/software_backend.cpp"}
NAMES_IT = re.compile(r"\bSoftwareBackend\b|software_backend\.h")


def files_naming_it():
    """The source files under src/, other than its own, that name the software backend's type or header."""
    found = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".h", ".cpp", ".c", ".hpp"):
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel in OWN_FILES:
            continue
        if NAMES_IT.search(path.read_text(encoding="utf-8", errors="replace")):
            found.append(rel)
    return found


class SoftwareBackendIsNamedOnce(unittest.TestCase):
    def test_one_place_creates_it(self):
        """SPEC-025 the software backend's type is named only in its own files and in the one place that creates it"""
        for own in OWN_FILES:
            self.assertTrue((ROOT / own).is_file(), f"{own} is not there")
        others = files_naming_it()
        if not others:
            self.fail("not built yet: no place in src/ creates the software backend (SPEC-025)")
        self.assertEqual(len(others), 1, "the software backend is named in more than one place: " + ", ".join(others))

    def test_core_and_the_screens_do_not_name_it(self):
        """SPEC-025 no file of core or of the screens names the software backend: the player knows a SoundBackend"""
        inside = [rel for rel in files_naming_it() if rel.startswith(("src/core/", "src/ui/"))]
        self.assertEqual(inside, [], "these know the software backend and must not: " + ", ".join(inside))
