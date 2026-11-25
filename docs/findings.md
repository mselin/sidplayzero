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

### FIND-007 USBSID-Pico needs a newer engine library than the device's system has
- Phase: 1, requirements
- Observed: USBSID-Pico support came in libsidplayfp 2.16.0. Debian trixie, which the device runs, has 2.12.0.
  The USBSID driver inside the library is GPL-3.0-or-later, and one of its headers says "version 2", which
  contradicts the rest.
- Concerns: the brief's fact "Debian trixie carries libsidplayfp 2.12.0" and the wish to "possibly support
  USBSID"; REQ-111, REQ-144
- Resolution so far: the licence is GPL-3 (REQ-144, CON-21), and the specification builds a library version
  that has USBSID support into the package, with that support switched off until the header's licence is
  cleared with its author (R9).
- Status: open; it matters only when REQ-111 is taken up

## Closed

### FIND-006 The complete HVSC archive cannot be relied on to unpack on the device
- Phase: 1, requirements; confirmed with measurements in phase 2, specification
- Observed: the complete collection is one 7-Zip archive of 81 MB whose tunes are in a single solid block
  compressed with a 340 MB dictionary. Unpacking it took 364 MB of memory at its peak with 7-Zip and 540 MB
  with libarchive. The RAR archive needed 391 MB with the one free program that can read it. The device has
  512 MB in all.
- Concerns: REQ-080 as approved, which had the app download and unpack the complete archive
- Resolution: the owner decided on 25 November 2026 that HVSC is set up for use on demand. In
  `requirements.md`, REQ-080 no longer depends on the complete archive, REQ-088 has tunes fetched when they
  are played, and REQ-089 keeps the complete download for devices that pass a memory check. CON-15 and CON-22
  hold the measurements. Carried forward into the specification in the same change.
- Status: closed, once the owner has approved the wording of the changed requirements

### FIND-009 Debian's engine library has a known vulnerability and no fix
- Phase: 1, requirements; settled in phase 2, specification
- Observed: upstream 3.1.2 fixes an out-of-bounds read, CVE-2026-111148, by checking that a tune's data
  offset lies inside the file. The 2.12.0 in Debian trixie takes the offset from the file header and uses it
  without that check, as does 2.16.1. On 25 November 2026 Debian's security, updates and backports archives
  for trixie held no newer version.
- Concerns: the brief's fact "Debian trixie carries libsidplayfp 2.12.0"; REQ-126
- Resolution: the specification builds upstream 3.1.2 into the package and does not depend on Debian's
  library. The app also checks a tune's header itself before handing it to the engine.
- Status: closed

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
