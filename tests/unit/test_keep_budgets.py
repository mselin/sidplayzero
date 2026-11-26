# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-240, SPEC-241: the measurements the budgets are held against are ones the app writes.

The specification has two tables: SPEC-240's, which names a measurement for each budget, and SPEC-241's,
which names the other keys the app writes. `core` has one table of every key it may write
(src/core/metrics.cpp). These tests read all three and compare them.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEY = re.compile(r"`([a-z][a-z0-9_]*(?:_N_[a-z0-9_]+)?)`")


def item(number):
    """The text of one item of the specification, up to the next item."""
    spec = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
    start = spec.index(f"**SPEC-{number} ")
    return spec[start:spec.index("**SPEC-", start + 8)]


def table_rows(text):
    """The rows of the first table in a piece of the document, as lists of cells, without its two head lines."""
    rows = [[cell.strip() for cell in line.strip().strip("|").split("|")]
            for line in text.split("\n") if line.startswith("|")]
    return rows[2:]


def budget_keys():
    """The measurement each row of SPEC-240's table names, in order; and the rows that name none."""
    keys, without = [], []
    for cells in table_rows(item(240)):
        found = KEY.findall(cells[3])
        if found:
            keys += found
        else:
            without.append(cells[0])
    return keys, without


def other_keys():
    """The keys of SPEC-241's own table, in order, with `bench_N_...` written out for N from 1 to 4."""
    keys = []
    for cells in table_rows(item(241)):
        found = KEY.findall(cells[0])
        if any("_N_" in key for key in found):
            if "for N from 1 to 4" not in cells[0]:
                raise AssertionError(f"SPEC-241's row for {found} no longer says which N it means")
            keys += [key.replace("_N_", f"_{n}_") for n in range(1, 5) for key in found]
        else:
            keys += found
    return keys


def core_rows():
    """The rows of core's table: (key, form, when)."""
    source = (ROOT / "src" / "core" / "metrics.cpp").read_text(encoding="utf-8")
    return re.findall(r'\{"([a-z0-9_]+)", MetricForm::(\w+), MetricWhen::(\w+)\}', source)


class Budgets(unittest.TestCase):
    def test_every_budget_names_a_measurement_the_app_writes(self):
        """SPEC-240 every measurement the budget table names is a key the app writes (SPEC-241)"""
        keys, without = budget_keys()
        self.assertEqual(len(keys), 17, keys)
        self.assertEqual(len(set(keys)), len(keys), "a measurement is named by two budgets")
        # One budget is measured by the host tests' own clocks and has no key: a bad file handled.
        self.assertEqual(without, ["A bad file handled"])
        written = [key for key, _, _ in core_rows()]
        for key in keys:
            self.assertIn(key, written, f"SPEC-240 names {key}, and the app's table of measurements does not have it")

    def test_tables_agree(self):
        """SPEC-241 SPEC-240 the app's table of measurements has the keys of the specification's two tables, in their order, and no other"""
        budget, _ = budget_keys()
        expected = budget + other_keys()
        have = [key for key, _, _ in core_rows()]
        self.assertEqual(have, expected)
        self.assertEqual(len(set(have)), len(have), "a key is in the table twice")

    def test_budget_measurements_are_numbers(self):
        """SPEC-240 each budget's measurement is written as a number, and step two of the index as a number or as not measured"""
        forms = {key: form for key, form, _ in core_rows()}
        for key in budget_keys()[0]:
            wanted = "NumberOrNotMeasured" if key == "index_tunes_per_second" else "Number"
            self.assertEqual(forms.get(key), wanted, key)


if __name__ == "__main__":
    unittest.main()
