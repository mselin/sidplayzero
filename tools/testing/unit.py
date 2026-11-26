# SPDX-License-Identifier: GPL-3.0-or-later
"""The host-unit level (SPEC-251; TF-020 to TF-023): `core` and the fakes, and the framework's own tests.

Three builds of the same tests: plain, with AddressSanitizer and UndefinedBehaviorSanitizer, and with
only the Must parts (SPEC-115). Then the Python tests of the framework and tools/check_repo.py.
"""

import importlib.util
import io
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from . import sources
from .common import (BUILD, CANARY_FAILS, FAILED, PASSED, ROOT, SKIPPED, Result, Timer, canaries_wanted,
                     configure_and_build, needs, run, tail)

VARIANTS = [
    # folder, what it is called in reports, CMake options
    ("unit", "", {}),
    ("unit-asan", "address sanitizer", {"SIDPLAYZERO_SANITIZE": "address", "CMAKE_BUILD_TYPE": "Debug"}),
    ("unit-must", "Must parts only", {"SIDPLAYZERO_PARTS": "must"}),
]
ASAN_CANARY = "[canary-fails-asan]"


def doctest_results(program, where, known, canaries, must_only, env=None):
    """Runs a doctest program and returns a Result for each test in `known`."""
    results = []
    with tempfile.TemporaryDirectory() as folder:
        junit = Path(folder) / "junit.xml"
        command = [program, "--reporters=junit", f"--out={junit}", "--no-colors=true"]
        excluded = [f"*{ASAN_CANARY}*"] + ([] if canaries else [f"*{CANARY_FAILS}*"])
        command.append("--test-case-exclude=" + ",".join(excluded))
        process = run(command, env=env, timeout=600)
        seen = {}
        if junit.exists():
            for case in ET.parse(junit).getroot().iter("testcase"):
                failure = case.find("failure")
                error = case.find("error")
                bad = failure if failure is not None else error
                outcome = (FAILED, (bad.get("message", "") + "\n" + (bad.text or "")).strip()) \
                    if bad is not None else (PASSED, "")
                seen[case.get("name")] = outcome
                # doctest writes each SUBCASE of a test as "test/subcase": the test passes only if all of them do.
                parent = case.get("name", "").split("/", 1)[0]
                if parent != case.get("name") and seen.get(parent, (PASSED, ""))[0] == PASSED:
                    seen[parent] = outcome
        for test in known:
            if ASAN_CANARY in test.name:
                continue
            if CANARY_FAILS in test.name and not canaries:
                continue
            if (must_only and test.parts) or "usbsid" in test.parts:    # no build of the tests has usbsid (SPEC-115)
                results.append(Result(test.name, SKIPPED, "needs the part " + ", ".join(test.parts), where))
            elif test.name in seen:
                results.append(Result(test.name, *seen[test.name], where))
            else:
                results.append(Result(test.name, FAILED, "the test did not run: " + tail(process.stdout, 5), where))
        if not junit.exists():
            results.append(Result("the unit test program", FAILED, tail(process.stdout, 20), where))
    return results


def asan_canary(program, where):
    """TF-022: a read past an array's end, which only the sanitizer build notices, by ending the program."""
    process = run([program, f"--test-case=*{ASAN_CANARY}*", "--no-colors=true"], timeout=120)
    name = next((t.name for t in sources.cpp_tests(["tests/framework"]) if ASAN_CANARY in t.name), ASAN_CANARY)
    if process.returncode != 0 and "AddressSanitizer" in process.stdout:
        return Result(name, FAILED, "AddressSanitizer: " + next(
            (line.strip() for line in process.stdout.split("\n") if "ERROR: AddressSanitizer" in line), ""), where)
    return Result(name, PASSED, "", where)


class _Collector(unittest.TestResult):
    def __init__(self):
        super().__init__()
        self.outcomes = []

    @staticmethod
    def _name(test):
        return test.shortDescription() or test.id()

    def _message(self, test, err):
        """The traceback's last line, as before, and the exception's own first line, which says which check
        failed when an assertion's message (a program's output, say) runs over several lines."""
        last = self._exc_info_to_string(err, test).strip().split("\n")[-1]
        first = f"{err[0].__name__}: " + (str(err[1]).strip().split("\n") or [""])[0]
        return last if first in last or last in first else f"{last} [{first[:300]}]"

    def addFailure(self, test, err):
        self.outcomes.append((self._name(test), FAILED, self._message(test, err)))

    def addError(self, test, err):
        self.outcomes.append((self._name(test), FAILED, self._message(test, err)))

    def addSkip(self, test, reason):
        self.outcomes.append((self._name(test), SKIPPED, reason))

    def addSubTest(self, test, subtest, err):
        # A failure inside self.subTest(...) belongs to the test it is part of; its success says nothing,
        # since addSuccess still follows for a test whose parts all passed, and addFailure does not.
        if err is not None:
            name = self._name(test)
            message = self._exc_info_to_string(err, subtest).strip().split("\n")[-1]
            self.outcomes = [o for o in self.outcomes if not (o[0] == name and o[1] == PASSED)]
            if not any(o[0] == name and o[1] == FAILED for o in self.outcomes):
                self.outcomes.append((name, FAILED, f"{subtest._subDescription().strip()}: {message}"))

    def addSuccess(self, test):
        name = self._name(test)
        if not any(o[0] == name and o[1] == FAILED for o in self.outcomes):
            self.outcomes.append((name, PASSED, ""))


def python_results(folder, canaries, env, where="", must_only=False):
    """Runs the unittest files of a folder in this process. A test's name is its docstring's first line."""
    old = dict(os.environ)
    os.environ.update(env)
    try:
        suite = unittest.TestSuite()
        for extra in (str(ROOT / folder), str(ROOT / "tools")):   # the tests' own helpers, and the tools
            if extra not in sys.path:
                sys.path.insert(0, extra)
        # Helpers the tests import (such as test_keys_support) are read afresh for each run: the simulator
        # level runs a folder's tests once for each build, and a helper reads its build when it is imported.
        for name, module in list(sys.modules.items()):
            if str(getattr(module, "__file__", "") or "").startswith(str(ROOT / folder) + os.sep):
                del sys.modules[name]
        for path in sorted((ROOT / folder).glob("test_*.py")):
            spec = importlib.util.spec_from_file_location(f"spz_{folder.replace('/', '_')}_{path.stem}", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            suite.addTests(unittest.defaultTestLoader.loadTestsFromModule(module))
        wanted = unittest.TestSuite()

        def walk(item):
            if isinstance(item, unittest.TestSuite):
                for child in item:
                    walk(child)
            elif canaries or CANARY_FAILS not in (item.shortDescription() or ""):
                wanted.addTest(item)

        walk(suite)
        collector = _Collector()
        stream, sys.stdout = sys.stdout, io.StringIO()
        try:
            wanted.run(collector)
        finally:
            sys.stdout = stream
        results = []
        for name, outcome, message in collector.outcomes:
            parts = sources.parts_of(name)
            # A test of a part runs only in a build that has it (SPEC-115): none has usbsid, and the Must-only
            # build has no part at all.
            if parts and (must_only or "usbsid" in parts):
                results.append(Result(name, SKIPPED, "needs the part " + ", ".join(parts), where))
            else:
                results.append(Result(name, outcome, message, where))
        return results
    finally:
        os.environ.clear()
        os.environ.update(old)


def level():
    needs("cmake", "ninja", "c++", "python3", "git")
    canaries = canaries_wanted()
    known = sources.cpp_tests(["tests/unit", "tests/framework"])
    results = []
    programs = {}
    run([sys.executable, ROOT / "tools" / "make_fixtures.py"], check=True)
    for folder, where, options in VARIANTS:
        with Timer(f"unit tests{': ' + where if where else ''}"):
            build = configure_and_build(BUILD / folder, {"SIDPLAYZERO_BUILD_APP": "OFF",
                                                         "SIDPLAYZERO_BUILD_UNIT_TESTS": "ON", **options},
                                        targets=["spz_unit_tests"])
            program = build / "tests" / "unit" / "spz_unit_tests"
            programs[folder] = program
            results += doctest_results(program, where, known, canaries, must_only=folder == "unit-must")
            if folder == "unit-asan" and canaries:
                results.append(asan_canary(program, where))
    with Timer("the framework's own tests, and the tests of the checker"):
        env = {"SPZ_UNIT_BUILD": str(BUILD / "unit")}
        results += python_results("tests/framework", canaries, env)
        results += python_results("tests/unit", canaries, env)     # unit-level tests that are easier in Python
    with Timer("tools/check_repo.py"):
        process = run([sys.executable, ROOT / "tools" / "check_repo.py"])
        results.append(Result("the repository's checks (tools/check_repo.py)",
                              PASSED if process.returncode == 0 else FAILED, tail(process.stdout, 12)))
    return results
