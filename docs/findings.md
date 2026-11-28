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

### FIND-036 The step-5 answer and the app's own record of the complete download disagree
- Phase: 5, implementation (found in the results of the device sitting, 28 November 2026)
- Observed: in `sidplayzero-device-20261128-123755.txt`, line 15 says `download_everything_offered = yes`, and lines 12 to 14 show every SPEC-145
  condition met (4,332 MiB of memory and swap, `7z` present, 15,453 MiB free); line 88 says
  `check_download_everything = not offered`. HVSC was still set up from the sitting of 27 November, so an
  unfinished earlier download would put "Finish the download" in that row in place of "Download
  everything" (SPEC-148), which the checklist does not mention.
- Concerns: SPEC-145, SPEC-148, `docs/device-checklist.md` step 5
- Resolution so far: none until it is known which rows the screen showed; the second sitting begins from
  an empty root, where no earlier download can be unfinished. The owner's screenshots of a set-up from empty
  show a likelier cause: when the set-up ends the app goes to the browser by itself, and the HVSC screen is
  reached only by Tab for Settings and then 5, which step 5 did not say. Step 5 of the checklist and of
  `tools/device-run.sh` now says so.
- Status: open. Asked the owner which rows the HVSC screen showed; the second sitting sets HVSC up from
  empty and looks again

### FIND-035 Two of the budgets were not measured at the sitting
- Phase: 5, implementation (found in the results of the device sitting, 28 November 2026)
- Observed: `sidplayzero-device-20261128-123755.txt` has no `hvsc_setup_s` line, because HVSC was already set up from the sitting of
  27 November and SPEC-140 did not run; and line 70 says `index_tunes_per_second = not measured`, because
  line 69 shows only four tunes present, below SPEC-240's 1,000.
- Concerns: SPEC-240's rows "Setting HVSC up" (2 minutes‡, REQ-080) and "Index, step two" (200 tunes a
  second‡, REQ-091)
- Resolution so far: the two budgets keep their marks; `device-run.sh --only 4,5,8` (FIND-038) gives the
  steps that measure them.
- Status: open. A second, short sitting sets HVSC up from an empty root, runs "Download everything", and runs
  the benchmark step again with the whole collection present

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
- Observed again in phase 4, tests: in libsidplayfp 3.1.2 it is not one header. The driver itself,
  `src/builders/usbsid-builder/driver/src/USBSID.cpp` and `USBSID.h`, says "version 2" with no "or any later
  version", as the exSID files do; the builder around it does not. So SPEC-011's test fails for a build with
  the part `usbsid` until those two files are cleared. Also, the setting `output` that SPEC-191 uses is in
  neither SPEC-101's rows nor SPEC-130's keys.
- Status: open; it matters only when REQ-111 is taken up. The part `usbsid` stays out of every build until then

## Closed

### FIND-039 The self-test dropped the set-up's time when HVSC was set up from empty
- Phase: 5, implementation (found from the owner's screenshots of a set-up from empty, 28 November 2026)
- Observed: `SelfTest::watch_setup` wrote `hvsc_setup_s` only if the listing was already open at its first
  check after the set-up's job ended, and forgot the start otherwise. The listing reopens later, on the long
  worker, after the ready notice, so on a set-up from empty the time was usually dropped. The simulator's
  benchmark test left `hvsc_setup_s` out, so nothing caught it.
- Concerns: SPEC-240's row "Setting HVSC up" ("from Enter on the notice to the notice that it is ready"),
  SPEC-241, FIND-035
- Resolution: the set-up now tells the self-test as its job begins (`HvscListener::hvsc_began`) and as it
  is ready (`HvscEvents::hvsc_ready`), so nothing is looked for between two checks; a first fix that still
  looked for the start every 100 ms failed the new test, whose set-up ends sooner than that. A set-up that
  fails or is stopped is not measured. The simulator test
  `test_setup_time` sets HVSC up from empty under the self-test and checks the key. It changes the self-test
  only, as SPEC-240 already says it should be measured.
- Status: closed. Within the owner's word of 28 November 2026 to fix the measurements

### FIND-038 A second sitting for what the first could not measure had to be the whole sitting again
- Phase: 5, implementation (found in the results of the device sitting, 28 November 2026)
- Observed: the measurements FIND-035 leaves open need steps 4, 5 and 8 only, but `tools/device-run.sh`
  gives every step from the one it has reached, about an hour with the tester present.
- Concerns: SPEC-260, `tools/device-run.sh`, `docs/device-checklist.md`
- Resolution: `--only` followed by step numbers gives those steps only; steps 1 and 11, the install and
  the removal with the results file, are always done, and `sitting.state` remembers the steps so the
  sitting is carried on as before. It changes the test tooling only, not the app.
- Status: closed. Within the owner's word of 28 November 2026 to send a package for a short second sitting

### FIND-034 The audio rate and format in the results were the ones asked for, not the ones opened
- Phase: 5, implementation (found in the results of the device sitting, 28 November 2026)
- Observed: `sidplayzero-device-20261128-123755.txt` lines 53 and 54 say `audio_rate = 48000` and `audio_format = s16 stereo`. The self-test
  wrote the setting `sample_rate` and a fixed "s16 stereo", so the lines would read the same whatever the
  system opened, and SPEC-243's open point "the system's sample rate and format" is not settled by them.
- Concerns: SPEC-241's row `audio_backend`, `audio_rate`, `audio_format` ("What miniaudio opened"); SPEC-243
- Resolution: the app now asks miniaudio, once the stream is open, for the rate, format and channels the
  backend opened it with, and writes those. SPEC-241 already says this; no document changes. The open point
  is settled at the second sitting. Line 52, `audio_backend = PulseAudio`, was already taken from miniaudio.
- Status: closed in the code; the measurement is owed by the second sitting (FIND-035)

### FIND-032 The device sitting confirms most of SPEC-240's budgets and SPEC-243's open points
- Phase: 5, implementation (the results of the device sitting, 28 November 2026, `sidplayzero-device-20261128-123755.txt`)
- Observed, against SPEC-240, each with its line of the results file:
  - line 7, `start_ms` 971, 563, 184, 211, 368, 174 and 504: all within 2 s (REQ-055)
  - line 8, `exit_ms` at most 115: within 1 s (REQ-120)
  - line 58, `folder_first_ms_max = 28`, and line 59, `folder_again_ms_max = 0`: within 1 s and 300 ms
    (REQ-003)
  - line 60, `rows_per_second_min = 23.7`, and line 61, `row_pause_ms_max = 71`: at least 20 and at most
    100 ms (REQ-003)
  - line 57, `key_to_sound_ms_max = 84`: within 500 ms (REQ-020)
  - line 55, `underrun_frames = 0`, and line 56, `cpu_percent = 25.5`: no underruns, and under 60 % (REQ-050)
  - line 64, `key_to_screen_ms_p95 = 4`: within 100 ms (REQ-054)
  - line 63, `search_ms_max = 65`: within 500 ms (REQ-090)
  - line 67, `index_step_one_s = 1.34`: within 60 s, and line 71, `index_underrun_frames = 0` (REQ-091)
- Observed, against SPEC-243's open points:
  - R8: line 11 shows the user in `audio`, `video`, `input` and `gpio`; lines 43 to 45 show the backlight
    writable and the framebuffer and keyboard open
  - R7: line 43, `backlight_writable = yes`; line 92, the screen stayed on past two minutes while a tune
    played; line 90, the brightness keys worked
  - R1: lines 12 to 15, every SPEC-145 condition met on the device
  - R2: lines 47 and 48, `speed_factor_accurate = 1.081081` and `speed_factor_light = 6.369427`; lines 72 to
    79, `auto` chose sidlite for all four benchmark tunes, whose render load was 15.8 % to 22.0 %
  - CON-20: line 49, `render_repeatable = yes`
- Concerns: SPEC-240, SPEC-243, SPEC-072, SPEC-004; REQ-003, REQ-020, REQ-050, REQ-054, REQ-055, REQ-090,
  REQ-091, REQ-120
- Resolution: SPEC-072's threshold of 2.5 is confirmed: reSIDfp at 1.08 times real time cannot play a
  one-SID tune with room to spare, sidlite plays the three-SID tune at 22 % load, and line 55 shows no
  underruns over the 30 minutes, so SPEC-004's default `auto` stands. The ‡ is removed from each budget
  above in `requirements.md` and `spec.md`. Three rows are not confirmed: memory (FIND-033), the detail
  line (FIND-037) and the two not measured (FIND-035); the sample rate and format wait on FIND-034.
- Status: closed. Confirmed by the owner on 28 November 2026

### FIND-031 A complete collection was not kept complete by an update
- Phase: 5, implementation (asked for by the owner after the device sitting, 27 November 2026)
- Observed: the release HVSC is set up with is recorded (`hv_sids.txt`, "HVSC #85"), and "Download
  everything" can be run whenever SPEC-145 allows it, but nothing remembered that the collection was
  complete: "Update to #m" brought only the new documents, and the new release's tunes would have been
  fetched one by one as they were played.
- Concerns: REQ-084, REQ-089, SPEC-102, SPEC-131, SPEC-144, SPEC-148
- Resolution: a complete download records `hvsc_complete_release` in `state.ini`; the HVSC screen says
  "HVSC #n, complete"; and "Update" of a complete collection runs the complete download of the newer
  release, when SPEC-145 allows it at that moment, so the collection stays complete. HVSC's smaller update
  archives are not used: they carry scripts of moves and deletions, and the complete download's own move,
  which removes only files the old release had and leaves the user's own, is already built and tested.
- Status: closed. Asked for by the owner, and the design confirmed by the owner, on 27 November 2026

### FIND-030 The device sitting's offline starts needed the SSH session to survive turning networking off
- Phase: 5, implementation (found at the device sitting, 27 November 2026)
- Observed: step 6 has the tester turn networking off, which ends the SSH session the script runs in; the
  tester had to keep the script alive with tmux.
- Concerns: SPEC-260, `tools/device-run.sh`, `docs/device-checklist.md`
- Resolution: the script keeps the step it has reached in `sitting.state` and carries on from there when it
  is run again with the same package; `--restart` begins again. Step 6 says to reconnect and run it again.
- Status: closed. Decided by the owner on 27 November 2026

### FIND-029 The key step asked for every key, and could not be taken up again
- Phase: 5, implementation (found at the device sitting, 27 November 2026)
- Observed: the key step asked for all 67 keys of SPEC-111 and SPEC-099, one at a time, which was a pain,
  and a second start of the step began again from the first key, so it was not done properly the second
  time.
- Concerns: SPEC-260, SPEC-241, SPEC-111
- Resolution: the step asks only for what could differ on a device: the 19 keys pressed with `fn` and the
  six characters of the `sym` layer, 25 in all. Each answer is written as it is given (`key_answer`), and a
  step started again begins at the first key with no answer.
- Status: closed. Decided by the owner on 27 November 2026

### FIND-028 HVSC writes some lengths with fewer than three decimals, and the listing refused HVSC
- Phase: 5, implementation (found at the device sitting, 27 November 2026)
- Observed: on the device, "Get HVSC" stopped with "HVSC could not be set up" at step 4. SPEC-142 had a
  length as `m:ss` or `m:ss.SSS`, and the reader took that literally; HVSC #85's `Songlengths.md5` also
  writes lengths such as `0:03.5` and `0:02.70`. 2,339 of its lines were skipped, more than one line in a
  hundred, so the whole listing was refused, and no tune of HVSC could be listed or fetched. The fixtures
  were written from the same sentence, so no test saw it; the test with HVSC's own documents runs only where
  `SIDPLAYZERO_REAL_HVSC` names them.
- Concerns: SPEC-142, SPEC-252, SPEC-140
- Resolution: a length is `m:ss`, or `m:ss` and a point with one to three digits of a second. SPEC-142 and
  SPEC-252 say so, the reader takes it, the fixtures write one length that way, and two unit tests check the
  forms. With HVSC #85's own database the listing now holds 61,157 tunes in 1,981 folders and 87,868
  lengths, none skipped, the figures SPEC-142 gives.
- Status: closed. A defect of the specification against HVSC's real file; nothing the owner decided changes

### FIND-025 The answer to a key can wait three seconds behind an earlier notice
- Phase: 5, implementation
- Observed: two tests press a key right after another key's notice and expect the new notice at once:
  SPEC-076's presses a volume key that fails right after one that worked (`MSG_VOLUME_FAILED` behind
  `MSG_VOLUME`), and SPEC-092's presses Mute right after Fav ("Muted" behind `MSG_FAV_ADDED`). SPEC-113 has
  a notice with another identifier wait its turn, up to 3 s, so the user would see the answer to the key
  before last.
- Concerns: SPEC-076, SPEC-092, SPEC-113
- Resolution: a notice raised by a key the user has just pressed replaces the notice showing, at
  once; notices that come from work in the background (a job ending, a fetch failing) still wait their turn.
  The answer in view is then always the answer to the last key. Both tests hold as written.
- Status: closed. Decided by the owner on 27 November 2026: the answer to a key replaces the notice showing.
  SPEC-113 says so; a notice raised while a key is handled is its answer, and so are the volume's,
  brightness's and screenshot's, which come from the quick worker

### FIND-027 A settings file too large to read was read, to be copied
- Phase: 5, implementation (found by the security review)
- Observed: SPEC-133 has a faulty `settings.ini` copied to `settings.ini.bad`. For a file over the 64 KiB
  limit, copying meant reading the whole file first: a settings file of some gigabytes (or a link to a
  huge file) made the app run out of memory at every start.
- Concerns: SPEC-133, SPEC-204
- Resolution: a `settings.ini` over the limit is never read; it is renamed to `settings.ini.bad` (or
  `.bad.N`), which keeps its bytes as they were. A faulty file within the limit is still copied. Since the
  rename leaves no `settings.ini`, the next start finds nothing faulty and makes no `.bad.2`.
- Status: closed. Confirmed by the owner on 27 November 2026

### FIND-026 The archive check trusted one line of 7z's listing, and the move trusted every name
- Phase: 5, implementation (found by the security review)
- Observed: SPEC-149 recognised a link only "by its attributes". 7-Zip lists other archive types under a
  `.7z` name, and for those it reports links as a mode (`lrwxrwxrwx`) or a `Symbolic Link`/`Hard Link`
  line, with no attributes: such an archive passed the app's check, and only 7-Zip's own refusal stopped
  the escape the review tried. SPEC-144's move then put every unpacked file into the collection, hidden
  names and any folder included, which matters when the collection setting names a wide folder.
- Concerns: SPEC-149, SPEC-144, SPEC-204, REQ-123
- Resolution: SPEC-149 now refuses an archive that is not of type 7z (listed and unpacked with `-t7z`), an
  entry that is a link or special by anything the listing says or of an unknown kind, and a listing longer
  than SPEC-204's limit; `7z` runs in its own process group with a time limit. Only paths that pass HVSC's
  rules (SPEC-154), and HVSC's documents, are moved into the collection. Nothing a user sees changes for a
  genuine HVSC archive.
- Status: closed. Confirmed by the owner on 27 November 2026

### FIND-024 Details the tests had to choose
- Phase: 4, tests
- Observed: the specification is silent on these, and each test had to pick one. Each choice is in the test
  and in a comment in the header the test uses.
  - SPEC-040: an empty `HVSC_BASE` counts as unset; `~` is replaced in it as in the setting.
  - SPEC-046 and SPEC-096: times are cut down, not rounded: 0:03.500 shows as 0:03, as 3:55.594 is 3:55.
  - SPEC-048: a root with only HVSC's documents holds entries (the absent tunes); a folder with only other
    files shows "(empty)".
  - SPEC-099: a prompt opens with the cursor at the end of its text.
  - SPEC-101: stepping a setting stops at the ends, and does not wrap.
  - SPEC-104: brightness "up" at 100 % sets nothing, as "down" at 10 % does.
  - SPEC-107: a help line is the key's name as SPEC-111 writes it on the device (`fn+X`), then the words.
  - SPEC-116: the names of what a message is about are `n`, `m`, `path`, `name` and `step`.
  - SPEC-131: `queue_place` counts from 1.
  - SPEC-141: a fetch-ahead counts towards the six fetches a minute.
  - SPEC-143: a failed Enter does not start the 60 s pause.
  - SPEC-170: playlist files end lines with a line feed and the file with one; seconds are cut down;
    an entry without a subtune number takes its default subtune's length.
  - SPEC-204: a line past the limit is cut and the rest of it dropped; a zero byte ends its line likewise.
  - SPEC-207: a key that turns the screen back on clears `backlight_saved`; the seconds count from the
    last key.
  - SPEC-241: which keys a run off the device writes, and the form of each value, are the table in
    `src/core/metrics.cpp` (`yes`/`no` for `unpacker`; the version keys with their names).
- Concerns: the items named above
- Resolution: the specification states each of these where its item is.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; each item named says it, with "(FIND-024)"

### FIND-023 The screen description has no field for some things the oracles look at
- Phase: 4, tests
- Observed: SPEC-254 names no field for the number of search results, "No results", "Indexing n %", that a
  filter is set, a row of the Playlists screen (name and count), the star on the now-playing screen, or the
  name of a prompt's or the Search field's text field. Nor does it say whether `help` holds every line,
  whether `player` is filled on every screen, and whether `boxes[].text` is the text as drawn.
- Concerns: SPEC-254, SPEC-091, SPEC-098, SPEC-100, SPEC-107, SPEC-164, SPEC-172
- Resolution: where no field is named for a thing on screen, a test reads it from `text`, the
  number of results is `list.total`, the search field's text is `fields[0]`, `help` holds every line,
  `player` is filled on every screen (`state = idle` with nothing loaded) and `boxes[].text` is the text as
  drawn. Phase 5 may add a field only by changing SPEC-254 and its schema.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-254 says it so

### FIND-022 Must items whose oracles need a part
- Phase: 4, tests
- Observed: SPEC-009, SPEC-151 and SPEC-154 are Must, but clauses of their oracles need the part
  `playlists` (playlist files, importers, editing a playlist) without saying "Part". A test of a Must item
  may not need a part (SPEC-115).
- Concerns: SPEC-009, SPEC-151, SPEC-154
- Resolution: those clauses are marked "Part `playlists`", and their tests carry that part, as
  they already do.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-009, SPEC-151 and SPEC-154 mark those clauses

### FIND-021 Two items said different things about the same case
- Phase: 4, tests
- Observed:
  - SPEC-133: a known key given twice is a fault "(the last wins)", and "the setting concerned keeps its
    default".
  - SPEC-133 and SPEC-204: a `settings.ini` over 64 KiB gives `MSG_SETTINGS_RESET` by one and
    `MSG_FILE_TOO_LARGE` by the other.
  - SPEC-154 and SPEC-178: the importers refuse a backslash, and turn backslashes into slashes.
  - SPEC-141 and SPEC-203: a fetched file that is no tune at all is `MSG_FETCH_MISMATCH` by one and
    `MSG_INVALID_SID` by the other.
  - SPEC-106 and SPEC-113: notices wait their turn, so `MSG_HOLD_TO_LEAVE` could wait longer than the one
    second after which the app has left.
  - SPEC-161: "records of tunes no longer there are removed" and "a record for every tune in the listing".
  - SPEC-134: "the `import` and `export` folders beside them" is read as inside the playlists folder by one
    area and could be read as beside it.
- Concerns: SPEC-106, SPEC-113, SPEC-133, SPEC-134, SPEC-141, SPEC-154, SPEC-161, SPEC-178, SPEC-203, SPEC-204
- Resolution: as the tests have it: the last of two lines wins, and the file is still a fault;
  a `settings.ini` over the limit is `MSG_SETTINGS_RESET`; importers turn backslashes into slashes first
  and then apply SPEC-154; the MD5 is compared first, so a page that is no tune is `MSG_FETCH_MISMATCH`;
  `MSG_HOLD_TO_LEAVE` shows at once, in place of any notice; a tune HVSC lists whose file is gone becomes
  absent again, and only a tune no listing has loses its record; the folders are
  `playlists/import` and `playlists/export`.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-106, SPEC-113, SPEC-133, SPEC-134, SPEC-141, SPEC-161 and SPEC-178 say it so

### FIND-020 Some clauses could not be tested as written, and were tested another way
- Phase: 4, tests
- Observed:
  - SPEC-022's oracle wants an index build held and a set-up download stalled at once; both are jobs of the
    long worker, which runs one at a time. Its third clause wants a volume key answered within 100 ms while
    a folder takes 2 s to list, but volume is on the same quick worker as the listing.
  - SPEC-040's "changing the setting makes the browser show the new root" needs `HVSC_BASE` unset, and the
    runner sets it for every script.
  - SPEC-096's "for each row of the state table a script brings the player to that state" cannot reach
    Loading: settling finishes a load at once, and no fake holds a read back.
  - SPEC-004 and SPEC-079 need "a stored speed measurement" in a script, and neither the file nor its form
    is named.
  - SPEC-150's "the release build refuses `http://127.0.0.1`" cannot be seen, since only a test build has
    a way in.
  - SPEC-072 times the engines "with the real clock" in tests that use the fake one.
- Concerns: SPEC-004, SPEC-022, SPEC-040, SPEC-072, SPEC-079, SPEC-096, SPEC-150
- Resolution: each oracle is reworded to what the tests do: SPEC-022 holds the index build in one
  script and stalls a release update (part `releases`) in another, and uses Tab, Down and ESC for the keys
  that must answer; SPEC-040's clause is a test that starts the program with `HVSC_BASE` unset; SPEC-096's
  Loading row is checked at the unit level; SPEC-072 names the file, `speed.ini` in the cache folder with
  `accurate = F` and `light = F`, and measures through the `Clock` seam; SPEC-150's clause is checked
  through `spz_tool fetch --release-rules`, which uses the release build's rules. No behaviour changes.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-004, SPEC-022, SPEC-040, SPEC-072, SPEC-096 and SPEC-150 say it so

### FIND-019 Things the layers cannot do as the specification has them
- Phase: 4, tests
- Observed: the `Files` seam (SPEC-021) and the rule that `core` uses only the C++ library (SPEC-020)
  leave no way to do four things the specification asks of `core`: map `listing.bin` (SPEC-142, "it is
  mapped, not loaded"); ask for the file system's block size (SPEC-146); append lines to `selftest.results`
  (SPEC-241, SPEC-260); see a change within the same second, since `FileInfo` keeps whole seconds and the
  folder cache goes by modification time (SPEC-043).
- Concerns: SPEC-020, SPEC-021, SPEC-043, SPEC-142, SPEC-146, SPEC-241
- Resolution: `Files` gains `map`, `block_size` and `append`, and `FileInfo` keeps nanoseconds
  as the file system gives them. The fake does each in memory. No behaviour a user sees changes. Made in
  Phase 5 with the code, because the fakes and the tests that use `Files` change together.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-021 lists the four operations and SPEC-142 maps the listing through `Files`; the code and the fakes follow in Phase 5

### FIND-018 The order of names did not say which way letters are folded
- Phase: 4, tests
- Observed: SPEC-042 sorts "without regard to the case of ASCII letters" and does not say whether letters
  are compared as lower or upper case. The two give different places to `_ [ ] ^` and the backtick:
  `Ace_2.sid` comes before or after `Aces.sid`. HVSC uses `_` everywhere, and `listing.bin` follows the same
  order.
- Concerns: SPEC-042, SPEC-142, SPEC-162
- Resolution: ASCII letters are compared as lower case, as `tools/make_fixtures.py` already
  does; SPEC-162's "in order of path" is that same order, folder by folder.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-042 and SPEC-162 say it so

### FIND-017 Holding a key gave one row more in its first second than the oracle counts
- Phase: 4, tests
- Observed: SPEC-045 has a held arrow repeat "after 400 ms, then 25 times a second", and its oracle has
  Down held for one second move 16 rows. Repeats at 400, 440, … 1,000 ms are sixteen, plus the press: 17.
- Concerns: SPEC-045, SPEC-093
- Resolution: the first repeat comes 440 ms after the press, then one every 40 ms, so that the
  oracle's 16 rows hold. The tests are written so.
- Status: closed. Accepted by the owner on 26 November 2026, at the Phase 4 gate; SPEC-045 says it so

### FIND-016 The key step did not say in which order it asks for keys, or how
- Phase: 4, tests
- Observed: SPEC-260's oracle has a script "feed the keys asked for with one wrong". A script is a list of
  lines and cannot read the screen to choose its next key, so it can only be written if the order of the
  keys is known. The item said "one key at a time, from SPEC-111's table and the characters", and nothing
  about the order, about what the screen shows for the key asked for, or about what counts as a wrong
  answer.
- Concerns: SPEC-260
- Resolution: SPEC-260 has a paragraph on the key step: 67 keys in the order of SPEC-111's table and then
  the six characters, the line "Key n/67: NAME", one answer for each key, and what `key_codes_wrong` holds.
  The key step is seen only in the device test; nothing a user of the app sees changes.
- Status: closed. Confirmed by the owner on 26 November 2026, at the Phase 4 gate

### FIND-015 The test commands were required to be in the README
- Phase: 4, tests
- Observed: REQ-161's acceptance had the test commands "in the README", and SPEC-250 put the list of what
  each level needs there too. On 25 November 2026 the owner asked for the way of working to be moved out of
  the README, so that the README is about taking the software into use.
- Concerns: REQ-161, SPEC-250
- Resolution: the commands, what each level needs, the phases and their rules are in `CONTRIBUTING.md`,
  which the README links to. REQ-161's acceptance and SPEC-250 say so.
- Status: closed. The change is the owner's own request

### FIND-014 The check for C64 ROMs asked for checksums nobody publishes
- Phase: 3, test framework
- Observed: SPEC-153 has the repository's check recognise the C64's ROMs by their SHA-256. The checksums of
  those ROMs that can be cited without having the ROMs are CRC-32 and SHA-1, in MAME's source
  (`src/mame/commodore/c64.cpp`). Working out SHA-256 sums would have meant downloading the ROMs, which
  this project does not do.
- Concerns: SPEC-153
- Resolution: SPEC-153 names the five ROM revisions and their SHA-1 as MAME publishes them, and
  `tools/check_repo.py` holds those. Nothing the app does changes.
- Status: closed. A correction to a check, not to behaviour; reported to the owner at the Phase 3 gate

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
- Status: closed. Accepted on 25 November 2026, when the owner approved the specification and had the pull
  request with the changed requirements merged

### FIND-010 "Missing" in the playlist requirements came to mean two things
- Phase: 2, specification (raised by the independent review, item RV-018)
- Observed: REQ-103 says an entry "whose tune is not on the device" is shown as missing and skipped, and
  REQ-104 offers to fetch such entries. Since FIND-006, most of HVSC is not on the device and any tune HVSC
  lists is fetched when played (REQ-088). Read literally, REQ-103 would make a playlist skip nearly
  everything on a device that fetches on demand.
- Concerns: REQ-103, REQ-104, REQ-093
- Resolution: in `requirements.md`, an entry is missing only when its tune is neither on the device nor in
  HVSC's list; one that HVSC lists is fetched when played. REQ-104 is the fetching of a playlist's tunes
  ahead of playing them, and REQ-093 points at REQ-088 for fetching a single tune. The specification's
  terms "absent" and "missing" (section 1, SPEC-173, SPEC-174) follow this.
- Status: closed. Accepted with the specification on 25 November 2026

### FIND-011 REQ-011 named the wrong requirement for stepping past the last subtune
- Phase: 2, specification (review item RV-018)
- Observed: REQ-011's acceptance said that stepping past either end of a tune's subtunes "follows REQ-014".
  REQ-014 is the setting for what happens when a song reaches its length by itself; it has values, "repeat"
  and "stop", that make no sense for a key press.
- Concerns: REQ-011
- Resolution: REQ-011 now says that next and previous subtune, past the last or the first, go to the next
  or previous tune (REQ-012). SPEC-061 says the same.
- Status: closed. Accepted with the specification on 25 November 2026

### FIND-012 An acceptance test needed HVSC's tunes where they may not be
- Phase: 2, specification (review item RV-018)
- Observed: REQ-051's acceptance had both engine settings "play the benchmark tunes on the host". The
  benchmark tunes are HVSC's, and REQ-087 keeps HVSC's files out of the repository, so no host test can
  have them.
- Concerns: REQ-051
- Resolution: REQ-051's acceptance uses the project's own test tunes on the host, with ones for two and
  three SID chips among them; the benchmark tunes are played on the device only (SPEC-242).
- Status: closed. Accepted with the specification on 25 November 2026

### FIND-013 The brightness keys do nothing while the app runs
- Phase: 2, specification (review item RV-043)
- Observed: the launcher handles `fn`+U and `fn`+I, the brightness keys, in the same key handler as the
  volume keys, and pauses that handler while an app runs (CON-09). The requirements gave the volume keys to
  the app (REQ-042) and said nothing of the brightness keys, so they would have done nothing in SidPlayZero.
- Concerns: CON-09; the requirements, which had no item for these keys
- Resolution: REQ-129 added (Should): the keys change the screen's brightness when the system lets the app.
  CON-09 names them. SPEC-104 specifies it. Whether the stock user may change the brightness is still not
  known (R7); the device run records it.
- Status: closed. Accepted with the specification on 25 November 2026

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
