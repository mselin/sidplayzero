# SidPlayZero test-framework tasks

| | |
|---|---|
| Status | **In progress.** This is the Phase 3 artifact: the list of what the test framework is made of. The framework itself follows it |
| Date | 25 November 2026 |
| Derives from | [`spec.md`](spec.md), approved 25 November 2026, section 15 above all |
| Gate | Every level runs one passing and one deliberately failing test with one command |
| Next artifact | `tests/` (Phase 4): the tests themselves, each tracing to SPEC ids |

## 1. What this phase builds, and what it does not

The specification says how each item is tested and at which level. This phase builds the machinery those
tests need, so that Phase 4 can write them before the app exists:

- the build, with the app as a **stub**: a program that starts, shows one screen, answers `--version`, and
  leaves when told. It is the least that lets every level run. Phase 5 replaces it with the app;
- the command `tools/test` and the rules it applies (levels, parts, the waiting list, traceability);
- for each level, the things a test at that level is written with: the fakes, the fixture generator, the
  simulator's script language and screen description, the pinned reference program, a local server, the
  package checks, the device script;
- the checks on every pull request.

It does not write the tests of the specification's items (Phase 4) and it does not build the app (Phase 5).
Where a piece of machinery cannot be exercised until the app exists, the task says so, and Phase 4's tests
exercise it from the waiting list.

- **TF-nnn** is one task. **Implements** names the SPEC items it serves. **Shown by** says how it is known to
  work: the framework's own tests, kept in `tests/framework/`, which are not tests of the app.
- A task is done when what it delivers is merged and its "shown by" holds on the pull request's checks.

## 2. The gate, and how it is shown

Each level has two **canaries**: a test that must pass, and one built to fail. The failing one is left out of
a normal run.

| Command | What it does |
|---|---|
| `tools/test LEVEL` | Runs the level: its framework tests and passing canary now, the specification's tests from Phase 4 on |
| `tools/test prove LEVEL` | Runs the level with the failing canary put in, and succeeds only if the level's command ended with a status other than 0 and named that canary and nothing else |
| `tools/test prove` | The same for every level that runs on this machine: unit, integration, fuzz, package, simulator |

The device level's two canaries need the device and the owner: `tools/device-run.sh --prove` installs the
stub, asks for one start from the launcher, and reports one check passed and one failed as intended
(TF-081). That run is part of this phase's gate and is asked for when the rest is in place.

## 3. Tasks

### 3.1 The build and the stub

**TF-001 Layout.** `src/core`, `src/platform`, `src/ui` and `src/app` for the three layers and the program;
`tests/` with a folder for each level, `framework/`, `fakes/` and `fixtures/`; `cmake/`, `packaging/` and
`third_party/`.
- Implements: SPEC-020
- Shown by: the build of TF-002 and the layering check of TF-003

**TF-002 CMake project and presets.** One CMake project, C++17, CMake 3.31 or newer. Presets `linux-x86-64`
and `cp0-cross`, and the workflow preset `cp0-cross-package`. Options: `SIDPLAYZERO_TEST_HOOKS`,
`SIDPLAYZERO_PARTS` (`all`, `must` or a list), and the sanitizer to build with. Both builds pass
`-ffile-prefix-map`.
- Implements: SPEC-001, SPEC-115
- Shown by: both presets build the stub on a clean checkout; `sidplayzero --version` prints the version of
  `app-builder.json`

**TF-003 The layers are enforced.** `core` is a library with no include path to LVGL, the engine library, the
sound library or libcurl; `ui` has none to the engine or sound library.
- Implements: SPEC-020
- Shown by: a framework test compiles a file that includes `lvgl.h` against `core`'s settings and expects
  the compiler to refuse

**TF-004 Pinned downloads.** One file, `cmake/pins.cmake`, names everything fetched from outside with its
version and SHA-256: the two engine archives, the `sidplayfp` program, LVGL, miniaudio, doctest, the device's
sysroot, and the store's and the template's check scripts. One helper downloads into `.cache/` and refuses a
file whose checksum differs.
- Implements: SPEC-002, SPEC-005, SPEC-071, SPEC-223, SPEC-224
- Shown by: a framework test gives the helper a file with a wrong checksum and sees it refused

**TF-005 The stub.** `sidplayzero` built from `src/app`: draws the empty screen of SPEC-048 with its one row,
does nothing on Enter, prints two version lines for `--version`, takes the lock, and leaves with status 0 on
a held ESC, SIGTERM or SIGINT. On the device it draws to the framebuffer and reads the keyboard without
grabbing it. Every source file of it is marked as a stub.
- Implements: SPEC-023, and as much of SPEC-048, SPEC-106, SPEC-111, SPEC-200 and SPEC-201 as the levels need
- Shown by: the passing canaries of the simulator, integration and package levels

### 3.2 `tools/test` and its rules

**TF-010 The command.** `tools/test` with the words `unit`, `integration`, `fuzz`, `package`, `host`,
`simulator`, `device` and `prove`. Each says what it needs installed and stops with that list if something
is missing; ends with status 0 only if everything it ran was in order; and prints the names of what was not.
- Implements: SPEC-250
- Shown by: `tools/test prove`; a framework test runs `tools/test unit` with a needed program hidden and sees
  the list

**TF-011 Tests name their items and parts.** A test carries `SPEC-nnn` for each item it checks and a
`parts:` line when it needs a part. `tools/check_repo.py` reads every test: an item that does not exist is a
fault, and so is a part that SPEC-115 does not have. A switch in the checker, turned on at the Phase 4 gate,
adds the rule that every SPEC item has a test at each of its levels other than the device.
- Implements: SPEC-256, SPEC-115
- Shown by: framework tests give the checker a test file with an unknown item, and one with an unknown
  part, and see each refused

**TF-012 The waiting list.** `tests/waiting.tsv`: a test's name, the reason (`code` or `images`) and the
words of the failure expected. The runner of every level counts a listed test as in order when it fails with
those words, and as a fault when it fails otherwise or passes.
- Implements: SPEC-256
- Shown by: a framework test runs a small level of its own with a listed test that fails as expected (status
  0), one that fails with other words (not 0) and one that passes (not 0)

**TF-013 Parts.** The build switch for each part of SPEC-115. The unit and simulator levels run twice, with
`all` and with `must`, and skip a test whose part is off.
- Implements: SPEC-115, SPEC-250
- Shown by: a canary marked `parts: playlists` runs in the first run and is reported as skipped in the second

**TF-014 The proof.** `tools/test prove`, as section 2 describes.
- Implements: SPEC-250's oracle; this phase's gate
- Shown by: the pull request's checks run it

### 3.3 The host-unit level

**TF-020 The unit test program.** doctest, one header, kept in `third_party/`. The unit tests link `core` and
the fakes and nothing else, and run with no network.
- Implements: SPEC-251
- Shown by: a framework test reads the link line and finds only `core`, the fakes and the C++ library; the
  level passes inside a network namespace with no interfaces where the machine allows making one

**TF-021 The seams and their fakes.** The interfaces of SPEC-021 and SPEC-025 as headers in `core`, and a fake
of each in `tests/fakes/`: `Engine`, `AudioSink`, `SoundBackend`, `Clock`, `Files`, `HttpClient`, `Unpacker`,
`SystemVolume`, `Backlight`, `Memory`. Each fake counts its calls and does what the specification's table
says of it: time moves only when told; a failure or a delay can be put at any file operation; requests are
answered from a table and logged; the fake backend reports the events it is told to.
- Implements: SPEC-021, SPEC-025
- Shown by: framework tests of each fake's own behaviour

**TF-022 Sanitizers.** The unit tests are built and run a second time with AddressSanitizer and
UndefinedBehaviorSanitizer.
- Implements: SPEC-250
- Shown by: `tools/test prove unit` includes a canary that reads past an array and is caught only in this run

**TF-023 Tests of the checker.** `tools/check_repo.py` is given copies of the documents and the key map with
one fault each, for every rule of SPEC-094 and SPEC-256.
- Implements: SPEC-094, SPEC-256
- Shown by: those tests

### 3.4 Fixtures

**TF-030 The generator.** `tools/make_fixtures.py` writes `tests/fixtures/` and its manifest, the same bytes
on every run. In this phase: the tunes of SPEC-252 (each a few dozen bytes of 6502 code written in the
generator), the bad tunes of SPEC-203, HVSC's documents in their observed forms, and the small collections
for releases 85 and 84. The generator is a list of makers, so that Phase 4 adds the playlists, import
files, archives and limit files beside the tests that use them, and the 61,157-tune tree on request.
- Implements: SPEC-252
- Shown by: two runs give identical files; a framework test checks each good tune's header against
  SPEC-203's table

**TF-031 No music.** The check of SPEC-153, in `tools/check_repo.py` for the repository and in the package
level for the `.deb`.
- Implements: SPEC-153
- Shown by: a framework test adds a tune that is not in the manifest and sees the check fail

### 3.5 The simulator level

**TF-040 Drawing with no display.** LVGL 9.5.0 drawing 320×170 into memory, with the fake clock driving its
timers.
- Implements: SPEC-253
- Shown by: the passing canary

**TF-041 Options and the runner.** The options of SPEC-253. `tests/simulator/run.py` gives each script a new
home folder, sets the environment, reads the script's header lines (items, parts, fakes folder, options),
runs it and keeps what it wrote.
- Implements: SPEC-253
- Shown by: the canaries

**TF-042 The script language.** Every command of SPEC-253, the settled rule with its limit, the statuses 1
and 2, and the report written on a failed `expect`. The commands that look at requests, samples and the fake
backend are built against the fakes; with the stub nothing makes a request or a sound, so their first real
use is by Phase 4's tests.
- Implements: SPEC-253
- Shown by: the passing canary reads the empty screen; the failing canary expects a wrong title and gets
  status 1 and a report; a framework script with an unknown command gets status 2; `restart` and `put` are
  exercised by framework scripts; two runs of one script give the same bytes

**TF-043 The screen description.** One function that writes the JSON of SPEC-254, and a check of every dump
against that item's tables: no other field, no other form.
- Implements: SPEC-254
- Shown by: the stub's dump passes the check; a framework test gives the check a dump with an unknown field,
  and one with `player.model` as `MOS6581`, and sees each refused

**TF-044 Pictures.** `shot` writes a PNG; the runner compares it with `tests/golden/NAME.png` when there is
one, writing the difference, and reports "no reference yet" when there is none.
- Implements: SPEC-255
- Shown by: framework tests with a stored image equal to the shot, one differing in a pixel, and none

**TF-045 The windowed build.** The same stub in a window, started under a virtual display.
- Implements: SPEC-253, SPEC-001
- Shown by: a framework test starts it, sees a frame drawn, and has it leave on a held ESC

**TF-046 C++ tests beside the scripts.** A test program of the simulator level that links `ui` and LVGL with no
display, for what a script cannot ask.
- Implements: SPEC-253, SPEC-010
- Shown by: a canary in it

### 3.6 The host-integration level, and fuzzing

**TF-050 The engine and the reference program.** The two engine libraries and `sidplayfp` 3.2.3 built from
their pinned archives into `.cache/`, once.
- Implements: SPEC-002, SPEC-071
- Shown by: the passing canary of TF-051

**TF-051 Reference renders.** A helper runs the reference program with SPEC-071's command and an empty home
folder and gives back the samples; another compares two sets of samples value for value.
- Implements: SPEC-071, SPEC-074, SPEC-075, SPEC-077
- Shown by: the passing canary renders a fixture tune twice and finds the samples equal; the failing canary
  expects two different subtunes to be equal

**TF-052 Running the real program.** A helper starts the program with a script on the real clock, sends it a
signal at a chosen moment, and reports its status, how long it took and what processes it left.
- Implements: SPEC-132, SPEC-201, SPEC-202
- Shown by: a framework test sends the stub SIGTERM and SIGINT and gets status 0 within a second, and
  SIGKILL and gets a clean start afterwards

**TF-053 A local server.** An HTTPS server on the loopback address with a certificate authority made for the
run, a table of answers (files, redirects, an answer that never ends, a self-signed certificate) and a log
of what it was asked.
- Implements: SPEC-150
- Shown by: a framework test fetches a file through it with the authority trusted, and is refused without

**TF-054 Stand-ins and real tools.** Programs named `pactl` and `7z` that record their arguments, to put on
the path of a test; and a check that the real `7z` is there.
- Implements: SPEC-007, SPEC-076, SPEC-144, SPEC-149
- Shown by: a framework test runs each stand-in and reads back the arguments

**TF-055 ThreadSanitizer.** The simulator's tests are run once more in a build with ThreadSanitizer.
- Implements: SPEC-022, SPEC-250
- Shown by: the level runs it; a framework program with a deliberate race is caught by `prove`

**TF-056 HVSC's own documents, when at hand.** With `SIDPLAYZERO_REAL_HVSC` naming a folder, a test reads
the documents there; without it the test is reported as skipped.
- Implements: SPEC-252, SPEC-142
- Shown by: the level's report shows the skip

**TF-060 Fuzzing.** `tools/test fuzz`: each target built with libFuzzer and the two sanitizers, run for
15 s‡ from `tests/fuzz/corpus/`, stopped by a crash, a report or an input slower than 2 s‡. Targets are a
list that Phase 4 adds to. In this phase there is one, over the lookup of a value in a screen description
by its path, which is the only reader `core` has before the app is built.
- Implements: SPEC-205
- Shown by: `tools/test prove fuzz` builds a reader with a deliberate fault and sees the command fail

### 3.7 The host-package level

**TF-070 The package.** The `cp0-cross-package` build: the device's toolchain and sysroot, the stub for
arm64, and CPack making the `.deb` with the files, fields and names of SPEC-220 to SPEC-222. The maintainer
comes from the environment. The icons are the project's own drawings, made by a script.
- Implements: SPEC-001, SPEC-220, SPEC-221, SPEC-222
- Shown by: the passing canary of TF-071

**TF-071 The checks on the package.** Each check of section 13 of the specification as a test: the file
list, the control fields, no maintainer scripts and no setuid bit, the size, the needed libraries against
`Depends`, the names against Debian trixie's index, the store's scripts at their pinned commit, the licence
lines and the notices file, no music, nothing private. The manifest's check waits for Phase 5's screenshots
on the waiting list.
- Implements: SPEC-011, SPEC-153, SPEC-220 to SPEC-227
- Shown by: the passing canary checks the file list; the failing canary expects a file that is not there

**TF-072 Installing it.** The `.deb` installed with `apt` in a clean Debian trixie arm64 container, run under
emulation, and `sidplayzero --version` run there.
- Implements: SPEC-221
- Shown by: that test

**TF-073 The benchmark program.** `tools/engine-bench/build.sh` run for the host and for arm64; the arm64
program is static and prints its twelve lines under emulation.
- Implements: SPEC-262
- Shown by: that test

### 3.8 The device level

**TF-080 The script and the checklist.** `tools/device-run.sh` and `docs/device-checklist.md`, with the request
and results files, the order of the sitting, the refusal to run as root, and the step that takes private
details out of the results.
- Implements: SPEC-258, SPEC-260
- Shown by: an integration test runs the script against a stand-in for the device's side, and its privacy
  step on a text made for it

**TF-081 The device's canaries.** `tools/device-run.sh --prove`: installs the stub, asks for one start from
the launcher and a held ESC, then makes two checks of the results: one that must hold (a start time was
recorded) and one built to fail (a version that the stub does not have). It removes the stub again.
- Implements: this phase's gate, for the device level
- Shown by: the owner's run on the device; before that, the same against the stand-in

### 3.9 Checks on every pull request

**TF-090 The workflow.** Jobs for the unit, integration and fuzz, package and simulator levels and for the
proof, each behind the one job "All checks passed". Downloads are cached by their checksums. Actions are
pinned by commit. `tools/check_repo.py` reads the workflow and refuses a job that "All checks passed" does
not wait for, and a secret other than `PRIVATE_TERMS`.
- Implements: SPEC-257
- Shown by: the checks of this phase's pull request; a framework test of the checker's two rules

**TF-091 Shown once.** A pull request with a deliberately failing unit test is opened, seen to end with a
failed "All checks passed", and closed without merging.
- Implements: SPEC-257's oracle
- Shown by: the link to that pull request, recorded here when it has been done

## 4. State

| Task | State |
|---|---|
| TF-001 to TF-091 | Not started |
