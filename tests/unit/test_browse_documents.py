# SPDX-License-Identifier: GPL-3.0-or-later
"""What the sources and the key map themselves must say about browsing and searching: SPEC-165 (HVSC's site
is not searched), and the labels SPEC-095 and SPEC-100 give the keys 4 to 8."""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def key_map():
    """The rows of docs/keymap.tsv, as dicts by its column names."""
    lines = [line for line in (ROOT / "docs" / "keymap.tsv").read_text(encoding="utf-8").split("\n")
             if line and not line.startswith("#")]
    names = lines[0].split("\t")
    return [dict(zip(names, line.split("\t"))) for line in lines[1:]]


def labels(place):
    """What the key map shows above the keys 4 to 8 in one place: {key: (label, part)}."""
    return {row["key"]: (row["label"], row["part"]) for row in key_map()
            if row["where"] == place and row["key"] in "45678"}


class SiteSearch(unittest.TestCase):
    def test_no_address_under_api(self):
        """SPEC-165 no source file of the program holds an address under /api/: HVSC's site is not searched"""
        found = []
        for path in sorted((ROOT / "src").rglob("*")):
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            found += [f"{path.relative_to(ROOT)}:{number}" for number, line in enumerate(text.split("\n"), 1)
                      if "/api/" in line]
        self.assertEqual(found, [])


class Labels(unittest.TestCase):
    def test_browser_labels(self):
        """SPEC-095 the key map's labels for the browser are Playing, Search, Lists, Fav and Add, the last three of the part playlists"""
        self.assertEqual(labels("browser"), {
            "4": ("Playing", "-"), "5": ("Search", "-"), "6": ("Lists", "playlists"), "7": ("Fav", "playlists"),
            "8": ("Add", "playlists")})

    def test_search_labels(self):
        """SPEC-100 the key map's labels for the search results are Edit, Folder, Filter, Fav and Add, with Filter of the part filters and the last two of playlists"""
        self.assertEqual(labels("search-results"), {
            "4": ("Edit", "-"), "5": ("Folder", "-"), "6": ("Filter", "filters"), "7": ("Fav", "playlists"),
            "8": ("Add", "playlists")})
        self.assertEqual(labels("search-field"), {})   # while text is typed the digits are typed (SPEC-099)


if __name__ == "__main__":
    unittest.main()
