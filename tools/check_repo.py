#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Repository checks, run by CI and by hand: python3 tools/check_repo.py

Checks every file tracked by git:
  * docs/requirements.md: REQ, CON and R ids are used once and in order; every requirement has a
    reason (Why), a priority and an acceptance criterion; every REQ-, CON- and R reference points at
    an id that is defined.
  * docs/findings.md: FIND ids are used once, and every finding has its five fields.
  * Shell scripts parse (bash -n).
  * No private network addresses (10.x, 172.16-31.x, 192.168.x).
  * None of the terms in the PRIVATE_TERMS environment variable (one per line): host names, user names
    and the like that must stay out of a public repository. Matches are reported by file and line only,
    never by the term, because the logs of a public repository are public.

Standard library only. Exits non-zero if anything fails.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip())
PRIORITIES = ("Must", "Should", "Could", "Won't")
FINDING_FIELDS = ("Phase", "Observed", "Concerns", "Resolution", "Status")
PRIVATE_IP = re.compile(r"(?<![\d.])(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}(?![\d.])")

errors = []


def fail(path, line, message):
    errors.append(f"{path}:{line}: {message}" if line else f"{path}: {message}")


def tracked_files():
    out = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT, text=True)
    return [p for p in out.split("\0") if p and (ROOT / p).is_file()]


def read_text(rel):
    data = (ROOT / rel).read_bytes()
    if b"\0" in data[:8192]:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def in_order(ids):
    return ids == sorted(ids, key=int)


def check_requirements(rel="docs/requirements.md"):
    """The checks on the requirements document. Returns the ids it defines, for other documents to refer to."""
    text = read_text(rel)
    if text is None:
        fail(rel, 0, "missing or not text")
        return set(), set()
    blocks = re.split(r"(?m)^(?=\*\*REQ-\d+ )", text)[1:]
    reqs = [re.match(r"\*\*REQ-(\d+)", b).group(1) for b in blocks]
    cons = re.findall(r"(?m)^\| CON-(\d+) \|", text)
    risks = re.findall(r"(?m)^\| R(\d+) \|", text)
    for name, ids in (("REQ", reqs), ("CON", cons), ("R", risks)):
        for i in sorted({i for i in ids if ids.count(i) > 1}):
            fail(rel, 0, f"{name}-{i} is defined more than once")
        if not in_order(ids):
            fail(rel, 0, f"{name} ids are not in ascending order")
    if not reqs:
        fail(rel, 0, "no requirements found")
    for rid, block in zip(reqs, blocks):
        body = re.split(r"(?m)^#{2,3} ", block)[0]
        positions = [body.find(f"\n- {field}:") for field in ("Why", "Priority", "Acceptance")]
        if min(positions) < 0:
            fail(rel, 0, f"REQ-{rid} needs a Why, a Priority and an Acceptance line")
        elif positions != sorted(positions):
            fail(rel, 0, f"REQ-{rid}: the lines go Why, Priority, Acceptance")
        priority = re.search(r"(?m)^- Priority: (.*)$", body)
        if priority and not priority.group(1).startswith(PRIORITIES):
            fail(rel, 0, f"REQ-{rid}: priority must start with one of {', '.join(PRIORITIES)}")
    for n, line in enumerate(text.split("\n"), 1):
        for i in re.findall(r"REQ-(\d+)", line):
            if i not in reqs:
                fail(rel, n, f"REQ-{i} is not defined")
        for i in re.findall(r"CON-(\d+)", line):
            if i not in cons:
                fail(rel, n, f"CON-{i} is not defined")
        for i in re.findall(r"(?<![\w-])R(\d+)(?![\w-])", line):
            if i not in risks:
                fail(rel, n, f"R{i} is not defined")
    return set(reqs), set(cons)


def check_findings(reqs, cons, rel="docs/findings.md"):
    text = read_text(rel)
    if text is None:
        fail(rel, 0, "missing or not text")
        return
    blocks = re.split(r"(?m)^(?=### FIND-\d+ )", text)[1:]
    ids = [re.match(r"### FIND-(\d+)", b).group(1) for b in blocks]
    for i in sorted({i for i in ids if ids.count(i) > 1}):
        fail(rel, 0, f"FIND-{i} is defined more than once")
    for fid, block in zip(ids, blocks):
        body = re.split(r"(?m)^## ", block)[0]
        for field in FINDING_FIELDS:
            if f"\n- {field}" not in body:
                fail(rel, 0, f"FIND-{fid} has no {field} line")
    for n, line in enumerate(text.split("\n"), 1):
        for i in re.findall(r"REQ-(\d+)", line):
            if i not in reqs:
                fail(rel, n, f"REQ-{i} is not defined in requirements.md")
        for i in re.findall(r"CON-(\d+)", line):
            if i not in cons:
                fail(rel, n, f"CON-{i} is not defined in requirements.md")


def check_shell(files):
    for rel in files:
        if rel.endswith(".sh"):
            result = subprocess.run(["bash", "-n", str(ROOT / rel)], capture_output=True, text=True)
            if result.returncode != 0:
                fail(rel, 0, "does not parse: " + result.stderr.strip().split("\n")[0])


def check_private(files):
    terms = [t.strip().lower() for t in os.environ.get("PRIVATE_TERMS", "").split("\n") if t.strip()]
    for rel in files:
        text = read_text(rel)
        if text is None:
            continue
        for n, line in enumerate(text.split("\n"), 1):
            if PRIVATE_IP.search(line) and "check_repo.py" not in rel:
                fail(rel, n, "private network address")
            low = line.lower()
            if any(t in low for t in terms):
                fail(rel, n, "private term")
    return len(terms)


def main():
    files = tracked_files()
    reqs, cons = check_requirements()
    check_findings(reqs, cons)
    check_shell(files)
    terms = check_private(files)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        print(f"\n{len(errors)} problem(s)", file=sys.stderr)
        return 1
    print(f"OK: {len(files)} files checked, {len(reqs)} requirements, {len(cons)} constraints, "
          f"PRIVATE_TERMS {'set (' + str(terms) + ' terms)' if terms else 'not set'}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
