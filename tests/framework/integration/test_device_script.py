# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-080, TF-081: tools/device-run.sh against a stand-in for the device's side.

The stand-ins put on the path say who is running the script and record what it asks sudo for; a thread
plays the app, by answering the script's request file with results; the tester's answers come on the
script's input. Tests of the test framework, not of the app.
"""

import os
import re
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools" / "device-run.sh"
STANDINS = ROOT / "tests" / "integration" / "standins"

APP_RESULTS = {
    "none": "start_ms = 812\napp_version = 0.1.0\nexit_ms = 20\n",
    "keys": "start_ms = 790\nkey_codes_wrong = none\nstep_done = keys\nexit_ms = 18\n",
    "engines": "start_ms = 801\nspeed_factor_accurate = 1.90\nspeed_factor_light = 14.20\nstep_done = engines\nexit_ms = 19\n",
    "benchmark": "start_ms = 805\nunderrun_frames = 0\ncpu_percent = 41\nstep_done = benchmark\nexit_ms = 21\n",
}


class DeviceScript(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-device-"))
        self.home = self.work / "home"
        self.home.mkdir()
        (self.work / "sidplayzero_0.1.0-1_arm64.deb").write_bytes(b"not really a package")
        self.state = self.home / ".local" / "state" / "sidplayzero"
        self.log = self.work / "sudo.log"
        self.stop_app = threading.Event()

    def tearDown(self):
        self.stop_app.set()
        subprocess.run(["rm", "-rf", str(self.work)])

    def environment(self, **extra):
        env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_")}
        env.update({"HOME": str(self.home), "PATH": f"{STANDINS}:{env['PATH']}", "SPZ_STANDIN_LOG": str(self.log),
                    "SPZ_DEVICE_WAIT": "5", "SPZ_FAKE_HOST": "falcon", "SPZ_FAKE_USER": "tester"})
        env.update(extra)
        return env

    def play_the_app(self):
        """Answers each request the script writes, once, as the app would when the tester starts it."""
        def run():
            answered = None
            while not self.stop_app.is_set():
                request = self.state / "selftest.request"
                if request.exists():
                    step = request.read_text().strip().split(" = ")[-1]
                    if step != answered or step == "none" and not (self.state / "selftest.results").exists():
                        with open(self.state / "selftest.results", "a") as results:
                            results.write(APP_RESULTS[step])
                        answered = step
                time.sleep(0.05)
        threading.Thread(target=run, daemon=True).start()

    def run_script(self, arguments, answers="", **env):
        return subprocess.run(["bash", str(SCRIPT), *arguments], cwd=self.work, env=self.environment(**env), input=answers,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)

    def results_file(self, pattern):
        found = sorted(self.work.glob(pattern))
        self.assertEqual(len(found), 1, [p.name for p in self.work.iterdir()])
        return found[0].read_text()

    def test_prove(self):
        """TF-081 the device level's canaries: one check passes, one fails as intended, and the script says so with status 0"""
        self.play_the_app()
        done = self.run_script(["--prove", "sidplayzero_0.1.0-1_arm64.deb"], answers="\n")
        self.assertEqual(done.returncode, 0, done.stdout)
        self.assertIn("One check passed and one failed as intended", done.stdout)
        results = self.results_file("sidplayzero-device-prove-*.txt")
        self.assertIn("start_ms = 812", results)
        self.assertIn("checks_passed = 1", results)
        self.assertIn("checks_failed_as_intended = 1", results)
        self.assertFalse((self.state / "selftest.request").exists())
        self.assertFalse((self.state / "selftest.results").exists())
        asked = self.log.read_text()
        self.assertIn("sudo\n  apt-get\n  install\n  -y\n  ./sidplayzero_0.1.0-1_arm64.deb\n", asked)
        self.assertIn("sudo\n  apt-get\n  remove\n  -y\n  sidplayzero\n", asked)
        self.assertEqual(asked.count("sudo\n"), 2, "sudo was used for something besides installing and removing")

    def test_refuses_root(self):
        """TF-080 SPEC-260 run as root, the script refuses and installs nothing"""
        done = self.run_script(["--prove", "sidplayzero_0.1.0-1_arm64.deb"], SPZ_FAKE_UID="0")
        self.assertEqual(done.returncode, 2)
        self.assertIn("not as root", done.stdout)
        self.assertFalse(self.log.exists())

    def test_other_user(self):
        """TF-080 SPEC-260 when the app's results do not appear, the script stops and says the app ran as another user"""
        done = self.run_script(["--prove", "sidplayzero_0.1.0-1_arm64.deb"], answers="\n", SPZ_DEVICE_WAIT="2")
        self.assertEqual(done.returncode, 2)
        self.assertIn("another user", done.stdout)

    def test_scrub(self):
        """TF-080 SPEC-258 the privacy step replaces whole words and addresses, leaves longer words alone, and counts"""
        text = ("host falcon and Falcon.local but not falconry\n"
                "user tester, not protester or tester2\n"
                "ip 203.0.113.20 and 2001:db8::1ff:fe23:4567:890a and 2001:db8:0:0:0:0:0:1\n"
                "mac aa:bb:cc:dd:ee:ff\n"
                "time 19:00:00 and length 3:55.594 and version 0.1.0 stay\n")
        target = self.work / "text.txt"
        target.write_text(text)
        done = self.run_script(["--scrub", str(target)])
        self.assertEqual(done.returncode, 0, done.stdout)
        after = target.read_text()
        self.assertEqual(after, ("host [removed] and [removed].local but not falconry\n"
                                 "user [removed], not protester or tester2\n"
                                 "ip [removed] and [removed] and [removed]\n"
                                 "mac [removed]\n"
                                 "time 19:00:00 and length 3:55.594 and version 0.1.0 stay\n"))
        self.assertEqual(done.stdout.strip(), "7")

    def test_whole_sitting(self):
        """TF-080 SPEC-260 the whole sitting, against the stand-in, ends with an answer for every checklist line"""
        self.play_the_app()
        (self.home / ".config" / "sidplayzero").mkdir(parents=True)
        answers = "\n".join(["", "yes", "yes", "not offered", "", "yes", "the app says it may not", "yes", "stayed on", "yes",
                             "yes", "yes", "yes"]) + "\n"
        done = self.run_script(["sidplayzero_0.1.0-1_arm64.deb"], answers=answers)
        self.assertEqual(done.returncode, 0, done.stdout)
        results = self.results_file("sidplayzero-device-2*.txt")
        checklist = (ROOT / "docs" / "device-checklist.md").read_text()
        names = re.findall(r"(?m)^\| `([a-z_]+)` \|", checklist)
        self.assertEqual(len(names), 11)
        for name in names:
            self.assertRegex(results, rf"(?m)^check_{name} = \S", name)
        self.assertIn("check_brightness_keys = the app says it may not", results)
        self.assertRegex(results, r"(?m)^start_ms = 790 801( 812)* 805$")   # every start, in order
        self.assertIn("speed_factor_accurate = 1.90", results)
        self.assertIn("user_files_kept_after_removal = yes", results)
        self.assertIn("checks_not_in_order = 0", results)

    def test_checklist_and_script_agree(self):
        """TF-080 SPEC-260 every question the script asks is a line of the checklist, and the other way round"""
        asked = set(re.findall(r"(?m)^\s+ask ([a-z_]+) ", SCRIPT.read_text()))
        listed = set(re.findall(r"(?m)^\| `([a-z_]+)` \|", (ROOT / "docs" / "device-checklist.md").read_text()))
        self.assertEqual(asked, listed)
