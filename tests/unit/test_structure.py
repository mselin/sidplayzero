# SPDX-License-Identifier: GPL-3.0-or-later
"""How the app is built, as far as the unit level can see it (docs/spec.md, section 3): the three layers
of SPEC-020 and the seams of SPEC-021. What needs the whole program built is tests/simulator/test_structure.py."""

import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("SPZ_UNIT_BUILD", ROOT / "build" / "unit"))
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')

# The headers of the C++ standard library (C++17), which is all that `core` may use (SPEC-020).
STANDARD = set("""
    algorithm any array atomic bitset cassert cctype cerrno cfenv cfloat charconv chrono cinttypes climits
    clocale cmath codecvt complex condition_variable csetjmp csignal cstdarg cstddef cstdint cstdio cstdlib
    cstring ctime cuchar cwchar cwctype deque exception execution filesystem forward_list fstream functional
    future initializer_list iomanip ios iosfwd iostream istream iterator limits list locale map memory
    memory_resource mutex new numeric optional ostream queue random ratio regex scoped_allocator set
    shared_mutex sstream stack stdexcept streambuf string string_view system_error thread tuple type_traits
    typeindex typeinfo unordered_map unordered_set utility valarray variant vector
""".split())
# What a test may include beside the standard library and the system's own headers.
OUTSIDE = re.compile(r"^(lvgl|lv_|sidplayfp|residfp|miniaudio|curl/|SDL|ui/|platform/|app/)")


def includes(path):
    """[(line number, '<' or '"', name)] of a source file's #include lines."""
    found = []
    for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
        match = INCLUDE.match(line)
        if match:
            found.append((number, match.group(1), match.group(2)))
    return found


def run(command, cwd=None, timeout=600):
    return subprocess.run([str(c) for c in command], cwd=cwd or ROOT, text=True, timeout=timeout,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace")


def include_paths(command):
    """The folders a compile command searches for headers, besides the compiler's own."""
    words = shlex.split(command)
    paths = []
    for index, word in enumerate(words):
        if word in ("-I", "-isystem", "-iquote") and index + 1 < len(words):
            paths.append(words[index + 1])
        elif word.startswith("-I") and len(word) > 2:
            paths.append(word[2:])
        elif word.startswith("-isystem") and len(word) > 8:
            paths.append(word[8:])
    return [str(Path(p).resolve()) for p in paths]


class Layers(unittest.TestCase):
    def test_unit_tests_link_core_and_nothing_else(self):
        """SPEC-020 the host-unit tests link `core` and the fakes and nothing else"""
        listed = run(["ninja", "-C", BUILD, "-t", "commands", "spz_unit_tests"]).stdout
        link = [line for line in listed.strip().split("\n") if "spz_unit_tests" in line and " -o " in line][-1]
        libraries = [word for word in shlex.split(link) if word.endswith((".a", ".so")) or word.startswith("-l")]
        self.assertEqual([Path(word).name for word in libraries], ["libspz_core.a"], link)
        # The fakes are one header, compiled into the tests: nothing of them is a library.
        self.assertTrue((ROOT / "tests" / "fakes" / "fakes.h").is_file())

    def test_core_has_one_include_path(self):
        """SPEC-020 `core` is built with an include path to its own folder and to nothing else"""
        commands = json.loads((BUILD / "compile_commands.json").read_text())
        core = [c for c in commands if Path(c["file"]).parent == ROOT / "src" / "core"]
        self.assertGreater(len(core), 5, "the build has no files of core")
        for entry in core:
            self.assertEqual(include_paths(entry["command"]), [str(ROOT / "src" / "core")], entry["file"])

    def test_core_includes_only_the_standard_library(self):
        """SPEC-020 a file of `core` includes `core`'s own headers and the C++ standard library, and nothing else"""
        checked = 0
        for path in sorted((ROOT / "src" / "core").rglob("*")):
            if path.suffix not in (".h", ".cpp"):
                continue
            for number, kind, name in includes(path):
                checked += 1
                where = f"{path.relative_to(ROOT)}:{number}: #include {name}"
                if kind == '"':
                    self.assertTrue((ROOT / "src" / "core" / name).is_file(), where + ": not a header of core")
                else:
                    self.assertIn(name, STANDARD, where + ": not a header of the C++ standard library")
        self.assertGreater(checked, 20)

    def test_lvgl_include_breaks_core_in_a_scratch_copy(self):
        """SPEC-020 in a scratch copy, adding an #include of an LVGL header to a `core` file makes the build fail"""
        with tempfile.TemporaryDirectory(prefix="spz-layers-") as folder:
            copy = Path(folder) / "copy"
            copy.mkdir()
            for name in ("CMakeLists.txt", "app-builder.json"):
                shutil.copy(ROOT / name, copy / name)
            shutil.copytree(ROOT / "cmake", copy / "cmake")
            shutil.copytree(ROOT / "src" / "core", copy / "src" / "core")
            options = ["-G", "Ninja", "-DSIDPLAYZERO_BUILD_APP=OFF"]
            configured = run(["cmake", "-S", copy, "-B", copy / "build", *options])
            self.assertEqual(configured.returncode, 0, configured.stdout[-800:])
            clean = run(["cmake", "--build", copy / "build", "--target", "spz_core"])
            self.assertEqual(clean.returncode, 0, "the scratch copy does not build as it is: " + clean.stdout[-800:])

            victim = copy / "src" / "core" / "version.cpp"
            victim.write_text('#include "lvgl.h"\n' + victim.read_text(encoding="utf-8"), encoding="utf-8")
            broken = run(["cmake", "--build", copy / "build", "--target", "spz_core"])
            self.assertNotEqual(broken.returncode, 0, "core was built with an LVGL header in it")
            self.assertIn("lvgl.h", broken.stdout)


class Seams(unittest.TestCase):
    def test_unit_tests_see_fakes_only(self):
        """SPEC-021 every host-unit test is built from doctest, the fakes, the tests' own helpers and `core`: no header of `ui`, `platform`, LVGL, the engine, miniaudio or libcurl"""
        sources = sorted((ROOT / "tests" / "unit").glob("*.cpp")) + sorted((ROOT / "tests" / "unit").glob("*.h")) \
            + [ROOT / "tests" / "fakes" / "fakes.h"]
        self.assertGreater(len(sources), 3)
        for path in sources:
            for number, kind, name in includes(path):
                where = f"{path.relative_to(ROOT)}:{number}: #include {name}"
                self.assertIsNone(OUTSIDE.match(name), where + ": a unit test reaches outside core and the fakes")
                if kind == '"' and not name.startswith("spz/"):
                    homes = [ROOT / "tests" / "unit" / name, ROOT / "tests" / "fakes" / name,
                             ROOT / "third_party" / "doctest" / name]
                    self.assertTrue(any(home.is_file() for home in homes), where + ": not doctest, the fakes or a helper")
                if kind == '"' and name.startswith("spz/"):
                    self.assertTrue((ROOT / "src" / "core" / name).is_file(), where + ": not a header of core")

    def test_unit_tests_include_paths(self):
        """SPEC-021 the unit tests are compiled with include paths to `core`, the fakes, doctest and their own folder only"""
        commands = json.loads((BUILD / "compile_commands.json").read_text())
        tests = [c for c in commands if "/tests/" in c["file"]]
        self.assertGreater(len(tests), 3, "the build has no unit tests")
        allowed = {str(ROOT / "src" / "core"), str(ROOT / "tests" / "fakes"), str(ROOT / "tests" / "unit"),
                   str(ROOT / "third_party" / "doctest")}
        for entry in tests:
            self.assertEqual(set(include_paths(entry["command"])) - allowed, set(), entry["file"])

    def test_core_does_not_reach_around_the_files_seam(self):
        """SPEC-021 no file of `core` opens files by itself: none includes <fstream> or <filesystem>, so Files is its only way to the card"""
        for path in sorted((ROOT / "src" / "core").rglob("*")):
            if path.suffix in (".h", ".cpp"):
                for number, _, name in includes(path):
                    self.assertNotIn(name, ("fstream", "filesystem"), f"{path.relative_to(ROOT)}:{number}")


if __name__ == "__main__":
    unittest.main()
