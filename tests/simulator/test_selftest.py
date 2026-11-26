# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-260: the self-test's steps, as the simulator sees them. What a script cannot do in a line, these
tests do: look at the screen every 50 ms of a whole benchmark, and count the lines of the results file."""

import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import simulator  # noqa: E402
from test_keys_support import parts_built  # noqa: E402

PROGRAM = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "sidplayzero"
BESIDE = ROOT / "tests" / "simulator"
RESULTS = ".local/state/sidplayzero/selftest.results"

BENCHMARK_SET = [
    "/MUSICIANS/H/Hubbard_Rob/Commando.sid",
    "/MUSICIANS/G/Galway_Martin/Arkanoid.sid",
    "/MUSICIANS/J/Jammer/Antipop_2SID.sid",
    "/DEMOS/A-F/Devils_ReSIDence_3SID.sid",
]


def budget_keys():
    """The measurements SPEC-240's table names, read from the document."""
    text = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
    table = text.split("**SPEC-240 ")[1].split("- Covers:")[0]
    keys = []
    for row in table.split("\n"):
        cells = [cell.strip() for cell in row.split("|")]
        if len(cells) >= 6 and re.fullmatch(r"`[a-z0-9_]+`", cells[4]):
            keys.append(cells[4].strip("`"))
    return keys


def lines_of(data):
    """The results file as [(key, value)], in order."""
    pairs = []
    for line in data.decode("utf-8").split("\n"):
        if line:
            key, _, value = line.partition(" = ")
            pairs.append((key, value))
    return pairs


class SelfTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-selftest-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def run_lines(self, header, lines):
        script = self.work / "generated.script"
        script.write_text("\n".join(header + lines) + "\n", encoding="utf-8")
        out = self.work / "out"
        out.mkdir()
        status, output, options = simulator.run_script(PROGRAM, script, out, collect=[RESULTS], beside=BESIDE,
                                                       timeout=600)
        self.assertEqual(status, 0, output[-600:])
        return out, options["collected"][RESULTS]

    def test_budget_table_is_read(self):
        """SPEC-260 SPEC-240 the table of budgets names seventeen measurements, which this test reads from the document"""
        keys = budget_keys()
        self.assertEqual(len(keys), 17, keys)
        self.assertEqual(len(set(keys)), 17, keys)
        for key in ("start_ms", "exit_ms", "hvsc_setup_s", "underrun_frames", "key_to_screen_ms_p95"):
            self.assertIn(key, keys)

    def test_benchmark(self):
        """SPEC-260 the benchmark step plays the set on the ordinary screens while the key script presses keys, then ends on the self-test screen with its keys written"""
        header = ["# SPEC-260 the benchmark step, looked at every 50 ms",
                  "# fakes: fakes/selftest_benchmark",
                  "# fixture: collections/bench/C64Music C64Music",
                  "# args: --backend fake --seed 1"]
        lines = ["dump at_00000"]
        for step in range(1, 801):                       # 40 s: the 30 s asked for, and time to build the index
            lines += ["advance 50", f"dump at_{step * 50:05d}"]
        lines += ["settle", "dump end", "quit"]
        out, results = self.run_lines(header, lines)
        dumps = [json.loads(path.read_text(encoding="utf-8")) for path in sorted(out.glob("at_*.json"))]
        self.assertEqual(len(dumps), 801)
        playing = dumps[:600]                            # the 30 s of benchmark_seconds

        first_second = [d["notice"]["id"] for d in dumps[:20] if d.get("notice")]
        self.assertIn("MSG_SELFTEST", first_second, "the notice MSG_SELFTEST did not show at the benchmark's start")

        jobs = [d["job"] for d in playing if d.get("job")]
        self.assertEqual(len(jobs), len(playing), "the job mark is not there for the whole of the benchmark")
        self.assertEqual({job["kind"] for job in jobs}, {"selftest"})
        self.assertEqual(len({job["total"] for job in jobs}), 1, "the job's total changed while it ran")
        done = [job["done"] for job in jobs]
        self.assertEqual(done, sorted(done), "the share of the benchmark done went backwards")
        self.assertLess(done[0], done[-1])
        self.assertTrue(all(d["mini"] and d["mini"]["job"] for d in playing), "the top line lost its job mark")

        screens = [d["screen"] for d in playing]
        self.assertNotIn("selftest", screens, "the self-test screen stayed open while the set played")
        turns = [s for i, s in enumerate(screens) if i == 0 or screens[i - 1] != s]
        text = " ".join(turns)
        self.assertRegex(text, r"browser( \w+)* search( \w+)* browser", "the browser and Search did not show in turn")
        self.assertIn("settings", turns, "the key script did not open Settings")
        typed = {field["text"] for d in playing if d["screen"] == "search" for field in (d.get("fields") or {}).values()}
        self.assertIn("hub 1985", typed, "the key script did not type its first query")
        self.assertIn("galway ark", typed, "the key script did not type its second query")
        titles = {d["title"] for d in playing if d["screen"] == "browser"}
        self.assertIn("/MUSICIANS/B/Bayliss_Richard", titles, "the key script did not open the first folder")
        self.assertIn("/MUSICIANS/S", titles, "the key script did not open the second folder")
        self.assertIn("/", titles, "the key script did not start from the root")

        self.assertTrue(all(d["player"] and d["player"]["state"] in ("playing", "loading") for d in playing[1:]),
                        "the set stopped playing before its time was up")
        order = []
        for d in playing:
            path = d["player"]["path"] if d.get("player") else None
            if path and (not order or order[-1] != path):
                order.append(path)
        self.assertEqual(order[:5], BENCHMARK_SET + BENCHMARK_SET[:1], "the set is not played in its order, round and round")
        self.assertEqual({d["player"]["subtune"] for d in playing if d.get("player") and d["player"]["path"]}, {1})

        end = json.loads((out / "end.json").read_text(encoding="utf-8"))
        self.assertEqual(end["screen"], "selftest", "the run did not end on the self-test screen")
        self.assertIn("Done. Hold ESC to leave.", end["lines"])

        self.assertIsNotNone(results, "selftest.results was not written")
        pairs = lines_of(results)
        keys = [key for key, _ in pairs]
        wanted = [key for key in budget_keys() if key not in ("start_ms", "exit_ms", "hvsc_setup_s")]
        if "detail" not in parts_built():
            wanted.remove("detail_ms_max")      # the detail line, and so its time, is the part `detail`'s
        for number in range(1, 5):
            wanted += [f"bench_{number}_engine", f"bench_{number}_render_load_percent"]
        for key in wanted:
            self.assertIn(key, keys, f"the benchmark step did not write {key}")
        self.assertIn(("step_done", "benchmark"), pairs)
        self.assertLess(max(keys.index(key) for key in wanted), keys.index("step_done"),
                        "step_done was written before the step's keys")
        values = dict(pairs)
        self.assertEqual(values["underrun_frames"], "0")
        self.assertEqual(values["index_tunes_per_second"], "not measured")   # fewer than 1,000 tunes are present
        self.assertEqual(len([key for key in keys if key == "start_ms"]), 1)

    def test_no_step(self):
        """SPEC-260 with the request none, two starts and exits leave two start_ms and two exit_ms lines and no step's keys"""
        header = ["# SPEC-260 two starts and two exits with no step asked for",
                  "# fakes: fakes/selftest_none",
                  "# args: --backend fake --seed 1"]
        _, results = self.run_lines(header, ["advance 1000", "restart", "advance 1000", "quit"])
        self.assertIsNotNone(results, "selftest.results was not written")
        pairs = lines_of(results)
        keys = [key for key, _ in pairs]
        self.assertEqual(keys.count("start_ms"), 2, "one start_ms line for each of the two starts")
        self.assertEqual(keys.count("exit_ms"), 2, "one exit_ms line for each of the two exits")
        self.assertNotIn("step_done", keys)
        for key in keys:
            self.assertNotRegex(key, r"^(key_codes_wrong|speed_factor_|render_|bench_)", "a step's key was written with no step asked for")
        for key in ("app_version", "engine_library"):
            self.assertIn(key, keys, f"{key} belongs to no step and is written at every exit")
        for key, value in pairs:
            if key in ("start_ms", "exit_ms"):
                self.assertRegex(value, r"^\d+$")
        self.assertLess(keys.index("start_ms"), keys.index("exit_ms"))
