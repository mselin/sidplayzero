# SPDX-License-Identifier: GPL-3.0-or-later
"""How the app is built, as far as the built program shows it: the fonts of SPEC-010 drawing the same pixels
on every run, and the three layers of SPEC-020 in the program's own build, where LVGL, the engine and the
sound library are all present and each layer must still see only what it may use."""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import png, simulator  # noqa: E402

BUILD = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator"))
PROGRAM = BUILD / "src" / "sidplayzero"
SCRIPTS = ROOT / "tests" / "simulator"
OUTSIDE = re.compile(r'#\s*include\s*[<"](sidplayfp|residfp|miniaudio|curl/)')
LVGL = re.compile(r'#\s*include\s*[<"](lvgl|lv_)')


def why(output):
    """One line that says where a script stopped."""
    for line in output.split("\n"):
        if line.startswith("line: "):
            return "the script stopped at: " + line[len("line: "):].strip()
    return "the script's output ended: " + " | ".join(output.strip().split("\n")[-3:])


class Fonts(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp(prefix="spz-fonts-"))

    def tearDown(self):
        shutil.rmtree(self.out, ignore_errors=True)

    def test_two_runs_give_the_same_pixels(self):
        """SPEC-010 two runs of the script that shows every printable Windows-1252 character give the same pixels, and each tune's title is drawn"""
        for run in ("one", "two"):
            (self.out / run).mkdir()
            status, output, _ = simulator.run_script(PROGRAM, SCRIPTS / "structure_fonts.script", self.out / run)
            self.assertEqual(status, 0, why(output))
        pictures = []
        for number in range(1, 8):
            name = f"structure_fonts_{number}"
            one, two = self.out / "one" / f"{name}.png", self.out / "two" / f"{name}.png"
            self.assertEqual(one.read_bytes(), two.read_bytes(), f"{name}.png differs between two runs")
            self.assertEqual((self.out / "one" / f"{name}.json").read_bytes(),
                             (self.out / "two" / f"{name}.json").read_bytes(), f"{name}.json differs between two runs")
            width, height, rows = png.read(one)
            self.assertEqual((width, height), (320, 170))
            pictures.append(b"".join(bytes(row) for row in rows))
            # The description holds the characters as Unicode: the title is the fixture's, decoded.
            description = json.loads((self.out / "one" / f"{name}.json").read_text(encoding="utf-8"))
            title = (ROOT / "tests" / "fixtures" / "tunes" / f"cp1252_{number}.sid").read_bytes()[0x16:0x36]
            self.assertEqual(description["lines"][0], title.rstrip(b"\0").decode("cp1252"))
        # Seven titles, seven different pictures: the text that differs is on the screen.
        self.assertEqual(len(set(pictures)), 7, "two tunes with different titles gave the same picture")


class Layers(unittest.TestCase):
    """SPEC-020's table, in the build of the program itself."""

    @classmethod
    def setUpClass(cls):
        commands = json.loads((BUILD / "compile_commands.json").read_text())
        cls.by_layer = {layer: [c for c in commands if c["file"].startswith(str(ROOT / "src" / layer) + "/")]
                        for layer in ("core", "ui", "platform")}

    def can_include(self, entry, header):
        """Whether a file compiled by the command of `entry` could include `header`: a probe is compiled in
        its place, with the same command but for where the output and the list of dependencies go."""
        with tempfile.TemporaryDirectory(prefix="spz-probe-") as folder:
            probe = Path(folder) / "probe.cpp"
            probe.write_text(f"#include <{header}>\nint spz_probe() {{ return 0; }}\n", encoding="utf-8")
            given = shlex.split(entry["command"])
            source = next(w for w in given if w.endswith(Path(entry["file"]).name) and not w.endswith(".o"))
            words, skip = [], 0
            for word in given:
                if skip:
                    skip -= 1
                elif word in ("-MT", "-MF"):
                    skip = 1
                elif word != "-MD":
                    words.append(str(probe) if word == source else word)
            words[words.index("-o") + 1] = str(Path(folder) / "probe.o")
            done = subprocess.run(words, cwd=entry["directory"], text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, errors="replace", timeout=300)
            return done.returncode == 0

    def distinct(self, layer):
        """One compile command for each set of flags a layer's files are built with."""
        seen = {}
        for entry in self.by_layer[layer]:
            flags = " ".join(w for w in shlex.split(entry["command"]) if Path(entry["file"]).stem not in w)
            seen.setdefault(flags, entry)
        self.assertGreater(len(seen), 0, f"the build has no files of {layer}")
        return list(seen.values())

    def test_core_sees_none_of_them(self):
        """SPEC-020 in the program's build, a file of `core` can include none of LVGL, the engine and miniaudio"""
        for entry in self.distinct("core"):
            self.assertTrue(self.can_include(entry, "spz/types.h"), "the probe itself does not compile as core")
            for header in ("lvgl.h", "sidplayfp/sidplayfp.h", "miniaudio.h"):
                self.assertFalse(self.can_include(entry, header), f"{entry['file']} is built with a path to {header}")

    def test_ui_sees_lvgl_and_core_only(self):
        """SPEC-020 in the program's build, a file of `ui` can include LVGL and `core`, and neither the engine nor miniaudio"""
        for entry in self.distinct("ui"):
            self.assertTrue(self.can_include(entry, "lvgl.h"), f"{entry['file']} cannot include lvgl.h")
            self.assertTrue(self.can_include(entry, "spz/types.h"), f"{entry['file']} cannot include core")
            for header in ("sidplayfp/sidplayfp.h", "miniaudio.h"):
                self.assertFalse(self.can_include(entry, header), f"{entry['file']} is built with a path to {header}")

    def test_platform_holds_the_libraries(self):
        """SPEC-020 in the program's build, `platform` is where the engine, miniaudio and libcurl are used: a file of it can include each"""
        entries = self.distinct("platform")
        for header in ("spz/types.h", "miniaudio.h", "sidplayfp/sidplayfp.h", "curl/curl.h"):
            self.assertTrue(any(self.can_include(entry, header) for entry in entries),
                            f"no file of platform is built with a path to {header}")

    def test_sources_name_only_what_their_layer_may_use(self):
        """SPEC-020 no source of `core` or `ui` includes the engine, miniaudio or libcurl, and no source of `core` includes LVGL"""
        for layer in ("core", "ui"):
            for path in sorted((ROOT / "src" / layer).rglob("*")):
                if path.suffix not in (".h", ".cpp", ".c"):
                    continue
                for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").split("\n"), 1):
                    where = f"{path.relative_to(ROOT)}:{number}: {line.strip()}"
                    self.assertIsNone(OUTSIDE.search(line), where)
                    if layer == "core":
                        self.assertIsNone(LVGL.search(line), where)


if __name__ == "__main__":
    unittest.main()
