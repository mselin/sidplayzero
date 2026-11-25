# SPDX-License-Identifier: GPL-3.0-or-later
"""Reading tests out of their sources: names, the SPEC and TF items they carry, and their parts (TF-011)."""

import re
from dataclasses import dataclass
from pathlib import Path

from .common import ROOT

TEST_CASE = re.compile(r'TEST_CASE\(\s*"((?:[^"\\]|\\.)*)"')
PARTS_IN_NAME = re.compile(r"\[parts:\s*([^\]]+)\]")
ITEM = re.compile(r"\b(?:SPEC|TF)-\d{3}\b")


@dataclass
class Test:
    name: str
    path: str            # relative to the repository
    line: int
    items: list          # "SPEC-203", "TF-014", ...
    parts: list          # the parts it needs, from "[parts: a b]" or a "parts:" header line


def parts_of(text):
    found = PARTS_IN_NAME.search(text)
    return found.group(1).replace(",", " ").split() if found else []


def cpp_tests(folders):
    """Every TEST_CASE in the .cpp files of these folders."""
    tests = []
    for folder in folders:
        for path in sorted(Path(ROOT / folder).glob("*.cpp")):
            for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
                for name in TEST_CASE.findall(line):
                    tests.append(Test(name, str(path.relative_to(ROOT)), number, ITEM.findall(name), parts_of(name)))
    return tests


def header_tests(folder, pattern):
    """Tests that are one file each (simulator scripts, shell tests): their items and parts are in the
    comment lines at the top, as "# SPEC-nnn ...", "# parts: a b", and the name is the file's."""
    tests = []
    for path in sorted(Path(ROOT / folder).glob(pattern)):
        items, parts, title = [], [], ""
        for line in path.read_text(encoding="utf-8").split("\n"):
            if not line.startswith("#"):
                break
            text = line.lstrip("#").strip()
            if text.startswith("parts:"):
                parts += text[len("parts:"):].replace(",", " ").split()
            else:
                items += ITEM.findall(text)
                if not title and text and not text.startswith(("SPDX", "!", "fakes:", "args:")):
                    title = text
        tests.append(Test(title or path.name, str(path.relative_to(ROOT)), 1, items, parts))
    return tests
