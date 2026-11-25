# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-003, TF-004, TF-020: rules of the build. Tests of the test framework, not of the app."""

import hashlib
import json
import re
import shlex
import shutil
import tempfile
import unittest
from pathlib import Path

from support import ROOT, run, unit_build


class Layers(unittest.TestCase):
    def test_core_cannot_include_lvgl(self):
        """TF-003 SPEC-020 a file of `core` that includes lvgl.h does not compile"""
        commands = json.loads((unit_build() / "compile_commands.json").read_text())
        entry = next(c for c in commands if c["file"].endswith("src/core/version.cpp"))
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "probe.cpp"
            source.write_text("#include <lvgl.h>\nint main() { return 0; }\n")
            words = shlex.split(entry["command"])
            words = [str(source) if w.endswith("version.cpp") else w for w in words]
            out = words.index("-o")
            words[out + 1] = str(Path(folder) / "probe.o")
            process = run(words, cwd=entry["directory"])
            self.assertNotEqual(process.returncode, 0, "core was able to include lvgl.h")
            self.assertIn("lvgl.h", process.stdout)

    def test_core_sources_name_no_outside_library(self):
        """TF-003 SPEC-020 no file of `core` includes LVGL, the engine, the sound library or libcurl"""
        outside = re.compile(r'#\s*include\s*[<"](lvgl|lv_|sidplayfp|residfp|miniaudio|curl/|SDL)')
        for path in sorted((ROOT / "src" / "core").rglob("*")):
            if path.suffix in (".h", ".cpp"):
                for number, line in enumerate(path.read_text().split("\n"), 1):
                    self.assertIsNone(outside.search(line), f"{path.relative_to(ROOT)}:{number}: {line.strip()}")


class UnitProgram(unittest.TestCase):
    def test_link_line(self):
        """TF-020 SPEC-251 the unit tests link `core`, the fakes and the C++ library, and nothing else"""
        process = run(["ninja", "-C", unit_build(), "-t", "commands", "spz_unit_tests"])
        link = [line for line in process.stdout.strip().split("\n") if "spz_unit_tests" in line and " -o " in line][-1]
        libraries = [word for word in shlex.split(link) if word.endswith((".a", ".so")) or word.startswith("-l")]
        self.assertEqual([Path(word).name for word in libraries], ["libspz_core.a"], link)

    def test_runs_without_a_network(self):
        """TF-020 SPEC-251 the unit tests pass where there is no network at all"""
        if shutil.which("unshare") is None or run(["unshare", "-rn", "true"]).returncode != 0:
            self.skipTest("this machine does not allow making a network namespace")
        program = unit_build() / "tests" / "unit" / "spz_unit_tests"
        process = run(["unshare", "-rn", program, "--test-case-exclude=*[canary-fails*"])
        self.assertEqual(process.returncode, 0, process.stdout[-800:])


class Pins(unittest.TestCase):
    SCRIPT = """
set(SPZ_CACHE_DIR "@CACHE@")
include("@ROOT@/cmake/fetch.cmake")
spz_pin(probe "file://@FILE@" "@SUM@")
spz_fetch(probe path)
message(STATUS "fetched ${path}")
"""

    def fetch(self, folder, checksum):
        payload = Path(folder) / "payload.bin"
        payload.write_bytes(b"pinned bytes")
        script = Path(folder) / "probe.cmake"
        script.write_text(self.SCRIPT.replace("@CACHE@", str(Path(folder) / "cache")).replace("@ROOT@", str(ROOT))
                          .replace("@FILE@", str(payload)).replace("@SUM@", checksum))
        return run(["cmake", "-P", script])

    def test_right_checksum(self):
        """TF-004 a pinned file with the right checksum is fetched into the cache"""
        with tempfile.TemporaryDirectory() as folder:
            process = self.fetch(folder, hashlib.sha256(b"pinned bytes").hexdigest())
            self.assertEqual(process.returncode, 0, process.stdout)
            self.assertTrue((Path(folder) / "cache" / "downloads" / "probe" / "payload.bin").exists())

    def test_wrong_checksum(self):
        """TF-004 SPEC-002 a pinned file with another checksum is refused and not kept"""
        with tempfile.TemporaryDirectory() as folder:
            process = self.fetch(folder, "0" * 64)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("Checksum mismatch for probe", process.stdout)
            self.assertFalse((Path(folder) / "cache" / "downloads" / "probe" / "payload.bin").exists())

    def test_python_reads_the_same_pins(self):
        """TF-004 the test tools read their pins from the same file as the build"""
        from testing import pins
        table = pins.load()
        text = (ROOT / "cmake" / "pins.cmake").read_text()
        for name in ("libsidplayfp", "libresidfp", "sidplayfp", "sysroot", "install_path_policy", "store_meta_policy"):
            self.assertIn(name, table)
            self.assertIn(table[name].sha256, text)
            self.assertEqual(len(table[name].sha256), 64)
