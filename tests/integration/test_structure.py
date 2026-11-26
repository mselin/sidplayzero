# SPDX-License-Identifier: GPL-3.0-or-later
"""How the app is built, checked with real parts on this machine (docs/spec.md, sections 2 and 3): the
documented build from a clean checkout (SPEC-001), the engine built in from its checked archives
(SPEC-002) and used as a library (SPEC-003), and the thread rules that need the real program (SPEC-022)."""

import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import common, pins, reference, simulator  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures"
BUILD = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator"))
PROGRAM = BUILD / "src" / "sidplayzero"
TOOL = BUILD / "src" / "spz_tool"
SCRIPTS = ROOT / "tests" / "simulator"

# SPEC-002's table, written here from the specification.
ENGINE_ARCHIVES = {
    "libsidplayfp": ("libsidplayfp-3.1.2.tar.gz", "6d16c20e523ac4d3e385531dafe247a830b59bcb4eb73a557f15cf4e7ae6da84"),
    "libresidfp": ("libresidfp-1.3.0.tar.gz", "6de8880dcfc11b993107f71b538262f08ae07fd0e595297ce776f641b5435b38"),
}
# The build of the ThreadSanitizer run, as tools/testing/integration.py makes it, so that one build serves both.
TSAN_OPTIONS = {"SIDPLAYZERO_TEST_HOOKS": "ON", "SIDPLAYZERO_DISPLAY": "window", "SIDPLAYZERO_BUILD_SIM_TESTS": "ON",
                "SIDPLAYZERO_SANITIZE": "thread", "CMAKE_BUILD_TYPE": "Debug", "SIDPLAYZERO_BUILD_RACE_CANARY": "ON"}
THREAD_SCRIPTS = ["structure_threads_download.script", "structure_threads_index.script",
                  "structure_threads_slow_files.script", "structure_audio_callback.script"]


def run(command, cwd=None, env=None, timeout=3600):
    merged = dict(os.environ)
    merged.update(env or {})
    return subprocess.run([str(c) for c in command], cwd=cwd or ROOT, env=merged, text=True, timeout=timeout,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace")


def one_line(text, lines=3):
    """The end of a program's output as one line, for a failure's message."""
    return " | ".join(line.strip() for line in text.strip().split("\n")[-lines:])


def why(output):
    """One line that says where a script stopped."""
    for line in output.split("\n"):
        if line.startswith("line: "):
            return "the script stopped at: " + line[len("line: "):].strip()
    return "the script's output ended: " + one_line(output)


def clean_checkout(into):
    """A copy of every file of the repository that is not ignored, as a clean checkout has them, with the
    downloads this machine already has put where the build keeps them, so that the copy fetches nothing
    that is here. LVGL's sources are shared, since the build only reads them; the pinned archives are
    copied, because a build removes one whose checksum is not its pin's."""
    copy = Path(into) / "sidplayzero"
    listed = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT,
                            text=True, stdout=subprocess.PIPE, check=True).stdout.split("\0")
    for name in listed:
        source = ROOT / name
        if name and source.is_file():
            target = copy / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    cache = copy / ".cache"
    (cache / "downloads").mkdir(parents=True)
    for folder in sorted((ROOT / ".cache").glob("lvgl-*")):
        os.symlink(folder.resolve(), cache / folder.name)
    for name in ("libsidplayfp", "libresidfp", "miniaudio"):
        have = ROOT / ".cache" / "downloads" / name
        if have.is_dir():
            shutil.copytree(have.resolve(), cache / "downloads" / name)
    return copy


class DocumentedBuild(unittest.TestCase):
    """SPEC-001 and SPEC-002, on a clean checkout: one copy and one build serve every test here."""

    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp(prefix="spz-checkout-"))
        cls.copy = clean_checkout(cls.work)
        cls.built = run(["cmake", "--workflow", "--preset", "linux-x86-64"], cwd=cls.copy)
        cls.program = cls.copy / "build" / "linux-x86-64" / "src" / "sidplayzero"

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)

    def test_workflow_builds_the_program(self):
        """SPEC-001 on a clean checkout, cmake --workflow --preset linux-x86-64 ends with status 0 and produces the program"""
        self.assertEqual(self.built.returncode, 0, one_line(self.built.stdout, 6))
        self.assertTrue(self.program.is_file(), "the workflow did not produce build/linux-x86-64/src/sidplayzero")
        self.assertTrue(self.program.stat().st_mode & stat.S_IXUSR)
        said = run([self.program, "--version"], timeout=60)
        self.assertEqual(said.returncode, 0, one_line(said.stdout))
        self.assertRegex(said.stdout.split("\n")[0], r"^SidPlayZero \d+\.\d+\.\d+$")

    def test_no_path_of_the_build_machine(self):
        """SPEC-001 the program built by the workflow holds no path of the folder it was built in"""
        self.assertEqual(self.built.returncode, 0, one_line(self.built.stdout, 6))
        data = self.program.read_bytes()
        for folder in (str(self.copy), str(self.copy.resolve()), str(self.work)):
            self.assertFalse(folder.encode() in data, f"the program holds the path {folder}")

    def test_pins_are_the_specifications(self):
        """SPEC-002 the build's pins for the two engine archives are the archives and the SHA-256 sums of the specification's table"""
        table = pins.load()
        for name, (archive, checksum) in ENGINE_ARCHIVES.items():
            self.assertIn(name, table)
            self.assertTrue(table[name].url.endswith("/" + archive), table[name].url)
            self.assertEqual(table[name].sha256, checksum, archive)

    def build_with_wrong_checksum(self, name):
        """Builds the copy afresh with one archive's pin changed: the archive is the right one, offered
        from a local file, and its pin has another checksum. Returns the finished build and its output."""
        archive, checksum = ENGINE_ARCHIVES[name]
        local = self.work / "archives" / archive
        local.parent.mkdir(exist_ok=True)
        shutil.copy(pins.fetch(name), local)
        wrong = checksum[:-4] + ("0000" if not checksum.endswith("0000") else "1111")
        pins_file = self.copy / "cmake" / "pins.cmake"
        original = pins_file.read_text(encoding="utf-8")
        changed = re.sub(r'spz_pin\(\s*' + name + r'\s+"[^"]+"\s+"[0-9a-f]{64}"\s*\)',
                         f'spz_pin({name}\n    "file://{local}"\n    "{wrong}")', original)
        self.assertNotEqual(changed, original, f"cmake/pins.cmake has no pin named {name}")
        cached = self.copy / ".cache" / "downloads" / name / archive
        try:
            pins_file.write_text(changed, encoding="utf-8")
            shutil.rmtree(self.copy / "build", ignore_errors=True)
            done = run(["cmake", "--preset", "linux-x86-64"], cwd=self.copy)
            output = done.stdout
            if done.returncode == 0:                       # the archive may be checked when it is built, not before
                done = run(["cmake", "--build", "--preset", "linux-x86-64"], cwd=self.copy)
                output += done.stdout
            return done, output
        finally:
            pins_file.write_text(original, encoding="utf-8")
            cached.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(pins.fetch(name), cached)          # the build removed the copy it refused

    def test_wrong_checksum_of_libsidplayfp(self):
        """SPEC-002 the build fails if the checksum of libsidplayfp-3.1.2.tar.gz differs from its pin"""
        done, output = self.build_with_wrong_checksum("libsidplayfp")
        self.assertNotEqual(done.returncode, 0, "the build went through with a wrong checksum for libsidplayfp-3.1.2.tar.gz")
        self.assertIn("Checksum mismatch for libsidplayfp", output, one_line(output))

    def test_wrong_checksum_of_libresidfp(self):
        """SPEC-002 the build fails if the checksum of libresidfp-1.3.0.tar.gz differs from its pin"""
        done, output = self.build_with_wrong_checksum("libresidfp")
        self.assertNotEqual(done.returncode, 0, "the build went through with a wrong checksum for libresidfp-1.3.0.tar.gz")
        self.assertIn("Checksum mismatch for libresidfp", output, one_line(output))


class EngineBuiltIn(unittest.TestCase):
    """SPEC-002 and SPEC-003, on the desktop program the simulator tests run."""

    @staticmethod
    def symbols():
        return run(["nm", "-C", PROGRAM], timeout=300).stdout

    def test_version_and_no_shared_engine(self):
        """SPEC-002 the program needs no libsidplayfp library when it starts, and --version prints libsidplayfp 3.1.2"""
        needed = [line for line in run(["readelf", "-d", PROGRAM]).stdout.split("\n") if "(NEEDED)" in line]
        self.assertGreater(len(needed), 0, "readelf -d lists no library at all")
        for line in needed:
            self.assertNotRegex(line, r"(?i)sidplayfp|residfp", "the program asks for the engine as a shared library")
        said = run([PROGRAM, "--version"], timeout=60)
        self.assertEqual(said.returncode, 0, one_line(said.stdout))
        self.assertIn("libsidplayfp 3.1.2", said.stdout,
                      "sidplayzero --version does not print libsidplayfp 3.1.2, but: " + one_line(said.stdout))

    def test_no_exsid(self):
        """SPEC-002 the program holds the engine, and no symbol of the exSID backend"""
        symbols = self.symbols()
        self.assertTrue(re.search(r"\bsidplayfp::", symbols), "the program holds no symbol of libsidplayfp: the engine is not built in")
        found = sorted({line.split()[-1] for line in symbols.split("\n") if re.search(r"(?i)exsid", line)})
        self.assertFalse(found, "symbols of the exSID backend: " + ", ".join(found[:5]))

    def test_library_calls(self):
        """SPEC-003 the program calls the library itself: SidTune from memory, sidplayfp's config, load, play, mix, mute and filter, and neither its SidDatabase nor the sidplayfp program"""
        symbols = self.symbols()
        for wanted in ("SidTune::SidTune(", "sidplayfp::config(", "sidplayfp::load(", "sidplayfp::play(",
                       "sidplayfp::mix(", "sidplayfp::mute(", "sidplayfp::filter("):
            self.assertTrue(wanted in symbols, f"the program has no symbol {wanted}...): the engine is not built in")
        self.assertFalse("SidDatabase::" in symbols, "the program uses the library's song length reader (SPEC-008)")

    def test_render_equals_the_reference(self):
        """SPEC-003 ten seconds of a fixture rendered by the app equal the sidplayfp program's WAV made with --delay=0, and the app starts no sidplayfp program to get them"""
        with tempfile.TemporaryDirectory(prefix="spz-render-") as folder:
            # A stand-in named sidplayfp, first on the path: if the app drove the program, this would run.
            marker = Path(folder) / "sidplayfp-was-run"
            standin = Path(folder) / "bin" / "sidplayfp"
            standin.parent.mkdir()
            standin.write_text(f"#!/bin/sh\necho \"$@\" >> '{marker}'\nexit 1\n")
            standin.chmod(0o755)
            out = Path(folder) / "out"
            out.mkdir()
            old_path = os.environ.get("PATH", "")
            os.environ["PATH"] = f"{standin.parent}{os.pathsep}{old_path}"
            try:
                status, output, _ = simulator.run_script(PROGRAM, SCRIPTS / "structure_engine_render.script", out)
            finally:
                os.environ["PATH"] = old_path
            self.assertEqual(status, 0, why(output))
            self.assertFalse(marker.exists(), "the app ran a program named sidplayfp")
            rendered = (out / "structure_engine_render.raw").read_bytes()
        self.assertEqual(len(rendered), 10 * 48000 * 4)
        wanted = reference.render(FIXTURES / "tunes" / "commando_test.sid", seconds=10, accurate=True)
        self.assertGreaterEqual(len(wanted), len(rendered), "the reference is shorter than ten seconds")
        self.assertIsNone(reference.first_difference(rendered, wanted[:len(rendered)]),
                          "the app's samples differ from the reference program's")
        self.assertGreater(len(set(rendered[::4])), 16, "the app rendered silence")


class Threads(unittest.TestCase):
    """SPEC-022, the two clauses of its oracle that belong to this level."""

    def test_no_thread_sanitizer_report(self):
        """SPEC-022 the scripts of the thread rules, run in the ThreadSanitizer build, pass and give no report"""
        build = common.configure_and_build(common.BUILD / "simulator-tsan", TSAN_OPTIONS)
        program = build / "src" / "sidplayzero"
        with tempfile.TemporaryDirectory(prefix="spz-tsan-") as folder:
            for name in THREAD_SCRIPTS:
                out = Path(folder) / Path(name).stem
                out.mkdir()
                status, output, _ = simulator.run_script(program, SCRIPTS / name, out, timeout=600)
                self.assertNotIn("ThreadSanitizer", output, f"{name}: " + one_line(output))
                self.assertEqual(status, 0, f"{name}: " + why(output))

    def test_audio_callback_guard(self):
        """SPEC-022 a test build makes the audio callback abort if it allocates or takes a lock"""
        # spz_tool audio-guard HOW runs the software backend's audio callback, with the guard of the test
        # build around it, for one second of frames from a fake engine. HOW is:
        #   clean      the callback as it is: prints "callbacks = N", N above 0, and ends with status 0
        #   allocate   the callback made to allocate memory once: the guard aborts the process (SIGABRT)
        #   lock       the callback made to take a mutex once: the guard aborts the process (SIGABRT)
        # When it aborts, the tool's output names the audio callback and what it did.
        clean = run([TOOL, "audio-guard", "clean"], timeout=120)
        self.assertEqual(clean.returncode, 0, one_line(clean.stdout))
        counted = re.search(r"(?m)^callbacks = (\d+)$", clean.stdout)
        self.assertIsNotNone(counted, one_line(clean.stdout))
        self.assertGreater(int(counted.group(1)), 0)
        for how in ("allocate", "lock"):
            done = run([TOOL, "audio-guard", how], timeout=120)
            self.assertEqual(done.returncode, -signal.SIGABRT, f"audio-guard {how} did not abort: " + one_line(done.stdout))
            self.assertIn("audio callback", done.stdout, one_line(done.stdout))

    def test_audio_callback_runs_without_the_abort(self):
        """SPEC-022 the program plays a fixture tune with the real engine through that guarded audio callback, and it does not abort"""
        with tempfile.TemporaryDirectory(prefix="spz-audio-") as folder:
            status, output, _ = simulator.run_script(PROGRAM, SCRIPTS / "structure_audio_callback.script", folder)
        self.assertNotEqual(status, -signal.SIGABRT, "the audio callback aborted: " + one_line(output))
        self.assertNotIn("audio callback", output, one_line(output))
        self.assertEqual(status, 0, why(output))


if __name__ == "__main__":
    unittest.main()
