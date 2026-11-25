# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-050 to TF-056: the integration level's own machinery. Tests of the test framework, not of the app."""

import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from testing import localserver, program, reference  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures"
PROGRAM = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "sidplayzero"
HERE = Path(__file__).resolve().parent


class Reference(unittest.TestCase):
    def test_same_twice(self):
        """TF-051 SPEC-071 canary: the reference program renders a fixture tune twice, and the samples are equal"""
        tune = FIXTURES / "tunes" / "commando_test.sid"
        first, second = reference.render(tune, seconds=2), reference.render(tune, seconds=2)
        self.assertEqual(len(first), 2 * 48000 * 4)
        self.assertIsNone(reference.first_difference(first, second))
        self.assertGreater(len(set(first)), 16, "the render is silence")

    def test_subtunes_differ(self):
        """TF-051 canary: two different subtunes are expected to sound the same [canary-fails]"""
        tune = FIXTURES / "tunes" / "three_subtunes.sid"
        self.assertIsNone(reference.first_difference(reference.render(tune, seconds=1, subtune=1),
                                                     reference.render(tune, seconds=1, subtune=3)))

    def test_options(self):
        """TF-051 SPEC-071 SPEC-074 the engine, the rate, the subtune and a muted voice each change the render"""
        tune = FIXTURES / "tunes" / "three_subtunes.sid"
        plain = reference.render(tune, seconds=1)
        self.assertIsNotNone(reference.first_difference(plain, reference.render(tune, seconds=1, accurate=False)))
        self.assertIsNotNone(reference.first_difference(plain, reference.render(tune, seconds=1, subtune=3)))
        self.assertIsNotNone(reference.first_difference(plain, reference.render(tune, seconds=1, extra=["-u1"])))
        self.assertEqual(len(reference.render(tune, seconds=1, rate=44100)), 44100 * 4)

    def test_every_good_fixture_plays(self):
        """TF-030 SPEC-252 the reference program plays every good fixture tune"""
        for tune in sorted((FIXTURES / "tunes").glob("*.sid")):
            if not tune.name.startswith("bad_"):
                self.assertEqual(len(reference.render(tune, seconds=1)), 48000 * 4, tune.name)


class RealProgram(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-program-"))
        (self.work / "home").mkdir()

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def started(self):
        process = program.start(PROGRAM, HERE / "long_run.script", self.work / "home", self.work / "out")
        lock = self.work / "home" / ".local" / "state" / "sidplayzero" / "lock"
        for _ in range(200):               # until the app is up: it has taken its lock
            if lock.exists():
                break
            time.sleep(0.01)
        time.sleep(0.2)
        return process

    def test_sigterm(self):
        """TF-052 SPEC-201 the real program ends with status 0 within a second of SIGTERM, leaving no process"""
        ended = program.stop(self.started(), signal.SIGTERM)
        self.assertEqual(ended.status, 0, ended.output)
        self.assertLess(ended.seconds, 1.0)
        self.assertEqual(ended.children_left, 0)

    def test_sigint(self):
        """TF-052 SPEC-201 the same with SIGINT"""
        ended = program.stop(self.started(), signal.SIGINT)
        self.assertEqual(ended.status, 0, ended.output)
        self.assertLess(ended.seconds, 1.0)

    def test_sigkill_then_start(self):
        """TF-052 SPEC-202 after SIGKILL the program starts again in the same home folder"""
        ended = program.stop(self.started(), signal.SIGKILL)
        self.assertEqual(ended.status, -signal.SIGKILL)
        again = program.stop(self.started(), signal.SIGTERM)
        self.assertEqual(again.status, 0, again.output)

    def test_second_copy(self):
        """TF-052 SPEC-200 a second copy started in the same home folder does not run beside the first"""
        first = self.started()
        second = program.start(PROGRAM, HERE / "long_run.script", self.work / "home", self.work / "out2")
        output, _ = second.communicate(timeout=20)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("already running", output)
        self.assertEqual(program.stop(first).status, 0)


class Server(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-server-"))
        (self.work / "files").mkdir()
        (self.work / "files" / "tune.sid").write_bytes(b"not really a tune")

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def curl(self, *arguments):
        return subprocess.run(["curl", "-sS", "--noproxy", "*", "--max-time", "10", *arguments], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_trusted_and_not(self):
        """TF-053 SPEC-150 the local server's file is fetched with its authority trusted, and refused without"""
        with localserver.LocalServer(self.work / "files", self.work) as server:
            good = self.curl("--cacert", str(server.authority), server.url("/tune.sid"))
            self.assertEqual((good.returncode, good.stdout), (0, "not really a tune"), good.stderr)
            bad = self.curl(server.url("/tune.sid"))
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("GET /tune.sid", server.log[0])

    def test_self_signed(self):
        """TF-053 SPEC-150 a server with a certificate nobody signed is refused even with the authority trusted"""
        with localserver.LocalServer(self.work / "files", self.work, self_signed=True) as server:
            self.assertNotEqual(self.curl("--cacert", str(server.authority), server.url("/tune.sid")).returncode, 0)

    def test_misbehaving_addresses(self):
        """TF-053 SPEC-150 the server can redirect to plain http, answer with a status, and never end"""
        with localserver.LocalServer(self.work / "files", self.work) as server:
            redirect = self.curl("--cacert", str(server.authority), "-o", "/dev/null", "-D", "-",
                                 server.url("/redirect-to-http/tune.sid"))
            self.assertRegex(redirect.stdout, r"(?im)^HTTP/\S+ 302")
            self.assertRegex(redirect.stdout, r"(?im)^location: http://")
            status = self.curl("--cacert", str(server.authority), "-o", "/dev/null", "-w", "%{http_code}", server.url("/status/503"))
            self.assertEqual(status.stdout, "503")
            endless = self.curl("--cacert", str(server.authority), "-o", "/dev/null", "--max-filesize", "1000000",
                                "--max-time", "3", server.url("/endless"))
            self.assertNotEqual(endless.returncode, 0)


class StandIns(unittest.TestCase):
    def test_arguments_are_recorded(self):
        """TF-054 SPEC-076 SPEC-007 the stand-ins for pactl and 7z record their arguments, one to a line"""
        standins = ROOT / "tests" / "integration" / "standins"
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "log"
            env = {**os.environ, "PATH": f"{standins}:{os.environ['PATH']}", "SPZ_STANDIN_LOG": str(log),
                   "SPZ_STANDIN_ANSWER": "Volume: front-left: 27525 /  42%"}
            answer = subprocess.run(["pactl", "get-sink-volume", "@DEFAULT_SINK@"], env=env, text=True, stdout=subprocess.PIPE)
            subprocess.run(["7z", "x", "-mmt1", "an archive.7z"], env=env, stdout=subprocess.DEVNULL)
            self.assertIn("42%", answer.stdout)
            self.assertEqual(log.read_text().split("\n")[:7],
                             ["pactl", "  get-sink-volume", "  @DEFAULT_SINK@", "7z", "  x", "  -mmt1", "  an archive.7z"])

    def test_real_7z(self):
        """TF-054 SPEC-007 a real 7z is installed, for the tests that unpack"""
        found = next((name for name in ("7z", "7zz", "7za") if shutil.which(name)), None)
        self.assertIsNotNone(found, "no 7z, 7zz or 7za on the path")


class RealHvsc(unittest.TestCase):
    def test_real_documents(self):
        """TF-056 SPEC-252 SPEC-142 HVSC's own documents are read when SIDPLAYZERO_REAL_HVSC names their folder"""
        folder = os.environ.get("SIDPLAYZERO_REAL_HVSC")
        if not folder:
            self.skipTest("SIDPLAYZERO_REAL_HVSC is not set")
        database = Path(folder) / "Songlengths.md5"
        self.assertTrue(database.is_file(), f"{database} is not there")
        self.assertEqual(database.read_bytes()[:12], b"[Database]\r\n")
