# SPDX-License-Identifier: GPL-3.0-or-later
"""What every test level shares: results, the waiting list, running programs, and the report.

A level is a function that returns a list of Result. tools/test judges the list against the waiting
list (SPEC-256, TF-012) and prints what was not in order.
"""

import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
CACHE = ROOT / ".cache"

# A test whose name holds this is built to fail (TF-014). It is left out unless canaries are asked for.
CANARY_FAILS = "[canary-fails"

# The words of the three outcomes a test can have.
PASSED, FAILED, SKIPPED = "passed", "failed", "skipped"


def canaries_wanted():
    """True when the failing canaries are to be run too: tools/test prove sets this."""
    return os.environ.get("SIDPLAYZERO_CANARIES") == "1"


@dataclass
class Result:
    name: str               # the test's whole name, as the waiting list would give it
    outcome: str            # PASSED, FAILED or SKIPPED
    message: str = ""       # why it failed, or why it was skipped
    where: str = ""         # the build or run it belongs to, such as "address sanitizer"

    def label(self):
        return f"{self.name} ({self.where})" if self.where else self.name


class Missing(Exception):
    """Programs a level needs are not installed."""

    def __init__(self, programs):
        super().__init__(", ".join(programs))
        self.programs = programs


def needs(*programs):
    """Stops the level with the list of what is missing (SPEC-250)."""
    missing = [p for p in programs if shutil.which(p) is None]
    if missing:
        raise Missing(missing)


def run(command, cwd=None, env=None, timeout=1800, check=False):
    """Runs a program and returns the finished process, with its output as text."""
    merged = dict(os.environ)
    if env:
        merged.update(env)
    process = subprocess.run([str(c) for c in command], cwd=cwd or ROOT, env=merged, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, errors="replace")
    if check and process.returncode != 0:
        raise RuntimeError(f"{' '.join(str(c) for c in command)} ended with status {process.returncode}:\n"
                           + tail(process.stdout, 40))
    return process


def tail(text, lines=15):
    return "\n".join(text.rstrip().split("\n")[-lines:])


def say(text):
    print(text, flush=True)


@dataclass
class Waiting:
    """One line of tests/waiting.tsv."""
    name: str
    reason: str
    words: str
    line: int


def load_waiting(path=None):
    """Reads the waiting list. Returns ({test name: Waiting}, [faults in the file itself])."""
    path = Path(path) if path else ROOT / "tests" / "waiting.tsv"
    entries, faults = {}, []
    if not path.exists():
        return entries, faults
    header = False
    for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if not header:
            header = True
            if cells != ["test", "reason", "words"]:
                faults.append(f"{path.name}:{number}: the first row must be: test, reason, words")
            continue
        if len(cells) != 3 or not all(c.strip() for c in cells):
            faults.append(f"{path.name}:{number}: a row is a test, a reason and words, separated by tabs")
            continue
        name, reason, words = cells
        if reason not in ("code", "images"):
            faults.append(f"{path.name}:{number}: the reason is 'code' or 'images'")
        if name in entries:
            faults.append(f"{path.name}:{number}: {name} is listed twice")
        entries[name] = Waiting(name, reason, words, number)
    return entries, faults


@dataclass
class Verdict:
    passed: int = 0
    waiting: int = 0
    skipped: int = 0
    faults: list = field(default_factory=list)   # (label, why)


def judge(results, waiting):
    """Applies the waiting list: a listed test is in order while it fails with the expected words."""
    verdict = Verdict()
    for result in results:
        listed = waiting.get(result.name)
        if result.outcome == SKIPPED:
            verdict.skipped += 1
        elif result.outcome == PASSED:
            if listed:
                verdict.faults.append((result.label(), "passes, but is on the waiting list: take it off"))
            else:
                verdict.passed += 1
        elif listed and listed.words in result.message:
            verdict.waiting += 1
        elif listed:
            verdict.faults.append((result.label(), f"on the waiting list for '{listed.words}', but failed with: "
                                   + first_line(result.message)))
        else:
            verdict.faults.append((result.label(), first_line(result.message) or "failed"))
    return verdict


def first_line(text):
    """The line of a failure that says the most: "not built yet: ..." if it is there, else the first line
    that is not just a place in a file."""
    lines = [line.strip() for line in text.strip().split("\n") if line.strip()]
    for line in lines:
        if "not built yet" in line:
            return line[line.index("not built yet"):].rstrip('"')[:300]
    for line in lines:
        if not re.fullmatch(r"\S+:\d+:", line):
            return line[:300]
    return lines[0][:300] if lines else ""


def proposed_words(message):
    """What a new line of the waiting list would give as the words of a failure: the stub that was
    reached, or for a script the line that did not hold."""
    if "not built yet" in message:
        return "not built yet"
    for line in message.split("\n"):
        if line.startswith("line: "):
            return line[len("line: "):].strip()
    return first_line(message)


def report(level, results, waiting, waiting_faults=()):
    """Prints the level's outcome and returns its exit status: 0 only if everything was in order."""
    verdict = judge(results, waiting)
    for fault in waiting_faults:
        verdict.faults.append(("tests/waiting.tsv", fault))
    say(f"== {level}: {len(results)} tests: {verdict.passed} passed, {verdict.waiting} waiting as expected, "
        f"{verdict.skipped} skipped, {len(verdict.faults)} not in order")
    for result in results:
        if result.outcome == SKIPPED:
            say(f"   skipped: {result.label()}: {result.message}")
    propose = os.environ.get("SIDPLAYZERO_PROPOSE_WAITING")
    if propose:
        # For whoever keeps tests/waiting.tsv: every test that failed and is not listed, as lines to look
        # at and, if the reason is the right one, to add.
        seen = set()
        with open(propose, "a", encoding="utf-8") as out:
            for result in results:
                if result.outcome == FAILED and result.name not in waiting and result.name not in seen \
                        and CANARY_FAILS not in result.name:
                    seen.add(result.name)
                    out.write(f"{result.name}\tcode\t{proposed_words(result.message)}\n")
    if verdict.faults:
        say(f"NOT IN ORDER ({len(verdict.faults)}):")
        for label, why in verdict.faults:
            say(f"  {level}: {label} :: {why}")
        return 1
    return 0


class Timer:
    """Says how long a step took."""

    def __init__(self, what):
        self.what = what

    def __enter__(self):
        self.start = time.monotonic()
        say(f"-- {self.what}")
        return self

    def __exit__(self, *_):
        say(f"   ({time.monotonic() - self.start:.0f} s)")


def configure_and_build(build_dir, options, targets=(), env=None):
    """Configures (if needed) and builds one build folder. Raises with the tail of the output on failure."""
    build_dir = Path(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    wanted = [f"-D{key}={value}" for key, value in sorted(options.items())]
    stamp = build_dir / ".spz-options"
    if not (build_dir / "build.ninja").exists() or not stamp.exists() or stamp.read_text() != "\n".join(wanted):
        run(["cmake", "-S", ROOT, "-B", build_dir, "-G", "Ninja", *wanted], env=env, check=True)
        stamp.write_text("\n".join(wanted))
    command = ["cmake", "--build", build_dir]
    for target in targets:
        command += ["--target", target]
    run(command, env=env, check=True)
    return build_dir
