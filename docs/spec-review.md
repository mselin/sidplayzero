# Independent review of the specification

| | |
|---|---|
| What was reviewed | Draft 1 of [`spec.md`](spec.md), against [`requirements.md`](requirements.md), with [`findings.md`](findings.md) as background |
| Who reviewed it | A reviewer with fresh context: a separate session that was given the documents and nothing of how they came about, and was asked whether tests could be written from the specification before any code exists. It also checked some of the cited evidence itself (the engine archives' checksums and API names, HVSC's document formats and server headers) |
| Date | 25 November 2026 |
| Verdict on draft 1 | Not fit to write tests from: 4 blockers, 29 major items, 11 minor items |
| What followed | Draft 2 of `spec.md`, the new [`keymap.tsv`](keymap.tsv), four findings against the requirements (FIND-010 to FIND-013), and changes to `tools/check_repo.py` |
| State | REVIEW-STATE |

This is the Phase 2 gate's record. Each item says what the reviewer found, what was done about it, and where
to look. An item is closed when the reviewer has checked the change and agreed.

## What the reviewer found sound

- The choice of a pinned libsidplayfp 3.1.2, built in statically, and of linking the library and not driving
  the `sidplayfp` program. The checksums, the API names and the fix for CVE-2026-111148 were verified.
- Safe writing of files, the settings table, the rules for requests, and the containment of unpacking.
- The package layout and the store, licence and publishing checks.
- No conflict with reserved keys: 4 to 8 are only the action bar, the media, volume, Help and Print Screen
  keys follow the platform's conventions, and the ESC timing fits the launcher's.
- Nothing goes against a decision in "Left out of version 1.0".

## Blockers

### RV-001 The simulator could not observe what its oracles asked for
- Found: the script language could press keys and take screenshots, but most simulator oracles needed to
  know which row is highlighted, what a field holds, or which message shows. The only other oracle was a
  reference image, which cannot exist before the code. Key names, the fakes' set-up and "settled" were
  undefined.
- Done: SPEC-254 defines a screen description, as data, with every field an oracle names. SPEC-253 defines
  the options, the fakes folder, the full script language with `expect` over the description, file, request
  and sample assertions, and the rule for "settled". SPEC-255 makes images approval artefacts of Phase 5;
  no oracle rests on an image alone. Key names are in SPEC-111.
- Status: RV-001-STATUS

### RV-002 "Host" meant two incompatible things
- Found: host tests were defined as `core` against fakes in under two minutes, yet host oracles needed the
  real engine, the real program being killed, a real `7z`, a TLS server, the built `.deb` and fuzzing.
  Song lengths and MD5 in `core` depended on the engine library.
- Done: five levels, each with a command and a time budget (SPEC-250): host-unit, host-integration,
  host-package, simulator and device; fuzzing has its own command. Every item's Level line uses them. `core`
  has its own MD5 and song length reader (SPEC-008, SPEC-142), so SPEC-251 holds literally. The
  ThreadSanitizer run is part of the integration level.
- Status: RV-002-STATUS

### RV-003 The key map did not list every key on every screen
- Found: the HVSC screen, About, the panels and Settings' keys 5 to 8 had no keys; non-enumerated settings
  had no way to be edited; REQ-080's three key presses from Settings took at least four.
- Done: `keymap.tsv` is the key map, one row for each key in each screen and panel, and part of the
  specification (SPEC-094). SPEC-101 says how each kind of setting is edited. SPEC-102 gives the HVSC
  screen's rows and keys; from Settings the keys 5, Enter, Enter set HVSC up, with an oracle.
- Status: RV-003-STATUS

### RV-004 The player's state table contradicted other items
- Found: SPEC-060 claimed to be complete, but fetching, device loss, engine switching and the wait before a
  fetch needed transitions it forbade.
- Done: SPEC-060 is a full table of every event in every state, with a Waiting state before Fetching, device
  loss, and "ignored" written out where nothing happens.
- Status: RV-004-STATUS

## Major

### RV-005 No key stopped playback
- Found: REQ-010 requires stop; no screen had a key for it.
- Done: key 5 on the now-playing screen is "Stop" (`keymap.tsv`, SPEC-096).
- Status: RV-005-STATUS

### RV-006 Idle, Fetching and Failed were not defined for the user
- Found: what the now-playing screen and the mini player show in each state, and what Play/Pause does when
  nothing is loaded, was left open; so were header values of "unknown" and forced values.
- Done: SPEC-096 has a table of what each line holds in each state, and says which chip model and clock are
  shown. SPEC-060 says what Play/Pause does in each state, and SPEC-095 for a highlighted folder. SPEC-109
  covers the mini player.
- Status: RV-006-STATUS

### RV-007 ESC could not leave the HVSC screen during a download
- Found: ESC asked "Stop the download?", so repeated ESC never reached the top screen, against REQ-062.
- Done: ESC and "Back" leave the screen and the job goes on (SPEC-102). "Stop" is its own key (SPEC-147).
  What happens by itself is told by a notice and never changes the screen (SPEC-113, SPEC-140).
- Status: RV-007-STATUS

### RV-008 Navigation was not a defined model
- Found: the "at most four presses of ESC" oracle was false; TAB could stack screens without end; "the
  menu" did not exist; now playing could not be reached from several screens.
- Done: SPEC-090 defines a stack in which no screen appears twice and which is at most six deep, with a
  corrected oracle. TAB does nothing in Settings, HVSC and About. P opens now playing from every screen
  where no text is being typed.
- Status: RV-008-STATUS

### RV-009 The sound backend's boundary was not written out
- Found: REQ-110's acceptance is that the specification defines the boundary; it was a one-line list, and
  tests mixed the levels on either side of it.
- Done: SPEC-025 gives the interface: its operations, its events and what a backend may say it cannot do.
  The player is tested against a fake backend; the software backend against a fake engine and sink
  (SPEC-070).
- Status: RV-009-STATUS

### RV-010 One worker thread did everything slow
- Found: a stalled download or minutes of unpacking would have blocked folder listing, volume keys and the
  state save.
- Done: SPEC-022 has a quick worker and a long worker, each with what it may never do, and oracles: a folder
  opens and a volume key works while a download is stalled; the audio callback aborts in a test build if it
  locks or allocates.
- Status: RV-010-STATUS

### RV-011 Underruns were counted without conditions
- Found: as written, every start, pause, song end and tune change would have counted, and REQ-050 demands
  zero.
- Done: SPEC-070 says exactly when the counter counts, and what happens to the buffer on stop, load, pause
  and song end. REQ-017's check (browse while playing, no underrun) is an oracle there.
- Status: RV-011-STATUS

### RV-012 The engine threshold had no headroom and no device evidence
- Found: 1.6 left no room inside the 60 % budget; one 2-second run decided it; the probe timed Debian's old
  engine, which is not the one shipped.
- Done: the threshold is 2.5‡, from three timed runs after a warm-up, per engine (SPEC-072), with both speed
  factors and each benchmark tune's load in the results (SPEC-241). SPEC-262 adds a static benchmark
  program of the shipped engines for the device, and says the threshold is confirmed or changed before
  Phase 5.
- Status: RV-012-STATUS

### RV-013 "Cannot keep up" switched silently and resumed "at the same position"
- Found: REQ-053 says the app tells the user and offers the lighter engine; a SID tune cannot seek.
- Done: SPEC-079: the subtune starts again from its beginning; every case shows a notice; with `accurate`
  the L key accepts the offer; it happens once for each tune loaded.
- Status: RV-013-STATUS

### RV-014 Absent tunes were given a default subtune nobody can know
- Found: the listing stored "the length of the default subtune", but the default is in the tune's header,
  which is not on the device.
- Done: the listing stores every length (SPEC-142). An absent tune shows its number of subtunes and the
  first length (SPEC-046). An entry without a subtune number means "the default, decided when the file is
  there" (section 1, SPEC-061).
- Status: RV-014-STATUS

### RV-015 What a playlist plays next was undefined
- Found: entries were "tune and subtune", but next always started the default subtune.
- Done: the queue is a list of entries with or without a subtune number (SPEC-062), and SPEC-065 says what
  follows each. SPEC-171 defines the favourite star for both kinds.
- Status: RV-015-STATUS

### RV-016 Paths from downloaded files were trusted
- Found: a path from the song length database decided where a fetched file was written, unchecked; the
  database was read by the engine library's unfuzzed reader.
- Done: SPEC-154 checks every HVSC path wherever it comes in. The app's own reader builds the listing
  (SPEC-142). That reader and the reader of `7z`'s listing are fuzz targets (SPEC-205).
- Status: RV-016-STATUS

### RV-017 The notice could be skipped
- Found: only one of five ways to a first download showed HVSC's notice; the text was a paraphrase and
  claimed everything came from HVSC's own site.
- Done: SPEC-152: no request of any kind until the notice is accepted, with an oracle for each way in. The
  text quotes HVSC's own two sentences and says the addresses are those in Settings. The wording is for the
  owner to approve.
- Status: RV-017-STATUS

### RV-018 Requirements were reinterpreted without findings
- Found: "missing" playlist entries, stepping past the last subtune, the benchmark tunes on the host, the
  real `Commando.sid` lengths, "n of m" and the package's file name all differed from the requirements.
- Done: FIND-010, FIND-011 and FIND-012 raised and resolved in `requirements.md`. The subtune reads "n of m"
  (SPEC-096). The real lengths of `Commando.sid` are checked against HVSC's own documents (SPEC-252) and on
  the device (SPEC-260, step 5). SPEC-220 says how the file's name follows REQ-140.
- Status: RV-018-STATUS

### RV-019 Failures had no taxonomy
- Found: which failures count toward "five in a row", what a 404, a timeout or a full card does, and
  whether a key press can retry during the back-off, were open.
- Done: SPEC-069 is a table of failures, each with its message, whether it counts, and what unattended
  playback does. The back-off applies to unattended playback only (SPEC-143). Messages for a failed fetch
  point to checking for a newer release.
- Status: RV-019-STATUS

### RV-020 Help was compared with itself
- Found: help was made from a table in the source and tested against that table.
- Done: `keymap.tsv` is one file that the specification includes, the app is built from, and the tests read
  (SPEC-094, SPEC-107). `tools/check_repo.py` checks its form.
- Status: RV-020-STATUS

### RV-021 Budgets had no measurements
- Found: several budgets had no key in the results; the index budget could not be measured on a device
  that fetches on demand; the "key script" was undefined.
- Done: SPEC-240 maps every budget to a measurement and how it is produced; SPEC-241 lists every key. The
  index budget is a rate. Fetch time is not counted in key-to-sound. SPEC-260 defines the key script.
- Status: RV-021-STATUS

### RV-022 The results file would have destroyed itself
- Found: the user's private group carries the user's name, and the privacy check deleted any file holding
  it.
- Done: the group named after the user is left out (SPEC-241). SPEC-258 replaces and never deletes, matches
  whole words, and covers IPv6 and network card addresses.
- Status: RV-022-STATUS

### RV-023 The device sitting was not planned
- Found: how the app is started, in what order things happen, and how the real keyboard is checked were
  not said; REQ-127's checklist was missing.
- Done: SPEC-260: the app is always started from the launcher and talks to the script through a request
  file; a table gives the order of the sitting, with a key step for the real key codes, a check that
  rendering is repeatable on the device, and lines for REQ-127.
- Status: RV-023-STATUS

### RV-024 The index did not notice changes
- Found: added and removed files, and a changed root, went unnoticed; the user's own tunes had no MD5 to be
  found again by.
- Done: the index records its root (SPEC-160). Step two runs at every start and for each folder the browser
  lists, and the user's own tunes get an MD5 (SPEC-161, SPEC-173).
- Status: RV-024-STATUS

### RV-025 The complete download could leave a mixed tree
- Found: a kill during the move had no recovery; the staging folder had no name; the `7z` child was not
  ended; storage was checked too late.
- Done: SPEC-144 has a named staging folder, a marker file with resume and roll-back at start, `7z` limited
  to one thread and ended with its process group, and playing switched off meanwhile. SPEC-145 gates the
  offer on the unpacker, memory, room and a writable folder, and HVSC must be set up first.
- Status: RV-025-STATUS

### RV-026 The audio references were not pinned
- Found: the `sidplayfp` program's version, build and command lines were not given, and stored hashes of
  floating-point output vary by machine.
- Done: SPEC-071 pins the program by version and checksum and gives the command; WAVs are made at test
  time and compared value for value; nothing is compared with stored samples.
- Status: RV-026-STATUS

### RV-027 Could features were built into Must tests
- Found: items had no priority, and Must screens' oracles required Could parts.
- Done: every item has a Priority line; key map rows and Settings rows carry priorities; SPEC-115 says what
  a part that is left out looks like, and that the tests pass with every Should and Could part off.
- Status: RV-027-STATUS

### RV-028 Muted voices and the filter were shown nowhere
- Found: the only display was the Could voice bars, whose "envelope level" the library does not report.
- Done: line 6 of the now-playing screen shows the chosen chip, the voices and the filter (SPEC-096,
  SPEC-074). The bars have a formula from the registers (SPEC-080).
- Status: RV-028-STATUS

### RV-029 Windows-1252 was only partly covered
- Found: the 27 characters at 0x80 to 0x9F would have drawn as boxes; a 10-pixel font went against the
  platform's advice.
- Done: the fonts cover all of Windows-1252, with a test over every character (SPEC-010, SPEC-098). All
  text is 12 pixels or larger (SPEC-091).
- Status: RV-029-STATUS

### RV-030 Messages had no identifiers, and panels could pop up under the user's fingers
- Found: tests could not name messages; a panel that appeared by itself could take an Enter meant for
  something else.
- Done: SPEC-116 lists every message with its identifier and kind. Notices never take keys (SPEC-113);
  only a key press opens a panel (SPEC-114).
- Status: RV-030-STATUS

### RV-031 Key codes and typing were not specified
- Found: no table of key codes, no map from codes to characters, no repeat rule, no field limits.
- Done: SPEC-111 has the names and codes; SPEC-099 the character map, the cursor keys and the field limits;
  SPEC-045 the app's own key repeat.
- Status: RV-031-STATUS

### RV-032 Skipping ahead assumed an engine eight times faster than it is
- Found: it would have caused underruns and then the "cannot keep up" message; elapsed time was defined
  two ways.
- Done: SPEC-066: skip renders as fast as the engine allows, up to eight times, underruns are not counted
  while skipping, and the elapsed time comes from the samples rendered.
- Status: RV-032-STATUS

### RV-033 The queue's life was undefined
- Found: whether edits change a playing queue, how it is restored, and how it ends when nothing can be
  played; `state.ini` had no key table.
- Done: the queue is a copy (SPEC-062); SPEC-131 has the key table and how each kind of queue is restored;
  SPEC-143 and SPEC-069 give the endings.
- Status: RV-033-STATUS

## Minor

### RV-034 Partial downloads and temporary files
- Found: HVSC's site sends no validators for resuming; partial and temporary files could not be told
  apart; the room check needed a size no request had fetched.
- Done: SPEC-132 names temporary and partial files differently and ages partial ones out. SPEC-147 resumes
  only with both validators and otherwise starts again. SPEC-146 uses fixed figures before the archive is
  listed.
- Status: RV-034-STATUS

### RV-035 Settings clean-ups
- Found: two settings had no row; comments and unknown keys would have counted as faults; ranges were
  missing.
- Done: `power_on_delay` is gone (it is fixed at 0, SPEC-067) and the notice's acceptance is state, not a
  setting (SPEC-131). SPEC-133 says what is and is not a fault. SPEC-130 gives ranges. SPEC-040 and
  SPEC-101 cover `HVSC_BASE`. The speed factors are cache (SPEC-134).
- Status: RV-035-STATUS

### RV-036 Tunes that a new release moved or changed
- Found: moved tunes stayed as duplicates; a changed tune's refetch contradicted "no request" and could
  overwrite the user's own file.
- Done: SPEC-148: a moved tune is renamed by its MD5; a changed one is fetched again only if the file is
  still HVSC's old one; SPEC-151 names that fetch.
- Status: RV-036-STATUS

### RV-037 Fetch timing
- Found: the 700 ms wait also delayed a tune chosen with Enter; nothing limited fetching in bulk; no room
  check before a single fetch.
- Done: SPEC-141: Enter fetches at once; the next entry is fetched shortly before its turn; unattended
  fetches are limited per minute; the card must have room.
- Status: RV-037-STATUS

### RV-038 Playlist details
- Found: name rules, collisions, the dialect samples, percent-encoding, duplicates and export's file name
  were open.
- Done: SPEC-170 and SPEC-172 give the name rules; SPEC-175 to SPEC-177 show each dialect as its player
  writes it; SPEC-179 names the export file. The dialect beyond REQ-105 is dropped.
- Status: RV-038-STATUS

### RV-039 Shuffle could not be tested
- Found: "the order the test lists" needs a defined generator and method.
- Done: SPEC-065 names Fisher–Yates and xorshift64*, where the chosen entry goes, what switching does, and
  the `--seed` option.
- Status: RV-039-STATUS

### RV-040 Build and package details
- Found: two preset names, a clean build that both passes and fails without a maintainer, build paths in
  the program, a library missing from Depends.
- Done: SPEC-001 names both presets and the prefix map; SPEC-221 adds `libpulse0`, checks needed libraries
  against Depends, and limits the maintainer rule to the package build; SPEC-225 defines the home-path
  check and its exceptions.
- Status: RV-040-STATUS

### RV-041 Boundaries nobody specified
- Found: empty and unreadable folders, an empty query, names that are not UTF-8, two instances, a
  screenshot that cannot be written, a collection copied in while the empty screen shows.
- Done: SPEC-041, SPEC-043 and SPEC-048 for folders; SPEC-100 and SPEC-162 for search; SPEC-131 for a
  remembered folder that is gone; SPEC-200 for a second instance; SPEC-108 for screenshots.
- Status: RV-041-STATUS

### RV-042 Online search rested on an address nobody offered
- Found: the site's search returns no path to fetch by, and its address was hard-coded.
- Done: SPEC-165: not built in version 1.0 and not specified further until HVSC's maintainers offer it;
  the request oracles show that no such address is ever asked.
- Status: RV-042-STATUS

### RV-043 Volume commands, the backlight after a kill, the brightness keys
- Found: `pactl`'s arguments were not given; a kill with the screen dark left it dark; the brightness keys
  did nothing.
- Done: SPEC-076 gives the argument lists. SPEC-207 saves the brightness before darkening and restores it
  at the next start. FIND-013 and the new REQ-129 give the brightness keys to the app (SPEC-104).
- Status: RV-043-STATUS

### RV-044 The fixtures would have tested the readers against themselves
- Found: both the fixture files and their readers come from this project; the real files' line endings and
  release line were not stated.
- Done: SPEC-142, SPEC-148 and SPEC-252 state the format facts as observed in release 85, and an optional
  test reads HVSC's own documents from a folder outside the repository.
- Status: RV-044-STATUS

## The second pass

SECOND-PASS
