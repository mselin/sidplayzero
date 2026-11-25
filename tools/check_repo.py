#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Repository checks, run by CI and by hand: python3 tools/check_repo.py

Checks every file tracked by git:
  * docs/requirements.md: REQ, CON and R ids are used once and in order; every requirement has a
    reason (Why), a priority and an acceptance criterion; every REQ-, CON- and R reference points at
    an id that is defined.
  * docs/findings.md: FIND ids are used once, and every finding has its five fields.
  * docs/spec.md: SPEC ids are used once and in order; every item has Covers, Priority, Oracle and
    Level lines; every requirement it names exists, and every requirement is covered by at least one
    item; every SPEC-, REQ-, CON-, FIND- and R reference points at something that is defined.
  * docs/keymap.tsv, which is part of the specification: six columns; one row for a place and key; a
    label only on the keys 4 to 8 and at most seven characters long; key names, places and priorities
    that the specification has.
  * Shell scripts parse (bash -n).
  * No private network addresses (10.x, 172.16-31.x, 192.168.x, and IPv6's unique-local and link-local
    ranges).
  * No path into one person's home folder (a named folder under /home or /Users, or anything under
    /root), except the folder "u" under /home, which the fixtures and examples use.
  * Nothing but a document names the store's login or publish command (nothing here publishes).
  * None of the terms in the PRIVATE_TERMS environment variable (one per line): host names, user names
    and the like that must stay out of a public repository, matched as whole words without regard to
    case. Matches are reported by file and line only, never by the term, because the logs of a public
    repository are public.

Standard library only. Exits non-zero if anything fails.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip())
PRIORITIES = ("Must", "Should", "Could", "Won't")
SPEC_PRIORITIES = ("Must", "Should", "Could")
SPEC_LINES = ("Covers", "Priority", "Oracle", "Level")
FINDING_FIELDS = ("Phase", "Observed", "Concerns", "Resolution", "Status")
LEVELS = ("host-unit", "host-integration", "host-package", "simulator", "device")
PRIVATE_IP = re.compile(r"(?<![\d.])(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}(?![\d.])")
PRIVATE_IP6 = re.compile(r"(?i)(?<![0-9a-z:])(?:f[cd][0-9a-f]{2}|fe[89ab][0-9a-f])(?::[0-9a-f]{0,4}){2,7}(?![0-9a-z:])")
HOME_PATH = re.compile(r"(?<![\w.$}])/(?:(?:home|Users)/(?!u/)[A-Za-z0-9._-]+/|root/[A-Za-z0-9._-]+)")
PUBLISHES = re.compile(r"czdev\s+(?:login|publish)")

# The key map (docs/keymap.tsv, SPEC-094). Key names are those of SPEC-111; places those of SPEC-090 and SPEC-114.
KEYMAP_COLUMNS = ["where", "key", "action", "label", "priority", "help"]
KEY_NAMES = set(
    "Esc Tab Enter Backspace Space Shift Up Down Left Right Home End PageUp PageDown Delete PlayPause Previous Next "
    "Mute VolumeDown VolumeUp BrightnessDown BrightnessUp Help PrintScreen EscHold RightHold Char".split()
) | set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
KEYMAP_PLACES = set(
    "always moving browser empty playing search-field search-results playlists playlist settings hvsc about "
    "confirm prompt pick-playlist more-playing more-playlist import filter text notice-panel help".split()
)
LABEL_KEYS = set("45678")
LABEL_MAX = 7
TYPING_PLACES = {"search-field", "prompt"}  # "moving" keys do not apply while text is typed
MAY_REPLACE_MOVING = {("playing", "Up"), ("playing", "Down")}  # SPEC-094

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
        return set(), set(), set()
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
    return set(reqs), set(cons), set(risks)


def check_spec(reqs, cons, risks, finds, rel="docs/spec.md"):
    """The checks on the specification. Returns {REQ id: [SPEC ids that cover it]}."""
    text = read_text(rel)
    if text is None:
        return None  # there is no specification before Phase 2
    blocks = re.split(r"(?m)^(?=\*\*SPEC-\d+ )", text)[1:]
    specs = [re.match(r"\*\*SPEC-(\d+)", b).group(1) for b in blocks]
    for i in sorted({i for i in specs if specs.count(i) > 1}):
        fail(rel, 0, f"SPEC-{i} is defined more than once")
    if not in_order(specs):
        fail(rel, 0, "SPEC ids are not in ascending order")
    covered = {}
    for sid, block in zip(specs, blocks):
        body = re.split(r"(?m)^#{2,3} ", block)[0]
        positions = [body.find(f"\n- {field}:") for field in SPEC_LINES]
        if min(positions) < 0 or any(body.count(f"\n- {field}:") != 1 for field in SPEC_LINES):
            fail(rel, 0, f"SPEC-{sid} needs one each of these lines: {', '.join(SPEC_LINES)}")
            continue
        if positions != sorted(positions):
            fail(rel, 0, f"SPEC-{sid}: the lines go {', '.join(SPEC_LINES)}")
        priority = re.search(r"(?m)^- Priority: (.*)$", body).group(1).strip()
        if priority not in SPEC_PRIORITIES:
            fail(rel, 0, f"SPEC-{sid}: Priority must be one of {', '.join(SPEC_PRIORITIES)}")
        covers = re.search(r"(?m)^- Covers: (.*)$", body).group(1)
        ids = re.findall(r"REQ-(\d+)", covers)
        if not ids or re.sub(r"REQ-\d+|[,\s]", "", covers):
            fail(rel, 0, f"SPEC-{sid}: Covers must be a list of REQ ids and nothing else")
        for i in ids:
            covered.setdefault(i, []).append(sid)
        level = re.search(r"(?m)^- Level: (.*)$", body).group(1)
        named = [x.strip() for x in level.split(",")]
        if not named or any(x not in LEVELS for x in named) or len(set(named)) != len(named):
            fail(rel, 0, f"SPEC-{sid}: Level must be one or more of {', '.join(LEVELS)}")
    for n, line in enumerate(text.split("\n"), 1):
        for i in re.findall(r"SPEC-(\d+)", line):
            if i not in specs:
                fail(rel, n, f"SPEC-{i} is not defined")
        for i in re.findall(r"REQ-(\d+)", line):
            if i not in reqs:
                fail(rel, n, f"REQ-{i} is not defined in requirements.md")
        for i in re.findall(r"CON-(\d+)", line):
            if i not in cons:
                fail(rel, n, f"CON-{i} is not defined in requirements.md")
        for i in re.findall(r"(?<![\w-])R(\d+)(?![\w-])", line):
            if i not in risks:
                fail(rel, n, f"R{i} is not defined in requirements.md")
        for i in re.findall(r"FIND-(\d+)", line):
            if i not in finds:
                fail(rel, n, f"FIND-{i} is not defined in findings.md")
    for i in sorted(reqs, key=int):
        if i not in covered:
            fail(rel, 0, f"REQ-{i} is not covered by any SPEC item")
    return covered


def check_findings(reqs, cons, rel="docs/findings.md"):
    text = read_text(rel)
    if text is None:
        fail(rel, 0, "missing or not text")
        return set()
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
        for i in re.findall(r"FIND-(\d+)", line):
            if i not in ids:
                fail(rel, n, f"FIND-{i} is not defined")
    return set(ids)


def check_keymap(rel="docs/keymap.tsv"):
    """The checks on the key map. Returns the number of rows, or None when there is no key map yet."""
    text = read_text(rel)
    if text is None:
        return None
    seen = {}
    header = False
    rows = 0
    for n, line in enumerate(text.split("\n"), 1):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if not header:
            header = True
            if cells != KEYMAP_COLUMNS:
                fail(rel, n, "the first row must name the columns: " + ", ".join(KEYMAP_COLUMNS))
            continue
        if len(cells) != len(KEYMAP_COLUMNS) or any(not c.strip() or c != c.strip() for c in cells):
            fail(rel, n, f"a row has {len(KEYMAP_COLUMNS)} cells, none empty, separated by single tabs")
            continue
        where, key, action, label, priority, _help = cells
        rows += 1
        if where not in KEYMAP_PLACES:
            fail(rel, n, f"'{where}' is not a screen or panel of the specification")
        if key not in KEY_NAMES:
            fail(rel, n, f"'{key}' is not a key name of SPEC-111")
        if not re.fullmatch(r"[a-z][a-z0-9_]*", action):
            fail(rel, n, "an action is lower-case words joined by underscores")
        if priority not in SPEC_PRIORITIES:
            fail(rel, n, f"priority must be one of {', '.join(SPEC_PRIORITIES)}")
        if label != "-" and key not in LABEL_KEYS:
            fail(rel, n, "only the keys 4 to 8 have a label")
        if label == "-" and key in LABEL_KEYS and where not in ("always", "moving"):
            fail(rel, n, "the keys 4 to 8 need a label where they do something")
        if label != "-" and len(label) > LABEL_MAX:
            fail(rel, n, f"a label is at most {LABEL_MAX} characters")
        if (where, key) in seen:
            fail(rel, n, f"{where} already has {key}, on line {seen[(where, key)]}")
        seen[(where, key)] = n
    for (where, key), n in seen.items():
        if where in ("always", "moving"):
            continue
        if ("always", key) in seen:
            fail(rel, n, f"{key} works the same everywhere (line {seen[('always', key)]}); {where} cannot change it")
        if ("moving", key) in seen and where not in TYPING_PLACES and (where, key) not in MAY_REPLACE_MOVING:
            fail(rel, n, f"{key} is a 'moving' key (line {seen[('moving', key)]}); {where} cannot change it")
    if ("always", "Esc") in seen and ("moving", "Esc") in seen:
        fail(rel, seen[("moving", "Esc")], "Esc is in 'always' already")
    if not header:
        fail(rel, 0, "no rows")
    return rows


def check_shell(files):
    for rel in files:
        if rel.endswith(".sh"):
            result = subprocess.run(["bash", "-n", str(ROOT / rel)], capture_output=True, text=True)
            if result.returncode != 0:
                fail(rel, 0, "does not parse: " + result.stderr.strip().split("\n")[0])


def check_private(files):
    terms = [t.strip() for t in os.environ.get("PRIVATE_TERMS", "").split("\n") if t.strip()]
    words = [re.compile(r"(?i)(?<!\w)" + re.escape(t) + r"(?!\w)") for t in terms]
    for rel in files:
        text = read_text(rel)
        if text is None:
            continue
        document = rel.endswith(".md")
        for n, line in enumerate(text.split("\n"), 1):
            if PRIVATE_IP.search(line) or PRIVATE_IP6.search(line):
                fail(rel, n, "private network address")
            if HOME_PATH.search(line):
                fail(rel, n, "a path into one person's home folder")
            if PUBLISHES.search(line) and not document and "check_repo.py" not in rel:
                fail(rel, n, "names the store's login or publish command; nothing here publishes (SPEC-226)")
            if any(w.search(line) for w in words):
                fail(rel, n, "private term")
    return len(terms)


def main():
    files = tracked_files()
    reqs, cons, risks = check_requirements()
    finds = check_findings(reqs, cons)
    covered = check_spec(reqs, cons, risks, finds)
    keys = check_keymap()
    if "--matrix" in sys.argv[1:] and covered is not None:
        for i in sorted(reqs, key=int):
            print(f"REQ-{i}: " + ", ".join(f"SPEC-{x}" for x in covered.get(i, [])))
    check_shell(files)
    terms = check_private(files)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        print(f"\n{len(errors)} problem(s)", file=sys.stderr)
        return 1
    spec = "" if covered is None else f"{sum(len(v) for v in covered.values())} coverage links from the specification, "
    spec += "" if keys is None else f"{keys} key map rows, "
    print(f"OK: {len(files)} files checked, {len(reqs)} requirements, {len(cons)} constraints, {spec}"
          f"PRIVATE_TERMS {'set (' + str(terms) + ' terms)' if terms else 'not set'}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
