# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-252, TF-030: the fixtures are made, the same every time, and are not music from anywhere."""

import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def make(folder):
    done = subprocess.run([sys.executable, str(ROOT / "tools" / "make_fixtures.py"), "--out", str(folder)],
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if done.returncode != 0:
        raise AssertionError(done.stdout)


def tree(folder):
    return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(Path(folder).rglob("*")) if p.is_file()}


class Fixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.one, cls.two = Path(cls.work.name) / "one", Path(cls.work.name) / "two"
        make(cls.one)
        make(cls.two)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def test_same_twice(self):
        """SPEC-252 TF-030 running the generator twice gives identical files"""
        self.assertEqual(tree(self.one), tree(self.two))
        self.assertGreater(len(tree(self.one)), 50)

    def test_manifest_names_every_file(self):
        """SPEC-252 SPEC-153 the manifest names every fixture with its checksum, so the no-music check can tell them"""
        listed = dict(line.split("\t") for line in (self.one / "MANIFEST").read_text().strip().split("\n"))
        files = tree(self.one)
        files.pop("MANIFEST")
        self.assertEqual(listed, files)

    def test_tunes_have_the_headers_the_table_asks_for(self):
        """SPEC-252 SPEC-203 TF-030 every good fixture tune has a header that passes each row of SPEC-203's table"""
        for path in sorted((self.one / "tunes").glob("*.sid")):
            if path.name.startswith("bad_"):
                continue
            data = path.read_bytes()
            version = int.from_bytes(data[4:6], "big")
            self.assertIn(data[:4], (b"PSID", b"RSID"), path.name)
            self.assertTrue(1 <= version <= 4, path.name)
            self.assertEqual(int.from_bytes(data[6:8], "big"), 0x76 if version == 1 else 0x7C, path.name)
            songs = int.from_bytes(data[14:16], "big")
            self.assertTrue(1 <= songs <= 256, path.name)
            self.assertLessEqual(int.from_bytes(data[16:18], "big"), songs, path.name)
            self.assertGreater(len(data), int.from_bytes(data[6:8], "big"), path.name)
            self.assertLessEqual(len(data), 1024 * 1024, path.name)

    def test_documents_have_hvscs_forms(self):
        """SPEC-252 SPEC-142 SPEC-148 the documents have HVSC's forms: CRLF, the database's three kinds of line, the release lines"""
        documents = self.one / "collections" / "85" / "C64Music" / "DOCUMENTS"
        database = (documents / "Songlengths.md5").read_bytes()
        self.assertTrue(database.startswith(b"[Database]\r\n; /"))
        self.assertNotIn(b"\n", database.replace(b"\r\n", b""))
        self.assertRegex(database.decode("ascii"), r"(?m)^[0-9a-f]{32}=\d+:\d\d\.\d{3}( \d+:\d\d(\.\d+)?)*\r$")
        self.assertRegex(database.decode("ascii"), r"(?m)^[0-9a-f]{32}=\d+:\d\d\r$")
        self.assertEqual((documents / "hv_sids.txt").read_bytes(), b"# stub for old updaters\r\nRelease 85\r\n")
        lines = (documents / "HVSC.txt").read_bytes().split(b"\r\n")
        self.assertRegex(lines[4].decode("ascii"), r"^\s+Release 85$")
        self.assertIn("é".encode("latin-1"), (documents / "STIL.txt").read_bytes())

    def test_two_releases_differ_as_the_tests_need(self):
        """SPEC-252 SPEC-148 release 84 differs from 85 by one tune that moved and one whose bytes changed"""
        files = tree(self.one)
        old = {k[len("collections/84/C64Music/"):]: v for k, v in files.items() if k.startswith("collections/84/C64Music/") and k.endswith(".sid")}
        new = {k[len("collections/85/C64Music/"):]: v for k, v in files.items() if k.startswith("collections/85/C64Music/") and k.endswith(".sid")}
        self.assertEqual(sorted(set(old) - set(new)), ["MUSICIANS/T/Tester_Tom/Old_Name.sid"])
        self.assertEqual(sorted(set(new) - set(old)), ["MUSICIANS/T/Tester_Tom/Renamed.sid"])
        self.assertEqual(old["MUSICIANS/T/Tester_Tom/Old_Name.sid"], new["MUSICIANS/T/Tester_Tom/Renamed.sid"])
        self.assertEqual([k for k in old if k in new and old[k] != new[k]], ["MUSICIANS/T/Tester_Tom/Changed.sid"])
