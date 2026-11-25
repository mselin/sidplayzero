# Independent review of the specification

| | |
|---|---|
| What was reviewed | Draft 1 of [`spec.md`](spec.md), against [`requirements.md`](requirements.md), with [`findings.md`](findings.md) as background |
| Who reviewed it | A reviewer with fresh context: a separate session that was given the documents and nothing of how they came about, and was asked whether tests could be written from the specification before any code exists. It also checked some of the cited evidence itself (the engine archives' checksums and API names, HVSC's document formats and server headers) |
| Date | 25 November 2026 |
| Verdict on draft 1 | Not fit to write tests from: 4 blockers, 29 major items, 11 minor items |
| What followed | Draft 2 of `spec.md`, the new [`keymap.tsv`](keymap.tsv), four findings against the requirements (FIND-010 to FIND-013), and changes to `tools/check_repo.py`; then a second pass by the same reviewer, and draft 3 |
| State | REVIEW-STATE |

This is the Phase 2 gate's record. Each item says what the reviewer found, what was done about it, and where
to look. An item is closed when the reviewer has checked the change and agreed. Items RV-001 to RV-044 are
from the first pass, on draft 1; RV-045 to RV-060 from the second, on draft 2; RV-061 to RV-066 from the
third, on draft 3.

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
- Status: the second pass found a remainder: the script language had no way to restart the app on the same home folder or to change a file during a run; "the fake backend" had no switch and no way to set what it reports; `advance` under the real clock was undefined. SPEC-253 adds `restart`, `put`, `remove`, `readonly` and `writable`, the option `--backend fake` with `fake backend EVENT`, and says what `advance` and `hold` do on the real clock. Closed; confirmed by the reviewer at the third pass

### RV-002 "Host" meant two incompatible things
- Found: host tests were defined as `core` against fakes in under two minutes, yet host oracles needed the
  real engine, the real program being killed, a real `7z`, a TLS server, the built `.deb` and fuzzing.
  Song lengths and MD5 in `core` depended on the engine library.
- Done: five levels, each with a command and a time budget (SPEC-250): host-unit, host-integration,
  host-package, simulator and device; fuzzing has its own command. Every item's Level line uses them. `core`
  has its own MD5 and song length reader (SPEC-008, SPEC-142), so SPEC-251 holds literally. The
  ThreadSanitizer run is part of the integration level.
- Status: closed; confirmed by the reviewer at the second pass

### RV-003 The key map did not list every key on every screen
- Found: the HVSC screen, About, the panels and Settings' keys 5 to 8 had no keys; non-enumerated settings
  had no way to be edited; REQ-080's three key presses from Settings took at least four.
- Done: `keymap.tsv` is the key map, one row for each key in each screen and panel, and part of the
  specification (SPEC-094). SPEC-101 says how each kind of setting is edited. SPEC-102 gives the HVSC
  screen's rows and keys; from Settings the keys 5, Enter, Enter set HVSC up, with an oracle.
- Status: closed; confirmed by the reviewer at the second pass

### RV-004 The player's state table contradicted other items
- Found: SPEC-060 claimed to be complete, but fetching, device loss, engine switching and the wait before a
  fetch needed transitions it forbade.
- Done: SPEC-060 is a full table of every event in every state, with a Waiting state before Fetching, device
  loss, and "ignored" written out where nothing happens.
- Status: the second pass found a remainder: other items still made transitions the table lacked: out of Failed after the unattended wait, the stop after five bad files, the pause when the sound device does not come back, the L key, and the guard during the complete download. SPEC-060 has rows for each, and for the fetch-ahead; SPEC-025 and SPEC-078 gain the event `device_gone`, whose message is a notice. Closed; confirmed by the reviewer at the third pass

## Major

### RV-005 No key stopped playback
- Found: REQ-010 requires stop; no screen had a key for it.
- Done: key 5 on the now-playing screen is "Stop" (`keymap.tsv`, SPEC-096).
- Status: closed; confirmed by the reviewer at the second pass

### RV-006 Idle, Fetching and Failed were not defined for the user
- Found: what the now-playing screen and the mini player show in each state, and what Play/Pause does when
  nothing is loaded, was left open; so were header values of "unknown" and forced values.
- Done: SPEC-096 has a table of what each line holds in each state, and says which chip model and clock are
  shown. SPEC-060 says what Play/Pause does in each state, and SPEC-095 for a highlighted folder. SPEC-109
  covers the mini player.
- Status: closed; confirmed by the reviewer at the second pass

### RV-007 ESC could not leave the HVSC screen during a download
- Found: ESC asked "Stop the download?", so repeated ESC never reached the top screen, against REQ-062.
- Done: ESC and "Back" leave the screen and the job goes on (SPEC-102). "Stop" is its own key (SPEC-147).
  What happens by itself is told by a notice and never changes the screen (SPEC-113, SPEC-140).
- Status: closed; confirmed by the reviewer at the second pass

### RV-008 Navigation was not a defined model
- Found: the "at most four presses of ESC" oracle was false; TAB could stack screens without end; "the
  menu" did not exist; now playing could not be reached from several screens.
- Done: SPEC-090 defines a stack in which no screen appears twice and which is at most six deep, with a
  corrected oracle. TAB does nothing in Settings, HVSC and About. P opens now playing from every screen
  where no text is being typed.
- Status: closed; confirmed by the reviewer at the second pass

### RV-009 The sound backend's boundary was not written out
- Found: REQ-110's acceptance is that the specification defines the boundary; it was a one-line list, and
  tests mixed the levels on either side of it.
- Done: SPEC-025 gives the interface: its operations, its events and what a backend may say it cannot do.
  The player is tested against a fake backend; the software backend against a fake engine and sink
  (SPEC-070).
- Status: closed; confirmed by the reviewer at the second pass

### RV-010 One worker thread did everything slow
- Found: a stalled download or minutes of unpacking would have blocked folder listing, volume keys and the
  state save.
- Done: SPEC-022 has a quick worker and a long worker, each with what it may never do, and oracles: a folder
  opens and a volume key works while a download is stalled; the audio callback aborts in a test build if it
  locks or allocates.
- Status: closed; confirmed by the reviewer at the second pass

### RV-011 Underruns were counted without conditions
- Found: as written, every start, pause, song end and tune change would have counted, and REQ-050 demands
  zero.
- Done: SPEC-070 says exactly when the counter counts, and what happens to the buffer on stop, load, pause
  and song end. REQ-017's check (browse while playing, no underrun) is an oracle there.
- Status: closed; confirmed by the reviewer at the second pass

### RV-012 The engine threshold had no headroom and no device evidence
- Found: 1.6 left no room inside the 60 % budget; one 2-second run decided it; the probe timed Debian's old
  engine, which is not the one shipped.
- Done: the threshold is 2.5‡, from three timed runs after a warm-up, per engine (SPEC-072), with both speed
  factors and each benchmark tune's load in the results (SPEC-241). SPEC-262 adds a static benchmark
  program of the shipped engines for the device, and says the threshold is confirmed or changed before
  Phase 5.
- Status: closed; confirmed by the reviewer at the second pass

### RV-013 "Cannot keep up" switched silently and resumed "at the same position"
- Found: REQ-053 says the app tells the user and offers the lighter engine; a SID tune cannot seek.
- Done: SPEC-079: the subtune starts again from its beginning; every case shows a notice; with `accurate`
  the L key accepts the offer; it happens once for each tune loaded.
- Status: closed; confirmed by the reviewer at the second pass

### RV-014 Absent tunes were given a default subtune nobody can know
- Found: the listing stored "the length of the default subtune", but the default is in the tune's header,
  which is not on the device.
- Done: the listing stores every length (SPEC-142). An absent tune shows its number of subtunes and the
  first length (SPEC-046). An entry without a subtune number means "the default, decided when the file is
  there" (section 1, SPEC-061).
- Status: closed; confirmed by the reviewer at the second pass

### RV-015 What a playlist plays next was undefined
- Found: entries were "tune and subtune", but next always started the default subtune.
- Done: the queue is a list of entries with or without a subtune number (SPEC-062), and SPEC-065 says what
  follows each. SPEC-171 defines the favourite star for both kinds.
- Status: closed; confirmed by the reviewer at the second pass

### RV-016 Paths from downloaded files were trusted
- Found: a path from the song length database decided where a fetched file was written, unchecked; the
  database was read by the engine library's unfuzzed reader.
- Done: SPEC-154 checks every HVSC path wherever it comes in. The app's own reader builds the listing
  (SPEC-142). That reader and the reader of `7z`'s listing are fuzz targets (SPEC-205).
- Status: closed; confirmed by the reviewer at the second pass

### RV-017 The notice could be skipped
- Found: only one of five ways to a first download showed HVSC's notice; the text was a paraphrase and
  claimed everything came from HVSC's own site.
- Done: SPEC-152: no request of any kind until the notice is accepted, with an oracle for each way in. The
  text quotes HVSC's own two sentences and says the addresses are those in Settings. The wording is for the
  owner to approve.
- Status: closed; confirmed by the reviewer at the second pass

### RV-018 Requirements were reinterpreted without findings
- Found: "missing" playlist entries, stepping past the last subtune, the benchmark tunes on the host, the
  real `Commando.sid` lengths, "n of m" and the package's file name all differed from the requirements.
- Done: FIND-010, FIND-011 and FIND-012 raised and resolved in `requirements.md`. The subtune reads "n of m"
  (SPEC-096). The real lengths of `Commando.sid` are checked against HVSC's own documents (SPEC-252) and on
  the device (SPEC-260, step 5). SPEC-220 says how the file's name follows REQ-140.
- Status: closed; confirmed by the reviewer at the second pass

### RV-019 Failures had no taxonomy
- Found: which failures count toward "five in a row", what a 404, a timeout or a full card does, and
  whether a key press can retry during the back-off, were open.
- Done: SPEC-069 is a table of failures, each with its message, whether it counts, and what unattended
  playback does. The back-off applies to unattended playback only (SPEC-143). Messages for a failed fetch
  point to checking for a newer release.
- Status: closed; confirmed by the reviewer at the second pass

### RV-020 Help was compared with itself
- Found: help was made from a table in the source and tested against that table.
- Done: `keymap.tsv` is one file that the specification includes, the app is built from, and the tests read
  (SPEC-094, SPEC-107). `tools/check_repo.py` checks its form.
- Status: closed; confirmed by the reviewer at the second pass

### RV-021 Budgets had no measurements
- Found: several budgets had no key in the results; the index budget could not be measured on a device
  that fetches on demand; the "key script" was undefined.
- Done: SPEC-240 maps every budget to a measurement and how it is produced; SPEC-241 lists every key. The
  index budget is a rate. Fetch time is not counted in key-to-sound. SPEC-260 defines the key script.
- Status: closed; confirmed by the reviewer at the second pass

### RV-022 The results file would have destroyed itself
- Found: the user's private group carries the user's name, and the privacy check deleted any file holding
  it.
- Done: the group named after the user is left out (SPEC-241). SPEC-258 replaces and never deletes, matches
  whole words, and covers IPv6 and network card addresses.
- Status: closed; confirmed by the reviewer at the second pass

### RV-023 The device sitting was not planned
- Found: how the app is started, in what order things happen, and how the real keyboard is checked were
  not said; REQ-127's checklist was missing.
- Done: SPEC-260: the app is always started from the launcher and talks to the script through a request
  file; a table gives the order of the sitting, with a key step for the real key codes, a check that
  rendering is repeatable on the device, and lines for REQ-127.
- Status: the second pass found a remainder: the five timed starts came before HVSC was set up, and the complete download came after the benchmark, so a full collection was never measured. SPEC-260 reorders the sitting: set-up, then the optional complete download, then the timed starts and the benchmark. Closed; confirmed by the reviewer at the third pass

### RV-024 The index did not notice changes
- Found: added and removed files, and a changed root, went unnoticed; the user's own tunes had no MD5 to be
  found again by.
- Done: the index records its root (SPEC-160). Step two runs at every start and for each folder the browser
  lists, and the user's own tunes get an MD5 (SPEC-161, SPEC-173).
- Status: closed; confirmed by the reviewer at the second pass

### RV-025 The complete download could leave a mixed tree
- Found: a kill during the move had no recovery; the staging folder had no name; the `7z` child was not
  ended; storage was checked too late.
- Done: SPEC-144 has a named staging folder, a marker file with resume and roll-back at start, `7z` limited
  to one thread and ended with its process group, and playing switched off meanwhile. SPEC-145 gates the
  offer on the unpacker, memory, room and a writable folder, and HVSC must be set up first.
- Status: the second pass found a remainder: a failure, as opposed to a kill, while files are being moved had no rule; and after a kill the old listing was rebuilt before the marker was acted on. SPEC-144 keeps the old listing as `listing.old`, says what a failure in steps 7 and 8 does and how "Finish the download" completes it; SPEC-200 acts on the marker before anything reads the database. Closed; confirmed by the reviewer at the third pass

### RV-026 The audio references were not pinned
- Found: the `sidplayfp` program's version, build and command lines were not given, and stored hashes of
  floating-point output vary by machine.
- Done: SPEC-071 pins the program by version and checksum and gives the command; WAVs are made at test
  time and compared value for value; nothing is compared with stored samples.
- Status: closed; confirmed by the reviewer at the second pass

### RV-027 Could features were built into Must tests
- Found: items had no priority, and Must screens' oracles required Could parts.
- Done: every item has a Priority line; key map rows and Settings rows carry priorities; SPEC-115 says what
  a part that is left out looks like, and that the tests pass with every Should and Could part off.
- Status: the second pass found a remainder: several Must oracles still reached into Should and Could features, and a feature with no item of its own had nothing to carry its tests. SPEC-115 is now a table of named parts, each with a build switch; the key map and the Settings table name the part of every row; oracles that reach into a part say so; SPEC-250 runs the unit and simulator levels with all parts and with none. SPEC-025 is Must. At the third pass six Must oracles still reached into a part without saying so, and the job mark sat inside the mini player's part. Each of those oracles now names its part (SPEC-060, SPEC-062, SPEC-065, SPEC-069, SPEC-143, SPEC-154), and the job mark is always built (SPEC-147). FOURTH-RV-027

### RV-028 Muted voices and the filter were shown nowhere
- Found: the only display was the Could voice bars, whose "envelope level" the library does not report.
- Done: line 6 of the now-playing screen shows the chosen chip, the voices and the filter (SPEC-096,
  SPEC-074). The bars have a formula from the registers (SPEC-080).
- Status: closed; confirmed by the reviewer at the second pass

### RV-029 Windows-1252 was only partly covered
- Found: the 27 characters at 0x80 to 0x9F would have drawn as boxes; a 10-pixel font went against the
  platform's advice.
- Done: the fonts cover all of Windows-1252, with a test over every character (SPEC-010, SPEC-098). All
  text is 12 pixels or larger (SPEC-091).
- Status: closed; confirmed by the reviewer at the second pass

### RV-030 Messages had no identifiers, and panels could pop up under the user's fingers
- Found: tests could not name messages; a panel that appeared by itself could take an Enter meant for
  something else.
- Done: SPEC-116 lists every message with its identifier and kind. Notices never take keys (SPEC-113);
  only a key press opens a panel (SPEC-114).
- Status: closed; confirmed by the reviewer at the second pass

### RV-031 Key codes and typing were not specified
- Found: no table of key codes, no map from codes to characters, no repeat rule, no field limits.
- Done: SPEC-111 has the names and codes; SPEC-099 the character map, the cursor keys and the field limits;
  SPEC-045 the app's own key repeat.
- Status: closed; confirmed by the reviewer at the second pass

### RV-032 Skipping ahead assumed an engine eight times faster than it is
- Found: it would have caused underruns and then the "cannot keep up" message; elapsed time was defined
  two ways.
- Done: SPEC-066: skip renders as fast as the engine allows, up to eight times, underruns are not counted
  while skipping, and the elapsed time comes from the samples rendered.
- Status: closed; confirmed by the reviewer at the second pass

### RV-033 The queue's life was undefined
- Found: whether edits change a playing queue, how it is restored, and how it ends when nothing can be
  played; `state.ini` had no key table.
- Done: the queue is a copy (SPEC-062); SPEC-131 has the key table and how each kind of queue is restored;
  SPEC-143 and SPEC-069 give the endings.
- Status: closed; confirmed by the reviewer at the second pass

## Minor

### RV-034 Partial downloads and temporary files
- Found: HVSC's site sends no validators for resuming; partial and temporary files could not be told
  apart; the room check needed a size no request had fetched.
- Done: SPEC-132 names temporary and partial files differently and ages partial ones out. SPEC-147 resumes
  only with both validators and otherwise starts again. SPEC-146 uses fixed figures before the archive is
  listed.
- Status: closed; confirmed by the reviewer at the second pass

### RV-035 Settings clean-ups
- Found: two settings had no row; comments and unknown keys would have counted as faults; ranges were
  missing.
- Done: `power_on_delay` is gone (it is fixed at 0, SPEC-067) and the notice's acceptance is state, not a
  setting (SPEC-131). SPEC-133 says what is and is not a fault. SPEC-130 gives ranges. SPEC-040 and
  SPEC-101 cover `HVSC_BASE`. The speed factors are cache (SPEC-134).
- Status: closed; confirmed by the reviewer at the second pass

### RV-036 Tunes that a new release moved or changed
- Found: moved tunes stayed as duplicates; a changed tune's refetch contradicted "no request" and could
  overwrite the user's own file.
- Done: SPEC-148: a moved tune is renamed by its MD5; a changed one is fetched again only if the file is
  still HVSC's old one; SPEC-151 names that fetch.
- Status: closed; confirmed by the reviewer at the second pass

### RV-037 Fetch timing
- Found: the 700 ms wait also delayed a tune chosen with Enter; nothing limited fetching in bulk; no room
  check before a single fetch.
- Done: SPEC-141: Enter fetches at once; the next entry is fetched shortly before its turn; unattended
  fetches are limited per minute; the card must have room.
- Status: closed; confirmed by the reviewer at the second pass

### RV-038 Playlist details
- Found: name rules, collisions, the dialect samples, percent-encoding, duplicates and export's file name
  were open.
- Done: SPEC-170 and SPEC-172 give the name rules; SPEC-175 to SPEC-177 show each dialect as its player
  writes it; SPEC-179 names the export file. The dialect beyond REQ-105 is dropped.
- Status: the second pass found a remainder: whether an imported playlist takes its name from the file or from its `#PLAYLIST:` line was said both ways. SPEC-175: the `#PLAYLIST:` line when there is one, else the file's name, with one rule for length, control characters and a name already taken. Closed; confirmed by the reviewer at the third pass

### RV-039 Shuffle could not be tested
- Found: "the order the test lists" needs a defined generator and method.
- Done: SPEC-065 names Fisher–Yates and xorshift64*, where the chosen entry goes, what switching does, and
  the `--seed` option.
- Status: the second pass found a remainder: the method was named but did not give one permutation. SPEC-065 gives the generator's steps and constants, the seed 0 case, the loop, where the chosen entry goes, and the expected order for seed 1. Closed; confirmed by the reviewer at the third pass

### RV-040 Build and package details
- Found: two preset names, a clean build that both passes and fails without a maintainer, build paths in
  the program, a library missing from Depends.
- Done: SPEC-001 names both presets and the prefix map; SPEC-221 adds `libpulse0`, checks needed libraries
  against Depends, and limits the maintainer rule to the package build; SPEC-225 defines the home-path
  check and its exceptions.
- Status: closed; confirmed by the reviewer at the second pass

### RV-041 Boundaries nobody specified
- Found: empty and unreadable folders, an empty query, names that are not UTF-8, two instances, a
  screenshot that cannot be written, a collection copied in while the empty screen shows.
- Done: SPEC-041, SPEC-043 and SPEC-048 for folders; SPEC-100 and SPEC-162 for search; SPEC-131 for a
  remembered folder that is gone; SPEC-200 for a second instance; SPEC-108 for screenshots.
- Status: closed; confirmed by the reviewer at the second pass

### RV-042 Online search rested on an address nobody offered
- Found: the site's search returns no path to fetch by, and its address was hard-coded.
- Done: SPEC-165: not built in version 1.0 and not specified further until HVSC's maintainers offer it;
  the request oracles show that no such address is ever asked.
- Status: closed; confirmed by the reviewer at the second pass

### RV-043 Volume commands, the backlight after a kill, the brightness keys
- Found: `pactl`'s arguments were not given; a kill with the screen dark left it dark; the brightness keys
  did nothing.
- Done: SPEC-076 gives the argument lists. SPEC-207 saves the brightness before darkening and restores it
  at the next start. FIND-013 and the new REQ-129 give the brightness keys to the app (SPEC-104).
- Status: closed; confirmed by the reviewer at the second pass

### RV-044 The fixtures would have tested the readers against themselves
- Found: both the fixture files and their readers come from this project; the real files' line endings and
  release line were not stated.
- Done: SPEC-142, SPEC-148 and SPEC-252 state the format facts as observed in release 85, and an optional
  test reads HVSC's own documents from a folder outside the repository.
- Status: closed; confirmed by the reviewer at the second pass

## The second pass

The reviewer read draft 2 and judged each item from the specification's own text. It also checked outside
sources again: the `sidplayfp` 3.2.3 archive and every option of the reference commands; every key code of
SPEC-111 and SPEC-099 against the keyboard driver; the two sentences quoted from HVSC; and the store's
scripts at their pinned commits.

Result: 37 of the 44 items closed and 7 with a remainder (above), and 16 new items, RV-045 to RV-060, most
of them introduced by the rewrite itself. None was a blocker. Draft 3 answers them.

### RV-045 (major) Tune fetches waited behind the long worker
- Found: fetching a tune was a "download" on the worker that also builds the index and sets HVSC up, so a
  tune chosen straight after set-up waited for the index.
- Done: SPEC-022 has a fetch worker of its own, on which a tune that is to be played goes first; SPEC-174
  gives way to it. An oracle holds the index build and checks the request is made at once.
- Status: closed; confirmed by the reviewer at the third pass

### RV-046 (major) The path rule shut out the user's own file names
- Found: every path had to be in HVSC's alphabet, so a tune named with a space or an accent could not be a
  favourite, a playlist entry or the remembered tune.
- Done: SPEC-154 has two rules: the strict one for the song length database and for anywhere a file is
  written, and a wider one for paths that are only read. SPEC-170 and SPEC-041 follow.
- Status: closed; confirmed by the reviewer at the third pass

### RV-047 (major) Fetching ahead was outside the state table
- Found: the next tune is requested while another plays; what happens at the song's end, on failure and on
  a key press was not said, and SPEC-143's test queues could not be reached unattended.
- Done: SPEC-141 defines the fetch-ahead and each of those cases; SPEC-060 has its rows; SPEC-143's queues
  start with a present tune.
- Status: closed; confirmed by the reviewer at the third pass

### RV-048 (major) Screen shortcuts acted inside panels
- Found: TAB and P were "moving" keys that also applied in panels, against "a panel takes the keys".
- Done: the key map has three groups: `always`, `lists` (which work in panels) and `screens` (which do
  not). SPEC-092, SPEC-094 and SPEC-107 follow, with an oracle.
- Status: closed; confirmed by the reviewer at the third pass

### RV-049 (major) The screen description's values had no stated form
- Found: two test writers could write `6581` and `MOS6581`; `boxes` cannot be written by hand before the
  code; a mark and the empty screen's action did not fit the fields.
- Done: SPEC-254 has a table of value forms, the mark `fetching`, `place`, `text_w` in `boxes`, the empty
  screen's row, and an oracle that leaves `boxes` and `list.objects` to property checks.
- Status: closed; confirmed by the reviewer at the third pass

### RV-050 (major) The self-test was only outlined
- Found: the request file's form, the step names, how plain starts differ from test starts, the user the
  script runs as, and the self-test's screens and keys were missing.
- Done: SPEC-260 gives the two files' forms, the four steps, who runs what, and the screens; the key map
  has the places `selftest` and `key-step`; SPEC-090 and SPEC-254 name the screen.
- Status: closed; confirmed by the reviewer at the third pass

### RV-051 (major) The wrong store check was named
- Found: the script SPEC-223 named does not check the summary's length; the store's own check is another
  script, which also wants a share code of four characters, a store icon of 128 to 512 pixels and
  screenshots of 320×170. The launcher icon's "64 pixels" had no source.
- Done: SPEC-223 lists the fields the store's script asks for and runs that script at its pinned commit;
  SPEC-222 gives the icon sizes of the app template (100, 100 and 80); SPEC-224 says which package checks
  are made, and that test builds carry a maintainer the store refuses.
- Status: closed; confirmed by the reviewer at the third pass

### RV-052 (major) A subtune mark of REQ-105 had been dropped
- Found: the leading `{N}` was removed with the dialect that was beyond the requirement, but REQ-105 lists
  that mark.
- Done: SPEC-175 reads a leading `{N}` again, with a sample line.
- Status: closed; confirmed by the reviewer at the third pass

### RV-053 (minor) Notices queued stale values
- Found: five volume presses showed five levels over 15 s; the offer of the light engine could expire
  before it showed; two things that happen by themselves did change the screen.
- Done: SPEC-113: a notice replaces one with the same identifier; the offer shows for its 10 s; the two
  exceptions are named and wait for panels and help to close.
- Status: closed; confirmed by the reviewer at the third pass

### RV-054 (minor) Unattended playback and the notice
- Found: with the notice not accepted, unattended playback reaching an absent tune had no rule.
- Done: it is a fetch that cannot be made (SPEC-069, SPEC-143, SPEC-152), with `MSG_NOTICE_NEEDED`.
- Status: closed; confirmed by the reviewer at the third pass

### RV-055 (minor) Lengths by path, against REQ-013
- Found: a file with other contents at an HVSC path got that path's lengths.
- Done: SPEC-063: by the file's MD5 for a present tune; by path only for an absent one.
- Status: closed; confirmed by the reviewer at the third pass

### RV-056 (minor) A simulator oracle compared with a stored reference
- Done: SPEC-070 compares with a second run that presses nothing.
- Status: closed; confirmed by the reviewer at the third pass

### RV-057 (minor) Key map odds and ends
- Found: help listed TAB where it does nothing; the filter's years were typed in a panel with no typing;
  the Search screen's two places were not named; a held Up on the now-playing screen would step 25 tunes a
  second; unnamed key codes; "F1 to F12" for ten names.
- Done: a `screens` key can be switched off in one place (`nothing`); SPEC-164's years are stepped;
  SPEC-100 and SPEC-254 name the places; SPEC-045 ties repeating to the action; SPEC-111 says unnamed codes
  are ignored and names F1 to F10.
- Status: closed; confirmed by the reviewer at the third pass

### RV-058 (minor) The font test did not fit one tune or the script language
- Done: SPEC-252 has seven tunes for the characters; SPEC-010's glyph check is a C++ test of the simulator
  level (SPEC-253).
- Status: closed; confirmed by the reviewer at the third pass

### RV-059 (minor) Undefined words and missing messages
- Found: "HVSC is set up" and "job" were never defined; some texts had no identifier.
- Done: SPEC-140 defines both; SPEC-116 gains `MSG_CHECK_FAILED`, `MSG_IMPORT_NOTHING`,
  `MSG_ENTRY_MISSING` and `MSG_USBSID_MISSING`, and puts `MSG_NEED_ROOM` right.
- Status: closed; confirmed by the reviewer at the third pass

### RV-060 (minor) Levels that did not fit their oracles, and two lists of key map rules
- Done: SPEC-110's and SPEC-258's oracles are integration tests; SPEC-257's is a check of the workflow file
  and one observation at the Phase 3 gate; SPEC-256 refers to SPEC-094's rules; SPEC-205 runs thirteen
  targets for 15 s‡ each inside a budget of eight minutes.
- Status: closed; confirmed by the reviewer at the third pass

## The third pass

The reviewer read only what draft 3 changed. It recomputed the shuffle order of SPEC-065, ran the store's
metadata script against SPEC-223's fields and its four bad copies, checked the app template's icon sizes,
and read `tools/check_repo.py`'s key map rules against SPEC-094.

Result: 22 of the 23 items it was asked about closed; RV-027 with a small remainder (above); and six new
items from draft 3's own changes, one of them major. Its verdict: fit to write tests from, except the tests
of the device benchmark until RV-061 is settled.

### RV-061 (major) The benchmark's key script could not reach the screens it measures
- Found: the benchmark was said to show the self-test screen in the browser's place, but its key script
  opens folders, the detail line and Search, which that screen cannot reach.
- Done: SPEC-260: the benchmark closes the self-test screen and plays on the app's ordinary screens, with
  the key script pressing keys as a user would; the job mark shows its progress; the self-test screen
  returns for "Done". SPEC-090 says the test opens and closes that screen.
- Status: FOURTH-RV-061

### RV-062 (minor) Which fetch is cancelled, and "both workers"
- Done: SPEC-141: a played tune's request cancels a fetch-ahead or an earlier such request; a file of "Fetch
  missing" in flight finishes first. SPEC-253's settled rule names three workers.
- Status: FOURTH-RV-062

### RV-063 (minor) Tests that wait, and a store check that reported by design
- Found: a test waiting for Phase 5's screenshots would have kept the required check red; and the tests'
  maintainer was one the store's script reports, against REQ-142's "report nothing".
- Done: SPEC-256 has a waiting list: a listed test is in order while it fails as expected, and a fault when
  it fails otherwise or passes. The tests build with a maintainer in the reserved `.test` name, which the
  store's script accepts (SPEC-221, SPEC-224).
- Status: FOURTH-RV-063

### RV-064 (minor) "Finish the download" needed room it does not use
- Done: SPEC-102 and SPEC-144: it is offered whenever a complete download was cut short while moving files,
  whatever SPEC-145's conditions say.
- Status: FOURTH-RV-064

### RV-065 (minor) Screen description leftovers
- Done: the fake backend's `position()` follows the fake clock (SPEC-025); `lines` and `text` are checked
  by what they contain, and the browser's title at the root is `/` (SPEC-254); `save-samples` and
  `expect-same` let SPEC-070 compare two recordings; `boxes` has `text_x` for SPEC-098.
- Status: FOURTH-RV-065

### RV-066 (minor) Six loose sentences
- Done: SPEC-069 says which failures wait 2 s‡ and which are passed over at once, and that
  `MSG_NOTICE_NEEDED` is a notice there; SPEC-060 says which keys take over from the unattended wait; the
  `selftest` place has no key of its own; the 10 s hold is in the first round (SPEC-260); an imported
  playlist's name is at most 40 characters with its number (SPEC-175).
- Status: FOURTH-RV-066

## The last check

FOURTH-PASS
