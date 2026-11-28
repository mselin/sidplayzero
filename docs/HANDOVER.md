# Handover

The working state of the project, so that any session can carry on from here. How to keep it is in
[`HANDOVER-PROTOCOL.md`](HANDOVER-PROTOCOL.md). Where a summary and this file disagree, this file wins.

## Task

go ahead with your recommendations

(The recommendations, on the results of the device sitting of 28 November 2026: confirm SPEC-072, SPEC-004
and REQ-050; look into memory and the detail line on the host and fix what can be fixed; fix the audio
measurement; then send a package for a short second sitting.)

## Plan

1. Fix the audio measurement: `audio_rate` and `audio_format` from what miniaudio opened. Check: builds with
   WERROR. **Done** (`4c69ee6`).
2. Findings for the results file (SPEC-243): confirmations and the ‡ removed (FIND-032), the audio fix
   (FIND-034), the two not measured (FIND-035), the step-5 contradiction (FIND-036). Check: `check_repo.py`
   finds only the two findings still to write. **Done** (`292ce38`).
3. `device-run.sh --only` for the second sitting (FIND-038), with tests. Check: `test_device_script.py`
   12 passed. **Done** (`e488102`).
4. The set-up's time dropped on a set-up from empty (FIND-039), found from the owner's screenshots: fix,
   simulator test `test_setup_time`; step 5 now says Tab, then 5 (FIND-036). Check: simulator level passes,
   and the test fails without the fix. **Done** (`b3cb91d`, `d6bd215`; the first fix failed the test, the
   second passes; simulator level 494 passed, 0 not in order).
5. Memory peak (FIND-033) and detail line (FIND-037): read the two agents' files, merge any fix, write the
   findings. Check: `check_repo.py` OK. **In progress** (agents running).
6. `tools/test unit`, then `tools/test simulator`, both with WERROR. Check: "0 not in order".
7. Open the PR; merge on the owner's word only.
8. Build the package and send it with the second sitting's instructions; ask again which rows the HVSC
   screen showed at step 5.

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
- Findings: up to FIND-039 (FIND-033 and FIND-037 still to write). Open: FIND-007 (USBSID-Pico: GPL-2-only
  driver files, part `usbsid` kept out), FIND-035 (two budgets not measured) and FIND-036 (step 5's answer
  against the app's record); `docs/findings.md`, "Open".
- The sitting of 28 November 2026 (`sidplayzero-device-20261128-123755.txt`) met every budget it measured
  but memory (`peak_memory_kb = 80824`, 64 MB) and the detail line (`detail_ms_max = 341`, 200 ms).
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
- The sitting of 28 November gave 1.08 and 6.37, sidlite for all four benchmark tunes at 15.8 % to 22.0 %
  load and no underruns: the threshold, SPEC-004's `auto` and REQ-050's 60 % are confirmed (FIND-032).

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

- sidplayzero, PRs #12 and #13 merged (`2fb03d7` on main).
- sidplayzero, branch `device-sitting-2` (worktree `/home/user/wt-main`): steps 1 to 3 of the plan, and step 4's change (FIND-039, step-5 wording).

## Verification

- Build with WERROR of `device-sitting-2` at `4c69ee6`: OK.
- `python3 -m pytest tests/framework/integration/test_device_script.py`: 12 passed.
- The audio fix has no host test (the platform sink has none); it is checked at the second sitting.
- `tools/test simulator` with WERROR at `d6bd215`: 588 tests, 494 passed, 94 skipped, 0 not in order.
- `tools/test unit` with WERROR at `d6bd215`: 1904 passed, 6 not in order, all six from `check_repo.py`
  finding FIND-033 and FIND-037 referenced but not yet written (step 5); no code test failed.

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

When the two agents report, read `handover-memory.md` and `handover-detail.md` in the scratchpad, merge
`inv-detail` (and any memory fix) into `device-sitting-2`, and write FIND-033 and FIND-037.

Candidate tasks after that:
- The second sitting: `./device-run.sh --only 4,5,8 PACKAGE.deb` with the collection moved aside; then
  close the remaining ‡ rows and set Phase 5's row to "Gate passed".
- A release (version, changelog, a tag), and publishing to the store, done by hand only when the owner asks.
- FIND-007, when the owner's USBSID-Pico arrives.

## Subagent files

- scratchpad `handover-memory.md`: the memory peak, worktree `/home/user/wt-memory`; running.
- scratchpad `handover-detail.md`: the detail line, worktree `/home/user/wt-detail`; running.
