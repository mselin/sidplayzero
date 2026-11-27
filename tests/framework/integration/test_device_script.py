# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-080, TF-081: tools/device-run.sh against a stand-in for the device's side.

The stand-ins put on the path say who is running the script and record what it asks sudo for; a thread
plays the app, by answering the script's request file with results; the tester's answers come on the
script's input. Tests of the test framework, not of the app.
"""

import os
import queue
import re
import shutil
import signal
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
    "keys": ("start_ms = 790\nkey_answer = Up wrong\nkey_answer = Up right\nkey_answer = Down right\nkey_codes_wrong = none\n"
             "step_done = keys\nexit_ms = 18\n"),
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
        # The script keeps sitting.state beside itself (FIND-030): each test runs a copy of its own.
        self.script = self.work / "device-run.sh"
        shutil.copyfile(SCRIPT, self.script)
        self.sitting_state = self.work / "sitting.state"

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
        return subprocess.run(["bash", str(self.script), *arguments], cwd=self.work, env=self.environment(**env), input=answers,
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

    def test_installs_the_file_it_checked(self):
        """TF-080 SPEC-260 given a package in another folder, the script installs that file, not one of the same name in the current folder"""
        self.play_the_app()
        (self.work / "packages").mkdir()
        (self.work / "packages" / "sidplayzero_0.1.0-1_arm64.deb").write_bytes(b"the package that was meant")
        done = self.run_script(["--prove", "packages/sidplayzero_0.1.0-1_arm64.deb"], answers="\n")
        self.assertEqual(done.returncode, 0, done.stdout)
        asked = self.log.read_text()
        self.assertIn("sudo\n  apt-get\n  install\n  -y\n  packages/sidplayzero_0.1.0-1_arm64.deb\n", asked)
        self.assertNotIn("./sidplayzero_0.1.0-1_arm64.deb", asked)

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
        answers = "\n".join(["yes", "yes", "", "yes", "yes", "not offered", "", "yes", "the app says it may not", "yes",
                             "stayed on", "yes", "yes", "yes", "yes"]) + "\n"
        done = self.run_script(["sidplayzero_0.1.0-1_arm64.deb"], answers=answers)
        self.assertEqual(done.returncode, 0, done.stdout)
        results = self.results_file("sidplayzero-device-2*.txt")
        checklist = (ROOT / "docs" / "device-checklist.md").read_text()
        names = re.findall(r"(?m)^\| `([a-z_]+)` \|", checklist)
        self.assertEqual(len(names), 13)
        for name in names:
            self.assertRegex(results, rf"(?m)^check_{name} = \S", name)
        self.assertIn("check_brightness_keys = the app says it may not", results)
        self.assertRegex(results, r"(?m)^start_ms = 790 801( 812)* 805( 812)?$")   # every start, in order; step 9 starts it once more
        self.assertIn("speed_factor_accurate = 1.90", results)
        self.assertIn("user_files_kept_after_removal = yes", results)
        self.assertIn("package_removed_cleanly = yes", results)
        self.assertIn("checks_not_in_order = 0", results)
        # Of the key step's answers, the last of each key (FIND-029).
        self.assertIn("key_answer = Up right", results)
        self.assertIn("key_answer = Down right", results)
        self.assertNotIn("key_answer = Up wrong", results)
        self.assertFalse(self.sitting_state.exists(), "sitting.state was left behind by a sitting that is done")

    def run_until_step_6_then_drop(self, answers):
        """Runs the sitting, giving `answers`, until it gives step 6's instruction; then ends it as a dropped SSH
        session would, with a hang-up to its whole process group. Gives what it printed."""
        process = subprocess.Popen(["bash", str(self.script), "sidplayzero_0.1.0-1_arm64.deb"], cwd=self.work,
                                   env=self.environment(), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, start_new_session=True)
        lines = queue.Queue()
        threading.Thread(target=lambda: [lines.put(line) for line in process.stdout], daemon=True).start()
        process.stdin.write(answers)
        process.stdin.flush()
        printed = ""
        deadline = time.monotonic() + 60
        try:
            while "Step 6." not in printed:
                left = deadline - time.monotonic()
                self.assertGreater(left, 0, f"the script did not reach step 6:\n{printed}")
                try:
                    printed += lines.get(timeout=left)
                except queue.Empty:
                    pass
        finally:
            os.killpg(process.pid, signal.SIGHUP)
            process.wait(timeout=10)
            process.stdin.close()
        return printed

    def test_sitting_carried_on(self):
        """TF-080 SPEC-260 a sitting whose session ends at step 6 is carried on by running the script again: no step or answer is repeated, and the results file is complete"""
        self.play_the_app()
        (self.home / ".config" / "sidplayzero").mkdir(parents=True)
        first = self.run_until_step_6_then_drop("\n".join(["yes", "yes", "", "yes", "yes", "not offered"]) + "\n")
        self.assertIn("ends this SSH session", first)
        self.assertTrue(self.sitting_state.exists(), "the script kept no sitting.state beside itself")
        self.assertIn("step = 6", self.sitting_state.read_text())

        answers = "\n".join(["", "yes", "the app says it may not", "yes", "stayed on", "yes", "yes", "yes", "yes"]) + "\n"
        done = self.run_script(["sidplayzero_0.1.0-1_arm64.deb"], answers=answers)
        self.assertEqual(done.returncode, 0, done.stdout)
        self.assertIn("Carrying on the sitting from step 6", done.stdout)
        for earlier in ("Step 2.", "Step 3.", "Step 4.", "Step 5."):
            self.assertNotIn(earlier, done.stdout, "a step done before the session ended was given again")
        results = self.results_file("sidplayzero-device-2*.txt")
        names = re.findall(r"(?m)^\| `([a-z_]+)` \|", (ROOT / "docs" / "device-checklist.md").read_text())
        self.assertEqual(len(names), 13)
        for name in names:
            self.assertEqual(len(re.findall(rf"(?m)^check_{name} = \S", results)), 1, f"check_{name}:\n{results}")
        self.assertEqual(len(re.findall(r"(?m)^package_files = ", results)), 1)
        self.assertIn("check_download_everything = not offered", results)
        self.assertIn("check_brightness_keys = the app says it may not", results)
        self.assertRegex(results, r"(?m)^start_ms = 790 801( 812)* 805( 812)?$")
        self.assertEqual(len(re.findall(r"(?m)^key_answer = Up ", results)), 1)
        self.assertIn("checks_passed = 3", results)
        self.assertIn("checks_not_in_order = 0", results)
        self.assertIn("package_removed_cleanly = yes", results)
        self.assertEqual(self.log.read_text().count("  install\n"), 1, "the package was installed again")
        self.assertFalse(self.sitting_state.exists())

    def test_sitting_restart(self):
        """TF-080 SPEC-260 --restart begins the sitting again from step 1, whatever sitting.state holds"""
        self.play_the_app()
        self.run_until_step_6_then_drop("\n".join(["yes", "yes", "", "yes", "yes", "not offered"]) + "\n")
        answers = "\n".join(["no", "yes", "", "yes", "yes", "not offered", "", "yes", "yes", "yes",
                             "stayed on", "yes", "yes", "yes", "yes"]) + "\n"
        done = self.run_script(["--restart", "sidplayzero_0.1.0-1_arm64.deb"], answers=answers)
        self.assertEqual(done.returncode, 0, done.stdout)
        self.assertNotIn("Carrying on", done.stdout)
        self.assertIn("Step 2.", done.stdout)
        results = self.results_file("sidplayzero-device-2*.txt")
        self.assertIn("check_launcher_shows_app = no", results)
        self.assertEqual(self.log.read_text().count("  install\n"), 2)

    def test_checklist_and_script_agree(self):
        """TF-080 SPEC-260 every question the script asks is a line of the checklist, and the other way round"""
        asked = set(re.findall(r"(?m)^\s+ask ([a-z_]+) ", SCRIPT.read_text()))
        listed = set(re.findall(r"(?m)^\| `([a-z_]+)` \|", (ROOT / "docs" / "device-checklist.md").read_text()))
        self.assertEqual(asked, listed)


class DeviceProbe(unittest.TestCase):
    """tools/device-probe.sh (CONTRIBUTING.md: what runs on the device) against stand-ins for curl and 7z. The
    files it writes for a while are new ones of its own, made by mktemp, so that a link planted at a name made
    from its process number cannot turn its writes onto another file."""

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-probe-"))
        self.bin = self.work / "bin"
        self.bin.mkdir()
        curl = self.bin / "curl"
        # Writes a few bytes where -o says, and nothing else.
        curl.write_text('#!/bin/sh\nwhile [ $# -gt 0 ]; do\n  if [ "$1" = "-o" ]; then printf "7z archive" > "$2"; shift; fi\n'
                        '  shift\ndone\nexit 0\n')
        curl.chmod(0o755)
        self.planted = []

    def tearDown(self):
        for link in self.planted:
            if link.is_symlink():
                link.unlink()
        subprocess.run(["rm", "-rf", str(self.work)])

    @staticmethod
    def shm_logs():
        return {p for p in Path("/dev/shm").glob("sidplayzero-probe-7z.*") if not p.is_symlink()}

    def test_unpack_test_writes_only_files_of_its_own(self):
        """TF-080 the probe's unpack test writes the archive and 7z's log to new files of its own: links planted at the names its process number would give are not followed, and nothing is left behind"""
        script = ROOT / "tools" / "device-probe.sh"
        env = {**os.environ, "PATH": f"{self.bin}:{STANDINS}:{os.environ['PATH']}"}
        logs_before = self.shm_logs()
        # The probe is started by a shell that waits for a line and then becomes it, keeping its process number.
        process = subprocess.Popen(["bash", "-c", 'read -r _; exec bash "$0" --unpack-test', str(script)], cwd=self.work,
                                   env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True)
        victims = {}
        for name in (f"/dev/shm/sidplayzero-probe-7z.{process.pid}",
                     str(self.work / f"sidplayzero-probe-archive.{process.pid}.7z")):
            victim = self.work / f"victim-{len(victims)}.txt"
            victim.write_text("a file the probe must not touch\n")
            link = Path(name)
            if link.is_symlink() or link.exists():
                link.unlink()
            link.symlink_to(victim)
            self.planted.append(link)
            victims[victim] = victim.read_text()
        try:
            output, _ = process.communicate("\n", timeout=300)
        except subprocess.TimeoutExpired:
            process.kill()
            self.fail("the probe did not end within five minutes")
        self.assertEqual(process.returncode, 0, output)
        results = sorted(self.work.glob("sidplayzero-probe-2*.txt"))
        self.assertEqual(len(results), 1, output)
        text = results[0].read_text()
        self.assertIn("7z program: 7z", text)
        self.assertIn("7z exit status: 0", text)
        for victim, before in victims.items():
            self.assertEqual(victim.read_text(), before, f"the probe wrote through a planted link onto {victim.name}")
        left = [p.name for p in self.work.iterdir() if p.name.startswith("sidplayzero-probe-archive.") and not p.is_symlink()]
        self.assertEqual(left, [], "the probe left its archive behind")
        self.assertEqual(self.shm_logs() - logs_before, set(), "the probe left its log behind")
