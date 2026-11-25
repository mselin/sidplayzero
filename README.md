# SidPlayZero

A SID player for the [M5Stack CardputerZero](https://docs.m5stack.com/en/CardputerZero), a pocket Linux
computer with a 320×170 screen and a keyboard. It will browse a collection of Commodore 64 music on the
device, normally the [High Voltage SID Collection](https://hvsc.c64.org), and play the tunes with the
[sidplayfp](https://github.com/libsidplayfp/libsidplayfp) engine, all from the device's own keys.

**There is nothing to install yet.** The requirements and the specification are approved, and the test
framework is built. What the program does so far is draw one screen: it is a stub that exists so that every
level of tests can run before the app is written.

## How it is being built

The app is built from its documents, in five phases. Each phase ends at a gate, and the next does not start
until that gate has passed.

| # | Phase | What it produces | State |
|---|---|---|---|
| 1 | Requirements | [`docs/requirements.md`](docs/requirements.md) | Approved 25 November 2026 |
| 2 | Specification | [`docs/spec.md`](docs/spec.md) and an independent review of it, [`docs/spec-review.md`](docs/spec-review.md) | Approved 25 November 2026, after independent review |
| 3 | Test framework | [`docs/test-framework-tasks.md`](docs/test-framework-tasks.md), then the harness for the levels of tests | Built; the device level's proof awaits a run on the device |
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
| [`tools/test`](tools/test) | One command for each level of tests |
| [`tools/make_fixtures.py`](tools/make_fixtures.py) | Makes the tests' inputs: original tunes of a few bytes each, and documents in HVSC's forms |
| [`tools/device-run.sh`](tools/device-run.sh), [`docs/device-checklist.md`](docs/device-checklist.md) | The device level: one script and a checklist, for one sitting at the device |
| [`docs/test-framework-tasks.md`](docs/test-framework-tasks.md) | What the test framework is made of, and how its gate is shown |
| `src/`, `tests/`, `cmake/`, `packaging/` | The stub program, the tests and the fakes, the build, and what goes into the package |
| [`.github/rulesets/`](.github/rulesets) | The branch and tag protection of this repository, as files that GitHub can import |

## Tests

One command for each level (`docs/spec.md`, SPEC-250). Each ends with status 0 only if everything it ran was
in order, names what was not, and stops with a list if a program it needs is not installed.

```bash
tools/test unit
```

```bash
tools/test simulator
```

```bash
tools/test integration
```

```bash
tools/test fuzz
```

```bash
tools/test package
```

`tools/test host` runs the unit, integration, fuzz and package levels in turn. `tools/test device` builds the
package and says what to copy to a CardputerZero and what to run there.

| Level | What it runs | Needs, on Debian or Ubuntu, besides `cmake` 3.31 or newer, `ninja-build`, `g++`, `python3` and `git` |
|---|---|---|
| unit | `core` against fakes, in three builds; the framework's own tests; the repository's checks | Nothing more |
| simulator | The program drawing its screens with no display, driven by scripts | `libsdl2-dev`, `xvfb` |
| integration | The pinned engine and reference player, the real program, a local server, a real `7z`, ThreadSanitizer | `libsdl2-dev`, `xvfb`, `7zip`, `pkg-config`, `make`, `curl`, `openssl` |
| fuzz | The fuzz targets, 15 s each | `clang`, `libclang-rt-dev` |
| package | The `.deb` for the device and every check on it, with an install in an emulated Debian arm64 container | `g++-aarch64-linux-gnu`, `qemu-user-static` (`qemu-user-binfmt` on newer releases), `docker`, `pkg-config`, `make`, `curl` |

Each level has a test that is built to fail, left out of a normal run. `tools/test prove` runs each level
with that test put in, and succeeds only if exactly that test is reported:

```bash
tools/test prove
```

Tests are written before the code they test. A test that cannot pass yet is listed in
[`tests/waiting.tsv`](tests/waiting.tsv) with the failure expected of it; it counts as in order while it
fails that way, and as a fault as soon as it passes or fails differently.

The build fetches a few things from outside, each pinned by version and checksum in
[`cmake/pins.cmake`](cmake/pins.cmake), into `.cache/`. The package level's first run fetches the device's
sysroot, 435 MB.

## The repository's own checks

```bash
python3 tools/check_repo.py
```

This checks that the requirements, the specification and the findings are well formed (every id used once and
in order, every requirement with a reason, a priority and an acceptance criterion, every specification item
with what it covers, its priority, how it is tested and at which level, every reference pointing at
something that exists), that every requirement is covered by the specification, that the key map is
consistent, that every test names items and parts that exist, that the shell scripts parse, that the
workflow has no job outside its one required check, that nothing names the store's login or publish command,
and that no SID tunes, C64 ROMs, private network addresses, home folder paths or private terms are in the
repository. `python3 tools/check_repo.py --matrix` prints which items cover each requirement. [`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs it on every pull request.

The check also reads an optional `PRIVATE_TERMS` repository secret: host names and the like, one per line,
that must never appear here. A match is reported by file and line only, never by the term.

## No music in this repository

SidPlayZero does not contain HVSC, any other tunes, or the Commodore 64's ROMs, and never will. The tunes in
HVSC are the copyright of their authors. The app downloads the collection to the device when its user asks
for it.

## Licence

SidPlayZero is free software, licensed under the GNU General Public License, version 3 or later. The full
text is in [`LICENSE`](LICENSE).
