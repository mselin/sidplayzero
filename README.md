# SidPlayZero

A SID player for the [M5Stack CardputerZero](https://docs.m5stack.com/en/CardputerZero), a pocket Linux
computer with a 320×170 screen and a keyboard. It will browse a collection of Commodore 64 music on the
device, normally the [High Voltage SID Collection](https://hvsc.c64.org), and play the tunes with the
[sidplayfp](https://github.com/libsidplayfp/libsidplayfp) engine, all from the device's own keys.

**There is nothing to install yet.** The requirements are approved, and the specification is in independent
review: two passes are done and the changes made after the second are being checked.

## How it is being built

The app is built from its documents, in five phases. Each phase ends at a gate, and the next does not start
until that gate has passed.

| # | Phase | What it produces | State |
|---|---|---|---|
| 1 | Requirements | [`docs/requirements.md`](docs/requirements.md) | Approved 25 November 2026; changes made since (see its first page) await approval |
| 2 | Specification | [`docs/spec.md`](docs/spec.md) and an independent review of it, [`docs/spec-review.md`](docs/spec-review.md) | In review: third pass under way |
| 3 | Test framework | The harness for the three levels of tests | Not started |
| 4 | Tests | `tests/`, each test tracing to the specification | Not started |
| 5 | Implementation | `src/` | Not started |

When a later phase finds that an earlier document is wrong or incomplete, the problem is not patched where it
was found. It is logged in [`docs/findings.md`](docs/findings.md), resolved in the earliest document that can
settle it, and carried forward from there.

## What is here

| Path | What it is |
|---|---|
| [`docs/requirements.md`](docs/requirements.md) | What the app has to do, and the facts about the device that this rests on |
| [`docs/spec.md`](docs/spec.md) | How the app does it: decisions, structure, screens and keys, states, limits, and how each item is tested |
| [`docs/keymap.tsv`](docs/keymap.tsv) | The key map: every key on every screen. It is part of the specification, and the app and its tests will be built from it |
| [`docs/spec-review.md`](docs/spec-review.md) | The independent review of the specification, and what was done about each point |
| [`docs/findings.md`](docs/findings.md) | Things that turned out differently from what an earlier document said |
| [`tools/device-probe.sh`](tools/device-probe.sh) | Collects facts about a CardputerZero that the requirements depend on. It changes nothing on the device |
| [`tools/engine-bench/`](tools/engine-bench) | A small program that times the two engines the app will ship, for the same visit to the device. `build.sh` builds it as a static arm64 program; the probe runs it with `--engine-bench` |
| [`tools/check_repo.py`](tools/check_repo.py) | The repository's own checks, run on every pull request |
| [`.github/rulesets/`](.github/rulesets) | The branch and tag protection of this repository, as files that GitHub can import |

## Checks

```bash
python3 tools/check_repo.py
```

This checks that the requirements, the specification and the findings are well formed (every id used once and
in order, every requirement with a reason, a priority and an acceptance criterion, every specification item
with what it covers, its priority, how it is tested and at which level, every reference pointing at
something that exists), that every requirement is covered by the specification, that the key map is
consistent, that the shell scripts parse, that nothing names the store's login or publish command, and that
no private network addresses, home folder paths or private terms are in the repository. `python3 tools/check_repo.py --matrix` prints which items
cover each requirement. [`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs it on every pull request.

The check also reads an optional `PRIVATE_TERMS` repository secret: host names and the like, one per line,
that must never appear here. A match is reported by file and line only, never by the term.

## No music in this repository

SidPlayZero does not contain HVSC, any other tunes, or the Commodore 64's ROMs, and never will. The tunes in
HVSC are the copyright of their authors. The app downloads the collection to the device when its user asks
for it.

## Licence

SidPlayZero is free software, licensed under the GNU General Public License, version 3 or later. The full
text is in [`LICENSE`](LICENSE).
