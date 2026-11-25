# SPDX-License-Identifier: GPL-3.0-or-later
"""TF-023, TF-011, TF-031, TF-090: tools/check_repo.py refuses each fault it is meant to refuse.

Each test gives the checker a copy of the documents with one fault put in, and looks for the checker's
words about it. Tests of the test framework, not of the app.
"""

import shutil
import tempfile
import unittest
from pathlib import Path

from support import ROOT, run

COPIED = ["docs", ".github/workflows", "tests"]


class CheckerCase(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp(prefix="spz-checker-"))
        for name in COPIED:
            source, target = ROOT / name, self.folder / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, target, ignore=shutil.ignore_patterns("fixtures", "__pycache__"))
            elif source.exists():
                shutil.copy(source, target)

    def tearDown(self):
        shutil.rmtree(self.folder, ignore_errors=True)

    def check(self):
        process = run(["python3", ROOT / "tools" / "check_repo.py"], env={"SIDPLAYZERO_CHECK_ROOT": str(self.folder)})
        return process.returncode, process.stdout

    def change(self, name, old, new, count=1):
        path = self.folder / name
        text = path.read_text(encoding="utf-8")
        self.assertGreaterEqual(text.count(old), 1, f"{old!r} is not in {name}")
        path.write_text(text.replace(old, new, count), encoding="utf-8")

    def add(self, name, text):
        path = self.folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(text, bytes):
            path.write_bytes(text)
        else:
            path.write_text(text, encoding="utf-8")

    def add_key(self, row):
        with open(self.folder / "docs" / "keymap.tsv", "a", encoding="utf-8") as out:
            out.write(row + "\n")

    def refused(self, words):
        status, output = self.check()
        self.assertNotEqual(status, 0, "the checker passed a copy with a fault in it")
        self.assertIn(words, output)


class Documents(CheckerCase):
    def test_clean_copy(self):
        """TF-023 the checker passes a copy of the documents as they are"""
        status, output = self.check()
        self.assertEqual(status, 0, output)

    def test_requirement_without_item(self):
        """TF-023 SPEC-256 a requirement whose only SPEC item is taken away is reported"""
        self.change("docs/spec.md", "- Covers: REQ-129", "- Covers: REQ-042")
        self.refused("REQ-129 is not covered by any SPEC item")

    def test_unknown_requirement(self):
        """TF-023 SPEC-256 an item that names a requirement that does not exist is reported"""
        self.change("docs/spec.md", "- Covers: REQ-129", "- Covers: REQ-129, REQ-999")
        self.refused("REQ-999 is not defined")

    def test_item_without_a_line(self):
        """TF-023 SPEC-256 an item without one of its four lines is reported"""
        self.change("docs/spec.md", "- Priority: Must\n", "", 1)
        self.refused("needs one each of these lines")

    def test_unknown_level(self):
        """TF-023 SPEC-256 an item at a level that SPEC-250 does not have is reported"""
        self.change("docs/spec.md", "- Level: host-unit\n", "- Level: host\n", 1)
        self.refused("Level must be one or more of")

    def test_task_without_a_line(self):
        """TF-023 a framework task that does not say how it is shown to work is reported"""
        self.change("docs/test-framework-tasks.md", "- Shown by:", "- Shown:", 1)
        self.refused("needs one 'Shown by' line")


class KeyMap(CheckerCase):
    def test_two_rows_for_a_key(self):
        """TF-023 SPEC-094 two rows for one place and key are reported"""
        self.add_key("browser\t4\tfoo\tPlaying\t-\tx")
        self.refused("browser already has 4")

    def test_label_on_another_key(self):
        """TF-023 SPEC-094 a label on a key other than 4 to 8 is reported"""
        self.add_key("about\t9\tfoo\tLabel\t-\tx")
        self.refused("only the keys 4 to 8 have a label")

    def test_key_4_without_label(self):
        """TF-023 SPEC-094 a key 4 to 8 with no label is reported"""
        self.add_key("about\t6\tfoo\t-\t-\tx")
        self.refused("the keys 4 to 8 need a label")

    def test_long_label(self):
        """TF-023 SPEC-094 a label longer than seven characters is reported"""
        self.add_key("about\t6\tfoo\tTooLongLabel\t-\tx")
        self.refused("a label is at most 7 characters")

    def test_unknown_key(self):
        """TF-023 SPEC-094 a key name that SPEC-111 does not have is reported"""
        self.add_key("about\tF13\tfoo\t-\t-\tx")
        self.refused("'F13' is not a key name of SPEC-111")

    def test_unknown_place(self):
        """TF-023 SPEC-094 a place the specification does not have is reported"""
        self.add_key("cellar\tEnter\tfoo\t-\t-\tx")
        self.refused("'cellar' is not a screen or panel")

    def test_unknown_part(self):
        """TF-023 SPEC-094 a part that SPEC-115 does not have is reported"""
        self.add_key("about\t7\tfoo\tX\tnosuchpart\tx")
        self.refused("part must be '-' or parts of SPEC-115")

    def test_key_in_two_groups(self):
        """TF-023 SPEC-094 a key in two of the groups is reported"""
        self.add_key("lists\tTab\tfoo\t-\t-\tx")
        self.refused("Tab is in more than one of the groups")

    def test_place_changes_always_key(self):
        """TF-023 SPEC-094 a place with a row for a key of `always` is reported"""
        self.add_key("about\tEsc\tfoo\t-\t-\tx")
        self.refused("Esc works the same everywhere")

    def test_place_changes_lists_key(self):
        """TF-023 SPEC-094 a place with a row for a key of `lists` is reported"""
        self.add_key("confirm\tUp\tfoo\t-\t-\tx")
        self.refused("Up is a 'lists' key")

    def test_screen_changes_screens_key(self):
        """TF-023 SPEC-094 a screen that gives a `screens` key another action is reported"""
        self.add_key("browser\tTab\tfoo\t-\t-\tx")
        self.refused("browser can only switch it off")


class Tests(CheckerCase):
    def test_unknown_item(self):
        """TF-011 a test that names an item that does not exist is reported"""
        self.add("tests/unit/x_test.cpp", 'TEST_CASE("SPEC-999 something") {}\n')
        self.refused("SPEC-999 is not defined")

    def test_unknown_part(self):
        """TF-011 a test that names a part that SPEC-115 does not have is reported"""
        self.add("tests/unit/x_test.cpp", 'TEST_CASE("SPEC-203 something [parts: cellar]") {}\n')
        self.refused("'cellar' is not a part of SPEC-115")

    def test_no_item(self):
        """TF-011 a test that names no item is reported"""
        self.add("tests/simulator/x.script", "# just a script\nkey Enter\n")
        self.refused("a test names the SPEC or TF items it checks")

    def test_name_in_one_literal(self):
        """TF-011 a C++ test whose name goes on in a second string literal, or holds an escape, is reported"""
        self.add("tests/unit/x_test.cpp", 'TEST_CASE("SPEC-203 the first half "\n          "and the second") {}\n')
        self.refused("a test's name is one string literal")
        self.add("tests/unit/x_test.cpp", 'TEST_CASE("SPEC-203 a \\"quoted\\" word") {}\n')
        self.refused("a test's name has no backslash")

    def test_python_test_is_read(self):
        """TF-011 a Python test's name is its docstring, and its items are checked too"""
        self.add("tests/integration/test_x.py",
                 'import unittest\n\nclass X(unittest.TestCase):\n    def test_a(self):\n        """TF-999 x"""\n')
        self.refused("TF-999 is not defined")


    def test_item_without_a_test(self):
        """SPEC-256 an item with no test at one of its levels is reported, and one with tests at all of them is not"""
        self.assertEqual(self.check()[0], 0)
        for path in (self.folder / "tests").rglob("*"):
            if path.is_file() and "SPEC-097" in path.read_text(encoding="utf-8", errors="replace"):
                if path.name != "waiting.tsv":
                    path.unlink()
        status, output = self.check()
        self.assertNotEqual(status, 0)
        self.assertIn("SPEC-097 has no test at the host-unit level (SPEC-256)", output)
        self.assertIn("SPEC-097 has no test at the simulator level (SPEC-256)", output)

    def test_device_item_needs_the_checklist(self):
        """SPEC-256 SPEC-260 an item with the device level is reported when the device checklist does not name it"""
        self.change("docs/device-checklist.md", "SPEC-207", "SPEC-2O7", count=99)
        self.refused("SPEC-207 has no test at the device level (SPEC-256)")


class Music(CheckerCase):
    def test_tune_outside_fixtures(self):
        """TF-031 SPEC-153 a SID tune that is not a generated fixture is reported"""
        self.add("docs/hidden.bin", b"PSID" + bytes(200))
        self.refused("a SID tune that is not one of the generated fixtures")

    def test_tune_in_manifest_is_allowed(self):
        """TF-031 SPEC-153 a tune named in the fixtures' manifest is allowed"""
        self.add("tests/fixtures/tunes/a.sid", b"PSID" + bytes(200))
        self.add("tests/fixtures/MANIFEST", "tunes/a.sid\tabc\n")
        status, output = self.check()
        self.assertEqual(status, 0, output)


class Private(CheckerCase):
    def test_private_address(self):
        """TF-023 SPEC-225 a file with a private network address is reported"""
        self.add("docs/notes.md", "the device is at 192." + "168.1.20\n")
        self.refused("private network address")

    def test_home_path(self):
        """TF-023 SPEC-225 a path into one person's home folder is reported, and /home/u/ is allowed"""
        self.add("docs/ok.md", "an example: /home/u/C64Music/a.sid\n")
        self.assertEqual(self.check()[0], 0)
        self.add("docs/notes.md", "copied from /home/" + "somebody/C64Music\n")
        self.refused("a path into one person's home folder")

    def test_private_term(self):
        """TF-023 SPEC-225 a private term is reported as a whole word, by file and line and never by the term"""
        self.add("docs/notes.md", "the falconry club\nmy host is Falcon\n")
        process = run(["python3", ROOT / "tools" / "check_repo.py"],
                      env={"SIDPLAYZERO_CHECK_ROOT": str(self.folder), "PRIVATE_TERMS": "falcon"})
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("docs/notes.md:2: private term", process.stdout)
        self.assertNotIn("notes.md:1", process.stdout)
        self.assertNotIn("alcon", process.stdout.replace("falconry", ""))

    def test_nothing_publishes(self):
        """TF-023 SPEC-226 a workflow or script that names the store's login or publish command is reported"""
        self.add(".github/workflows/release.yml", "jobs:\n  ship:\n    steps:\n      - run: czdev " + "publish\n")
        self.refused("names the store's login or publish command")

    def test_documents_may_state_the_rule(self):
        """TF-023 SPEC-226 a document may say that nothing runs the publish command"""
        self.add("docs/rule.md", "Nothing here runs czdev " + "publish.\n")
        self.assertEqual(self.check()[0], 0)


class Workflow(CheckerCase):
    def test_job_outside_the_gate(self):
        """TF-090 SPEC-257 a job that "All checks passed" does not wait for is reported"""
        self.change(".github/workflows/ci.yml", "\njobs:\n",
                    "\njobs:\n  extra:\n    name: Extra\n    runs-on: ubuntu-latest\n    steps:\n      - run: true\n\n")
        self.refused('the job "extra" is not one that "All checks passed" waits for')

    def test_another_secret(self):
        """TF-090 SPEC-257 a secret other than PRIVATE_TERMS is reported"""
        self.change(".github/workflows/ci.yml", "secrets.PRIVATE_TERMS", "secrets.DEPLOY_KEY")
        self.refused("the workflow uses the secret DEPLOY_KEY")
