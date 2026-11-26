# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-116: the message table in `core` is the specification's table."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Messages(unittest.TestCase):
    def test_table_is_the_specifications(self):
        """SPEC-116 every identifier of the specification's table is in core's table once, with its kind, and no other"""
        spec = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
        table = spec[spec.index("**SPEC-116"):spec.index("## 7.")]
        expected = []
        for line in table.split("\n"):
            if line.startswith("| `MSG"):
                cells = [cell.strip() for cell in line.strip("|").split("|")]
                kind = cells[1].split(";")[0].strip().capitalize()
                expected += [(name, kind) for name in re.findall(r"MSG_[A-Z0-9_]+", cells[0])]
        source = (ROOT / "src" / "core" / "messages.cpp").read_text(encoding="utf-8")
        have = re.findall(r'\{"(MSG_[A-Z0-9_]+)", MessageKind::(\w+)\}', source)
        self.assertEqual(have, expected)
        self.assertEqual(len(set(name for name, _ in have)), len(have), "an identifier is in the table twice")
