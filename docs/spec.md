# SidPlayZero specification

| | |
|---|---|
| Status | **Draft 1, awaiting review and the owner's approval.** This is the Phase 2 artifact |
| Date | 25 November 2026 |
| Derives from | [`requirements.md`](requirements.md), approved 25 November 2026, with the changes made for FIND-006 |
| Review | [`spec-review.md`](spec-review.md) |
| Next artifact | `docs/test-framework-tasks.md` (Phase 3) |

## 1. How to read this document

The requirements say what SidPlayZero has to do. This document says how it does it, precisely enough that
tests can be written from it before any of it is built.

- **SPEC-nnn** is one item of the specification. Each has three lines under it:
  - **Covers** names the requirements it implements. Every requirement is covered by at least one item, and
    `tools/check_repo.py` checks that.
  - **Oracle** says how a test knows the item is met: what is observed, and what it is compared with.
  - **Level** says where that test runs: on the **host**, against fakes; in the **simulator**, where the
    real app draws its real screens at 320×170; or on the **device**.
- A number marked **‡** comes from the requirements as a proposed budget, or is proposed here. It holds until
  a measurement on the device says otherwise (section 14).
- Section 2 records the decisions the requirements left to this document (R1 to R9 there), each with the
  evidence it rests on.

Terms are those of the requirements. Three more are used here:

| Term | Meaning |
|---|---|
| Present | A tune whose file is on the device |
| Absent | A tune that HVSC lists and whose file is not on the device yet |
| Queue | The ordered list of tunes that "next" and "previous" move through: the folder, search result or playlist that playback was started from |

## 2. Decisions

**SPEC-001 Language, toolkit and build.** The app is written in C++17 and draws with LVGL 9.5.0. It is built
with CMake 3.31 or newer, with the presets of the CardputerZero Template: `linux-x86-64` builds it for a Linux
desktop, and `cp0-cross` builds it for the device with the `aarch64-linux-gnu` compiler against M5Stack's
sysroot and packs the `.deb` with CPack.
- Covers: REQ-149, REQ-140
- Oracle: on a clean checkout, `cmake --workflow --preset linux-x86-64` and `cmake --workflow --preset
  cp0-cross-package` both end with status 0 and produce the program and the `.deb`.
- Level: host

**SPEC-002 The engine library is built in.** Tunes are played by libsidplayfp 3.1.2 with libresidfp 1.3.0.
Both are built from their release archives, which the build downloads and checks against these SHA-256 sums
before use, and are linked into the program statically:

| Archive | SHA-256 |
|---|---|
| `libsidplayfp-3.1.2.tar.gz` | `6d16c20e523ac4d3e385531dafe247a830b59bcb4eb73a557f15cf4e7ae6da84` |
| `libresidfp-1.3.0.tar.gz` | `6de8880dcfc11b993107f71b538262f08ae07fd0e595297ce776f641b5435b38` |

The library is configured with `--without-exsid`, and with `--without-usbsid` unless SPEC-192 is switched on.
The package does not depend on Debian's `libsidplayfp6`.
- Covers: REQ-056, REQ-126, REQ-141, REQ-144
- Oracle: the build fails if a checksum differs; `readelf -d` on the program lists no `libsidplayfp`; the
  About screen shows "libsidplayfp 3.1.2"; the program contains no symbol of the exSID backend.
- Level: host
- Evidence (R3, FIND-009): Debian trixie's 2.12.0 uses a tune's data offset without the check that fixes
  CVE-2026-111148, and Debian has shipped no fix. Version 3.1.2 has the fix, builds as a static library, and
  its release archive needs no extra build tools. Building it in also keeps the app working when a later
  system image carries a different version.

**SPEC-003 The library is linked, not the `sidplayfp` program driven.** The app calls libsidplayfp directly:
`SidTune` to load a tune, `sidplayfp::config`, `load`, `play` and `mix` to render it, `mute` and `filter` for
the sound controls.
- Covers: REQ-056, REQ-163
- Oracle: SPEC-071's comparison passes, which is only possible with direct control of the power-on delay.
- Level: host
- Evidence (R4): apps are ordinary processes and may use threads (CON-03). Rendering through this interface
  gave output identical, sample for sample, to the WAV written by the `sidplayfp` program built from the same
  library: 960,027 of 960,027 samples with reSIDfp, 960,053 of 960,053 with sidlite, in mono; and every
  value of ten seconds in stereo, for tunes with one, two and three SID chips.

**SPEC-004 Two engines, and a setting that chooses between them.** The setting `engine` has three values:
`accurate` is reSIDfp, `light` is sidlite, and `auto`, the default, picks one of the two for each tune by
SPEC-072.
- Covers: REQ-051
- Oracle: with a fake clock and a recorded speed measurement, each setting loads the expected engine for a
  one-SID and a two-SID fixture tune; the engine's name is on the now-playing screen.
- Level: host, simulator
- Evidence (R2, R3): on an x86-64 host reSIDfp rendered a one-SID tune at 12 to 17 times real time and a
  two-SID tune at 9 to 11 times; sidlite rendered both at 62 to 78 times. The device's processor is roughly
  ten times slower, which puts reSIDfp near the limit for one SID and under it for two. The rule in SPEC-072
  decides from a measurement on the device itself, so no guess is built in.

**SPEC-005 Sound goes out through miniaudio.** Samples are handed to the system through miniaudio 0.11.25, a
single source file (public domain or MIT-0) built into the program. It tries the PulseAudio backend first and
ALSA second, and loads their libraries when the program runs, so the package needs no audio library at build
time. The app reaches it only through the `AudioSink` interface of SPEC-021.
- Covers: REQ-043, REQ-044, REQ-141
- Oracle: `readelf -d` lists neither `libpulse` nor `libasound`; on the device the results file names the
  backend in use as PulseAudio.
- Level: host, device
- Evidence (R5): the device runs PipeWire's PulseAudio server (CON-08); the stock Music app and the
  `sidplayfp` program both use miniaudio in this way.

**SPEC-006 Downloads use libcurl.** HTTP requests are made with the system's libcurl (`libcurl4t64`), behind
the `HttpClient` interface of SPEC-021, with the rules of SPEC-150.
- Covers: REQ-124, REQ-141
- Oracle: the package's `Depends` names `libcurl4t64` and `ca-certificates`; SPEC-150's tests pass.
- Level: host

**SPEC-007 The complete archive is unpacked by the system's `7z`, and only behind a memory check.** For
REQ-089 the app runs the `7z` program from Debian's `7zip` package as a child process, without a shell. The
package recommends `7zip` and does not depend on it. Section 8 has the steps.
- Covers: REQ-089, REQ-141
- Oracle: with a fake `7z` on the path, the host test sees it started with the expected arguments and no
  shell; without one, the complete download is not offered and the reason is shown.
- Level: host
- Evidence (R1, FIND-006): unpacking release #85 peaked at 364 MB with 7-Zip and 540 MB with libarchive.
  7-Zip is the smaller, is in Debian's main archive, and needs no code of ours to read the format.

**SPEC-008 The search index is a file of our own, searched in memory.** There is no database library. The
index is the file described in SPEC-160, and a search is a single pass over it (SPEC-162).
- Covers: REQ-090, REQ-091
- Oracle: SPEC-162's timing on the generated 61,157-tune collection.
- Level: host, device
- Evidence (R6): 61,157 tunes come to under 12 MB of index. One pass over text of that size is a matter of
  tens of milliseconds on the host, which leaves room in the 500 ms‡ budget on the device.

**SPEC-009 Settings, state and playlists are text files.** Settings and state are INI-style text (SPEC-130,
SPEC-131). A playlist is an M3U8 file with extra comment lines (SPEC-170).
- Covers: REQ-070, REQ-107
- Oracle: files written by the app are valid UTF-8 text; a file written by hand from the descriptions in
  SPEC-130 and SPEC-170 loads.
- Level: host

**SPEC-010 Fonts are built into the program.** Text is drawn with bitmap fonts generated ahead of time from
DejaVu Sans, at 10, 12 and 14 pixels, covering U+0020 to U+00FF and the symbols the screens use. The generated
font sources are in the repository. The program loads no font files when it runs.
- Covers: REQ-031, REQ-160
- Oracle: every character of the Windows-1252 set that maps into U+00A0 to U+00FF is drawn as a glyph, not a
  box, in a simulator screenshot; screenshots are identical on any machine that builds the simulator.
- Level: simulator
- Evidence: DejaVu Sans is the font the platform recommends for western text (CON-18). Drawing it from
  built-in bitmaps removes the font library and its version from the picture, so a screenshot can be compared
  byte for byte.

**SPEC-011 Licences.** The app's own files are GPL-3.0-or-later, each carrying an SPDX line. What is built in:
libsidplayfp and libresidfp (GPL-2.0-or-later, used here under GPL-3), LVGL (MIT), miniaudio (MIT-0), fonts
made from DejaVu Sans (its own free licence). The package installs `LICENSE` and a third-party notices file
naming each of these, with its licence text, in `/usr/share/doc/sidplayzero/`.
- Covers: REQ-144
- Oracle: a check finds an SPDX line in every source file of ours, finds each built-in component in the
  notices file, and finds no file under a "GPL version 2 only" notice among the sources compiled.
- Level: host

## 3. Structure

**SPEC-020 Three layers.** The source is in three parts, and the build enforces which may use which:

| Part | Holds | May use |
|---|---|---|
| `core` | The collection, song lengths, the player's state machine, the queue, playlists and their import, settings, the index and search, the download logic | The C++ standard library only |
| `platform` | The real engine (libsidplayfp), sound output (miniaudio), HTTP (libcurl), the `7z` runner, system volume, the keyboard and the display | `core`, and the libraries named |
| `ui` | Screens, the key map, drawing | `core`, LVGL |

`core` is built as a library with no include path to LVGL, libsidplayfp, miniaudio or libcurl.
- Covers: REQ-110, REQ-160
- Oracle: the host tests link `core` alone; adding an `#include` of LVGL or libsidplayfp to a `core` file
  makes the build fail.
- Level: host

**SPEC-021 Seams.** `core` reaches the outside world only through these interfaces. Each has a real
implementation in `platform` and a fake for the tests.

| Interface | What it does | Fake used by tests |
|---|---|---|
| `Engine` | Loads a tune from bytes, renders samples, mutes voices, switches the filter, reads the SID registers | Produces a known ramp of samples, counts calls, can be made slow or made to fail |
| `AudioSink` | Opens a stream at a rate, pulls samples through a callback, reports loss of the device | Pulls on demand under the test's control, can refuse to open or drop the device |
| `SoundBackend` | The pair of the two above, or a hardware SID (SPEC-190) | A recorder |
| `Clock` | Monotonic time and sleeping | Time that moves only when the test says |
| `Files` | Reads, writes, lists, renames, syncs, reports free space, under a root | A folder of the test's own, with faults that can be injected at any write |
| `HttpClient` | Fetches a URL to a file, with progress, range and cancel | A table of URLs with canned answers and failures, and a log of requests |
| `Unpacker` | Lists and unpacks an archive into a folder | A scripted program |
| `SystemVolume` | Reads and changes the system volume and mute | A recorder |
| `Memory` | Reports available memory and swap | Numbers set by the test |

- Covers: REQ-110, REQ-160
- Oracle: every host test of `core` runs with fakes only, and a counter in each fake proves the code under
  test went through the interface.
- Level: host

**SPEC-022 Threads.** The program has four threads, and each kind of work has one home:

| Thread | Does | Must never |
|---|---|---|
| UI | Runs LVGL, handles keys, owns the player's state machine and all screen objects | Block for more than 50 ms‡: no file, network or engine call that can take longer |
| Render | Runs the engine and fills the ring buffer of SPEC-070 | Touch LVGL or the file system |
| Audio | miniaudio's callback: copies from the ring buffer to the device | Lock, allocate or wait |
| Worker | File reading, folder listing, the index, downloads, running `7z` and `pactl` | Touch LVGL |

Threads talk through queues of messages. A result from the worker reaches the screens as a message handled on
the UI thread.
- Covers: REQ-017, REQ-054
- Oracle: a build with ThreadSanitizer runs the host and simulator tests without a report; with a `Files` fake
  that takes 2 s to list a folder, the simulator still answers a key within 100 ms‡.
- Level: host, simulator

**SPEC-023 Names.** The package and the program are `sidplayzero`. The launcher shows "SidPlayZero". The same
string names the folders under the XDG directories (SPEC-134).
- Covers: REQ-140
- Oracle: the built package's control field `Package`, the installed program's name and the `.desktop`
  entry's `Name` have these values.
- Level: host

## 4. The collection and the browser

**SPEC-040 Finding the collection.** At start, and whenever the setting changes, the collection root is the
first of these that is set: the environment variable `HVSC_BASE`, the setting `collection`, `~/C64Music`. A
leading `~` is replaced by the home folder. The root does not have to exist.
- Covers: REQ-001
- Oracle: three host cases (variable set; variable unset and setting set; neither) each give the expected
  root; changing the setting while running makes the browser show the new root at once.
- Level: host, simulator

**SPEC-041 What a folder shows.** A folder's entries are the union of two sources: the folder as it is on the
device, and, when HVSC's song length database is present in the root (SPEC-142), the tunes and folders it
lists there. A tune that both sources name appears once. Only folders and files whose names end in `.sid`,
in any case, are entries; names starting with a dot are left out. Each tune entry is **present** or
**absent**.
- Covers: REQ-002, REQ-006, REQ-088
- Oracle: for a test root holding three `.sid` files, a `.txt` file and a hidden file, and a database listing
  two of those tunes and two others, the folder has five tune entries, three present and two absent.
- Level: host

**SPEC-042 Order.** Folders come before tunes. Within each group the order is by name, compared without regard
to the case of ASCII letters, with ties broken by plain byte order.
- Covers: REQ-002
- Oracle: the entries of a test folder come out in an order written down by hand in the test.
- Level: host

**SPEC-043 Folders are listed off the UI thread and drawn a screenful at a time.** Opening a folder asks the
worker for its entries and shows them when they arrive; if that takes more than 150 ms‡ the list area says
"Reading…" meanwhile. The last 16‡ folders listed stay in memory. The list draws only the rows that are on the
screen, whatever the number of entries.
- Covers: REQ-003
- Oracle: on the host, a folder of 1,298 entries is listed and the model reports any row in constant time;
  in the simulator the number of LVGL objects in the list is the same for 6 entries and for 1,298; on the
  device the times of REQ-003 hold for the two folders it names and the program's memory stays under 64 MB‡.
- Level: host, simulator, device

**SPEC-044 Opening, going back, and where the highlight lands.** Enter or Right on a folder opens it with the
first entry highlighted, or the entry that was highlighted the last time this folder was open in this run.
Left, or a short ESC, goes to the parent with the folder just left highlighted. At the collection root a
short ESC does nothing.
- Covers: REQ-002
- Oracle: a simulator script opens a folder, moves down two rows, goes back and opens it again: the third
  row is highlighted; after going back, the folder's own row is highlighted.
- Level: simulator

**SPEC-045 Moving in a list.** Up and Down move one row and stop at the ends. Page Up and Page Down move by the
number of rows on the screen. Home and End go to the first and last entry. A key held down repeats after
400 ms, then 25 times a second. The top line shows the highlight's position as "n/total".
- Covers: REQ-005, REQ-003
- Oracle: in a 1,298-entry test folder each key lands on the expected row and the indicator reads the expected
  text; holding Down for one second of the fake clock moves 16 rows.
- Level: simulator

**SPEC-046 The detail line.** Below the list, one line describes the highlighted entry once the highlight has
rested on it for 150 ms‡. For a present tune: title, author and the length of its default subtune, read from
the file's header by the worker. For an absent tune: its length from the database and "not on device". For a
folder: nothing.
- Covers: REQ-004
- Oracle: highlighting the fixture tune "Commando Test" shows its title, author and length within 200 ms‡ of
  the fake clock; an absent tune shows the database's length and the words "not on device".
- Level: simulator

**SPEC-047 A collection that is not HVSC.** With no song length database in the root, the browser shows the
folder tree as it is on the device, every tune is present, and every length is the default of SPEC-063.
- Covers: REQ-006
- Oracle: a root with three fixture tunes and no `DOCUMENTS` folder lists three present tunes, and each plays
  for the default length.
- Level: host, simulator

**SPEC-048 Nothing there yet.** When the root does not exist, or holds no entry, the browser's place is taken
by a screen that says no collection was found, names the root, and offers two things: "Get HVSC" on Enter,
which starts SPEC-140, and a line telling the user to copy a collection into that folder.
- Covers: REQ-007
- Oracle: started with an empty home folder, the simulator's first screenshot matches the reference image,
  and Enter leads to the HVSC notice.
- Level: simulator

**SPEC-049 How absent tunes look.** An absent tune's row is drawn in the dimmed text colour with a small cloud
mark at its right edge. While it is being fetched the mark is replaced by a progress mark.
- Covers: REQ-088
- Oracle: a screenshot of a folder with one present and one absent tune matches the reference image.
- Level: simulator

## 5. Playing

**SPEC-060 The player's states.** The player is one state machine on the UI thread.

| State | Meaning |
|---|---|
| Idle | Nothing is loaded |
| Fetching | The current tune is absent and is being downloaded (SPEC-141) |
| Loading | The tune's file is being read, checked (SPEC-203) and given to the engine |
| Playing | Samples are being rendered and played |
| Paused | The engine is loaded; rendering and output are stopped at a known sample |
| Failed | The current tune could not be fetched or loaded; a message says why |

| Event | From | To | What happens |
|---|---|---|---|
| Play a tune | any | Fetching if absent, else Loading | The queue is set from where the tune was chosen (SPEC-062) |
| Fetched | Fetching | Loading | |
| Could not fetch | Fetching | Failed | SPEC-143 |
| Loaded | Loading | Playing | Elapsed time is 0 |
| Could not load | Loading | Failed | SPEC-069 |
| Play/Pause key | Playing | Paused | Output stops within one buffer; the ring buffer keeps its contents |
| Play/Pause key | Paused | Playing | Output resumes with the next sample; none is lost or repeated |
| Play/Pause key | Idle or Failed | Fetching or Loading | Plays the highlighted or current tune |
| Stop | any | Idle | The engine is unloaded and the stream closed |
| Song end | Playing | by SPEC-065 | |
| Next, previous, restart | Playing, Paused, Failed | by SPEC-061, SPEC-062, SPEC-066 | |

No other transition exists.
- Covers: REQ-010, REQ-019
- Oracle: a host test drives every row of the table against the fake engine and sink and checks the state
  after each event; after pause and resume, the samples the sink received are exactly the fake engine's ramp
  with no gap and no repeat.
- Level: host

**SPEC-061 Subtunes.** A tune starts at its default subtune, as its header gives it. "Next subtune" and
"previous subtune" move by one. Next from the last subtune is "next tune" (SPEC-062); previous from the first
is "previous tune".
- Covers: REQ-011
- Oracle: for the three-subtune fixture with default 2: play starts at 2, next gives 3, next again starts the
  following tune; previous from 2 gives 1, previous again starts the preceding tune.
- Level: host

**SPEC-062 The queue.** Playing a tune from the browser makes the queue that folder's tunes in the order
shown. Playing a search result makes it the results. Playing a playlist entry makes it the playlist.
"Next tune" and "previous tune" move one place in the queue and start that tune's default subtune. At the last
tune, next stops playback unless repeat-all is on, when it goes to the first; previous at the first tune
restarts it. Browsing elsewhere does not change the queue.
- Covers: REQ-012, REQ-017
- Oracle: in a three-tune folder, next from the second plays the third and next again stops; started from a
  playlist whose order differs from the folder's, next follows the playlist.
- Level: host

**SPEC-063 Song lengths.** When the root has `DOCUMENTS/Songlengths.md5`, the worker loads it at start with
libsidplayfp's `SidDatabase`. A tune's lengths are looked up with the MD5 that `SidTune::createMD5New` gives,
which is the MD5 of the whole file. A subtune with no entry, and every subtune when there is no database or
it cannot be read, gets the setting `default_length`, which starts as 3:30 and where 0 means "until told
otherwise". A database that cannot be read gives one message, at start.
- Covers: REQ-013
- Oracle: with a database written for the fixtures, each fixture subtune reports the length the database
  gives, to the millisecond; a tune left out of it reports the default; a database of random bytes gives the
  message and the default for every tune, and no crash.
- Level: host

**SPEC-064 When a song has ended.** A subtune has ended when the number of samples rendered for it reaches its
length times the sample rate, rounded down. The render thread stops at exactly that sample and tells the UI
thread. The elapsed time shown is samples played divided by the rate.
- Covers: REQ-014
- Oracle: with a 2.000-second length at 48,000 Hz the sink receives exactly 96,000 samples for that subtune
  before the next begins.
- Level: host

**SPEC-065 What happens next.** The setting `when_song_ends` decides what follows a song's end:

| Value | Next |
|---|---|
| `subtunes` (default) | The tune's next subtune; after the last, the next tune in the queue |
| `next_tune` | The next tune in the queue |
| `repeat` | The same subtune again |
| `stop` | Nothing: the player goes to Idle |

The setting `repeat_all` makes "next tune" at the end of the queue go to its start. The setting `shuffle`
makes the queue a random permutation of its tunes, made once when the queue is set and again each time
repeat-all wraps round; the permutation comes from a seed that tests can fix.
- Covers: REQ-014, REQ-016
- Oracle: with 2-second test lengths and the fake clock, each value of `when_song_ends` produces the sequence
  of tunes and subtunes written in the test; the last tune of a queue ends in Idle unless `repeat_all` is on;
  with `shuffle` on and seed 1, ten tunes play once each, in the order the test lists, before any repeats.
- Level: host

**SPEC-066 Starting again and skipping ahead.** "Restart" loads the current subtune again from its beginning.
The skip key is Right on the now-playing screen, once it has been held for 400 ms. While it is held, the
render thread renders eight‡ chunks for each one it puts in the ring buffer,
so the elapsed time advances eight times as fast; sound continues at normal pitch in short pieces. Releasing
the key returns to normal at once.
- Covers: REQ-015
- Oracle: restart sets elapsed time to 0 and the sink then receives the tune's first samples; holding skip
  for one second of the fake clock advances elapsed time by at least four seconds‡.
- Level: host

**SPEC-067 How the engine is set for a tune.** For each tune the engine is configured with: the sample rate of
SPEC-073; the SID model and the clock from the tune's header, unless the settings `sid_model` or `clock` force
one (SPEC-077); the engine of SPEC-072; resampling by interpolation when the engine is reSIDfp; and a
power-on delay fixed at 0. The setting `power_on_delay = random` restores the library's own behaviour.
- Covers: REQ-163, REQ-046
- Oracle: SPEC-071.
- Level: host

**SPEC-068 ROMs.** The settings `rom_kernal`, `rom_basic` and `rom_chargen` name ROM files. Each is used if it
exists and has the right size (8,192 bytes, 8,192 bytes, 4,096 bytes), and is otherwise ignored with one
message at start. A tune whose header says it needs BASIC is not loaded without a BASIC ROM: the player goes
to Failed with "This tune needs the C64 BASIC ROM", and SPEC-069 applies.
- Covers: REQ-018
- Oracle: the fixture tune marked as BASIC fails with that message when no ROM is set and is handed to the
  engine when a ROM file of the right size is set; a ROM file of the wrong size gives the message at start.
- Level: host

**SPEC-069 Tunes that will not play.** A tune that fails SPEC-203's check, or that the engine refuses, puts the
player in Failed within 1 s‡, with a message naming the file and the reason. If playback was unattended (it
got there by a song ending, not by a key), the player moves to the next tune after 2 s‡. After five‡ tunes
in a row have failed, it goes to Idle and the message says playback stopped because several files could not
be played. A tune that plays resets the count.
- Covers: REQ-019
- Oracle: a queue of five truncated fixtures and one good one ends in Idle with that message after the fifth;
  a queue of good, bad, good plays both good ones.
- Level: host

**SPEC-070 From the engine to the loudspeaker.** The render thread asks the engine for about 5 ms of samples at
a time and writes them, as 16-bit stereo, into a ring buffer of 8,192‡ frames. It keeps the buffer at least
half full and sleeps when it is full. The audio callback copies what it needs from the buffer. If the buffer
holds too little, the callback plays what there is, fills the rest with silence, and adds the number of
missing frames to an **underrun counter**. Output for a tune starts when 2,048‡ frames are in the buffer.
- Covers: REQ-020, REQ-050, REQ-017
- Oracle: with the fake engine and sink, the samples received equal the samples produced, in order; with the
  engine made slower than real time the counter rises by exactly the frames not delivered; on the device the
  time from the key to the first samples in the callback is at most 500 ms‡ for the benchmark tunes.
- Level: host, device

**SPEC-071 The same tune renders the same.** With the settings of SPEC-067, rendering a given subtune twice
gives the same samples. For the fixture tunes and both engines, the first ten seconds rendered by the app
equal the first ten seconds of the WAV written by the `sidplayfp` program (built from the same library) with
`--delay=0`, `-s` for stereo, and the same engine and rate. The expected sample hashes are kept in the
repository.
- Covers: REQ-163, REQ-056
- Oracle: a host test compares hashes of two renders, then compares the app's samples with the WAV's, one by
  one; a third test fails if a stored hash differs from what is rendered.
- Level: host

**SPEC-072 Choosing the engine automatically.** On its first start, and again whenever the program's version
or the setting `sample_rate` has changed, the worker measures how fast reSIDfp renders: two seconds of the
one-SID fixture tune, whose bytes are built into the program, timed with the real clock. The result, the
**speed factor** (seconds rendered per
second taken), is stored in the cache folder. With `engine = auto`, a tune that uses *n* SID chips is played
with reSIDfp when the factor divided by *n* is at least 1.6‡, and with sidlite otherwise. Until the first
measurement is done, `auto` means sidlite.
- Covers: REQ-050, REQ-051
- Oracle: on the host, with a stored factor of 4.0, one- and two-SID fixtures get reSIDfp and a three-SID
  fixture gets sidlite; with 1.5 all three get sidlite. On the device the results file holds the measured
  factor, and the benchmark set plays for 30 minutes with the underrun counter at 0 and the program's
  processor use averaging at most 60 %‡ of one core.
- Level: host, device

**SPEC-073 Sample rate.** The setting `sample_rate` is 44100 or 48000. The default is 48000. The engine renders
at this rate and the stream is opened at this rate; if the system runs at another rate, the system converts.
A change takes effect when the next tune is loaded.
- Covers: REQ-052
- Oracle: with each value, the fake sink is opened at that rate and receives that many samples per second of
  song.
- Level: host

**SPEC-074 Muting voices and switching the filter.** Each of the three voices of each SID chip in use can be
switched off and on, and so can each chip's filter, while a tune plays. The change is passed to the engine's
`mute` and `filter` and takes effect within one render chunk. The choices last until another tune is loaded,
when everything is on again.
- Covers: REQ-040, REQ-041
- Oracle: with voice 1 muted from the first sample, ten seconds of a fixture equal the `sidplayfp` program's
  WAV made with `-u1`; with the filter off they equal the WAV made with `-nf`; loading the next tune leaves
  nothing muted.
- Level: host

**SPEC-075 Tunes for more than one SID chip.** A tune's header says how many chips it uses and at which
addresses, and the engine is given as many. The library's mixer is set for stereo, which places two chips left
and right and three left, centre and right.
- Covers: REQ-045
- Oracle: ten seconds of the two-SID fixture equal the `sidplayfp` program's stereo WAV of it; the now-playing
  screen shows "2 SID".
- Level: host, simulator

**SPEC-076 Volume.** Volume up and volume down read the system volume through `SystemVolume` and set it to the
nearest multiple of 5 % above or below, never above 100 % or below 0 %. Mute toggles the system's mute. A small
panel shows the new level, or "Muted", for 1.5 s. The real `SystemVolume` runs `pactl` on the worker thread,
with fixed arguments and no shell. If `pactl` is missing or fails, the panel says the volume could not be
changed.
- Covers: REQ-042, REQ-127
- Oracle: from 42 %, up asks for 45 % and down for 40 %; at 100 % up asks for nothing; the panel appears in
  the simulator screenshot; on the device the level heard changes and the launcher shows the same level.
- Level: host, simulator, device

**SPEC-077 Forcing the chip and the clock.** The settings `sid_model` (`auto`, `6581`, `8580`) and `clock`
(`auto`, `pal`, `ntsc`) default to `auto`, which follows each tune's header. Another value forces it for every
tune.
- Covers: REQ-046
- Oracle: with `sid_model = 8580`, ten seconds of a fixture marked 6581 equal the `sidplayfp` program's WAV
  made with `-mnf` (the 8580, forced); with `clock = ntsc`, the WAV made with `-vnf`.
- Level: host

**SPEC-078 When sound cannot be opened, or goes away.** If the stream cannot be opened when a tune is to play,
the player goes to Failed with "No sound device"; the rest of the app keeps working, and the next attempt to
play tries again. If miniaudio reports that the device was lost or changed while playing, the app closes the
stream and opens it again, up to five times at intervals of 200 ms‡; rendering waits meanwhile, so no part of
the tune is skipped. If that fails, the player goes to Paused with the same message.
- Covers: REQ-044, REQ-043
- Oracle: with the fake sink refusing to open, play shows the message and the browser still answers keys;
  when it accepts, play works; with the fake sink dropping the device for 300 ms mid-tune, the samples
  received are complete and in order. On the device, plugging and unplugging headphones twice leaves the tune
  playing with a gap under 1 s‡.
- Level: host, device

**SPEC-079 When it cannot keep up.** If the underrun counter rises in three‡ separate seconds within ten
seconds, the app acts on the tune that is playing. With `engine = auto` it reloads the tune with sidlite at
the same position and remembers, for this run, to use sidlite for tunes with that many chips. With
`engine = accurate` it shows a panel: "This tune is too heavy for the accurate engine", with Enter to switch
this tune to the light engine and ESC to carry on.
- Covers: REQ-053
- Oracle: with the fake engine at 0.8 times real time, `auto` switches within 10 s‡ of the fake clock and
  the underrun counter then stays still; `accurate` shows the panel within 10 s‡, and Enter switches.
- Level: host, simulator

**SPEC-080 The register display.** Sixty times a second at most, the render thread copies the SID registers
(`getSidStatus`) to the UI thread. The now-playing screen draws, for each of the three voices of the first
chip, a bar whose height follows the voice's envelope level, and marks a muted voice.
- Covers: REQ-034
- Oracle: with the fake engine returning known register values, the screenshot shows the three bars at the
  expected heights; paused, two screenshots a second apart are identical.
- Level: simulator

## 6. Screens and keys

**SPEC-090 The screens.** The app has these screens. "Back" is a short ESC.

| Screen | Reached from | Back goes to |
|---|---|---|
| Browser (or the empty screen of SPEC-048) | Start | The parent folder; nothing at the root |
| Now playing | Browser, Search, Playlist, by key 4 or by playing a tune | Where it was opened from |
| Search | Browser, by key 5 | Browser |
| Playlists | Browser, by key 6 | Browser |
| Playlist | Playlists, by Enter | Playlists |
| Settings | Any of the above, by TAB or the menu | Where it was opened from |
| HVSC | Settings, or the empty screen | Where it was opened from |
| About | Settings | Settings |
| Help | Any screen, by `fn`+H | The screen under it |

Panels (a question, a text prompt, a list to pick from, a message) open over a screen and return to it.
- Covers: REQ-060, REQ-062
- Oracle: a simulator script walks every edge of this table and checks the screen's title after each step;
  from every screen, at most four short presses of ESC reach the browser.
- Level: simulator

**SPEC-091 The layout.** Every screen is 320×170 and has three bands:

| Band | Rows of pixels | Holds |
|---|---|---|
| Top line | 0 to 15 | Left: the screen's title, or the folder's path with its start cut off if too long, and the list position. Right: the mini player of SPEC-109 |
| Content | 16 to 135 | Lists have rows 20 pixels high: six rows, or five and the detail line |
| Action bar | 136 to 169 | Five labels centred at x = 49, 105, 161, 217 and 273, each with a tick below it pointing at its key, for the keys 4, 5, 6, 7 and 8 |

Body text is the 12-pixel font, the top line and action bar the 10-pixel font, and the title on the
now-playing screen the 14-pixel font. The theme is dark: light text on a near-black background, the highlight
a filled bar.
- Covers: REQ-061, REQ-031
- Oracle: reference screenshots of every screen; a test measures that no label's box in the action bar
  overlaps its neighbour and each is centred on its x.
- Level: simulator

**SPEC-092 Keys that work everywhere.** These keys do the same thing on every screen, including while text is
being typed (except where noted):

| Key on the device | Key code received | Does |
|---|---|---|
| `fn`+Q | Play/Pause | Play or pause |
| `fn`+W, `fn`+E | Previous, Next | Previous tune, next tune |
| `fn`+A, `fn`+S, `fn`+D | Mute, Volume down, Volume up | SPEC-076 |
| `fn`+H | Help | Show or hide help (SPEC-107) |
| `fn`+J | Print Screen | Screenshot (SPEC-108) |
| ESC, short | Esc | Back; in a panel, cancel |
| ESC, held | Esc | Leave the app (SPEC-106) |
| TAB | Tab | Open Settings. Not while typing |
| Space | Space | Play or pause. Not while typing |

- Covers: REQ-061, REQ-060
- Oracle: a simulator script sends each key on every screen of SPEC-090 and checks its effect.
- Level: simulator

**SPEC-093 Arrows, lists and the action bar.** Outside text entry: Up, Down, Left and Right arrive either as
arrow key codes (`fn`+F, `fn`+X, `fn`+Z, `fn`+C) or as the letters F, X, Z and C, and both do the same. Enter
confirms. The keys 4 to 8 trigger the action whose label is above them; a key with no label does nothing.
Home, End, Page Up and Page Down work in every list (SPEC-045). No other letter or digit does anything, apart
from the keys SPEC-096 gives the now-playing screen.
- Covers: REQ-061, REQ-005
- Oracle: on the browser, the letter X and the Down key code each move the highlight one row; pressing 9 or Q
  changes nothing; each of 4 to 8 does what its label says.
- Level: simulator

**SPEC-094 The key map is one table.** The source holds one table with a row for every key on every screen:
screen, key, action, and the words shown for it in the action bar and in help. Key handling, the action bar
and the help screen are all made from it. Sections 6, 9 and 10 of this document describe its contents.
- Covers: REQ-063, REQ-061
- Oracle: a host test reads the table and fails if a screen has two rows for one key, an action-bar key
  without a label, or a row that SPEC-092 or SPEC-093 contradicts.
- Level: host

**SPEC-095 The browser's keys.** Up and Down move; Enter or Right opens a folder or plays a tune; Left goes to
the parent. The action bar:

| Key | Label | Does |
|---|---|---|
| 4 | Playing | Opens the now-playing screen. Empty when nothing is loaded |
| 5 | Search | Opens Search |
| 6 | Lists | Opens Playlists |
| 7 | Fav | Marks or unmarks the highlighted tune as a favourite (SPEC-171). Empty on a folder |
| 8 | Add | Opens the "add to playlist" panel for the highlighted tune (SPEC-172). Empty on a folder |

- Covers: REQ-002, REQ-061, REQ-100, REQ-101
- Oracle: a simulator script exercises each key and compares screenshots.
- Level: simulator

**SPEC-096 The now-playing screen.** The content band shows, from the top: the title (14 pixels); the author;
the released line; a line with "n/m" for the subtune, elapsed and total time, and a progress bar; a line with
the chip model, "PAL" or "NTSC", "1 SID", "2 SID" or "3 SID", and the engine's name; a line with the state
("Playing", "Paused", "Stopped", "Fetching…", or the failure message), marks for a favourite and for the play
mode, and the voice bars of SPEC-080. With `default_length` at 0 and no database entry the total time reads
"--:--".

| Key | Does |
|---|---|
| Left, Right | Previous subtune, next subtune. Right acts when it is released, if that is within 400 ms |
| Up, Down | Previous tune, next tune in the queue |
| Enter | Play or pause |
| 1, 2, 3 | Switch voice 1, 2 or 3 of the chosen chip off or on |
| 9 | Switch the chosen chip's filter off or on |
| 0 | Choose the next chip, for tunes with more than one |
| Right, held for 400 ms or more | Skip ahead while held (SPEC-066), with no change of subtune |

| Key | Label | Does |
|---|---|---|
| 4 | Browse | Opens the browser at this tune's folder, with the tune highlighted |
| 5 | Restart | SPEC-066 |
| 6 | Mode | Steps `when_song_ends` through its four values and shows the new one |
| 7 | Fav | Marks or unmarks this tune and subtune as a favourite |
| 8 | More | Opens a panel: Add to playlist; Repeat all on/off; Shuffle on/off; Notes (SPEC-097) |

- Covers: REQ-030, REQ-011, REQ-012, REQ-040, REQ-041, REQ-015, REQ-016
- Oracle: with the fixture tune "Commando Test" loaded, the screenshot matches the reference and each field
  equals the value in the fixture's header and database entry; a script exercises every key.
- Level: simulator

**SPEC-097 Notes from STIL.** "Notes" shows the entry for the playing tune from `DOCUMENTS/STIL.txt`, read by
the worker and converted from Latin-1: the part for the file and the part for the playing subtune, in a
scrolling text panel. With no STIL file or no entry it says there are no notes.
- Covers: REQ-033
- Oracle: for a fixture tune with an entry in a fixture STIL file, the text shown equals the entry; an entry
  with "é" shows that letter.
- Level: host, simulator

**SPEC-098 Text that does not fit.** Text from tune files is converted from Windows-1252 to Unicode before it
is drawn. In a list row and in the top line, text too long for its box ends in "…". On the now-playing screen
the title, author and released lines scroll sideways when too long, pausing 1 s at each end. No text is drawn
outside its box.
- Covers: REQ-031
- Oracle: the fixture tune titled "Hülsbeck Tëst" with a 32-character author shows both correctly; a test
  checks that every label's drawn width is at most its box's width.
- Level: simulator

**SPEC-099 Typing.** A text field is in use on the Search screen and in the prompts for a playlist's name.
While it is, letters, digits, Space and the symbols of the `sym` layer are typed into it, Backspace removes
the character before the cursor, Left and Right arrive only as arrow key codes and move the cursor, Enter
accepts and ESC cancels. The keys 4 to 8, TAB, Space and the letters F, X, Z, C type their characters and do
nothing else. The action bar shows no labels while a field is in use; in their place it shows "Enter: OK" and
"ESC: cancel".
- Covers: REQ-064
- Oracle: sending the key codes for "hub 1985" puts exactly that text in the Search field; the letters F, X,
  Z and C are typed, not treated as arrows.
- Level: simulator

**SPEC-100 Search.** The Search screen has a text field above a result list. It opens with the field in use.
Enter runs the search and moves to the list; Up from the first result returns to the field. In the list:
Enter plays the result, and the queue becomes the results.

| Key | Label | Does |
|---|---|---|
| 4 | Edit | Returns to the field |
| 5 | Folder | Opens the browser at the result's folder, with the tune highlighted |
| 6 | Filter | Opens the filter panel (SPEC-164) |
| 7 | Fav | Marks or unmarks the result as a favourite |
| 8 | Add | Opens the "add to playlist" panel |

The top line shows the number of results. While the index is being built it shows "Indexing n %" and searches
what has been indexed so far.
- Covers: REQ-090, REQ-064
- Oracle: a simulator script types a query on the fixture collection, gets the expected rows, plays one, and
  opens one in the browser.
- Level: simulator

**SPEC-101 Settings.** The Settings screen is a list of settings, each row showing its name and value. Left
and Right, or Enter, step a setting through its values; a setting that is a path or an address opens a text
prompt. Rows, in order: Collection folder; HVSC (opens the HVSC screen); Engine; Sample rate; When a song
ends; Repeat all; Shuffle; Default length; SID model; Clock; ROM files (three rows); Resume on start; Screen
off while playing; Download addresses (two rows); About. Key 4, "Back", closes it. A change is saved at once
(SPEC-130).
- Covers: REQ-070, REQ-051, REQ-052, REQ-083, REQ-018
- Oracle: a simulator script changes every row, restarts the app, and reads the same values back.
- Level: simulator

**SPEC-106 Leaving.** A short press of ESC (released within 500 ms‡) is "back". When ESC has been held for
500 ms‡ a panel appears: "Keep holding ESC to leave". At 1,000 ms‡ the app begins SPEC-201's exit. Releasing
between the two closes the panel and does nothing else, not even "back".
- Covers: REQ-062
- Oracle: with the fake clock: a 200 ms press goes back; a 700 ms press shows and hides the panel and leaves
  the screen unchanged; a 1,000 ms press ends the program with status 0. On the device, holding ESC returns to
  the launcher before the launcher's own 3-second limit.
- Level: simulator, device

**SPEC-107 Help.** `fn`+H opens a panel listing the keys of the screen under it, taken from the table of
SPEC-094: first that screen's keys, then the keys that work everywhere. Up and Down scroll it; `fn`+H or ESC
closes it.
- Covers: REQ-063
- Oracle: for every screen, a host test compares the lines of help text with the table's rows for that
  screen; the simulator shows the panel over the screen and removes it.
- Level: host, simulator

**SPEC-108 Screenshots.** Print Screen writes what LVGL has drawn, 320×170, as a PNG named
`sidplayzero_YYYYMMDD_HHMMSS_mmm.png` in `~/Pictures/Screenshots`, creating the folder if needed, and shows
the file's path for 2 s. The same function makes the screenshots of the simulator tests.
- Covers: REQ-065
- Oracle: after the key, a PNG of 320×170 exists there and equals the screen's reference image.
- Level: simulator

**SPEC-109 The mini player.** While a tune is loaded, the right part of every screen's top line shows a mark
for playing or paused, the tune's title cut to fit, and the elapsed time. The now-playing screen does not show
it.
- Covers: REQ-032
- Oracle: screenshots of the browser, Search and Settings during playback each contain it, with the time of
  the fake clock.
- Level: simulator

**SPEC-110 No pointer.** The device build creates no pointer or touch input. Every panel and screen takes its
keys from SPEC-092 to SPEC-101, so nothing needs anything but the keyboard.
- Covers: REQ-060
- Oracle: the simulator tests reach every screen and change every setting by key codes alone; the device
  build's input set-up contains one keypad device and nothing else.
- Level: simulator

**SPEC-111 Keys from the device and from the desktop.** On the device, keys are read from the evdev keyboard
without grabbing it. The path comes from `SIDPLAYZERO_KEYBOARD`, else it is
`/dev/input/by-path/platform-3f804000.i2c-event`, else the first device under `/dev/input` that reports the
keys A to Z. In the simulator, the desktop's keys are mapped to the same key codes, with F1 to F12 standing
for the `fn` keys of SPEC-092. Both feed one function, which is the only place key codes become actions.
- Covers: REQ-061, REQ-127, REQ-143
- Oracle: a host test feeds recorded evdev events for each key of CON-06 and gets the expected action; the
  device build never calls `EVIOCGRAB`.
- Level: host, device

**SPEC-112 How fast the screen answers.** Every key event is stamped when it is read, and again when the first
screen update after it has been flushed. The differences are kept as a histogram and written out by SPEC-241.
- Covers: REQ-054
- Oracle: on the device, during the 30-minute benchmark with a script pressing a key every two seconds, 95 %
  of the differences are at most 100 ms‡.
- Level: device

## 7. Settings and what is remembered

**SPEC-130 The settings file.** Settings are in `settings.ini` in the configuration folder (SPEC-134): lines of
`key = value`, `#` for comments, UTF-8. A setting that is missing has its default. The file is written through
SPEC-132 each time a setting changes.

| Key | Values | Default |
|---|---|---|
| `collection` | A path | `~/C64Music` |
| `engine` | `auto`, `accurate`, `light` | `auto` |
| `sample_rate` | `44100`, `48000` | `48000` |
| `when_song_ends` | `subtunes`, `next_tune`, `repeat`, `stop` | `subtunes` |
| `repeat_all`, `shuffle` | `on`, `off` | `off` |
| `default_length` | `m:ss`, or `0` | `3:30` |
| `sid_model` | `auto`, `6581`, `8580` | `auto` |
| `clock` | `auto`, `pal`, `ntsc` | `auto` |
| `power_on_delay` | `fixed`, `random` | `fixed` |
| `rom_kernal`, `rom_basic`, `rom_chargen` | A path, or empty | empty |
| `resume_on_start` | `on`, `off` | `off` |
| `screen_off_after` | Seconds, or `0` for never | `0` |
| `url_files` | An `https://` address | `https://hvsc.c64.org/download/C64Music` |
| `url_archive` | An `https://` address containing `{release}` | `https://hvsc.brona.dk/HVSC/HVSC_{release}-all-of-them.7z` |
| `hvsc_notice_accepted` | `on`, `off` | `off` |

- Covers: REQ-070, REQ-001, REQ-083
- Oracle: a host test sets each key to each value, reloads, and reads it back; a file with only one key gives
  defaults for the rest.
- Level: host

**SPEC-131 The state file.** `state.ini` in the state folder holds what is remembered between runs: the folder
open in the browser and the highlighted entry's name; the tune, subtune and queue source of what was playing;
What SPEC-079 remembers is not kept here. It is written through SPEC-132 two seconds‡ after any
of these change, and at exit. On start the browser opens that folder with that entry highlighted; if the
folder is gone it opens the nearest parent that exists, and the root if none does.
- Covers: REQ-071, REQ-121
- Oracle: stopping three levels down in the fixture tree and starting again shows the same folder and
  highlight; with the folder removed, the parent opens; after SIGKILL the restored position is at most 10 s‡
  behind.
- Level: host, simulator

**SPEC-132 Writing a file safely.** Every file the app writes for keeping (settings, state, playlists, the
index, fetched tunes) is written to a new file beside it with a temporary name, flushed to the card with
`fsync`, renamed over the old name, and the folder flushed too. Temporary files left by an earlier run are
removed at start.
- Covers: REQ-073
- Oracle: with a `Files` fake that fails at every possible point of 200 saves in turn, the file read back
  afterwards is always complete and is either the old or the new contents; a second test kills the real
  program at random moments and checks the same.
- Level: host

**SPEC-133 Settings that cannot be read.** A line that is not `key = value`, an unknown key, or a value outside
its range is skipped, and the setting keeps its default. If any line was skipped, or the file is not valid
UTF-8, the original file is copied to `settings.ini.bad` before anything is written, and one message at start
says that some settings could not be read and were set to their defaults. The same goes for `state.ini`,
without the message.
- Covers: REQ-074
- Oracle: started with a `settings.ini` of random bytes, the app starts, shows the message, and
  `settings.ini.bad` holds the original bytes; a file from a "newer version" with an unknown key keeps its
  known keys.
- Level: host, simulator

**SPEC-134 Folders.** With `NAME` standing for `sidplayzero`:

| What | Folder |
|---|---|
| Settings | `$XDG_CONFIG_HOME/NAME`, else `~/.config/NAME` |
| Playlists | `$XDG_DATA_HOME/NAME/playlists`, else `~/.local/share/NAME/playlists` |
| State and measurements | `$XDG_STATE_HOME/NAME`, else `~/.local/state/NAME` |
| The index, the listing, the speed factor, partial downloads | `$XDG_CACHE_HOME/NAME`, else `~/.cache/NAME` |

Each is created, for the user only, when first needed. The package's removal scripts touch none of them, and
none of the collection.
- Covers: REQ-075, REQ-070
- Oracle: with each variable set, the app's files appear under it; installing and removing the package in a
  clean container leaves a planted settings file, playlist and collection in place.
- Level: host

**SPEC-135 Carrying on at start.** With `resume_on_start = on`, the app starts the tune and subtune recorded in
`state.ini` from its beginning, with its queue, once the browser is showing. An absent tune is not fetched for
this.
- Covers: REQ-072
- Oracle: with the setting on and a recorded tune, the fake sink receives that subtune's first samples after
  start; with it off, nothing plays.
- Level: host

## 8. HVSC

**SPEC-140 Setting HVSC up.** "Get HVSC", from the empty screen (Enter) or from the HVSC screen, runs these
steps on the HVSC screen:

1. If `hvsc_notice_accepted` is off: show where the collection comes from and HVSC's notice (the text in
   SPEC-152), with "Enter: I understand" and "ESC: cancel". Enter sets the setting on.
2. Check that the card has room (SPEC-146).
3. Download four documents from `url_files`, into a new folder in the cache: `DOCUMENTS/Songlengths.md5`,
   `DOCUMENTS/STIL.txt`, `DOCUMENTS/HVSC.txt` and `DOCUMENTS/BUGlist.txt`, with progress (SPEC-147).
4. Check them: the song length file must load and list at least one tune, and `HVSC.txt` must name a release.
5. Move them into `DOCUMENTS` under the collection root, creating it, through SPEC-132.
6. Build the listing (SPEC-142) and start the index (SPEC-161). Show "HVSC #n is ready" and open the browser
   at the root.

From the empty screen this is two key presses: Enter to start and Enter on the notice. If any step fails, what
was there before is untouched, the screen says which step failed and why, and Enter tries again.
- Covers: REQ-080, REQ-085, REQ-081
- Oracle: on the host, with a fake `HttpClient` serving fixture documents, the steps end with the fixture
  collection listed and a chosen absent tune playing; without the notice accepted, no request is made; with a
  download failing, the root is unchanged. On the device, from an empty home folder, it ends within two
  minutes‡ with HVSC listed and a tune playing.
- Level: host, simulator, device

**SPEC-141 Fetching a tune when it is played.** To play an absent tune the app requests `url_files` followed by
the tune's HVSC path, saving to a temporary file in the cache. When it has arrived, its MD5 is compared with
the one the song length database gives for that path. If they are equal the file is moved to its HVSC path
under the root (SPEC-132), marked present, added to the index, and played. If not, it is deleted and the
player goes to Failed with "The downloaded file does not match HVSC's record". Rules:

- One fetch at a time. A newer request replaces one that is waiting, and cancels one in flight if it is for a
  tune no longer wanted.
- A tune is requested only after it has been the one wanted for 700 ms‡, so stepping past tunes fetches
  nothing.
- A fetch that fails for lack of network puts the player in Failed with "Cannot fetch: no network", and for
  the next 60 s‡ absent tunes fail at once without a request.
- Nothing is fetched ahead of need: not the next tune in the queue, and not a folder.

- Covers: REQ-088, REQ-124, REQ-086
- Oracle: with the fake `HttpClient`: playing an absent tune makes one request, for exactly that path, and the
  file appears at its HVSC path and plays; playing it again makes none; an answer with other bytes is rejected
  and nothing is stored; "next" pressed 50 times within five seconds of the fake clock makes one request, for
  the tune landed on; after a network failure, the next absent tune makes no request for 60 s‡.
- Level: host

**SPEC-142 The listing.** HVSC's song length database names every tune: each entry is a comment line holding
the tune's HVSC path, followed by a line with its MD5 and lengths. The worker reads the file once and writes
`listing.bin` to the cache: every path with its MD5 and the length of its default subtune, in the order of
SPEC-042, with a table of where each folder starts. It is rebuilt when the database's size or modification
time differs from what the listing records. A folder's absent tunes (SPEC-041) come from here.
- Covers: REQ-088, REQ-003
- Oracle: the listing built from the fixture database has one record per entry, in order; from HVSC #85's own
  database (on the device, or wherever a tester has it) it has 61,157 tunes in 1,981 folders.
- Level: host, device

**SPEC-143 Absent tunes without a network.** When an absent tune cannot be fetched and playback was unattended,
the player moves to the next tune in the queue that is present, without requests (SPEC-141's 60 s‡ rule). If
there is none, it goes to Idle with "The rest of this list is not on the device". When the user chose the
tune by a key, the player stays in Failed.
- Covers: REQ-088, REQ-086
- Oracle: with the fake network down, a queue of absent, absent, present plays the third after one failed
  request; a queue of absent tunes only ends in Idle with that message.
- Level: host

**SPEC-144 The complete download.** "Download everything", on the HVSC screen, is shown only when SPEC-145's
check passes; otherwise the screen shows SPEC-145's explanation in its place. It runs these steps:

1. Find the release number: the one in the root's `HVSC.txt`, or, after a check for a newer one (SPEC-148),
   that one.
2. Check memory (SPEC-145) and room (SPEC-146). Stop playback and unload the engine.
3. Download `url_archive`, with `{release}` replaced, to the cache, with progress (SPEC-147).
4. List the archive with `7z l -slt` and check every entry (SPEC-149).
5. Unpack with `7z x` into a new folder beside the collection root, on the same file system.
6. Check the result: no links or special files, and a song length database that loads.
7. Move it into place: every file of the new tree is renamed over its place under the root, `DOCUMENTS` last.
   Then remove files that the old listing named and the new tree does not have. Files the user put there are
   never removed.
8. Rebuild the listing and the index. Delete the archive.

A failure or a cancel at steps 1 to 6 leaves the root exactly as it was and removes what was downloaded or
unpacked, except that a partial archive is kept for SPEC-147 to continue.
- Covers: REQ-089, REQ-081, REQ-082
- Oracle: on the host, with the fake `HttpClient`, a real `7z` and a small 7-Zip fixture archive, the steps
  end with the fixture tree under the root and every tune present; cancelling at each of steps 3 to 6 leaves
  the root's contents, compared file by file, unchanged; a file the test planted in the root survives step 7.
- Level: host

**SPEC-145 The memory check.** The complete download needs a `7z` program on the path, and available memory
plus free swap of at least 450 MiB‡ as `Memory` reports them (`MemAvailable` and `SwapFree`). When either is
missing the HVSC screen says so in words: either "Install the 7zip package to download everything", or "This
device does not have enough free memory to unpack HVSC (n MiB free, 450 MiB needed)", and in both cases
"Tunes are fetched as you play them. To have everything, copy a C64Music folder into" followed by the root.
- Covers: REQ-089
- Oracle: with the fake `Memory` at 449 MiB the action is absent and the second text is shown with the
  number; at 450 MiB with a `7z` present it is offered; with no `7z` the first text is shown. On the device
  the results file records what the check found and, if it passed and the tester chose to run it, that the
  complete download finished.
- Level: host, simulator, device

**SPEC-146 Room on the card.** Before a download the app adds up what it is about to write and compares it with
the free space on the file system holding the destination. For SPEC-140 that is 16 MiB. For SPEC-144 it is the
archive's size from the server, plus the sizes of the archive's entries each rounded up to the file system's
block size (known after step 4, estimated as six times the archive's size before it), plus a tenth, plus
64 MiB. If there is less, nothing is downloaded and a message gives both figures in MiB.
- Covers: REQ-082
- Oracle: with the `Files` fake reporting one byte less than needed, no request is made and the message
  shows the two numbers; with exactly enough, the download starts.
- Level: host

**SPEC-147 Progress, cancelling and carrying on.** While a download or unpacking runs, the HVSC screen shows
the step in words, bytes done and total, and a bar; while `7z` unpacks it shows the number of files written
so far. ESC asks "Stop the download?", and Enter stops it. A partial file is kept in the cache with the
server's `ETag` and `Last-Modified`; the next attempt asks for the rest with a `Range` request when those
still match, and starts again when they do not or the server does not honour the range. Leaving the HVSC
screen does not stop a download.
- Covers: REQ-081
- Oracle: with the fake `HttpClient`, a download cut at 50 % and tried again requests bytes from the cut and
  ends with a file equal to the whole; with a changed `ETag` it requests from 0; cancelling leaves nothing in
  the cache but the partial file.
- Level: host, simulator

**SPEC-148 Releases.** The HVSC screen shows "HVSC #n", from the first line of the form "Release n" in the
root's `DOCUMENTS/HVSC.txt`. "Check for a newer release" requests `DOCUMENTS/HVSC.txt` from `url_files` and
compares its number. If it is higher, the screen offers "Update to #m", which runs SPEC-140's steps 2 to 6
again. Afterwards, a present tune whose file's MD5 differs from the new database's for its path is fetched
again when next played, if a network is there, and played as it is if not. A present tune the new database
does not list stays, as a tune of the device's own. Nothing checks for releases unless the user asks.
- Covers: REQ-084, REQ-086
- Oracle: with fixture documents for release 84 set up and a fake server offering 85: the check reports 85;
  after updating, the listing equals 85's; a tune whose bytes changed is requested once when played; a
  playlist entry whose folder was renamed still plays (SPEC-173).
- Level: host

**SPEC-149 Unpacking safely.** Before unpacking, every entry in `7z`'s listing is checked. The archive is
refused, and nothing is unpacked, if any entry: has a path that starts with `/` or contains a `..` part or a
backslash; is a link or a special file by its attributes; or lies outside one top-level folder. It is also
refused if there are more than 200,000 entries or their sizes add up to more than 2 GiB. `7z` is run with the
new folder as its only output place. After unpacking, the new folder is walked without following links, and
any link or special file found makes the app delete the folder and stop.
- Covers: REQ-123, REQ-122
- Oracle: fixture archives, one for each trick (an absolute path, a `..` part, a link pointing outside, two
  top-level folders, too many entries, too large in total), are each refused, and afterwards no file exists
  outside the new folder and the new folder is gone.
- Level: host

**SPEC-150 Rules for every request.** Every request is HTTPS, with the server's certificate and name checked
against the system's certificates. Redirects are followed at most five times and only to HTTPS. A request is
abandoned if it cannot connect within 15 s, or if it runs slower than 1 KiB/s for 30 s. A download stops, and
fails, past its limit: 1 MiB for a tune, 32 MiB for a document, 512 MiB for the archive. The request names
the app and its version as its user agent and sends nothing else about the user or the device: no cookies, no
identifiers. What is downloaded is only ever read as data; none of it is run. A test build, and only a test
build, also accepts `http://127.0.0.1`.
- Covers: REQ-124, REQ-083
- Oracle: against a local test server: a self-signed certificate is refused; a redirect to `http://` is
  refused; an endless answer is cut at the limit; the request as the server received it has the user agent
  and no cookie; with `url_files` and `url_archive` pointed at the local server, the server's log is the
  complete list of requests made. A check of the release build finds it refuses `http://127.0.0.1`.
- Level: host

**SPEC-151 The network is used only when asked.** The app makes a request only in these cases: SPEC-140,
SPEC-141 for a tune the user is playing or that unattended playback has reached, SPEC-144, SPEC-148's check,
SPEC-174, and SPEC-165 when it is switched on. Starting, browsing, searching, playing present tunes and
changing settings make none.
- Covers: REQ-086, REQ-124
- Oracle: a host test runs a scripted session that starts the app, browses, searches, plays present tunes,
  edits a playlist and changes every setting, with a `HttpClient` that fails the test if it is called.
- Level: host

**SPEC-152 HVSC's notice.** The notice shown by SPEC-140, and again on the About screen, is this text:

> The High Voltage SID Collection (HVSC) is a free collection made by the HVSC crew: hvsc.c64.org.
> All tunes in it are the copyright of their authors or publishers. Use of them for any purpose other
> than private enjoyment is subject to copyright law. SidPlayZero downloads HVSC's files from HVSC's own
> site to this device at your request. It does not contain or redistribute them.

Once HVSC is set up, the About screen also offers the full disclaimer from the root's `DOCUMENTS/HVSC.txt`.
- Covers: REQ-085, REQ-148
- Oracle: the notice's reference screenshot; SPEC-140's oracle shows that nothing is downloaded before it is
  accepted.
- Level: simulator

**SPEC-153 No music in the package or the repository.** A check walks the repository and the built `.deb` and
fails on any file whose first four bytes are `PSID` or `RSID`, unless it is under `tests/fixtures/` and named
in that folder's manifest, and on any file of exactly 8,192 or 4,096 bytes whose SHA-256 is that of a C64
KERNAL, BASIC or character ROM. The fixtures are written by a generator in the repository (SPEC-252).
- Covers: REQ-087
- Oracle: the check passes on the real tree and fails when a tune that is not in the manifest is added.
- Level: host

## 9. Searching

**SPEC-160 The index.** The index is one file, `index.bin`, in the cache folder. It has a header (a format
number, and the size and modification time of the song length database it was built from), then one record
per tune: the HVSC path, or the path under the root for a tune HVSC does not list; whether it is present; and,
for present tunes, the title, author and released line from the file's header, the chip model, the clock, and
the year (the first four-digit number in the released line). With each record goes its **search text**: the
path, title, author and released line, in lower case, with accented letters reduced to their base letters and
every character that is not a letter or digit turned into a space.
- Covers: REQ-091
- Oracle: an index built from the fixture collection, read back, has one record per tune with the expected
  fields; the search text of "/MUSICIANS/H/Hubbard_Rob/Commando.sid" contains the words "hubbard", "rob" and
  "commando".
- Level: host

**SPEC-161 Building it.** The worker builds the index when there is none, when its format number is old, when
the song length database has changed, or when the user asks ("Rebuild index" on the HVSC screen). Step one
writes a record for every tune HVSC lists (SPEC-142), without opening any tune: this takes seconds and makes
every path searchable. Step two reads the first 124 bytes of each present tune, 200‡ files at a time with a
pause between batches while a tune is playing, and adds their fields. The file is rewritten through SPEC-132
after every 2,000‡ tunes, so an interrupted build continues from there. A tune fetched or added later is
added to the index when it arrives. Progress is shown on the Search and HVSC screens as a percentage.
- Covers: REQ-091
- Oracle: on the host, for a generated collection of 61,157 tunes: the build completes; interrupted after
  10,000 tunes and restarted, it reads only the remaining files (counted by the `Files` fake) and the result
  equals an uninterrupted build's; searching during the build returns matches from what is done. On the
  device, the results file holds the time step two took for the tunes present, and the underrun counter
  during it.
- Level: host, device

**SPEC-162 What a search matches.** The query is treated like search text and split at spaces into words. A
tune matches when every word of the query is the beginning of some word in the tune's search text. Results
are in order of path, and at most 500‡ are returned; the count shown is the true number of matches. A search
is one pass over the index and runs on the worker.
- Covers: REQ-090
- Oracle: on the fixture collection a table of queries, including "hub 1985", "1985 hub", "HUB", "huls" for
  "Hülsbeck" and a word that matches only a path, each give exactly the expected tunes; an absent tune is
  found by a word from its path and not by its title. On the device a two-word query over the HVSC #85 index
  returns within 500 ms‡.
- Level: host, device

**SPEC-164 Filters.** The filter panel limits results to a chip model (6581 or 8580), a clock (PAL or NTSC),
and a range of years. Absent tunes, whose headers are not known, are left out while any filter is set.
- Covers: REQ-092
- Oracle: each filter alone, and all together, remove exactly the fixture tunes that do not match.
- Level: host

**SPEC-165 Searching HVSC's site.** This item is specified and switched off: the build option
`SIDPLAYZERO_ONLINE_SEARCH` is off by default, and stays off until HVSC's maintainers have agreed to the use
of their site's search address. When on, the Search screen gains the action "Online", which sends the query to
`https://hvsc.c64.org/api/v1/sids?q=` and lists the titles, authors and released lines returned; choosing one
looks its path up and plays it through SPEC-141.
- Covers: REQ-093
- Oracle: with the option on and a fake `HttpClient` answering as the site does, the results are listed and
  choosing one plays it; with the option off, no request to that address is ever made.
- Level: host

## 10. Playlists and favourites

**SPEC-170 A playlist's file.** A playlist is a UTF-8 text file in the playlists folder (SPEC-134), named after
the playlist with `.m3u8` added. It is an extended M3U file that any player can read, with two kinds of line
of its own:

```
#EXTM3U
#PLAYLIST:Loading tunes
#EXTINF:235,Rob Hubbard - Commando
#SIDTUNE:subtune=1 md5=6d019ecba831a9f853675aac29a61c10
/MUSICIANS/H/Hubbard_Rob/Commando.sid
```

`#PLAYLIST:` holds the name as the user typed it. Each entry is up to three lines: an optional `#EXTINF:` with
the length in seconds and "author - title"; an optional `#SIDTUNE:` with the subtune and the MD5 of the tune's
file; and the tune itself. A tune inside the collection is written as its HVSC path, starting with `/`. A tune
outside it is written as `file://` followed by its full path. Without a `#SIDTUNE:` line the entry means the
tune's default subtune. Lines the app does not know are kept when the file is saved again.
- Covers: REQ-107, REQ-102
- Oracle: a playlist written by hand from this description, with and without the optional lines, loads and
  plays the expected subtunes; saved again, it keeps an unknown `#` line it contained.
- Level: host

**SPEC-171 Favourites.** Favourites are the playlist named "Favourites", which is listed first and cannot be
renamed or deleted. "Fav" adds the tune and subtune if they are not in it and removes them if they are, and
shows which it did. A tune that is a favourite carries a star in the browser, in search results and on the
now-playing screen.
- Covers: REQ-100
- Oracle: marking a fixture tune twice adds then removes it; marking it once and restarting shows the star
  and one entry in the file.
- Level: host, simulator

**SPEC-172 Making and changing playlists.** The Playlists screen lists the playlists with their number of
entries. Enter opens one.

| Key | Label | Does |
|---|---|---|
| 4 | Back | Returns to the browser |
| 5 | New | Asks for a name and makes an empty playlist |
| 6 | Rename | Asks for a new name |
| 7 | Delete | Asks "Delete this playlist?", and deletes it on Enter |
| 8 | Import | Opens the import panel (SPEC-175) |

A name is 1 to 40 characters, with no `/` and no control characters, and must differ from every other
playlist's name without regard to case. The Playlist screen lists the entries: title, or file name when the
title is not known, the subtune, and a mark for absent or missing (SPEC-173). Enter plays from that entry.

| Key | Label | Does |
|---|---|---|
| 4 | Back | Returns to Playlists |
| 5 | Up | Moves the entry one place up |
| 6 | Down | Moves the entry one place down |
| 7 | Remove | Removes the entry |
| 8 | More | Opens a panel: Fetch missing (SPEC-174); Export (SPEC-179) |

The "add to playlist" panel lists the playlists and "New playlist…". Enter adds the tune with its subtune: the
playing subtune from the now-playing screen, the default subtune from the browser or Search. Adding an entry
that is already there adds nothing and says so. Every change is saved at once through SPEC-132.
- Covers: REQ-101, REQ-064
- Oracle: a simulator script makes a playlist, adds three fixture tunes, moves the third to the top, plays it
  through with the fake clock, renames it, and deletes it; the file on disk after each step is as expected.
- Level: host, simulator

**SPEC-173 Finding an entry's tune.** An entry is resolved in this order: a file at its path; else, if it has
an MD5 and the listing (SPEC-142) has a tune with that MD5 at another path, that tune, and the entry's path is
corrected the next time the playlist is saved; else, if the listing has its path, the tune is **absent** and
can be fetched; else the entry is **missing**. A missing entry stays in the playlist, is shown with its mark,
and is skipped by playback.
- Covers: REQ-102, REQ-103
- Oracle: after a fixture folder is renamed in the collection and the listing, every entry that pointed into
  it resolves and plays; a playlist of present, missing, present plays the first and third and still holds
  three entries afterwards.
- Level: host

**SPEC-174 Fetching for a playlist.** Playing an absent entry fetches it (SPEC-141). "Fetch missing" fetches
the playlist's absent entries one after another, with a pause of 250 ms‡ between them, showing "n of m" and a
bar. It fetches at most 200‡ in one go, and says how many remain if there are more. ESC stops it after the
file in flight. It stops at the first failure for lack of network.
- Covers: REQ-104
- Oracle: with the fake `HttpClient`: a playlist with three absent entries makes three requests, in order,
  and all three become present; one with 250 makes 200 and reports 50 remaining; the request log holds only
  those paths.
- Level: host

**SPEC-175 Importing: where from, and M3U.** The import panel lists the files ending in `.m3u`, `.m3u8`,
`.js2`, `.json` or `.spl` in the folder `import` beside the playlists folder, and in the collection root.
Enter imports one into a new playlist named after the file. The file is only read. An M3U or M3U8 file is
read as UTF-8 if it is valid UTF-8 and as Latin-1 if not, with either kind of line ending. `#PLAYLIST:` gives
the name. Each line that is not empty and does not start with `#` is an entry, and its subtune comes from the
first of these that applies: a `#SIDTUNE:` line before it; a `#PHOSPHOR:` line before it holding `song=N`; an
ending `?N` on the line; an ending `/tune_NNN.sid` after a name ending in `.sid`; a `{N}` at its start. Other
`#` lines are ignored.
- Covers: REQ-105
- Oracle: one fixture file for each of the dialects (VSID, sidplaywx, Phosphor, SIDPLAY for macOS, Audacious,
  MPD, in_sidplay2), written as that player writes it, imports with the expected tunes, subtunes and order.
- Level: host

**SPEC-176 Importing: JSidplay2.** A `.js2` file is one tune per line; a leading `<HVSC>` is removed. A
`.json` file is an array of objects, and each object's `filename` is a tune. Neither carries subtunes.
- Covers: REQ-105
- Oracle: fixture files of both kinds import with the expected tunes in order.
- Level: host

**SPEC-177 Importing: Sidplay/w.** A `.spl` file starts with `[SIDPLAY/Windows Playlist]` and a `Format=`
line. In format 1, each numbered section has `Filename=`, `Subsong=` and `Time=`. In format 2, each entry is a
block of lines beginning with `file=`, with `subtune=` and `time=` among them. A `BaseDir=` line, in either
spelling of its name, is put in front of file names that are not full paths. Names of keys are read without
regard to case, and keys the app does not know are ignored.
- Covers: REQ-105
- Oracle: the format-1 sample from Sidplay/w's own change notes and a format-2 file written to match the
  HVSC Top 100 playlist's layout each import with the expected tunes and subtunes.
- Level: host

**SPEC-178 Matching an imported path to a tune.** Backslashes become slashes. Then the first of these that
finds a tune in the listing or under the root is used: the part after `/C64Music/`, if the path has one; the
path as it stands, as an HVSC path; the path with a `/` put in front; the path taken relative to the folder of
the file being imported; the same four again without regard to case; the file name alone, if exactly one tune
in the listing has that name. An entry that none of them finds is kept as a missing entry holding the original
text. After an import a panel says how many entries were imported and lists those not found.
- Covers: REQ-105, REQ-103
- Oracle: a fixture file with one entry for each of those forms, and one that matches nothing, imports with
  every form resolved to the expected tune and the last reported as not found and kept.
- Level: host

**SPEC-179 Exporting.** "Export" writes the playlist to the folder `export` beside the playlists folder, as
`#EXTM3U`, then for each entry an `#EXTINF:` line and the HVSC path with `?N` added when the subtune is not the
tune's default.
- Covers: REQ-106
- Oracle: exporting a fixture playlist and importing the file gives a playlist with the same tunes, subtunes
  and order.
- Level: host

## 11. Real SID chips

**SPEC-190 The sound backend can be replaced.** The player does not use `Engine` and `AudioSink` directly. It
uses a `SoundBackend`, which loads a tune, starts, pauses, stops, reports the position, mutes voices, switches
the filter, and says which of those it can do. The software backend is made of `Engine`, the ring buffer and
`AudioSink` (SPEC-070). No file of the collection, the queue, playlists, search or the screens names the
software backend.
- Covers: REQ-110
- Oracle: every host test of the player runs against a fake `SoundBackend`; a check finds no reference to the
  software backend's type outside `platform` and the one place that creates it.
- Level: host

**SPEC-191 USBSID-Pico.** This item is specified and switched off: the build option `SIDPLAYZERO_USBSID` is off
by default and stays off until the licence wording of the driver's `usbsid_protocol.h` has been cleared with
its author (R9, FIND-007). When on, libsidplayfp is built with its USBSID backend, the package depends on
`libusb-1.0-0`, and the setting `output` (`software`, the default, or `usbsid`) appears. With `usbsid`, the
engine runs in step with the real clock and drives the device; nothing goes to `AudioSink`. Subtunes, song
lengths and SPEC-065 work unchanged, because they belong to the player. If the device is not there, or goes
away, the player goes to Paused with "USBSID-Pico is not connected", and playing again tries again.
- Covers: REQ-111
- Oracle: with the option on and a fake hardware backend, the player's tests of SPEC-060 to SPEC-065 pass
  unchanged, and a backend that reports the device gone gives the message. On the device, the checklist plays
  a tune on a USBSID-Pico, steps through its subtunes, and unplugs it.
- Level: host, device

**SPEC-192 Permission to use the device.** With `SIDPLAYZERO_USBSID` on, the package installs
`/usr/lib/udev/rules.d/60-sidplayzero.rules`, holding one rule that gives the logged-in user access to USB
device `cafe:4011` by tagging it `uaccess`. The rule runs no program.
- Covers: REQ-112, REQ-125
- Oracle: the store's install-path check accepts the file; a check finds no `RUN`, `PROGRAM` or `IMPORT` in
  it. On the device, the app opens the USBSID-Pico as the normal user.
- Level: host, device

## 12. Starting, stopping and staying safe

**SPEC-200 Starting.** The app starts in this order: read `settings.ini` and `state.ini`; set up the display
and the keyboard; show the browser at the remembered folder; and only then, on the worker, load the song
length database, check the listing and the index, and take SPEC-072's measurement if it is due. Nothing in
this order uses the network. From the start of the process to a browser that answers keys takes at most
2 s‡.
- Covers: REQ-055, REQ-086
- Oracle: the simulator, started with networking forbidden, shows the browser and answers a key; on the
  device the start time written by SPEC-241, over five starts with the cache already there, is at most
  2,000 ms‡ each time.
- Level: simulator, device

**SPEC-201 Stopping.** A held ESC (SPEC-106), SIGTERM and SIGINT all do the same: stop taking keys; close the
sound stream; write `state.ini` (SPEC-132); tell the worker to stop, which keeps any partial download for
later; and exit with status 0. This takes at most 1 s‡. A second signal during it ends the process at once.
- Covers: REQ-120, REQ-127
- Oracle: a test sends SIGTERM while a fixture tune plays: the process ends with status 0 within 1 s‡, and
  `state.ini` names that tune; the same with SIGINT.
- Level: host, simulator

**SPEC-202 After being killed.** At start the app removes its temporary files, and throws away and rebuilds
any cache file (`index.bin`, `listing.bin`, the speed factor) whose format number or recorded sizes do not
fit. Nothing else needs repair, because of SPEC-132.
- Covers: REQ-121
- Oracle: a test starts the real program, sends SIGKILL at a random moment within ten seconds, and starts it
  again, 50 times: every start reaches the browser, and the position restored is at most 10 s‡ behind the one
  at the kill.
- Level: host

**SPEC-203 A tune file is checked before the engine sees it.** A file is given to the engine only if: it is
between 124 bytes and 1 MiB long (118 bytes for a version 1 header); it starts with `PSID` or `RSID`; its
version is 1 to 4; its data offset is 0x76 for version 1 and 0x7C otherwise, and is less than the file's
length; and its number of songs is 1 to 256. Otherwise the tune fails with "Not a valid SID file". The same
check is made on a fetched file before it is stored.
- Covers: REQ-122, REQ-126
- Oracle: the fixture set of bad tunes (truncated, wrong magic, data offset past the end, no songs, too
  large) is each rejected with that message, and the fake engine's load is never called for them; with the
  real engine, under AddressSanitizer, each is rejected without a report.
- Level: host

**SPEC-204 Other files are read with limits.** A text file is read at most this far: settings 64 KiB, a
playlist or a file being imported 4 MiB, STIL 16 MiB, the song length database 32 MiB. A larger file is
refused with a message. A line longer than 4,096 bytes is cut there. A playlist holds at most 20,000 entries;
the rest of the file is not read, and a message says so.
- Covers: REQ-122
- Oracle: fixture files past each limit, with a million-character line and with zero bytes inside, load or
  are refused as described, within 2 s‡ each, under AddressSanitizer.
- Level: host

**SPEC-205 Fuzzing.** The tune check of SPEC-203, the playlist and import readers, the listing builder, the
STIL reader and the settings reader each have a fuzz target. The host level runs each for 30 s‡ from a small
set of starting files, with AddressSanitizer and UndefinedBehaviorSanitizer on, and fails on any crash, report
or input that takes longer than 2 s‡.
- Covers: REQ-122
- Oracle: the fuzz run's status.
- Level: host

**SPEC-206 An ordinary user's program.** The app never asks for more rights: it has no setuid file, runs no
`sudo`, and installs no service and no scripts that run at installation. It needs only what the stock image
gives its user: the framebuffer, the keyboard, sound and the home folder.
- Covers: REQ-125, REQ-127
- Oracle: the store's package checks pass; the `.deb` has no maintainer scripts; on the device the app runs
  from the launcher, and the results file records the groups of the user it ran as.
- Level: host, device

**SPEC-207 The screen while playing.** Whatever the system does to the screen, rendering and output do not
depend on the screen or on drawing. With `screen_off_after` above 0, the app turns the backlight off after
that many seconds without a key while a tune plays, having noted its level, and any key turns it back on and
does nothing else. It restores the level when playback pauses or stops and when the app exits. If the app
cannot write the backlight, the setting's row reads "Not available on this device" and stays at 0.
- Covers: REQ-128, REQ-127
- Oracle: with a fake backlight file, the level goes to 0 after the time, returns on a key, and is restored
  at exit; with a read-only one the row shows the words. On the device, a tune left playing for two minutes
  plays to its end with the underrun counter at 0, whatever the screen did.
- Level: host, device

**SPEC-208 Messages.** Every message the user can see is in one table in the source, with an identifier, so
tests name messages by identifier and the wording lives in one place. A message is shown in a panel that
Enter or ESC closes, except those this document says are shown for a time.
- Covers: REQ-019, REQ-044, REQ-007
- Oracle: a host test fails if a message is shown that is not in the table, or if two rows have the same
  identifier.
- Level: host

## 13. The package

**SPEC-220 What is installed.** The `.deb` installs these files and nothing else:

| File | What it is |
|---|---|
| `/usr/bin/sidplayzero` | The program |
| `/usr/share/APPLaunch/applications/sidplayzero.desktop` | The launcher's entry |
| `/usr/share/APPLaunch/share/images/sidplayzero.png`, `sidplayzero_80.png`, `sidplayzero_100.png` | The launcher's icons |
| `/usr/share/doc/sidplayzero/LICENSE`, `THIRD-PARTY-NOTICES.md`, `README.md` | Licence and notices |
| `/usr/lib/udev/rules.d/60-sidplayzero.rules` | Only with SPEC-192 |

It has no maintainer scripts and no configuration files. The file is named
`sidplayzero_<version>-1_arm64.deb`.
- Covers: REQ-140, REQ-142, REQ-125
- Oracle: `dpkg-deb -c` on the built package lists exactly these paths; `dpkg-deb -e` finds only `control`
  and `md5sums`.
- Level: host

**SPEC-221 What it depends on.** `Depends`: `libc6`, `libstdc++6`, `libgcc-s1`, `libcurl4t64`,
`ca-certificates`, `pulseaudio-utils`. `Recommends`: `7zip`. All are in Debian trixie's main archive.
`Architecture` is `arm64`. `Maintainer` comes from the build setting `SIDPLAYZERO_MAINTAINER`, which has no
default in the repository.
- Covers: REQ-141, REQ-143
- Oracle: a check reads the package's `Depends` and `Recommends` and finds each name in trixie's main arm64
  index; installing the package in a clean trixie arm64 container with only the main archive succeeds; the
  build fails when no maintainer is given.
- Level: host

**SPEC-222 The launcher's entry.** The `.desktop` file has `Type=Application`, `Name=SidPlayZero`,
`Exec=/usr/bin/sidplayzero`, `Icon=share/images/sidplayzero.png` and `Terminal=false`.
- Covers: REQ-140
- Oracle: the file in the package has exactly these lines. On the device the launcher shows the name and
  icon, starts the app, and returns when it exits.
- Level: host, device

**SPEC-223 The store's manifest.** `app-builder.json` has `app_name` "SidPlayZero", `package_name`
"sidplayzero", the version, and a `store` section with: a summary of at most 80 characters; a description;
the category "Media"; `license` "GPL-3.0-or-later"; the source repository; the author, from a build setting;
a share code; the icon; three to six screenshots, which are reference images from the simulator tests; and
the seven permissions, of which only `network` is true, and `additional_hardware` too when SPEC-191 is on.
- Covers: REQ-145
- Oracle: the store's own metadata check, at a pinned version, passes on the manifest and its files.
- Level: host

**SPEC-224 The store's checks run on every build.** The checks on every pull request run the store's
`install_path_policy.py` and its package checks, at a pinned commit of the store's repository, on the built
`.deb`.
- Covers: REQ-142
- Oracle: the checks pass on the real package and fail when a file is added under `/usr/bin` with another
  name.
- Level: host

**SPEC-225 Nothing of one person's.** The repository's check (`tools/check_repo.py`) looks through every
tracked file and through the files of the built package for private network addresses, for the private terms
kept in the `PRIVATE_TERMS` secret, and for paths under `/home/`. Everything that differs between devices
comes from the environment variables `HVSC_BASE`, `SIDPLAYZERO_KEYBOARD` and the XDG variables, or from
`settings.ini`.
- Covers: REQ-143
- Oracle: the check passes on the real tree and fails when a file with a private term or a `/home/` path is
  added.
- Level: host

**SPEC-226 Nothing publishes.** The repository's check fails if any workflow or script contains `czdev login`
or `czdev publish`.
- Covers: REQ-146
- Oracle: the check passes on the real tree and fails when such a line is added to a workflow.
- Level: host

**SPEC-227 Size.** The built `.deb` is at most 20 MB‡, and a check fails the build when it is larger.
- Covers: REQ-147
- Oracle: the check's status, and the size it prints.
- Level: host

**SPEC-228 About.** The About screen shows: "SidPlayZero" and its version; "libsidplayfp 3.1.2" and
"libresidfp 1.3.0", read from the libraries; the licence, "GPL-3.0-or-later", and where its text is installed;
the address of the source code; and HVSC's notice (SPEC-152). `sidplayzero --version` prints the first two
lines.
- Covers: REQ-148, REQ-126, REQ-085
- Oracle: the About screen's screenshot matches its reference, and the version in it equals the version in
  the package's control file and in `app-builder.json`.
- Level: host, simulator

**SPEC-229 Installing and removing.** On a stock system image,
`sudo apt install ./sidplayzero_<version>-1_arm64.deb` installs the package and what it depends on.
`sudo apt remove sidplayzero` removes every file of SPEC-220 and nothing else.
- Covers: REQ-140, REQ-141, REQ-075
- Oracle: on the device: install, start from the launcher, play a tune, remove; afterwards `dpkg -L` shows
  nothing left and the settings, playlists and collection are still there.
- Level: device

## 14. Budgets and how they are measured

**SPEC-240 The budgets.** These are the numbers the tests hold the app to. Each is a proposal until the device
run has measured it (SPEC-243).

| Budget | Value | From | Measured |
|---|---|---|---|
| First screenful of a folder listed before | 300 ms‡ | REQ-003 | Device |
| First screenful of a folder not listed before | 1 s‡ | REQ-003 | Device |
| Rows per second with an arrow held | 20‡ or more, no pause over 100 ms‡ | REQ-003 | Device |
| Memory in use (peak resident size) | 64 MB‡ | REQ-003 | Device |
| Detail line after the highlight rests | 200 ms‡ | REQ-004 | Simulator |
| Key to first samples | 500 ms‡ | REQ-020 | Device |
| Underruns in 30 minutes of the benchmark set | 0 | REQ-050 | Device |
| Processor use while playing | 60 %‡ of one core, on average | REQ-050 | Device |
| Key to screen, 95 of 100 | 100 ms‡ | REQ-054 | Device |
| Start to a browser that answers | 2 s‡ | REQ-055 | Device |
| Exit after SIGTERM | 1 s‡ | REQ-120 | Host, device |
| Search, two words, HVSC #85 | 500 ms‡ | REQ-090 | Device |
| Index, step two, complete collection | 5 minutes‡ | REQ-091 | Device |
| Setting HVSC up | 2 minutes‡ | REQ-080 | Device |
| Any bad file handled | 2 s‡ | REQ-122 | Host |

- Covers: REQ-003, REQ-020, REQ-050, REQ-054, REQ-055, REQ-090, REQ-091
- Oracle: each row's figure in the results file of the device run, or in the named level's test.
- Level: host, simulator, device

**SPEC-241 Measurements the app takes of itself.** Started with `--metrics FILE`, the app writes, when it
exits, a text file of `key = value` lines: `start_ms`; `key_to_screen_ms_p50`, `_p95` and `_max`;
`key_to_sound_ms_max`; `underrun_frames`; `render_load_percent`; `peak_memory_kb`; `audio_backend`;
`audio_rate`; `speed_factor`; `index_step_two_s`; `index_tunes`; `folder_open_ms_max`; `search_ms_max`;
`memory_check_mib`; `user_groups`. Nothing else is written, and no value holds a name or an address.
- Covers: REQ-050, REQ-054, REQ-055, REQ-162
- Oracle: a simulator run with `--metrics` writes exactly these keys.
- Level: simulator, device

**SPEC-242 The benchmark set.** The device run plays four tunes from HVSC, in a loop: an ordinary one,
`/MUSICIANS/H/Hubbard_Rob/Commando.sid`; one that plays samples, `/MUSICIANS/G/Galway_Martin/Arkanoid.sid`; one
for two SID chips, `/MUSICIANS/J/Jammer/Antipop_2SID.sid`; and one for three,
`/DEMOS/A-F/Devils_ReSIDence_3SID.sid`. They are not in the repository. The tester sets HVSC up on the device
and the run fetches them through the app, as SPEC-141 does for any tune.
- Covers: REQ-050, REQ-087
- Oracle: the results file names the four tunes as played and gives the engine used for each.
- Level: device

**SPEC-243 What the device run settles.** After the device run, every ‡ in this document and in the
requirements is either confirmed by the measurement or raised as a finding in `findings.md`, to be resolved in
the document it belongs to. The same run settles what Phase 1 left unknown: the system's sample rate and
format, the user's groups and what they allow, what the screen does while an app runs, and whether the
device passes SPEC-145's check.
- Covers: REQ-050, REQ-164
- Oracle: `findings.md`, after the device run, holds a finding or a confirmation for each ‡.
- Level: device

## 15. Tests

**SPEC-250 Three levels, one command each.** The script `tools/test` takes one word: `host`, `simulator` or
`device`. `tools/test host` builds `core` with its fakes and runs the host tests, then the same under
AddressSanitizer and UndefinedBehaviorSanitizer, then the fuzz targets, then the repository's checks.
`tools/test simulator` builds the app for `linux-x86-64` and runs the simulator tests. `tools/test device`
prints what to copy to the device and what to run there (SPEC-254). Each ends with status 0 only if everything
it ran passed.
- Covers: REQ-160, REQ-161
- Oracle: on a clean checkout each command runs; a deliberately failing test at each level makes its command
  end with a non-zero status.
- Level: host, simulator, device

**SPEC-251 The host level.** Host tests are C++ programs using the doctest framework (one header, MIT),
linked with `core` and the fakes of SPEC-021. They use no LVGL, no sound device, no network and no clock but
the fake one, and the whole level runs in under two minutes‡.
- Covers: REQ-160
- Oracle: the level passes with networking forbidden and no sound device.
- Level: host

**SPEC-252 Fixtures are made, not collected.** `tools/make_fixtures.py` writes everything under
`tests/fixtures/` and a manifest of it. The tunes are original: each is a few dozen bytes of 6502 code,
listed in the generator, that sets up a voice and changes its pitch on every call, so each subtune sounds
different and renders the same every time. The set has: a one-subtune tune ("Commando Test"); a
three-subtune tune with default 2; two- and three-SID tunes; tunes marked 6581 and 8580, PAL and NTSC; a tune
marked as needing BASIC; a tune titled "Hülsbeck Tëst" with 32-character fields; and the bad tunes of
SPEC-203. It also writes a song length database, a STIL file and an `HVSC.txt` for them, in HVSC's formats;
small collections laid out like HVSC; playlists in every format of SPEC-175 to SPEC-177; the archives of
SPEC-149; and, into a temporary folder when a test asks, a collection of 61,157 tunes in 1,981 folders shaped
like HVSC #85.
- Covers: REQ-087, REQ-160
- Oracle: running the generator twice gives identical files; SPEC-153's check accepts them; the real engine
  loads every good tune.
- Level: host

**SPEC-253 The simulator level.** The app built for the desktop takes `--headless --script FILE`. In this
mode it opens no window and no sound device: LVGL draws into memory, time comes from the fake clock, sound
goes to a recording sink, and `HttpClient`, `Unpacker`, `SystemVolume` and `Memory` are the fakes, set up from
a folder named on the command line. The engine is the real one unless the script asks for the fake. A script
is lines of: `key NAME`, `hold NAME MS`, `type TEXT`, `advance MS`, `shot NAME`, `expect title TEXT`,
`expect state TEXT`, `quit`. `shot` writes the screen as a PNG, and the test compares its pixels with
`tests/golden/NAME.png`; if they differ it fails and writes the screen it got and a picture of the
difference. One more test starts the windowed simulator under a virtual display and checks that it draws a
frame.
- Covers: REQ-160, REQ-149
- Oracle: a script that takes a screenshot of the empty-collection screen passes against its reference and
  fails when one pixel of the reference is changed.
- Level: simulator

**SPEC-254 The device level.** The device level is one script, `tools/device-run.sh`, and one checklist,
`docs/device-checklist.md`, done in one sitting. The script: installs the `.deb` it is given; starts the app
five times with `--metrics` and records the start times; runs `--selftest`, which takes SPEC-072's
measurement, builds the index, plays the benchmark set (SPEC-242) for 30 minutes with a key script, and writes
the measurements of SPEC-241; records SPEC-145's check; and gathers it all into one results file. The
checklist has the steps a person does and ticks: the launcher shows the app; a held ESC returns to the
launcher; sound comes from the speaker; headphones plugged and unplugged twice; the volume keys change the
level and the launcher agrees afterwards; a tune left for two minutes plays on; setting HVSC up from an empty
folder; removing the package leaves the user's files.
- Covers: REQ-162, REQ-160, REQ-043
- Oracle: the results file from the reference device has every key listed in SPEC-241 and a line for each
  checklist step.
- Level: device

**SPEC-255 The results file holds nothing private.** The script writes only the keys listed in SPEC-241 and
the checklist's answers. Before it finishes it searches the file for the device's host name, the user's name
and any IPv4 address, and if it finds one it deletes the file and says why.
- Covers: REQ-162, REQ-143
- Oracle: a host test runs the gathering step with a planted host name in one measurement and sees the file
  refused; the real results file passes `tools/check_repo.py`'s private-details check.
- Level: host, device

**SPEC-256 Every item has a test, and every test an item.** From Phase 4 on, each test carries the identifiers
of the SPEC items it checks, in its name or in a comment of the form `SPEC-nnn`. `tools/check_repo.py` fails
if a requirement has no SPEC item, if a SPEC item names a requirement that does not exist, if a SPEC item has
no test, or if a test names a SPEC item that does not exist. Until Phase 4 it checks the first two.
- Covers: REQ-164
- Oracle: the check passes on the real documents, and fails when a requirement's only SPEC item is removed
  or a SPEC item names a requirement that does not exist.
- Level: host

**SPEC-257 Checks on every pull request.** A GitHub Actions workflow runs `tools/test host` and
`tools/test simulator` on every pull request and on every push to `main`, behind the one job, "All checks
passed", that the repository requires. The device level is never run there.
- Covers: REQ-161
- Oracle: the workflow's result on a pull request that breaks a host test is a failed "All checks passed".
- Level: host
