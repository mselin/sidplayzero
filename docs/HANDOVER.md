# Handover

The working state of the project, so that any session can carry on from here. How to keep it is in
[`HANDOVER-PROTOCOL.md`](HANDOVER-PROTOCOL.md). Where a summary and this file disagree, this file wins.

## Task

update CONTRIBUTING.md's stale phase rows

## Plan

1. Rewrite the State cells of Phase 3 (the device proof has run) and Phase 5 (merged, waiting list empty,
   FIND-025 to FIND-031 settled, the sitting owed) in `CONTRIBUTING.md`'s phase table. Check: the two rows
   match "Established" below, and `python3 tools/check_repo.py` is OK. **Done.**
2. Open a pull request with `CONTRIBUTING.md` and this file. Check: the PR's only changes are these two files.
   **Done when the PR is open.**

## Established

The project and its documents:
- SidPlayZero is a SID player for the M5Stack CardputerZero: a native LVGL 9.5 app on Debian 13 arm64,
  playing with libsidplayfp 3.1.2 and libresidfp 1.3.0, built in statically from pinned archives
  (`cmake/pins.cmake`).
- It is built spec-first in five gated phases (`CONTRIBUTING.md`, "Five phases"). The documents are:
  - the requirements: `docs/requirements.md`;
  - the design: `docs/spec.md`, with the key map `docs/keymap.tsv` as part of it;
  - the spec's independent review: `docs/spec-review.md`;
  - the test framework: `docs/test-framework-tasks.md`;
  - the device sitting: `docs/device-checklist.md`;
  - findings: `docs/findings.md`.
- The roadmap is the phase table in `CONTRIBUTING.md`; there is no separate roadmap file.

Where each phase stands:
- Phases 1 to 4 have passed their gates (25 and 26 November 2026).
- Phase 5 (the app) is built and merged: PRs #7 to #11, the last merge being `f261aec`.
- What is left of Phase 5's gate is the owner's sitting with the app on the device.
- Phase 3's device-level proof (`tools/device-run.sh --prove`) ran on the device on 27 November 2026, in
  order: 1 check passed, 1 failed as intended, 0 not in order.

Findings and test state:
- Findings: 31 (FIND-001 to FIND-031). Only FIND-007 (USBSID-Pico: GPL-2-only driver files, part `usbsid`
  kept out) is open (`docs/findings.md`, "Open").
- FIND-025 to FIND-031 were decided or confirmed by the owner on 26 and 27 November 2026.
- `tests/waiting.tsv` lists no test: every test of the specification passes (checked with `grep -v "^#"
  tests/waiting.tsv`).
- 20 reference images are approved (`tests/golden/`). Five of them are the store's screenshots
  (`packaging/store/`, `app-builder.json`).

Engine speed and SPEC-072's threshold:
- On the device, the speed factors were 1.12 (reSIDfp) and 6.62 (sidlite), from the device results of
  `--prove`.
- SPEC-072's threshold is still the proposal 2.5 (`src/core/spz/engine_choice.h:24`), so `auto` will choose
  sidlite for every tune on this device.
- Confirming the threshold waits on the full sitting's results file (SPEC-262, SPEC-243).

Rules for working here:
- Merge only on the owner's word, and only when "All checks passed" is green. This repository has no
  "merge when green" rule of its own (unlike `mselin/mselin` and `mselin/mselin.github.io`).
- Never push to `main`.
- Nothing is published to the store by a script.
- No host name, user name or address of the owner's device in the repository: `tools/check_repo.py` and the
  `PRIVATE_TERMS` secret enforce it.
- CI builds with warnings as errors (`SIDPLAYZERO_WERROR=ON`, `.github/workflows/ci.yml:18`). Local runs
  should set it too: `SIDPLAYZERO_WERROR=ON tools/test unit`.
- `docs/requirements.md` and `docs/spec.md` change only through a finding. A change to what the app does needs
  the owner's approval.

## Changes

- sidplayzero, PR #12 (merged, `2f17c9f`): `docs/HANDOVER-PROTOCOL.md` and `docs/HANDOVER.md`, new.
- sidplayzero, branch `contributing-phase-rows`: the Phase 3 and 5 rows of `CONTRIBUTING.md`, and this file.

## Verification

- `python3 tools/check_repo.py`: OK.
- `grep -v "^#" tests/waiting.tsv`: only the header line, so no test waits.
- Not run: the test levels, since no code changed.

## Dead ends

Things tried earlier in this project, so that nobody tries them again:
- Resuming a stopped subagent that ran on a model whose usage limit was hit only fails again. Start a fresh
  agent on the files the first one left.
- Running `tools/test unit` and `tools/test simulator` at once in one worktree breaks the simulator run, since
  the unit level regenerates `tests/fixtures`.
- `git reset --hard` is refused in agents' worktrees; `git merge --ff-only origin/BRANCH` on a clean worktree
  gives the same result.
- On GitHub's runner the package level can fail with "arm64 emulation … not installed". That is the runner,
  not the change; one rerun of the job has passed it.

## Next

Wait for the owner's full device sitting with the package built from `main` at `f261aec` (SHA-256
`28b5405e…dae546b0`): `./device-run.sh sidplayzero_0.1.0-1_arm64.deb`, which writes
`sidplayzero-device-*.txt` beside it.

From that results file:
- confirm or change SPEC-072's threshold and table, SPEC-004's default and REQ-050's budget (SPEC-262), as a
  finding;
- check every budget of SPEC-240 (SPEC-243);
- answer each checklist line;
- then set Phase 5's row in `CONTRIBUTING.md`'s phase table to "Gate passed", closing the gate.

Candidate tasks after that:
- A release (version, changelog, a tag), and publishing to the store, done by hand only when the owner asks.
- FIND-007, when the owner's USBSID-Pico arrives.

## Subagent files

None.
