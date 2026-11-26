# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-241: a simulator run with --metrics writes the measurements file when it exits.

The run is a plain one: an empty home folder, the empty screen, and out again, with no step of the device
test asked for. Which keys such a run writes is the column `MetricWhen` of the table in
src/core/metrics.cpp: those marked EveryRun, which are start_ms, exit_ms, and the keys of SPEC-241 that
belong to no step and need nothing that only the device has.
"""

import getpass
import json
import os
import re
import shutil
import socket
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import simulator  # noqa: E402

PROGRAM = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "sidplayzero"
FAKES = ROOT / "tests" / "simulator" / "fakes" / "keep_metrics"

SCRIPT = """\
# SPEC-241 a plain run for the measurements file: the empty screen, and out again
# fakes: {fakes}
# args: --backend fake
expect screen = empty
quit
"""

# What the conditions for the complete download found: written only where that part is built.
DOWNLOAD_ALL_KEYS = {"memory_check_mib", "unpacker", "card_free_mib", "download_everything_offered"}

FORMS = {
    "Number": r"\d+(\.\d+)?",
    "NumberOrNotMeasured": r"\d+(\.\d+)?|not measured",
    "YesNo": r"yes|no",
    "Engine": r"accurate|light",
    "Text": r"[^\n]*",
}


def core_table():
    """{key: (form, when)} from core's table of measurements."""
    source = (ROOT / "src" / "core" / "metrics.cpp").read_text(encoding="utf-8")
    return {key: (form, when) for key, form, when in
            re.findall(r'\{"([a-z0-9_]+)", MetricForm::(\w+), MetricWhen::(\w+)\}', source)}


class MetricsFile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp(prefix="spz-keep-metrics-"))
        script = cls.work / "plain_run.script"
        script.write_text(SCRIPT.format(fakes=FAKES), encoding="utf-8")
        out = cls.work / "out"
        out.mkdir()
        cls.file = cls.work / "metrics.txt"
        cls.status, cls.output, _ = simulator.run_script(PROGRAM, script, out, extra_args=("--metrics", str(cls.file)))
        cls.text = cls.file.read_text(encoding="utf-8") if cls.file.exists() else None
        cls.table = core_table()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)

    def values(self):
        """The file as {key: value}. Fails if it is not there, or a line is not `key = value`, or a key is there twice."""
        said = " / ".join(line.strip() for line in self.output.strip().split("\n") if line.strip())[-600:]
        self.assertEqual(self.status, 0, "the run's exit status; it said: " + said)
        self.assertIsNotNone(self.text, "the run wrote no measurements file")
        self.assertTrue(self.text.endswith("\n"), "the file's last line has no end")
        found = {}
        for line in self.text.split("\n")[:-1]:
            match = re.fullmatch(r"([a-z][a-z0-9_]*) =(?: (.*))?", line)
            self.assertIsNotNone(match, f"not `key = value`: {line!r}")
            self.assertNotIn(match.group(1), found, f"{match.group(1)} is in the file twice")
            found[match.group(1)] = match.group(2) or ""
        return found

    def value(self, key):
        values = self.values()
        self.assertIn(key, values, f"the measurements file has no {key}; it holds: {sorted(values)}")
        return values[key]

    def test_keys(self):
        """SPEC-241 SPEC-201 a run started with --metrics writes, when it exits, a file whose keys are exactly those that apply off the device"""
        every_run = {key for key, (_, when) in self.table.items() if when == "EveryRun"}
        self.assertLessEqual({"start_ms", "exit_ms", "app_version", "engine_library", "user_groups"}, every_run)
        keys = set(self.values())
        for key in keys:
            self.assertIn(key, self.table, f"{key} is in neither table of the specification: nothing else is written")
        missing = sorted(every_run - keys - DOWNLOAD_ALL_KEYS)
        unwanted = sorted(keys - every_run - DOWNLOAD_ALL_KEYS)
        if missing or unwanted:
            self.fail(f"the measurements file's keys are not those that apply off the device: it lacks {missing} "
                      f"and has {unwanted} beside them")

    def test_forms(self):
        """SPEC-241 each value in the measurements file has the form of its key"""
        version = json.loads((ROOT / "app-builder.json").read_text(encoding="utf-8"))["version"]
        self.assertRegex(self.value("start_ms"), r"^\d+$")
        self.assertRegex(self.value("exit_ms"), r"^\d+$")
        self.assertRegex(self.value("app_version"), rf"^(SidPlayZero )?{re.escape(version)}$")
        self.assertRegex(self.value("engine_library"), r"^(libsidplayfp )?3\.1\.2$")
        self.assertRegex(self.value("user_groups"), r"^([a-z_][a-z0-9_.-]*( [a-z_][a-z0-9_.-]*)*)?$")
        for key, value in self.values().items():
            form = self.table[key][0]
            self.assertRegex(value, rf"^({FORMS[form]})$", f"{key} is to be a value of the form {form}")

    def test_download_everything(self):
        """SPEC-241 the measurements file says what the conditions for the complete download found [parts: download-all]"""
        # The fakes: 310 MiB of memory available and 150 MiB of swap free; 800 MiB free on the card; a 7z.
        # HVSC is not set up in this run, so the complete download is not offered.
        self.assertEqual(self.value("memory_check_mib"), "460")
        self.assertEqual(self.value("card_free_mib"), "800")
        self.assertEqual(self.value("unpacker"), "yes")
        self.assertEqual(self.value("download_everything_offered"), "no")

    def test_nothing_private(self):
        """SPEC-241 no value in the measurements file holds the name of the user or of the machine"""
        user, machine = getpass.getuser(), socket.gethostname()
        groups = self.value("user_groups").split()
        self.assertNotIn(user, groups, "user_groups holds the group named after the user")
        for key, value in self.values().items():
            words = re.findall(r"[A-Za-z0-9_.-]+", value.lower())
            self.assertNotIn(user.lower(), words, f"{key} holds the user's name")
            if key != "user_groups":          # a group may happen to have the machine's name
                self.assertNotIn(machine.lower(), words, f"{key} holds the machine's name")
            self.assertIsNone(re.search(r"\b\d{1,3}(\.\d{1,3}){3}\b", value), f"{key} holds an address")


if __name__ == "__main__":
    unittest.main()
