# Working on SidPlayZero

How this project is worked on: the order things are done in, the rules that hold throughout, and how to run
the tests and checks. What the app is and how to use it is in the [README](README.md).

## Five phases, each with a gate

The app is built from its documents. Each phase ends at a gate, and the next does not start until that gate
has passed.

| # | Phase | What it produces | Gate | State |
|---|---|---|---|---|
| 1 | Requirements | [`docs/requirements.md`](docs/requirements.md) | The owner approves | Approved 25 November 2026 |
| 2 | Specification | [`docs/spec.md`](docs/spec.md), and an independent review of it, [`docs/spec-review.md`](docs/spec-review.md) | Every review item closed; then the owner approves | Approved 25 November 2026 |
| 3 | Test framework | [`docs/test-framework-tasks.md`](docs/test-framework-tasks.md), then the framework | Every level runs one passing and one deliberately failing test with one command | Built and merged 25 November 2026; the device level's proof is still owed |
| 4 | Tests | `tests/`, each test tracing to the specification | Every item has a test; the tests fail for the right reasons against stubs | Gate passed 26 November 2026: the tests merged, and FIND-016 to FIND-024 accepted by the owner and carried into the specification |
| 5 | Implementation | `src/` | All tests pass on the host and in the simulator; the device checklist passes | Built 26 November 2026: every host and simulator test passes but those that wait on the owner's decision on FIND-025; the screens' images approved 26 November 2026; code-quality and security reviews done and fixed. The device sitting is owed |

## Rules that hold throughout

- **Findings flow backwards.** When a later phase finds that an earlier document is wrong or incomplete, the
  problem is not patched where it was found. It is logged in [`docs/findings.md`](docs/findings.md), resolved
  in the earliest document that can settle it, and carried forward from there one phase at a time. A change
  to a requirement, or to what the specification has the app do, needs the owner's approval again.
- **Every change is a pull request.** Nothing is pushed to `main`. A pull request is merged only when its
  one required check, "All checks passed", is green.
- **Nothing is published by a script.** No workflow or script logs in to the CardputerZero store or publishes
  to it. That is done by hand, when the owner asks.
- **No music, and nothing private.** No tunes of HVSC or anyone else, no C64 ROMs, and no host name, user
  name or network address of anybody's device are ever in the repository or the package. The checks below
  enforce it.
- **The device needs its owner.** Anything that runs on the device is one script and a short checklist,
  done in one sitting with the owner present: [`docs/device-checklist.md`](docs/device-checklist.md).

## The tests

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

| Level | What it runs | Needs, on Debian or Ubuntu, besides `cmake` 3.31 or newer, `ninja-build`, `g++`, `python3`, `git`, `make` and `libcurl4-openssl-dev` |
|---|---|---|
| unit | `core` against fakes, in three builds; the framework's own tests; the repository's checks | Nothing more |
| simulator | The program drawing its screens with no display, driven by scripts | `libsdl2-dev`, `xvfb` |
| integration | The pinned engine and reference player, the real program, a local server, a real `7z`, ThreadSanitizer | `libsdl2-dev`, `xvfb`, `7zip`, `pkg-config`, `curl`, `openssl` |
| fuzz | The fuzz targets, 15 s each, the engine's among them | `clang`, `libclang-rt-dev` |
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

## What is where

| Path | What it is |
|---|---|
| `docs/` | The requirements, the specification and its key map, the review, the findings, the test-framework tasks, the device checklist |
| `src/core/` | Everything the app does that is not drawing and not the outside world. Uses the C++ library only |
| `src/ui/`, `src/platform/`, `src/app/` | Screens and drawing; the outside world (display, keyboard, and later the engine, sound and network); the program |
| `tests/unit/`, `tests/simulator/`, `tests/integration/`, `tests/fuzz/`, `tests/package/` | The tests of the specification's items, by level |
| `tests/framework/` | The test framework's own tests, and the tests built to fail |
| `tests/fakes/` | What the unit tests give `core` in place of the outside world |
| `tests/waiting.tsv` | The tests that wait for code, each with the failure expected of it |
| `tools/test` | One command for each level of tests |
| `tools/check_repo.py` | The repository's own checks |
| `tools/make_fixtures.py` | Makes the tests' inputs: original tunes of a few bytes each, and documents in HVSC's forms |
| `tools/device-run.sh`, `tools/device-probe.sh`, `tools/engine-bench/` | What runs on the device: the test sitting, a read-only probe of the device's facts, and a benchmark of the two engines |
| `cmake/`, `CMakePresets.json`, `packaging/` | The build, the pinned downloads, and what goes into the package |
| `.github/` | The workflow that runs the checks, and the repository's protection rules as files GitHub can import |
