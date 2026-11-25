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
  * docs/keymap.tsv, which is part of the specification (SPEC-094): six columns; one row for a place
    and key; a label on the keys 4 to 8 and on no other, at most seven characters long; key names,
    places and priorities that the specification has; no place changing a key that works everywhere.
  * docs/test-framework-tasks.md: TF ids are used once and in order, and every task says what it
    implements and how it is shown to work.
  * Tests (TF-011): every test names the SPEC or TF items it checks, and they exist; the parts it
    names are parts of SPEC-115.
  * No music (SPEC-153): no SID tune outside the generated fixtures, and no C64 ROM.
  * The workflow (SPEC-257): every job is one that "All checks passed" waits for, and the only secret
    used is PRIVATE_TERMS.
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

import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

# The tests of this checker (tests/framework/test_checker.py) point it at a copy with one fault in it.
_OTHER_ROOT = os.environ.get("SIDPLAYZERO_CHECK_ROOT")
ROOT = Path(_OTHER_ROOT) if _OTHER_ROOT else Path(
    subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True, cwd=Path(__file__).resolve().parent).strip())
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
KEYMAP_COLUMNS = ["where", "key", "action", "label", "part", "help"]
PARTS = set(  # the parts that may be left out (SPEC-115)
    "detail mini modes shuffle restart skip voices bars notes rate force roms too-slow brightness screenshot "
    "playlists import-more export filters resume screen-off releases download-all usbsid".split()
)
KEY_NAMES = set(
    "Esc Tab Enter Backspace Space Shift Up Down Left Right Home End PageUp PageDown Delete PlayPause Previous Next "
    "Mute VolumeDown VolumeUp BrightnessDown BrightnessUp Help PrintScreen EscHold RightHold Char Any".split()
) | set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
KEYMAP_GROUPS = ("always", "lists", "screens")
KEYMAP_SCREENS = set(  # screens and parts of screens: the "screens" keys work here
    "browser empty playing search-results playlists playlist settings hvsc about selftest".split()
)
KEYMAP_PANELS = set(  # panels and help: the "lists" keys work here, the "screens" keys do not
    "confirm pick-playlist more-playing more-playlist import filter text notice-panel help".split()
)
TYPING_PLACES = {"search-field", "prompt"}  # only the "always" keys work while text is typed
ALONE_PLACES = {"key-step"}  # takes every key itself: no group applies (SPEC-260)
KEYMAP_PLACES = set(KEYMAP_GROUPS) | KEYMAP_SCREENS | KEYMAP_PANELS | TYPING_PLACES | ALONE_PLACES
LABEL_KEYS = set("45678")
LABEL_MAX = 7
MAY_REPLACE_LISTS = {("playing", "Up"), ("playing", "Down")}  # SPEC-094: they step through tunes there

errors = []


def fail(path, line, message):
    errors.append(f"{path}:{line}: {message}" if line else f"{path}: {message}")


def tracked_files():
    if _OTHER_ROOT:
        return sorted(str(p.relative_to(ROOT)) for p in ROOT.rglob("*") if p.is_file())
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
        where, key, action, label, part, _help = cells
        rows += 1
        if where not in KEYMAP_PLACES:
            fail(rel, n, f"'{where}' is not a screen or panel of the specification")
        if key not in KEY_NAMES:
            fail(rel, n, f"'{key}' is not a key name of SPEC-111")
        if not re.fullmatch(r"[a-z][a-z0-9_]*", action):
            fail(rel, n, "an action is lower-case words joined by underscores")
        if part != "-" and any(name not in PARTS for name in part.split("|")):
            fail(rel, n, "part must be '-' or parts of SPEC-115, joined by '|'")
        if label != "-" and key not in LABEL_KEYS:
            fail(rel, n, "only the keys 4 to 8 have a label")
        if label == "-" and key in LABEL_KEYS and where not in KEYMAP_GROUPS:
            fail(rel, n, "the keys 4 to 8 need a label where they do something")
        if label != "-" and len(label) > LABEL_MAX:
            fail(rel, n, f"a label is at most {LABEL_MAX} characters")
        if (where, key) in seen:
            fail(rel, n, f"{where} already has {key}, on line {seen[(where, key)]}")
        seen[(where, key)] = n
    actions = {}
    for n, line in enumerate(text.split("\n"), 1):
        cells = line.split("\t")
        if len(cells) == len(KEYMAP_COLUMNS) and cells != KEYMAP_COLUMNS and not line.startswith("#"):
            actions[(cells[0], cells[1])] = cells[2]
    for (where, key), n in seen.items():
        action = actions.get((where, key))
        if where in KEYMAP_GROUPS:
            if any((g, key) in seen for g in KEYMAP_GROUPS if g != where):
                fail(rel, n, f"{key} is in more than one of the groups {', '.join(KEYMAP_GROUPS)}")
            if action == "nothing":
                fail(rel, n, "'nothing' is for switching a 'screens' key off in one place")
            continue
        if where in ALONE_PLACES:
            continue
        if ("always", key) in seen:
            fail(rel, n, f"{key} works the same everywhere (line {seen[('always', key)]}); {where} cannot change it")
        if ("lists", key) in seen and where not in TYPING_PLACES and (where, key) not in MAY_REPLACE_LISTS:
            fail(rel, n, f"{key} is a 'lists' key (line {seen[('lists', key)]}); {where} cannot change it")
        if ("screens", key) in seen and where in KEYMAP_SCREENS and action != "nothing":
            fail(rel, n, f"{key} is a 'screens' key (line {seen[('screens', key)]}); {where} can only switch it off")
        if action == "nothing" and not (("screens", key) in seen and where in KEYMAP_SCREENS):
            fail(rel, n, "'nothing' is for switching a 'screens' key off on one screen")
    if not header:
        fail(rel, 0, "no rows")
    return rows


def check_tasks(specs, rel="docs/test-framework-tasks.md"):
    """The checks on the test-framework tasks. Returns the TF ids it defines."""
    text = read_text(rel)
    if text is None:
        return set()  # there are no tasks before Phase 3
    blocks = re.split(r"(?m)^(?=\*\*TF-\d+ )", text)[1:]
    ids = [re.match(r"\*\*TF-(\d+)", b).group(1) for b in blocks]
    for i in sorted({i for i in ids if ids.count(i) > 1}):
        fail(rel, 0, f"TF-{i} is defined more than once")
    if not in_order(ids):
        fail(rel, 0, "TF ids are not in ascending order")
    for tid, block in zip(ids, blocks):
        body = re.split(r"(?m)^#{2,3} ", block)[0]
        for line in ("Implements", "Shown by"):
            if body.count(f"\n- {line}:") != 1:
                fail(rel, 0, f"TF-{tid} needs one '{line}' line")
    for n, line in enumerate(text.split("\n"), 1):
        for i in re.findall(r"SPEC-(\d+)", line):
            if i not in specs:
                fail(rel, n, f"SPEC-{i} is not defined in spec.md")
        for i in re.findall(r"TF-(\d+)", line):
            if i not in ids:
                fail(rel, n, f"TF-{i} is not defined")
    return set(ids)


# Where tests live, and how a test's name is found in each kind of file (TF-011).
CPP_TEST = re.compile(r'TEST_CASE\(\s*"((?:[^"\\]|\\.)*)"')
PY_TEST = re.compile(r'(?m)^\s+def test\w*\(self[^)]*\):\s*\n\s+(?:r?"""|\'\'\')(.*)')
TEST_ITEM = re.compile(r"\b(SPEC|TF)-(\d{3})\b")
TEST_PARTS = re.compile(r"\[parts:\s*([^\]]+)\]")


def tests_in(rel, text):
    """The tests of one file, as (line number, name, parts)."""
    found = []
    if rel.endswith("_fuzz.cpp"):
        for n, line in enumerate(text.split("\n"), 1):
            if line.startswith("// test: "):
                found.append((n, line[len("// test: "):].strip(), []))
                break
    elif rel.endswith(".cpp"):
        for n, line in enumerate(text.split("\n"), 1):
            for name in CPP_TEST.findall(line):
                parts = TEST_PARTS.search(name)
                found.append((n, name, parts.group(1).replace(",", " ").split() if parts else []))
    elif rel.endswith(".py") and Path(rel).name.startswith("test_"):
        for match in PY_TEST.finditer(text):
            name = match.group(1).strip().rstrip('"').rstrip("'").strip()
            parts = TEST_PARTS.search(name)
            found.append((text.count("\n", 0, match.start()) + 2, name,
                          parts.group(1).replace(",", " ").split() if parts else []))
    elif rel.endswith((".script", ".test.sh")):
        head, parts = [], []
        for line in text.split("\n"):
            if not line.startswith("#"):
                break
            body = line.lstrip("#").strip()
            if body.startswith("parts:"):
                parts += body[len("parts:"):].replace(",", " ").split()
            else:
                head.append(body)
        found.append((1, " ".join(head), parts))
    return found


def check_tests(files, specs, tasks):
    """Every test names items that exist and parts that exist. Returns how many tests were read."""
    count = 0
    for rel in files:
        if not rel.startswith("tests/") or rel.startswith("tests/fixtures/"):
            continue
        text = read_text(rel)
        if text is None:
            continue
        for n, name, parts in tests_in(rel, text):
            count += 1
            items = TEST_ITEM.findall(name)
            if not items:
                fail(rel, n, "a test names the SPEC or TF items it checks")
            for kind, number in items:
                if number not in (specs if kind == "SPEC" else tasks):
                    fail(rel, n, f"{kind}-{number} is not defined")
            for part in parts:
                if part not in PARTS:
                    fail(rel, n, f"'{part}' is not a part of SPEC-115")
    return count


# SPEC-153. The ROMs are known by their SHA-1, as published in MAME's source (src/mame/commodore/c64.cpp).
C64_ROMS = {
    "1d503e56df85a62fee696e7618dc5b4e781df1bb": "KERNAL 901227-03",
    "0e2e4ee3f2d41f00bed72f9ab588b83e306fdb13": "KERNAL 901227-02",
    "87cc04d61fc748b82df09856847bb5c2754a2033": "KERNAL 901227-01",
    "79015323128650c742a3694c9429aa91f355905e": "BASIC 901226-01",
    "adc7c31e18c7c7413d54802ef2f4193da14711aa": "character ROM 901225-01",
}


def music_fault(data, rel, manifest):
    """Why these bytes may not be here: a SID tune that is not a listed fixture, or a C64 ROM. Else None."""
    if data[:4] in (b"PSID", b"RSID") and rel not in manifest:
        return "a SID tune that is not one of the generated fixtures (SPEC-153)"
    if len(data) in (8192, 4096):
        rom = C64_ROMS.get(hashlib.sha1(data).hexdigest())
        if rom:
            return f"a Commodore 64 ROM, {rom} (SPEC-153)"
    return None


def check_music(files):
    manifest_file = ROOT / "tests" / "fixtures" / "MANIFEST"
    manifest = set()
    if manifest_file.is_file():
        manifest = {"tests/fixtures/" + line.split("\t")[0] for line in manifest_file.read_text().split("\n") if line}
    for rel in files:
        try:
            data = (ROOT / rel).read_bytes()
        except OSError:
            continue
        why = music_fault(data, rel, manifest)
        if why:
            fail(rel, 0, why)


def check_workflow(rel=".github/workflows/ci.yml"):
    """SPEC-257: nothing runs beside the one required job, and no secret but PRIVATE_TERMS is used."""
    text = read_text(rel)
    if text is None:
        return
    jobs, in_jobs = [], False
    for line in text.split("\n"):
        if re.match(r"^jobs:\s*$", line):
            in_jobs = True
        elif in_jobs and re.match(r"^\S", line):
            in_jobs = False
        elif in_jobs:
            job = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
            if job:
                jobs.append(job.group(1))
    gate = [j for j in jobs if re.search(rf"(?m)^  {re.escape(j)}:\s*\n(?:    .*\n|\s*\n)*?    name: All checks passed\s*$", text)]
    if len(gate) != 1:
        fail(rel, 0, 'one job must be named "All checks passed"')
        return
    block = re.search(rf"(?ms)^  {re.escape(gate[0])}:\s*\n(.*?)(?=^  \S|\Z)", text).group(1)
    needs = re.search(r"(?m)^    needs:\s*\[([^\]]*)\]", block)
    waited = [n.strip() for n in needs.group(1).split(",")] if needs else []
    for job in jobs:
        if job != gate[0] and job not in waited:
            fail(rel, 0, f'the job "{job}" is not one that "All checks passed" waits for')
    for n, line in enumerate(text.split("\n"), 1):
        for secret in re.findall(r"secrets\.([A-Za-z0-9_]+)", line):
            if secret != "PRIVATE_TERMS":
                fail(rel, n, f"the workflow uses the secret {secret}; only PRIVATE_TERMS is allowed (SPEC-257)")


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
    specs = set(re.findall(r"(?m)^\*\*SPEC-(\d+) ", read_text("docs/spec.md") or ""))
    tasks = check_tasks(specs)
    tests = check_tests(files, specs, tasks)
    check_music(files)
    check_workflow()
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
    spec += f"{len(tasks)} framework tasks, {tests} tests, " if tasks else ""
    print(f"OK: {len(files)} files checked, {len(reqs)} requirements, {len(cons)} constraints, {spec}"
          f"PRIVATE_TERMS {'set (' + str(terms) + ' terms)' if terms else 'not set'}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
