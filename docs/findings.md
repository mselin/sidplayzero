# Findings

A finding is something that turns out not to be as an earlier document says. It is logged here, raised against
the phase before the one it was found in, and resolved in the earliest document that can settle it. The
resolution is then carried forward one phase at a time, and each document on the way is updated and passes
its gate again. That is how the requirements, the specification and the tests stay a true description of the
app.

Each finding says which phase it was found in, what was observed, which earlier document it concerns, how it
was resolved and whether it is still open.

Phase 1 has no earlier document of this project to raise findings against. What it found differs from the
brief the project started from, so the findings below are raised against that brief and resolved in
`requirements.md`.

## Open

### FIND-006 The complete HVSC archive may not unpack on the device
- Phase: 1, requirements
- Observed: the complete collection is one 7-Zip archive of 80.8 MB, on a mirror that HVSC's site links to and
  does not run. HVSC's archives are "solid", and some have used compression methods that need a lot of memory
  or that some unpackers do not have. The device has 512 MB of RAM. The mirror could not be reached from the
  environment this was researched in, so the archive itself has not been inspected.
- Concerns: the brief's candidate "downloading HVSC locally should be easy"; REQ-080
- Resolution: none yet. Phase 2 inspects the archive and tries unpacking it within a memory limit (R1 in
  `requirements.md`). If it cannot be done on the device, REQ-080 comes back to Phase 1 to be changed.
- Status: open, with Phase 2

### FIND-007 USBSID-Pico needs a newer engine library than the device's system has
- Phase: 1, requirements
- Observed: USBSID-Pico support came in libsidplayfp 2.16.0. Debian trixie, which the device runs, has 2.12.0.
  The USBSID driver inside the library is GPL-3.0-or-later, and one of its headers says "version 2", which
  contradicts the rest.
- Concerns: the brief's fact "Debian trixie carries libsidplayfp 2.12.0" and the wish to "possibly support
  USBSID"; REQ-111, REQ-144
- Resolution so far: the licence is GPL-3 (REQ-144, CON-21). How the engine library reaches the device is R3,
  and the header is R9, both for Phase 2.
- Status: open, with Phase 2

### FIND-009 Whether Debian's engine library has a known vulnerability is not known
- Phase: 1, requirements
- Observed: upstream 3.1.2 fixes an out-of-bounds read, CVE-2026-111148. Whether the 2.12.0 in Debian trixie
  has the fault, or a fix for it, has not been checked.
- Concerns: the brief's fact "Debian trixie carries libsidplayfp 2.12.0"; REQ-126
- Resolution: none yet. Phase 2 checks the affected code against 2.12.0 and Debian's security record before
  choosing the library (R3).
- Status: open, with Phase 2

## Closed

### FIND-001 The emulator is not part of `czdev` any more
- Phase: 1, requirements
- Observed: the brief says the `czdev` tool has a desktop emulator that needs a Rust toolchain. That emulator
  has been removed. `czdev` now only scaffolds and publishes. The desktop simulator is part of the app
  template, built with SDL2, and it runs without a display.
- Concerns: the brief's platform facts on the app model and the emulator
- Resolution: CON-11 and CON-12; the simulator level of REQ-160 is built on the template's simulator
- Status: closed

### FIND-002 Apps are ordinary programs, not code loaded into the launcher
- Phase: 1, requirements
- Observed: the brief suspected, from a manifest value starting `lvgl-dl`, that apps are loaded into a host
  process. That manifest field is no longer read. The launcher starts each app as a child process and waits
  for it.
- Concerns: the brief's open question on the app runtime model
- Resolution: CON-03 and CON-04. Threads and blocking calls are unrestricted, which removes the brief's main
  reason for keeping the `sidplayfp` program as a fall-back (R4)
- Status: closed

### FIND-003 "reSID is the lighter engine" is true only of older versions
- Phase: 1, requirements
- Observed: Debian's 2.12.0 has reSID and reSIDfp. Upstream removed reSID in 3.0.0 and added a light engine
  called sidlite.
- Concerns: the brief's fact about the engines
- Resolution: CON-13. REQ-051 asks for "a light engine and the most accurate one" without naming them. Which
  library version, and so which engines, is R3
- Status: closed

### FIND-004 `sidplayfp -w` does not give the same output twice
- Phase: 1, requirements
- Observed: the brief proposes comparing the app's sound with a WAV from `sidplayfp -w`, once the output is
  confirmed to be the same every time. By default it is not: two renders of the same tune differed, because
  the engine starts with a random power-on delay. With `--delay=0` three renders were identical, with both
  engines.
- Concerns: the brief's testing expectations
- Resolution: CON-20 and REQ-163
- Status: closed

### FIND-005 The launcher's documentation and its code disagree on how apps are ended
- Phase: 1, requirements
- Observed: the launcher's packaging guide says holding the Home key for 5 s sends SIGINT. The code sends
  SIGTERM after ESC has been held for 3 s, and SIGKILL 3 s later.
- Concerns: the brief's open question on the app lifecycle
- Resolution: CON-04 follows the code. REQ-120 covers both signals
- Status: closed

### FIND-008 One part of the engine library cannot be used under GPL-3
- Phase: 1, requirements
- Observed: the library's exSID hardware backend is GPL version 2 only.
- Concerns: the brief's decision that the app has a licence compatible with the library's
- Resolution: REQ-144 requires the library to be built without it. exSID is not wanted (section 5 of
  `requirements.md`)
- Status: closed
