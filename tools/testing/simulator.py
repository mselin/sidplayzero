# SPDX-License-Identifier: GPL-3.0-or-later
"""The simulator level (SPEC-253 to SPEC-255; TF-040 to TF-046): the program drawing its real screens with
no display, driven by scripts.

A test is a script. Comment lines at its top give its name and what it needs:

    # SPEC-048 the empty screen names the collection folder      <- the name, with the items it checks
    # parts: playlists                                           <- parts it needs (SPEC-115)
    # fakes: fakes/setup                                         <- the --fakes folder, beside the script
    # fixture: collections/85/C64Music C64Music                  <- copies a folder or file of tests/fixtures
    #                                                               into the home folder before the start
    # args: --backend fake                                       <- more options for the program
    # expect-status: 1                                           <- for the framework's own scripts only

The level runs twice, with every part built and with only the Must parts, as the unit level does.
"""

import contextlib
import fcntl
import json
import os
import shlex
import shutil
import sys
from pathlib import Path

from . import png, screen, sources
from .common import (BUILD, CANARY_FAILS, FAILED, PASSED, ROOT, SKIPPED, Result, Timer, canaries_wanted,
                     configure_and_build, needs, run, tail)
from .unit import doctest_results, python_results

SCRIPT_FOLDERS = ["tests/simulator", "tests/framework/sim"]
GOLDEN = ROOT / "tests" / "golden"
FIXTURES = ROOT / "tests" / "fixtures"
TEST_ROOT = Path(os.environ.get("SIDPLAYZERO_TEST_ROOT", "/tmp/sidplayzero-test"))
RUN_BY_OTHER_TESTS = {"window.script"}   # needs a window: tests/framework/sim/test_simulator.py runs it
VARIANTS = [
    ("simulator", "", {}),
    ("simulator-must", "Must parts only", {"SIDPLAYZERO_PARTS": "must"}),
]


def header(path):
    """The directive lines of a script's header: fakes, args, expect-status."""
    found = {"fakes": "", "args": [], "status": 0, "fixtures": []}
    for line in Path(path).read_text(encoding="utf-8").split("\n"):
        if not line.startswith("#"):
            break
        text = line.lstrip("#").strip()
        if text.startswith("fakes:"):
            found["fakes"] = text[len("fakes:"):].strip()
        elif text.startswith("fixture:"):
            source, target = text[len("fixture:"):].split()
            found["fixtures"].append((source, target))
        elif text.startswith("args:"):
            found["args"] = shlex.split(text[len("args:"):])
        elif text.startswith("expect-status:"):
            found["status"] = int(text[len("expect-status:"):])
    return found


def home_environment(home):
    home = Path(home)
    return {
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(home / ".config"),
        "XDG_DATA_HOME": str(home / ".local" / "share"),
        "XDG_STATE_HOME": str(home / ".local" / "state"),
        "XDG_CACHE_HOME": str(home / ".cache"),
        "HVSC_BASE": str(home / "C64Music"),
        "SDL_AUDIODRIVER": "dummy",
    }


@contextlib.contextmanager
def new_home():
    """A new, empty home folder for one script. It is always the same path, so that what a script sees and
    draws does not differ from run to run or from machine to machine (SPEC-253); a lock keeps two runs on
    one machine from using it at once."""
    TEST_ROOT.mkdir(parents=True, exist_ok=True)
    with open(TEST_ROOT / ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        home = TEST_ROOT / "home"
        shutil.rmtree(home, ignore_errors=True)
        home.mkdir()
        try:
            yield str(home)
        finally:
            shutil.rmtree(home, ignore_errors=True)


def run_script(program, script, out, extra_args=(), prefix=(), timeout=180):
    """Runs one script in a new, empty home folder. Returns (status, output, the script's header)."""
    script = Path(script)
    options = header(script)
    with new_home() as home:
        fakes = script.parent / options["fakes"] if options["fakes"] else None
        if fakes and (fakes / "home").is_dir():
            shutil.copytree(fakes / "home", home, dirs_exist_ok=True)
        for source, target in options["fixtures"]:
            origin, place = FIXTURES / source, Path(home) / target
            place.parent.mkdir(parents=True, exist_ok=True)
            if origin.is_dir():
                shutil.copytree(origin, place, dirs_exist_ok=True)
            else:
                shutil.copy(origin, place)
        command = [*prefix, program, "--script", script, "--out", out]
        if "--window" not in extra_args:
            command.insert(len(prefix) + 1, "--headless")
        if fakes:
            command += ["--fakes", fakes]
        command += options["args"] + [a for a in extra_args if a != "--window"]
        process = run(command, env={**home_environment(home), "SPZ_FIXTURES": str(FIXTURES)}, timeout=timeout)
    return process.returncode, process.stdout, options


def judge_output(out, name):
    """Checks what a script left behind: every dump against SPEC-254, every picture against its reference.
    Returns (problems, notes)."""
    problems, notes = [], []
    for dump in sorted(Path(out).glob("*.json")):
        if dump.name == "failure.json":
            continue
        try:
            found = screen.problems(json.loads(dump.read_text(encoding="utf-8")))
        except ValueError as error:
            found = [f"not JSON: {error}"]
        problems += [f"{dump.name}: {p}" for p in found]
    for picture in sorted(Path(out).glob("*.png")):
        if picture.name in ("failure.png",) or picture.name.endswith(".diff.png"):
            continue
        reference = GOLDEN / picture.name
        if not reference.exists():
            notes.append(f"{picture.name}: no reference yet")
            continue
        different = png.compare(picture, reference, picture.with_suffix(".diff.png"))
        if different:
            problems.append(f"{picture.name}: {different} pixel(s) differ from tests/golden/{picture.name}")
    return problems, notes


def script_results(program, where, canaries, must_only, keep):
    results = []
    for folder in SCRIPT_FOLDERS:
        for test in sources.header_tests(folder, "*.script"):
            if CANARY_FAILS in test.name and not canaries:
                continue
            if Path(test.path).name in RUN_BY_OTHER_TESTS:
                continue
            if must_only and test.parts:
                results.append(Result(test.name, SKIPPED, "needs the part " + ", ".join(test.parts), where))
                continue
            out = keep / (where.replace(" ", "-") or "all") / Path(test.path).stem
            shutil.rmtree(out, ignore_errors=True)
            out.mkdir(parents=True)
            status, output, options = run_script(program, ROOT / test.path, out)
            problems, _notes = judge_output(out, test.name)
            if status != options["status"]:
                problems.insert(0, f"ended with status {status}, not {options['status']}: " + tail(output, 6))
            results.append(Result(test.name, FAILED if problems else PASSED, "\n".join(problems), where))
    return results


def level(variants=VARIANTS, extra_options=None, run_python=True):
    needs("cmake", "ninja", "c++", "cc", "python3", "git", "sdl2-config")
    canaries = canaries_wanted()
    run([sys.executable, ROOT / "tools" / "make_fixtures.py"], check=True)
    keep = BUILD / "simulator-out"
    results = []
    for folder, where, options in variants:
        with Timer(f"simulator tests{': ' + where if where else ''}"):
            build = configure_and_build(BUILD / folder, {
                "SIDPLAYZERO_TEST_HOOKS": "ON", "SIDPLAYZERO_DISPLAY": "window", "SIDPLAYZERO_BUILD_SIM_TESTS": "ON",
                **options, **(extra_options or {})})
            program = build / "src" / "sidplayzero"
            results += script_results(program, where, canaries, folder.endswith("-must"), keep)
            known = sources.cpp_tests(["tests/simulator", "tests/framework/sim"])
            results += doctest_results(build / "tests" / "simulator" / "spz_sim_tests", where, known, canaries,
                                       must_only=folder.endswith("-must"))
            if run_python and not where:
                results += python_results("tests/framework/sim", canaries, {"SPZ_SIM_BUILD": str(build)})
                results += python_results("tests/simulator", canaries, {"SPZ_SIM_BUILD": str(build)})
    return results
