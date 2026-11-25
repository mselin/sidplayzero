# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks on the .deb (docs/spec.md, section 13), each a test of the item it names."""

import hashlib
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_repo  # noqa: E402
from testing import deb, pins  # noqa: E402

DEB = Path(os.environ.get("SPZ_DEB", ""))
EXPECTED_FILES = [
    "/usr/bin/sidplayzero",
    "/usr/share/APPLaunch/applications/sidplayzero.desktop",
    "/usr/share/APPLaunch/share/images/sidplayzero.png",
    "/usr/share/APPLaunch/share/images/sidplayzero_100.png",
    "/usr/share/APPLaunch/share/images/sidplayzero_80.png",
    "/usr/share/doc/sidplayzero/LICENSE",
    "/usr/share/doc/sidplayzero/README.md",
    "/usr/share/doc/sidplayzero/THIRD-PARTY-NOTICES.md",
]
DEPENDS = ["libc6", "libstdc++6", "libgcc-s1", "libcurl4t64", "ca-certificates", "libpulse0", "pulseaudio-utils"]
# Which Debian package each shared library the program may ask for belongs to.
LIBRARY_PACKAGES = {
    "libc.so.6": "libc6", "libm.so.6": "libc6", "ld-linux-aarch64.so.1": "libc6", "libpthread.so.0": "libc6",
    "libdl.so.2": "libc6", "libstdc++.so.6": "libstdc++6", "libgcc_s.so.1": "libgcc-s1",
    "libcurl.so.4": "libcurl4t64", "libpulse.so.0": "libpulse0", "libusb-1.0.so.0": "libusb-1.0-0",
}


def png_size(data):
    return struct.unpack(">II", data[16:24])


def load_script(name):
    """One of the store's own scripts, at its pinned commit, as a module."""
    spec = importlib.util.spec_from_file_location(name, pins.fetch(name))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PackageCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp(prefix="spz-deb-"))
        cls.tree = deb.unpack(DEB, cls.work / "root")
        cls.control = deb.control(DEB)
        cls.entries = deb.files(DEB)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)


class Contents(PackageCase):
    def test_files(self):
        """SPEC-220 the package installs exactly the files the specification lists, and their folders"""
        files = sorted(path for mode, _, path in self.entries if not mode.startswith("d"))
        self.assertEqual(files, sorted(EXPECTED_FILES))
        folders = {path for mode, _, path in self.entries if mode.startswith("d")}
        for path in EXPECTED_FILES:
            self.assertIn(str(Path(path).parent), folders)

    def test_no_scripts(self):
        """SPEC-220 SPEC-206 SPEC-134 the package has no maintainer scripts and no configuration files"""
        self.assertEqual(deb.control_files(DEB), ["control", "md5sums"])

    def test_file_name(self):
        """SPEC-220 the file's name is made of Package, Version and Architecture, and the version ends in -1"""
        self.assertEqual(DEB.name, f"{self.control['Package']}_{self.control['Version']}_{self.control['Architecture']}.deb")
        self.assertEqual(self.control["Version"], deb.version() + "-1")

    def test_owned_by_root_without_special_bits(self):
        """SPEC-206 SPEC-220 nothing in the package is setuid or setgid, and everything belongs to root"""
        for mode, owner, path in self.entries:
            self.assertEqual(owner, "root/root", path)
            self.assertNotIn("s", mode.lower(), path)

    def test_names(self):
        """SPEC-023 the package and the program are sidplayzero, and the launcher shows SidPlayZero"""
        self.assertEqual(self.control["Package"], "sidplayzero")
        self.assertTrue((self.tree / "usr/bin/sidplayzero").is_file())
        desktop = (self.tree / "usr/share/APPLaunch/applications/sidplayzero.desktop").read_text()
        self.assertIn("\nName=SidPlayZero\n", desktop)

    def test_size(self):
        """SPEC-227 the package is at most 20 MB"""
        self.assertLessEqual(DEB.stat().st_size, 20 * 1000 * 1000, f"{DEB.stat().st_size} bytes")


class Launcher(PackageCase):
    def test_desktop_entry(self):
        """SPEC-222 the launcher's entry has exactly the lines the specification gives"""
        lines = (self.tree / "usr/share/APPLaunch/applications/sidplayzero.desktop").read_text().strip().split("\n")
        self.assertEqual(lines, ["[Desktop Entry]", "Type=Application", "Name=SidPlayZero", "Exec=/usr/bin/sidplayzero",
                                 "Icon=share/images/sidplayzero.png", "Terminal=false"])

    def test_icons(self):
        """SPEC-222 the three icons are 100, 100 and 80 pixels square"""
        images = self.tree / "usr/share/APPLaunch/share/images"
        for name, size in (("sidplayzero.png", 100), ("sidplayzero_100.png", 100), ("sidplayzero_80.png", 80)):
            self.assertEqual(png_size((images / name).read_bytes()), (size, size), name)


class Dependencies(PackageCase):
    def test_fields(self):
        """SPEC-221 Depends, Recommends and Architecture are those of the specification"""
        self.assertEqual([d.strip() for d in self.control["Depends"].split(",")], DEPENDS)
        self.assertEqual(self.control["Recommends"], "7zip")
        self.assertEqual(self.control["Architecture"], "arm64")
        self.assertEqual(self.control["Maintainer"], deb.TEST_MAINTAINER)

    def test_in_debian(self):
        """SPEC-221 SPEC-006 every package named is in Debian trixie's main archive for arm64"""
        known = deb.trixie_packages()
        for name in DEPENDS + ["7zip"]:
            self.assertIn(name, known)

    def test_needed_libraries(self):
        """SPEC-221 SPEC-002 every library the program needs belongs to a package in Depends, and the engine is not one"""
        for library in deb.needed_libraries(self.tree / "usr/bin/sidplayzero"):
            self.assertNotIn("sidplayfp", library)
            self.assertIn(library, LIBRARY_PACKAGES, f"{library}: which package is this?")
            self.assertIn(LIBRARY_PACKAGES[library], DEPENDS)

    def test_maintainer_is_required(self):
        """SPEC-221 a package build with no maintainer fails and says why"""
        with tempfile.TemporaryDirectory() as folder:
            env = {k: v for k, v in os.environ.items() if k != "SIDPLAYZERO_MAINTAINER"}
            done = subprocess.run(["cmake", "-S", str(ROOT), "-B", folder, "-G", "Ninja", "--preset", "cp0-cross"],
                                  env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT)
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("SIDPLAYZERO_MAINTAINER", done.stdout)

    def test_installs_in_clean_debian(self):
        """SPEC-221 SPEC-001 the package installs with apt in a clean Debian trixie arm64 container, runs, and is removed again"""
        done = deb.in_container(DEB, f"""
            apt-get update -q >/dev/null
            apt-get install -y -q --no-install-recommends /dist/{DEB.name} >/dev/null
            sidplayzero --version
            apt-get remove -y -q sidplayzero >/dev/null
            test ! -e /usr/bin/sidplayzero && test ! -e /usr/share/APPLaunch/applications/sidplayzero.desktop
            echo removed-cleanly
        """)
        self.assertEqual(done.returncode, 0, done.stdout[-1500:])
        self.assertIn(f"SidPlayZero {deb.version()}", done.stdout)
        self.assertIn("removed-cleanly", done.stdout)


class Store(PackageCase):
    def bad_paths(self, package):
        policy = load_script("install_path_policy")
        return [f"{path}: {why}" for path, why in policy.rejected_paths(str(package), "sidplayzero")]

    def test_install_paths(self):
        """SPEC-224 the store's install-path check, at its pinned commit, reports nothing on the package"""
        self.assertEqual(self.bad_paths(DEB), [])

    def test_install_paths_notice_a_stray_file(self):
        """SPEC-224 the same check reports a copy of the package with a file /usr/bin/other in it"""
        def add(tree):
            (tree / "usr/bin/other").write_text("#!/bin/sh\n")
        copy = deb.repack(DEB, self.work / "stray", add)
        self.assertTrue(any("usr/bin/other" in line for line in self.bad_paths(copy)))

    def test_package_checks(self):
        """SPEC-224 the store's other checks of a package hold: the fields, the name, the launcher's entry, no service"""
        for field in ("Package", "Version", "Architecture", "Maintainer"):
            self.assertTrue(self.control.get(field), field)
        self.assertRegex(self.control["Package"], r"^[a-z0-9][a-z0-9+.-]+$")
        paths = [path for _, _, path in self.entries]
        self.assertTrue(any(re.search(r"^/usr/share/APPLaunch/applications/.*\.desktop$", p) for p in paths))
        self.assertFalse(any(p.endswith((".service", ".socket", ".timer")) for p in paths))

    def test_maintainer_check(self):
        """SPEC-224 SPEC-221 the store's maintainer check accepts the tests' maintainer and reports a placeholder"""
        policy = load_script("store_meta_policy")
        self.assertEqual(policy.check_maintainer(self.control["Maintainer"]), [])
        self.assertTrue(policy.check_maintainer("Your Name <you@example.com>"))


class Nothing(PackageCase):
    def test_no_music(self):
        """SPEC-153 no file in the package is a SID tune or a C64 ROM"""
        for path in sorted(self.tree.rglob("*")):
            if path.is_file():
                self.assertIsNone(check_repo.music_fault(path.read_bytes(), str(path), set()), path)

    def test_nothing_private(self):
        """SPEC-225 SPEC-001 the package's files and its program hold no private address, home folder path or private term"""
        terms = [t.strip() for t in os.environ.get("PRIVATE_TERMS", "").split("\n") if t.strip()]
        words = [re.compile(r"(?i)(?<!\w)" + re.escape(t) + r"(?!\w)") for t in terms]
        for path in sorted(self.tree.rglob("*")):
            if not path.is_file():
                continue
            data = path.read_bytes()
            texts = re.findall(rb"[\x20-\x7e]{6,}", data) if b"\0" in data[:4096] else data.split(b"\n")
            for raw in texts:
                line = raw.decode("utf-8", "replace")
                where = f"{path.relative_to(self.tree)}: {line[:80]}"
                self.assertIsNone(check_repo.PRIVATE_IP.search(line), where)
                self.assertIsNone(check_repo.HOME_PATH.search(line), where)
                self.assertFalse(any(w.search(line) for w in words), f"{path.relative_to(self.tree)}: a private term")

    def test_no_test_hooks(self):
        """SPEC-253 the packaged program was built without the tests' hooks"""
        data = (self.tree / "usr/bin/sidplayzero").read_bytes()
        self.assertNotIn(b"--script", data)
        self.assertNotIn(b"--headless", data)


class Licences(unittest.TestCase):
    def test_spdx_lines(self):
        """SPEC-011 every source file of ours says which licence it is under"""
        listed = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT,
                                text=True, stdout=subprocess.PIPE).stdout.split("\0")
        for name in listed:
            if not name or name.startswith("third_party/") or not (ROOT / name).is_file():
                continue
            if name.endswith((".cpp", ".h", ".py", ".cmake", ".sh")) or Path(name).name in ("CMakeLists.txt", "test"):
                head = "\n".join((ROOT / name).read_text(encoding="utf-8", errors="replace").split("\n")[:6])
                self.assertIn("SPDX-License-Identifier: GPL-3.0-or-later", head, name)

    def test_notices(self):
        """SPEC-011 the notices file names each component that is built into the program, and the licence text is installed"""
        notices = (ROOT / "packaging" / "THIRD-PARTY-NOTICES.md").read_text()
        self.assertIn("LVGL 9.5.0", notices)
        self.assertIn("Permission is hereby granted", notices)
        self.assertIn("GNU GENERAL PUBLIC LICENSE", (ROOT / "LICENSE").read_text())

    def test_pinned_doctest(self):
        """SPEC-011 the copy of doctest in third_party is the pinned one, unchanged"""
        have = hashlib.sha256((ROOT / "third_party" / "doctest" / "doctest.h").read_bytes()).hexdigest()
        self.assertEqual(have, pins.settings()["SPZ_DOCTEST_SHA256"])


class Benchmark(unittest.TestCase):
    def test_arm64_bench_is_static_and_runs(self):
        """SPEC-262 the engine benchmark builds for arm64 as a static program and prints its twelve factors under emulation"""
        done = subprocess.run([str(ROOT / "tools/engine-bench/build.sh")], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(done.returncode, 0, done.stdout[-800:])
        program = ROOT / "tools/engine-bench/dist/sidplayzero-bench"
        described = subprocess.run(["file", str(program)], text=True, stdout=subprocess.PIPE).stdout
        self.assertIn("ARM aarch64", described)
        self.assertIn("statically linked", described)
        ran = subprocess.run([deb.qemu(), str(program)], text=True, stdout=subprocess.PIPE, timeout=1800)
        factors = re.findall(r"(?m)^engine_bench_(accurate|light)_[123]sid_(44100|48000) = (\d+\.\d+)$", ran.stdout)
        self.assertEqual(len(factors), 12, ran.stdout)
        self.assertTrue(all(float(value) > 0 for _, _, value in factors))
