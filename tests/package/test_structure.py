# SPDX-License-Identifier: GPL-3.0-or-later
"""How the app is built, checked on the .deb for the device (docs/spec.md, sections 2, 3 and 11): the
documented package build (SPEC-001), the engine built in (SPEC-002), sound through miniaudio (SPEC-005),
licences (SPEC-011), names (SPEC-023), and, when the part `usbsid` is built, its rule file (SPEC-192)."""

import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import common, deb, pins  # noqa: E402

DEB = Path(os.environ.get("SPZ_DEB", ""))
PACKAGE_BUILD = ROOT / "build" / "cp0-cross"          # the folder of the preset cp0-cross (CMakePresets.json)
RULES = "usr/lib/udev/rules.d/60-sidplayzero.rules"
EM_AARCH64 = 183

# Licence notices that allow version 2 of the GPL and no later one, in the wordings the engine's archive
# has them, and the SPDX form. A file that also says "any later version" is not under version 2 only.
GPL2_ONLY = re.compile(
    r"(?i)General Public License,? version 2(?:\.0)? as published"
    r"|License: GPLv2(?![+\w])"
    r"|Free Software Foundation[,;]? version 2(?:\.0)?(?: of the License)?\s*[.;]"
    r"|GPL-2\.0(?:-only\b|(?![-+\w.]))")
GPL_LATER = re.compile(r"(?i)any later version|GPL-2\.0-or-later|GPL-2\.0\+|GPLv2\+|GPLv2 or later")


def one_line(text, lines=3):
    return " | ".join(line.strip() for line in text.strip().split("\n")[-lines:])


def plain(text):
    """A source file's text with its line breaks and the comment marks around them taken out, so that a
    notice reads on from line to line, also when it is drawn in a box of stars."""
    return re.sub(r"[ \t]*\*?[ \t]*\r?\n[ \t]*(?:\*+|//|#)?[ \t]*", " ", text)


def gpl2_only(text):
    text = plain(text)
    return bool(GPL2_ONLY.search(text)) and not GPL_LATER.search(text)


def parts_built(build):
    """The parts a build folder was configured with, from its cache (cmake/parts.cmake)."""
    cache = Path(build) / "CMakeCache.txt"
    if not cache.exists():
        return set()
    asked = re.search(r"(?m)^SIDPLAYZERO_PARTS:[A-Z]+=(.*)$", cache.read_text(encoding="utf-8"))
    return set(re.split(r"[;,]", asked.group(1))) if asked else set()


class PackageCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp(prefix="spz-deb-"))
        cls.tree = deb.unpack(DEB, cls.work / "root")
        cls.control = deb.control(DEB)
        cls.program = cls.tree / "usr" / "bin" / "sidplayzero"

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)

    def strings(self):
        return subprocess.run(["strings", "-n", "4", str(self.program)], text=True, stdout=subprocess.PIPE,
                              errors="replace").stdout

    def run_on_device_libraries(self, *arguments):
        """Runs the packaged program under emulation, against the libraries of the device's sysroot."""
        sysroot = common.CACHE / ("sysroot-" + pins.load()["sysroot"].sha256)
        self.assertTrue((sysroot / "usr" / "include").is_dir(), "the device's sysroot is not unpacked")
        return subprocess.run([deb.qemu(), "-L", str(sysroot), str(self.program), *arguments], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace", timeout=300)


class DocumentedBuild(PackageCase):
    def test_workflow_produced_the_package(self):
        """SPEC-001 with the maintainer set, cmake --workflow --preset cp0-cross-package has produced the .deb, for arm64, from the preset's own build folder"""
        self.assertTrue(DEB.is_file(), "the package build wrote no .deb")
        self.assertEqual(DEB.parent, ROOT / "dist")
        self.assertEqual(DEB.read_bytes()[:21], b"!<arch>\ndebian-binary")
        self.assertEqual(self.control["Maintainer"], "SidPlayZero test build <build@sidplayzero.test>")
        described = subprocess.run(["file", "-b", str(self.program)], text=True, stdout=subprocess.PIPE).stdout
        self.assertIn("ELF 64-bit", described)
        self.assertIn("aarch64", described)
        # Built by the aarch64-linux-gnu compiler against M5Stack's sysroot, the pinned one.
        commands = json.loads((PACKAGE_BUILD / "compile_commands.json").read_text(encoding="utf-8"))
        ours = next(c["command"] for c in commands if c["file"].endswith("src/app/main.cpp"))
        self.assertRegex(ours.split()[0], r"aarch64-linux-gnu-g\+\+")
        self.assertIn("sysroot-" + pins.load()["sysroot"].sha256, ours)
        self.assertRegex(ours, r"-std=(c|gnu)\+\+17\b")
        self.assertIn("-ffile-prefix-map=", ours)

    def test_no_path_of_the_build_machine(self):
        """SPEC-001 the packaged program holds no path of the machine that built it"""
        data = self.program.read_bytes()
        for folder in {str(ROOT), str(ROOT.resolve()), str(common.CACHE.resolve())}:
            self.assertFalse(folder.encode() in data, f"the program holds the path {folder}")


class Engine(PackageCase):
    def test_package_does_not_depend_on_debians_engine(self):
        """SPEC-002 the package does not depend on Debian's libsidplayfp, and the program asks for no such library"""
        for field in ("Depends", "Recommends", "Suggests", "Pre-Depends"):
            self.assertNotRegex(self.control.get(field, ""), r"(?i)sidplayfp|residfp", field)
        for library in deb.needed_libraries(self.program):
            self.assertNotRegex(library, r"(?i)sidplayfp|residfp")

    def test_version_names_the_engine(self):
        """SPEC-002 the packaged program, run under emulation, prints libsidplayfp 3.1.2 by --version, and holds nothing of the exSID backend"""
        said = self.run_on_device_libraries("--version")
        self.assertEqual(said.returncode, 0, one_line(said.stdout))
        self.assertEqual(said.stdout.split("\n")[0], f"SidPlayZero {deb.version()}")
        self.assertIn("libsidplayfp 3.1.2", said.stdout,
                      "sidplayzero --version does not print libsidplayfp 3.1.2, but: " + one_line(said.stdout))
        # The packaged program is stripped, so its text is searched: the backend names itself in its messages.
        found = sorted({line for line in self.strings().split("\n") if re.search(r"(?i)exsid", line)})
        self.assertFalse(found, "the program holds text of the exSID backend: " + "; ".join(found[:5]))


class Sound(PackageCase):
    def test_sound_libraries_are_loaded_when_the_program_runs(self):
        """SPEC-005 readelf -d lists neither libpulse nor libasound, and the program holds their names, to load them when it runs"""
        for library in deb.needed_libraries(self.program):
            self.assertNotRegex(library, r"libpulse|libasound", "the program is linked against a sound library")
        strings = self.strings()
        for name in ("libpulse.so", "libasound.so"):
            self.assertTrue(name in strings, f"the program does not hold the name {name}: miniaudio is not built in")

    def test_package_depends_on_pulse(self):
        """SPEC-005 the package depends on libpulse0, which the program loads, and not on ALSA's library"""
        depends = [name.strip().split(" ")[0] for name in self.control["Depends"].split(",")]
        self.assertIn("libpulse0", depends)
        self.assertNotIn("libasound2", depends)
        self.assertNotIn("libasound2t64", depends)


class Licences(PackageCase):
    def test_notices_name_every_component(self):
        """SPEC-011 the installed notices file names each component that is built in, each with its licence's text, beside the installed LICENSE"""
        doc = self.tree / "usr" / "share" / "doc" / "sidplayzero"
        licence = (doc / "LICENSE").read_text(encoding="utf-8")
        self.assertIn("GNU GENERAL PUBLIC LICENSE", licence)
        self.assertIn("Version 3, 29 June 2007", licence)
        notices = (doc / "THIRD-PARTY-NOTICES.md").read_text(encoding="utf-8")
        wanted = {
            "LVGL 9.5.0": "LVGL (MIT)",
            "Permission is hereby granted, free of charge": "the MIT licence's text",
            "libsidplayfp 3.1.2": "libsidplayfp",
            "libresidfp 1.3.0": "libresidfp",
            "GPL-2.0-or-later": "the engine's licence, GPL-2.0-or-later, used here under GPL-3",
            "miniaudio 0.11.25": "miniaudio",
            "MIT No Attribution": "the MIT-0 licence's text",
            "DejaVu Sans": "the fonts made from DejaVu Sans",
            "Bitstream Vera": "the DejaVu fonts' licence text",
        }
        missing = [what for words, what in wanted.items() if words not in notices]
        self.assertFalse(missing, "the notices file lacks: " + "; ".join(missing))

    def test_no_gpl2_only_source_is_compiled(self):
        """SPEC-011 no file under a "GPL version 2 only" notice is among the sources compiled into the package's program"""
        # 1. What the build compiles by its own rules: the app's sources and LVGL's.
        commands = json.loads((PACKAGE_BUILD / "compile_commands.json").read_text(encoding="utf-8"))
        self.assertGreater(len(commands), 50)
        for entry in commands:
            source = Path(entry["file"])
            self.assertFalse(gpl2_only(source.read_text(encoding="utf-8", errors="replace")),
                             f"{source} is compiled and is under GPL version 2 only")
        # 2. The engine, built from its two archives. Some files in them are under version 2 only: the
        #    exSID backend and the USBSID driver. None of those may have been compiled for the device.
        only = set()
        for name in ("libsidplayfp", "libresidfp"):
            with tarfile.open(pins.fetch(name)) as archive:
                for member in archive.getmembers():
                    if member.isfile() and member.name.endswith((".c", ".cc", ".cpp", ".cxx")):
                        if gpl2_only(archive.extractfile(member).read().decode("utf-8", "replace")):
                            only.add(Path(member.name).stem)
        objects = self.objects_for_the_device()
        self.assertTrue(any("SidTune" in name for name in objects),
                        "the package's build compiled no source of the engine: the engine is not built in")
        for name in sorted(objects):
            stem = name.split(".")[0]
            for source in only:
                self.assertFalse(stem == source or stem.endswith("-" + source),
                                 f"{objects[name]} was compiled from {source}, which is under GPL version 2 only")

    @staticmethod
    def objects_for_the_device():
        """{file name: path} of every object file compiled for arm64, in the package's build folder and in
        the cache beside it, wherever the build keeps the engine's."""
        found = {}
        for top in (PACKAGE_BUILD, common.CACHE):
            for folder, folders, files in os.walk(top, followlinks=True):
                folders[:] = [f for f in folders if not f.startswith(("sysroot-", "lvgl-", "downloads", "debian"))]
                for name in files:
                    if name.endswith((".o", ".lo", ".obj")):
                        path = Path(folder) / name
                        with open(path, "rb") as file:
                            head = file.read(20)
                        if head[:4] == b"\x7fELF" and int.from_bytes(head[18:20], "little") == EM_AARCH64:
                            found[name] = path
        return found


class Names(PackageCase):
    def test_one_name_everywhere(self):
        """SPEC-023 the package and the program are sidplayzero and the launcher shows SidPlayZero, in the control file, the manifest, the launcher's entry and the program's own words"""
        manifest = json.loads((ROOT / "app-builder.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["package_name"], "sidplayzero")
        self.assertEqual(manifest["app_name"], "SidPlayZero")
        self.assertEqual(self.control["Package"], manifest["package_name"])
        self.assertTrue(os.access(self.program, os.X_OK), "/usr/bin/sidplayzero is not a program")
        desktop = (self.tree / "usr/share/APPLaunch/applications/sidplayzero.desktop").read_text(encoding="utf-8")
        self.assertIn(f"\nName={manifest['app_name']}\n", desktop)
        self.assertIn("\nExec=/usr/bin/sidplayzero\n", desktop)
        self.assertTrue(f"SidPlayZero {manifest['version']}" in self.strings(), "the program does not hold its own name and version")


class UsbSid(PackageCase):
    """The part `usbsid` is not built unless asked for (SPEC-115); without it these tests are skipped."""

    def setUp(self):
        if "usbsid" not in parts_built(PACKAGE_BUILD):
            self.skipTest("needs the part usbsid")

    def test_rule_file(self):
        """SPEC-192 the package installs one udev rule, which the store's install-path check accepts, that tags USB device cafe:4011 uaccess and runs no program [parts: usbsid]"""
        rules = self.tree / RULES
        self.assertTrue(rules.is_file(), f"the package has no /{RULES}")
        spec = importlib.util.spec_from_file_location("install_path_policy", pins.fetch("install_path_policy"))
        policy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(policy)
        rejected = [f"{path}: {why}" for path, why in policy.rejected_paths(str(DEB), "sidplayzero")]
        self.assertFalse(rejected, "the store's install-path check refuses: " + "; ".join(rejected))

        text = rules.read_text(encoding="utf-8")
        for word in ("RUN", "PROGRAM", "IMPORT"):
            self.assertNotIn(word, text, f"the rule file holds {word}")
        lines = [line.strip() for line in text.split("\n") if line.strip() and not line.strip().startswith("#")]
        self.assertEqual(len(lines), 1, "the file holds one rule")
        rule = lines[0]
        self.assertRegex(rule, r'ATTRS?\{idVendor\}=="cafe"')
        self.assertRegex(rule, r'ATTRS?\{idProduct\}=="4011"')
        self.assertRegex(rule, r'TAG\+="uaccess"')

    def test_depends_on_libusb(self):
        """SPEC-191 SPEC-221 with the part usbsid built, the package depends on libusb-1.0-0 [parts: usbsid]"""
        depends = [name.strip().split(" ")[0] for name in self.control["Depends"].split(",")]
        self.assertIn("libusb-1.0-0", depends)


if __name__ == "__main__":
    unittest.main()
