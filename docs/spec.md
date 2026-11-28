# SidPlayZero specification

| | |
|---|---|
| Status | **Approved by the owner on 25 November 2026**, after independent review in which every item was closed. This is the Phase 2 artifact. Changed for FIND-015 to FIND-024, found in Phases 3 and 4; the owner accepted those of Phase 4 on 26 November 2026 |
| Date | 25 November 2026; changed 26 November 2026 |
| Derives from | [`requirements.md`](requirements.md), approved 25 November 2026, with the changes made for FIND-006 and FIND-010 to FIND-013 |
| Part of it | [`keymap.tsv`](keymap.tsv), the key map (SPEC-094) |
| Review | [`spec-review.md`](spec-review.md): 67 items over four passes, all closed |
| Next artifact | `docs/test-framework-tasks.md` (Phase 3) |

## 1. How to read this document

The requirements say what SidPlayZero has to do. This document says how it does it, precisely enough that
tests can be written from it before any of it is built.

- **SPEC-nnn** is one item of the specification. Each has four lines under it:
  - **Covers** names the requirements it implements. Every requirement is covered by at least one item, and
    `tools/check_repo.py` checks that.
  - **Priority** is Must, Should or Could, as in the requirements. The Should and Could features that can be
    left out of version 1.0 are named parts, and leaving one out touches no Must item's tests (SPEC-115).
  - **Oracle** says how a test knows the item is met: what is observed, and what it is compared with.
  - **Level** says where that test runs (SPEC-250): **host-unit**, the `core` code against fakes;
    **host-integration**, real parts on the host (the real engine, the real program, a local server, a real
    `7z`); **host-package**, the built `.deb`; **simulator**, the real app drawing its real screens at 320×170
    with no window; or **device**.
- A number marked **‡** comes from the requirements as a proposed budget, or is proposed here. It holds until
  a measurement on the device says otherwise (section 14).
- Section 2 records the decisions the requirements left to this document (R1 to R9 there), each with the
  evidence it rests on.

Terms are those of the requirements. These are added here:

| Term | Meaning |
|---|---|
| Present | A tune whose file is on the device |
| Absent | A tune that HVSC lists and whose file is not on the device yet |
| Missing | A playlist entry that matches no tune on the device and none that HVSC lists |
| Entry | A tune, with or without a subtune number. Without one it means the tune's default subtune |
| Queue | The ordered list of entries that "next" and "previous" move through (SPEC-062) |
| Notice | A line of text shown for a few seconds without taking the keys (SPEC-113) |
| Panel | A box that takes the keys until it is closed (SPEC-114) |
| Screen description | What the simulator reports about the screen, as data (SPEC-254) |

## 2. Decisions

**SPEC-001 Language, toolkit and build.** The app is written in C++17 and draws with LVGL 9.5.0. It is built
with CMake 3.31 or newer. The preset `linux-x86-64` builds it for a Linux desktop. The preset `cp0-cross`
builds it for the device with the `aarch64-linux-gnu` compiler against M5Stack's sysroot, release 0.0.4, which
the build downloads and checks by SHA-256; the workflow preset `cp0-cross-package` also packs the `.deb` with
CPack. Both builds pass `-ffile-prefix-map` so that no path of the build machine ends up in the program.
- Covers: REQ-149, REQ-140
- Priority: Must
- Oracle: on a clean checkout, `cmake --workflow --preset linux-x86-64` ends with status 0 and produces the
  program; with the environment variable `SIDPLAYZERO_MAINTAINER` set to
  `SidPlayZero test build <build@sidplayzero.test>`, `cmake --workflow --preset cp0-cross-package` ends with
  status 0 and produces the `.deb`.
- Level: host-integration, host-package

**SPEC-002 The engine library is built in.** Tunes are played by libsidplayfp 3.1.2 with libresidfp 1.3.0.
Both are built from their release archives, which the build downloads and checks against these SHA-256 sums
before use, and are linked into the program statically:

| Archive | SHA-256 |
|---|---|
| `libsidplayfp-3.1.2.tar.gz` | `6d16c20e523ac4d3e385531dafe247a830b59bcb4eb73a557f15cf4e7ae6da84` |
| `libresidfp-1.3.0.tar.gz` | `6de8880dcfc11b993107f71b538262f08ae07fd0e595297ce776f641b5435b38` |

The library is configured with `--without-exsid`, and with `--without-usbsid` unless SPEC-191 is switched on.
The package does not depend on Debian's `libsidplayfp6`.
- Covers: REQ-056, REQ-126, REQ-141, REQ-144
- Priority: Must
- Oracle: the build fails if a checksum differs; `readelf -d` on the program lists no `libsidplayfp`;
  `sidplayzero --version` prints "libsidplayfp 3.1.2"; the program contains no symbol of the exSID backend.
- Level: host-integration, host-package
- Evidence (R3, FIND-009): Debian trixie's 2.12.0 uses a tune's data offset without the check that fixes
  CVE-2026-111148, and Debian has shipped no fix. Version 3.1.2 has the fix, builds as a static library, and
  its release archive needs no extra build tools. Building it in also keeps the app working when a later
  system image carries a different version.

**SPEC-003 The library is linked, not the `sidplayfp` program driven.** The app calls libsidplayfp directly:
`SidTune` to load a tune from memory, `sidplayfp::config`, `load`, `play` and `mix` to render it, `mute` and
`filter` for the sound controls. It does not use the library's file loading or its `SidDatabase`.
- Covers: REQ-056, REQ-163
- Priority: Must
- Oracle: SPEC-071's comparison passes, which is only possible with direct control of the power-on delay.
- Level: host-integration
- Evidence (R4): apps are ordinary processes and may use threads (CON-03). Rendering through this interface
  gave output identical, sample for sample, to the WAV written by the `sidplayfp` program built from the same
  library: 960,027 of 960,027 samples with reSIDfp and 960,053 of 960,053 with sidlite in mono, and every
  value of ten seconds in stereo for tunes with one, two and three SID chips.

**SPEC-004 Two engines, and a setting that chooses between them.** The setting `engine` has three values:
`accurate` is reSIDfp, `light` is sidlite, and `auto`, the default, picks one of the two for each tune by
SPEC-072.
- Covers: REQ-051
- Priority: Must
- Oracle: with a stored speed measurement (a `speed.ini` in the cache folder, SPEC-072), each setting gives
  the expected engine for one-, two- and three-SID fixture tunes (SPEC-072's table), and with none `auto`
  gives sidlite; the engine's name is in the screen description of the
  now-playing screen.
- Level: host-unit, simulator
- Evidence (R2, R3): on an x86-64 host the benchmark program of SPEC-262 gave, for reSIDfp, 15 to 24 times
  real time with one SID chip, 10 to 12 with two and 7 to 8 with three; and for sidlite 125 to 175 times
  with any number. The device's processor is roughly ten times slower, which puts reSIDfp near the limit for
  one SID and under it for more. SPEC-072 decides from a measurement on the device itself, and SPEC-262 gets that measurement
  before the thresholds are fixed.

**SPEC-005 Sound goes out through miniaudio.** Samples are handed to the system through miniaudio 0.11.25, a
single source file (public domain or MIT-0) built into the program. It tries the PulseAudio backend first and
ALSA second, and loads their libraries when the program runs. The app reaches it only through the `AudioSink`
interface of SPEC-021.
- Covers: REQ-043, REQ-044, REQ-141
- Priority: Must
- Oracle: `readelf -d` lists neither `libpulse` nor `libasound`; the package depends on `libpulse0`
  (SPEC-221); on the device the results file names the backend in use as PulseAudio.
- Level: host-package, device
- Evidence (R5): the device runs PipeWire's PulseAudio server (CON-08); the stock Music app and the
  `sidplayfp` program both use miniaudio in this way.

**SPEC-006 Downloads use libcurl.** HTTP requests are made with the system's libcurl (`libcurl4t64`), behind
the `HttpClient` interface of SPEC-021, with the rules of SPEC-150.
- Covers: REQ-124, REQ-141
- Priority: Must
- Oracle: the package's `Depends` names `libcurl4t64` and `ca-certificates`; SPEC-150's tests pass.
- Level: host-integration, host-package

**SPEC-007 The complete archive is unpacked by the system's `7z`, and only behind a check.** For REQ-089 the
app runs the `7z` program from Debian's `7zip` package as a child process, without a shell, behind the
`Unpacker` interface. The package recommends `7zip` and does not depend on it. SPEC-144 has the steps.
- Covers: REQ-089, REQ-141
- Priority: Should
- Oracle: with the fake `Unpacker`, the complete download runs its steps in order; with a real `7z` and a
  fixture archive it ends with the fixture tree in place; the real `Unpacker` starts `7z` with an argument
  list and no shell (checked by a `7z` stand-in that records its arguments).
- Level: host-unit, host-integration
- Evidence (R1, FIND-006): unpacking release #85 peaked at 364 MB with 7-Zip and 540 MB with libarchive.
  7-Zip is the smaller, is in Debian's main archive, and needs no code of ours to read the format.

**SPEC-008 The listing and the search index are files of our own.** There is no database library and no use of
libsidplayfp's song length reader. HVSC's song length database is read by the app's own reader (SPEC-142)
into the listing, and a search is one pass over the index (SPEC-162). The MD5 function is the app's own, in
`core`.
- Covers: REQ-090, REQ-091, REQ-013
- Priority: Must
- Oracle: the MD5 function gives the RFC 1321 test vectors; SPEC-142's and SPEC-162's oracles.
- Level: host-unit
- Evidence (R6): 61,157 tunes come to under 12 MB of index, and one pass over text of that size is tens of
  milliseconds on the host. Reading the database ourselves keeps one fuzzed reader for a file that comes from
  the network, and lets absent tunes show their lengths.

**SPEC-009 Settings, state and playlists are text files.** Settings and state are INI-style text (SPEC-130,
SPEC-131). A playlist is an M3U8 file with extra comment lines (SPEC-170).
- Covers: REQ-070, REQ-107
- Priority: Must
- Oracle: files written by the app are valid UTF-8 text; a file written by hand from the description in
  SPEC-130 loads. Part `playlists`: the same for SPEC-170's playlist files (FIND-022).
- Level: host-unit

**SPEC-010 Fonts are built into the program.** Text is drawn with bitmap fonts generated ahead of time from
DejaVu Sans at 12 and 14 pixels. They cover U+0020 to U+007E, U+00A0 to U+00FF, the 27 characters that
Windows-1252 has at 0x80 to 0x9F (such as Š, š, Ž, ž, Œ, œ, €, curly quotes, dashes, … and ™), and the
symbols the screens use. The generated font sources are in the repository. The program loads no font files
when it runs.
- Covers: REQ-031, REQ-160
- Priority: Must
- Oracle: a C++ test program of the simulator level (SPEC-253) asks LVGL for the glyph of each of the 218
  printable Windows-1252 characters, in both fonts, and finds one for each; the fixture tunes whose fields
  between them hold all of those characters (SPEC-252) give screen descriptions with the same characters as
  Unicode; two runs of a script on different machines give the same pixels.
- Level: simulator
- Evidence: DejaVu Sans is the font the platform recommends for western text, at 12 pixels or more (CON-18).
  Drawing it from built-in bitmaps removes the font library and its version from the picture.

**SPEC-011 Licences.** The app's own files are GPL-3.0-or-later, each carrying an SPDX line. What is built in:
libsidplayfp and libresidfp (GPL-2.0-or-later, used here under GPL-3), LVGL (MIT), miniaudio (MIT-0), doctest
in the tests only (MIT), and fonts made from DejaVu Sans (its own free licence). The package installs
`LICENSE` and a third-party notices file naming each of these, with its licence text, in
`/usr/share/doc/sidplayzero/`.
- Covers: REQ-144
- Priority: Must
- Oracle: a check finds an SPDX line in every source file of ours, finds each built-in component in the
  notices file, and finds no file under a "GPL version 2 only" notice among the sources compiled.
- Level: host-package

## 3. Structure

**SPEC-020 Three layers.** The source is in three parts, and the build enforces which may use which:

| Part | Holds | May use |
|---|---|---|
| `core` | The collection, the listing and song lengths, MD5, the player's state machine, the queue, the software sound backend's buffer and render loop, playlists and their import, settings, the index and search, the download logic, the key map, the message table | The C++ standard library only |
| `platform` | The real engine (libsidplayfp), sound output (miniaudio), HTTP (libcurl), the `7z` runner, system volume, the backlight, the keyboard and the display | `core`, and the libraries named |
| `ui` | Screens, drawing | `core`, LVGL |

`core` is built as a library with no include path to LVGL, libsidplayfp, miniaudio or libcurl.
- Covers: REQ-110, REQ-160
- Priority: Must
- Oracle: the host-unit tests link `core` and the fakes and nothing else; a test adds an `#include` of an
  LVGL header to a `core` file in a scratch copy and sees the build fail.
- Level: host-unit

**SPEC-021 Seams.** `core` reaches the outside world only through these interfaces. Each has a real
implementation in `platform` and a fake for the tests.

| Interface | What it does | Fake used by tests |
|---|---|---|
| `Engine` | Loads a tune from bytes with a subtune and settings, renders samples, mutes voices, switches filters, reads the SID registers | Produces a known ramp of samples, counts calls, can be made slow or made to refuse |
| `AudioSink` | Opens a stream at a rate, pulls samples through a callback, reports loss of the device | Pulls on demand under the test's control, records what it got, can refuse to open or drop the device |
| `SoundBackend` | SPEC-025 | A recorder that reports song ends when told |
| `Clock` | Monotonic time, timers and sleeping | Time that moves only when the test says |
| `Files` | Reads, writes, appends, maps a file read-only, lists, renames, syncs, reports free space, the file system's block size and modification times to the nanosecond (FIND-019) | A folder of the test's own, with delays and failures that can be injected at any operation |
| `HttpClient` | Fetches a URL to a file, with progress, range and cancel; reports the kind of failure | A table of URLs with canned answers and failures, and a log of requests |
| `Unpacker` | Says whether it is available; lists and unpacks an archive into a folder; can be stopped | A scripted stand-in |
| `SystemVolume` | Reads and changes the system volume and mute | A recorder holding a level |
| `Backlight` | Says whether it can be changed; reads and sets the level | A recorder holding a level |
| `Memory` | Reports available memory and free swap | Numbers set by the test |

- Covers: REQ-110, REQ-160
- Priority: Must
- Oracle: every host-unit test runs with fakes only, and a counter in each fake proves the code under test
  went through the interface.
- Level: host-unit

**SPEC-022 Threads.** The app's own code runs on six threads, and each kind of work has one home. Libraries
may start threads of their own; none of the app's code runs on them except the audio callback.

| Thread | Does | Must never |
|---|---|---|
| UI | Runs LVGL, handles keys, owns the player's state machine and all screen objects | Call anything that can take more than 50 ms‡: no file, network, engine or child-process call |
| Render | Runs the engine and fills the ring buffer (SPEC-070) | Touch LVGL or files |
| Audio | miniaudio's callback: copies from the ring buffer to the device | Lock, allocate or wait |
| Quick worker | Folder listing, reading a tune, reading headers for the detail line, saving state and settings, volume, backlight | Run a job that can take more than 2 s‡ |
| Fetch worker | Downloads single tunes: the tune to be played, the one fetched ahead, and a playlist's (SPEC-141, SPEC-174) | Do anything else |
| Long worker | Setting HVSC up, the complete download and unpacking, building the index and the listing, the speed measurement | Touch LVGL |

Threads talk through queues of messages; a result reaches the screens as a message handled on the UI thread.
Each worker runs one job at a time, in the order asked, except that a job can be cancelled while it waits or
runs, and that on the fetch worker a tune that is to be played goes ahead of everything waiting there. So a
tune is fetched at once whatever the long worker is busy with. The audio callback uses only a lock-free ring
buffer and atomic counters.
- Covers: REQ-017, REQ-054
- Priority: Must
- Oracle: with a fake `HttpClient` that stalls a download for 30 s, the simulator opens a folder within
  300 ms‡ and a volume key reaches the fake `SystemVolume` within 100 ms‡ of the fake clock; with the index
  build held by a slow `Files` fake, Enter on an absent tune makes its request at once, and so it does, part
  `releases`, while a release update's download is stalled; with a `Files` fake that takes 2 s to list a
  folder, Tab, Down and ESC are still answered within 100 ms‡ (FIND-020); the integration level's
  ThreadSanitizer run of the simulator tests gives no report; a test build makes the audio callback abort
  if it allocates or takes a lock, and the integration tests run without that abort.
- Level: simulator, host-integration

**SPEC-023 Names.** The package and the program are `sidplayzero`. The launcher shows "SidPlayZero". The same
string names the folders under the XDG directories (SPEC-134).
- Covers: REQ-140
- Priority: Must
- Oracle: the built package's control field `Package`, the installed program's name and the `.desktop`
  entry's `Name` have these values.
- Level: host-package

**SPEC-025 The sound backend.** The player makes sound only through `SoundBackend`. Its operations:

| Operation | Meaning |
|---|---|
| `load(bytes, subtune, settings)` | Prepare a subtune; fails with a reason. `settings` holds the engine, sample rate, chip model, clock and ROMs |
| `start()`, `pause()`, `resume()`, `stop()` | Begin output at the first sample; stop and continue without losing or repeating a sample; unload |
| `set_length(samples)` | Report "song ended" after exactly this many samples of the subtune; 0 means never |
| `position()` | Samples of the subtune produced so far |
| `set_muted(chip, voice, on)`, `set_filter(chip, on)` | Sound controls |
| `set_skip(on)` | Skip ahead (SPEC-066) |
| `registers(chip)` | The chip's last-written registers, for SPEC-080 |
| `can()` | Which of mute, filter, skip and registers this backend supports |
| `underruns()` | Frames the backend could not deliver in time (SPEC-070) |

And its events, delivered on the UI thread: `song_ended`; `device_lost`, `device_back` and `device_gone`
(SPEC-078); `failed(reason)`. The fake `SoundBackend` records the operations, reports whatever events the
test tells it to, and returns the registers the test gives it; its `position()` moves on with the fake
clock, at the sample rate, while it is started and not paused.
The **software backend**, in `core`, is made of an `Engine`, the ring buffer and an `AudioSink`. No file of
the collection, the queue, playlists, search or the screens names the software backend.
- Covers: REQ-110, REQ-010
- Priority: Must
- Oracle: every host-unit test of the player (SPEC-060 to SPEC-069) runs against the fake `SoundBackend`;
  the software backend's own tests (SPEC-064, SPEC-070, SPEC-078) run it against the fake `Engine` and
  `AudioSink`; a check finds no reference to the software backend's type outside its own files and the one
  place that creates it.
- Level: host-unit

## 4. The collection and the browser

**SPEC-040 Finding the collection.** At start, and whenever the setting changes, the collection root is the
first of these that is set: the environment variable `HVSC_BASE`, the setting `collection`, `~/C64Music`. A
leading `~` is replaced by the home folder. The root does not have to exist. When `HVSC_BASE` is set, the
Settings row for the collection shows its value with "(from HVSC_BASE)" and cannot be changed.
An empty `HVSC_BASE` counts as unset, and a `~` at the start of the variable is replaced as it is in the
setting (FIND-024).
- Covers: REQ-001
- Priority: Must
- Oracle: three cases (variable set; variable unset and setting set; neither) each give the expected root;
  changing the setting while the program runs with `HVSC_BASE` unset makes the browser show the new root at
  once; with the
  variable set, Enter on the row changes nothing.
- Level: host-unit, simulator

**SPEC-041 What a folder shows.** A folder's entries are the union of two sources: the folder as it is on the
device, and, when the root has a listing (SPEC-142), the tunes and folders HVSC lists there. A tune that both
name appears once. Only folders, and files whose names end in `.sid` in any case, are entries. Names starting
with a dot are left out, and so is the root's `DOCUMENTS` folder. A name that is not valid UTF-8 is shown with
"?" for each bad byte and still opens and plays; its path is not a collection path (SPEC-154), so "Fav" and
"Add" have no label on it and it is not remembered between runs. Each tune is **present** or **absent**. A folder that cannot be read
shows no entries and the notice `MSG_FOLDER_UNREADABLE`.
- Covers: REQ-002, REQ-006, REQ-088
- Priority: Must
- Oracle: for a test root holding three `.sid` files, a `.txt` file, a hidden file and a `DOCUMENTS` folder,
  and a listing naming two of those tunes and two others, the folder has five tune entries, three present
  and two absent, and no `DOCUMENTS`.
- Level: host-unit

**SPEC-042 Order.** Folders come before tunes. Within each group the order is by name, compared without regard
to the case of ASCII letters, with ties broken by plain byte order. Letters are compared as lower case, so
`_ [ ] ^` and the backtick come before letters: `Ace_2.sid` before `Aces.sid` (FIND-018). The listing, the
index and search results use this same order.
- Covers: REQ-002
- Priority: Must
- Oracle: the entries of a test folder come out in an order written down by hand in the test.
- Level: host-unit

**SPEC-043 Folders are listed off the UI thread and drawn a screenful at a time.** Opening a folder asks the
quick worker for its entries and shows them when they arrive; if that takes more than 150 ms‡ the list area
says "Reading…" meanwhile. The last 16‡ folders listed stay in memory, each with its folder's modification
time; a folder whose time has changed, or that has gained or lost a present tune through the app, is listed
again when opened. The list draws only the rows that are on the screen, whatever the number of entries. An
empty folder shows "(empty)" and the position "0/0".
- Covers: REQ-003
- Priority: Must
- Oracle: in host-unit, a folder of 1,298 entries is listed and the model returns any row in constant time;
  in the simulator the screen description's `list.objects` is the same for 6 entries and for 1,298; a file
  added to a cached folder by the test appears when the folder is opened again; on the device the times of
  REQ-003 hold for the two folders it names and `peak_memory_kb` stays under 64 MB‡.
- Level: host-unit, simulator, device

**SPEC-044 Opening, going back, and where the highlight lands.** Enter or Right on a folder opens it with the
first entry highlighted, or the entry that was highlighted the last time this folder was open in this run.
Left, or a short ESC, goes to the parent with the folder just left highlighted. At the collection root Left
and a short ESC do nothing.
- Covers: REQ-002
- Priority: Must
- Oracle: a simulator script opens a folder, moves down two rows, goes back and opens it again: the screen
  description has the third row highlighted; after going back, the folder's own row is highlighted.
- Level: simulator

**SPEC-045 Moving in a list.** Up and Down move one row and stop at the ends. Page Up and Page Down move by the
number of rows on the screen. Home and End go to the first and last entry. The app makes its own key repeat
and ignores the keyboard's. Repeating belongs to the action, not the key: moving in a list (`up`, `down`,
`page_up`, `page_down`), moving the cursor and deleting in a text field, and stepping a value with Left or
Right repeat while the key is held: the first repeat comes 440 ms‡ after the press, and then one every 40 ms
(25 a second) (FIND-017). No other action repeats, so Up and
Down on the now-playing screen, where they step through tunes, act once for each press. The top line shows the
highlight's position as "n/total".
- Covers: REQ-005, REQ-003
- Priority: Must
- Oracle: in a 1,298-entry test folder each key lands on the expected row and the screen description's
  `position` reads the expected text; holding Down for one second of the fake clock moves 16 rows; holding
  Enter for one second acts once, and so does holding Down on the now-playing screen.
- Level: simulator

**SPEC-046 The detail line.** Below the list, one line describes the highlighted entry once the highlight has
rested on it for 150 ms‡. For a present tune: title, author and the length of its default subtune, which the
quick worker reads from the file: the header, and the file's MD5 for the length (SPEC-063). For an absent tune, whose header is not known: "not on device", the
number of subtunes and the length of the first, from the listing. For a folder: nothing.
Times are cut down to the second, never rounded: 0:03.500 shows as 0:03, as 3:55.594 shows as 3:55 (FIND-024).
- Covers: REQ-004
- Priority: Should
- Oracle: highlighting the fixture tune "Commando Test" gives a screen description whose `detail` holds its
  title, author and length within 200 ms‡ of the fake clock; an absent tune's holds "not on device", the
  count and the first length. On the device, with HVSC set up, highlighting
  `/MUSICIANS/H/Hubbard_Rob/Commando.sid` once it is present shows "Commando", "Rob Hubbard" and 3:55.
- Level: simulator, device

**SPEC-047 A collection that is not HVSC.** With no listing in the root, the browser shows the folder tree as
it is on the device, every tune is present, and every length is the default of SPEC-063.
- Covers: REQ-006
- Priority: Must
- Oracle: a root with three fixture tunes and no `DOCUMENTS` folder lists three present tunes, and each plays
  for the default length.
- Level: host-unit, simulator

**SPEC-048 Nothing there yet.** When the root does not exist, or holds no entry, the browser's place is taken
by the **empty screen**: it says no collection was found, names the root, and offers "Get HVSC" on Enter,
which starts SPEC-140, and a line telling the user to copy a collection into that folder. While this screen
shows, the quick worker looks at the root every 5 s‡, and the browser replaces the screen as soon as the root
has an entry and no panel or help is open.
A root that holds only HVSC's documents holds entries, its absent tunes; a folder that holds only files that
are not entries shows "(empty)" (FIND-024).
- Covers: REQ-007
- Priority: Must
- Oracle: started with an empty home folder, the screen description says `screen = empty`, holds the root's
  path in `text`, and has the one row "Get HVSC"; Enter opens the notice panel; a tune that the script puts
  into the root makes the browser appear within 5 s‡ of the fake clock, and with help open, when help is
  closed.
- Level: simulator

**SPEC-049 How absent tunes look.** An absent tune's row is drawn in the dimmed text colour with a small cloud
mark at its right edge. While it is being fetched the mark is a progress mark.
- Covers: REQ-088
- Priority: Must
- Oracle: the screen description of a folder with one present and one absent tune gives the second row the
  mark `absent`, and `fetching` while the fake `HttpClient` holds its request.
- Level: simulator

## 5. Playing

**SPEC-060 The player's states.** The player is one state machine on the UI thread. It always has a **current
entry**, or none; the current entry stays after playback stops, so that it can be played again.

| State | Meaning |
|---|---|
| Idle | Nothing is loaded. There may be a current entry |
| Waiting | The current entry is absent and was reached by stepping; the app waits before fetching (SPEC-141) |
| Fetching | The current entry's file is being downloaded |
| Loading | The file is being read, checked (SPEC-203) and given to the backend |
| Playing | The backend is producing sound |
| Paused | The backend is loaded and stopped at a known sample |
| Failed | The current entry could not be fetched or loaded; a message says why |

What each event does in each state. "—" means the event is ignored there. "→ S" means go to S.

| Event | Idle | Waiting | Fetching | Loading | Playing | Paused | Failed |
|---|---|---|---|---|---|---|---|
| Play an entry (Enter on a tune) | set queue; → Fetching if absent, else Loading | same, replacing the wait | same, cancelling the fetch if it is for another tune | same | same, after `stop()` | same, after `stop()` | same |
| Play/Pause key | play the current entry from its start, if there is one | — | — | — | `pause()`; → Paused | `resume()`; → Playing | play the current entry again |
| Stop | — | → Idle | cancel the fetch; → Idle | → Idle | `stop()`; → Idle | `stop()`; → Idle | → Idle |
| Next or previous tune, next or previous subtune | by SPEC-061 and SPEC-062, if there is a queue | same, restarting the wait | same, cancelling the fetch | same | same, after `stop()` | same, after `stop()` | same |
| Restart (part `restart`) | play the current entry | — | — | — | load the same subtune again | load the same subtune again; → Playing | play the current entry again |
| Wait is over | — | → Fetching | — | — | — | — | — |
| Fetched | — | — | → Loading | — | — | — | — |
| Could not fetch | — | — | → Failed (SPEC-069) | — | — | — | — |
| Loaded | — | — | — | `start()`; → Playing | — | — | — |
| Could not load | — | — | — | → Failed (SPEC-069) | — | — | — |
| `song_ended` | — | — | — | — | the next entry by SPEC-065; if its fetch-ahead is still running, → Fetching, taking that request over; if its fetch-ahead failed, → Failed with that failure | — | — |
| Fetch-ahead done, or failed (SPEC-141) | — | — | — | — | remembered for the next entry | remembered | — |
| `device_lost`, `device_back` | — | — | — | — | stays Playing (SPEC-078) | — | — |
| `device_gone` | — | — | — | — | `pause()`; → Paused, with the notice `MSG_NO_SOUND` | — | — |
| `failed` from the backend | — | — | — | → Failed | → Failed | → Failed | — |
| The unattended wait of SPEC-069 is over | — | — | — | — | — | — | the next entry; or → Idle with `MSG_STOPPED_BAD_FILES` after five bad files in a row |
| Too slow (part `too-slow`) | — | — | — | — | by SPEC-079 | — | — |
| The L key, while SPEC-079's offer shows (part `too-slow`) | — | — | — | — | load the same subtune again with the light engine | — | — |

A step to an entry that is present goes to Loading; to one that is absent, to Waiting. In Failed, any of
this table's events that a key makes takes the place of the unattended wait; keys that do not touch the
player, such as moving in the browser, leave it running. While the complete download has playing switched off (SPEC-144), every event that
would load a tune is answered with the notice `MSG_UNPACKING` and does nothing else.
- Covers: REQ-010, REQ-019
- Priority: Must
- Oracle: a host-unit test sends every event in every state against the fake `SoundBackend`, `HttpClient`
  and `Clock`, and checks the state and the calls made after each, cell by cell of this table (the rows of
  a part that is not built being left out); after pause and resume the software backend's test (SPEC-070)
  shows the samples complete and in order.
- Level: host-unit

**SPEC-061 Subtunes.** An entry without a subtune number plays the tune's default subtune, as the header gives
it. "Next subtune" and "previous subtune" move by one within the tune. Next from the last subtune does "next
tune" (SPEC-062), and previous from the first does "previous tune". A subtune number that the tune does not
have is treated as the default.
- Covers: REQ-011
- Priority: Must
- Oracle: for the three-subtune fixture with default 2: play starts at 2, next gives 3, next again starts the
  following entry; previous from 2 gives 1, previous again starts the preceding entry.
- Level: host-unit

**SPEC-062 The queue.** The queue is a list of entries, copied at the moment a tune is played and not changed
afterwards by anything done to its source:

| Played from | The queue is | Its entries have subtune numbers |
|---|---|---|
| The browser | That folder's tunes, in the order shown | No |
| Search results | The results, in the order shown | No |
| A playlist | The playlist's entries, in order | As the playlist has them |

"Next tune" and "previous tune" move one place and play that entry. At the last entry, next stops playback
(→ Idle) unless repeat-all is on, when it goes to the first; previous at the first entry plays it again.
Browsing, searching or editing a playlist does not change the queue.
- Covers: REQ-012, REQ-017
- Priority: Must
- Oracle: in a three-tune folder, next from the second plays the third and next again leaves the player
  Idle; a new search made while the results of an earlier one play leaves the queue as it was. Part
  `playlists`: started from a playlist whose order differs from the folder's, next follows the playlist;
  deleting that playlist while it plays leaves the queue as it was.
- Level: host-unit

**SPEC-063 Song lengths.** A subtune's length comes from the listing (SPEC-142). For a present tune it is
looked up by the MD5 of the file, which the app computes whenever it reads the file; the path plays no part,
so a file with other contents at an HVSC path does not get that path's lengths. For an absent tune, which
has no file yet, it is looked up by the path. When the listing does not have it, or there is no listing, the
length is the setting `default_length`, which starts as 3:30, can be 0:05 to 59:59, and where 0 means "until
told otherwise". Lengths are held in milliseconds.
- Covers: REQ-013
- Priority: Must
- Oracle: with a database written for the fixtures, each fixture subtune reports the length the database
  gives, to the millisecond; a tune copied to another path is still found by its MD5; a different tune put
  at a listed path reports the default, not that path's lengths; a tune left out reports the default. On the device, with HVSC #85 set up, `/MUSICIANS/H/Hubbard_Rob/Commando.sid` reports 3:55.594
  for subtune 1 and 1:01.288 for subtune 2.
- Level: host-unit, device

**SPEC-064 When a song has ended.** The player gives the backend the subtune's length as a number of samples:
the length in milliseconds times the sample rate, divided by 1,000 and rounded down. The software backend
renders exactly that many samples for the subtune, lets the sink play all of them, and then reports
`song_ended`. The elapsed time shown is `position()` divided by the rate.
- Covers: REQ-014
- Priority: Must
- Oracle: with a 2.000-second length at 48,000 Hz the fake sink receives exactly 96,000 frames for that
  subtune, and `song_ended` comes after the last of them.
- Level: host-unit

**SPEC-065 What happens next.** The setting `when_song_ends` decides what follows `song_ended`:

| Value | Next |
|---|---|
| `subtunes` (default) | If the entry has no subtune number: the tune's next subtune, and after the last, the next entry. If it has one: the next entry |
| `next_tune` | The next entry |
| `repeat` | The same subtune again |
| `stop` | Nothing: → Idle |

"The next entry" at the end of the queue is → Idle, unless the setting `repeat_all` is on, when it is the
first entry. The values other than `subtunes`, and `repeat_all`, are the part `modes`; `shuffle` is the part
`shuffle` (SPEC-115).

With `shuffle` on, the queue is played in a random order, made in exactly this way so that a test can know
it:

- The generator is xorshift64*. Its state *x* is a 64-bit number, started from the seed, or from
  0x9E3779B97F4A7C15 when the seed is 0. Each number is made by `x ^= x >> 12; x ^= x << 25; x ^= x >> 27`,
  keeping 64 bits, and is *x* times 0x2545F4914F6CDD1D, again keeping 64 bits. With seed 1 the first number
  is 0x47E4CE4B896CDD1D.
- A list *a* of *m* entries is permuted by going from *i* = *m* − 1 down to 1, taking the next number modulo
  *i* + 1 as *j*, and swapping *a[i]* with *a[j]*.
- When a queue is set, the chosen entry is taken out, the others are permuted in their queue order, and the
  chosen entry is put first. Each time repeat-all goes round, all the entries are permuted from their
  original order; the generator carries on from where it was.
- Switching `shuffle` on while a queue plays permutes the entries after the current one. Switching it off
  puts the queue back in its original order, with the place at the current entry.
- The seed is the clock's value at start, unless the test option `--seed N` gives it (SPEC-253).
- Covers: REQ-014, REQ-016
- Priority: Must
- Oracle: with 2-second test lengths and the fake clock: `subtunes` on a folder queue of the three-subtune
  fixture (default 2) and a one-subtune tune plays subtunes 2 and 3 and then the second tune, and ends in
  Idle. Part `playlists`: on a playlist whose entry names subtune 1 it plays that subtune and then the next
  entry. Part `modes`: `next_tune`, `repeat` and `stop` each give the sequence the table says, and with `repeat_all` the
  last entry is followed by the first. Part `shuffle`: with seed 1 and a queue of ten entries numbered 1 to
  10 in order, started at entry 3, the order played is 3, 4, 8, 1, 10, 9, 5, 6, 7, 2, and with repeat-all
  the second round is 9, 8, 1, 2, 4, 10, 3, 7, 6, 5.
- Level: host-unit

**SPEC-066 Starting again and skipping ahead.** "Restart" loads the current subtune again from its beginning.
"Skip ahead" (the part `skip`) is on while its key is held: the software backend then renders as fast as the engine
allows, up to eight‡ times real time, and plays every eighth chunk, so `position()` and the elapsed time run
ahead. Underruns are not counted while skipping, nor for one second after.
- Covers: REQ-015
- Priority: Should
- Oracle: restart sets `position()` to 0 and the sink then receives the tune's first samples. Part `skip`:
  with the fake engine, holding skip for one second of the fake clock advances `position()` by at least
  four seconds‡ of samples and adds nothing to `underruns()`.
- Level: host-unit

**SPEC-067 How the engine is set for a tune.** For each tune the engine is configured with: the sample rate of
SPEC-073; the SID model and the clock from the tune's header, or the library's defaults (6581, PAL) when the
header says "unknown" or "any", unless the settings force one (SPEC-077); the engine of SPEC-072; the
library's default resampling ("resample and interpolate"); and a power-on delay fixed at 0.
- Covers: REQ-163, REQ-046
- Priority: Must
- Oracle: SPEC-071.
- Level: host-integration

**SPEC-068 ROMs.** The settings `rom_kernal`, `rom_basic` and `rom_chargen` name ROM files. Each is used if it
exists and has the right size (8,192, 8,192 and 4,096 bytes), and is otherwise ignored with the notice
`MSG_ROM_IGNORED` at start. A tune whose header says it needs BASIC is not loaded without a BASIC ROM: the
load fails with `MSG_NEEDS_BASIC`.
- Covers: REQ-018
- Priority: Must
- Oracle: the fixture tune marked as BASIC fails with that message when no ROM is set. Part `roms`: it is
  handed to the backend when a ROM file of the right size is set, and a ROM file of the wrong size gives the
  notice.
- Level: host-unit

**SPEC-069 When an entry cannot be played.** Each kind of failure has a message and a rule. "Unattended" means
the entry was reached by a song ending, not by a key.

| Failure | Message | Counts as a bad file | Unattended |
|---|---|---|---|
| The file fails SPEC-203's check | `MSG_INVALID_SID` | Yes | Next entry after 2 s‡ |
| The engine refuses it | `MSG_ENGINE_REFUSED` | Yes | Next entry after 2 s‡ |
| It needs a ROM that is not set | `MSG_NEEDS_BASIC` | Yes | Next entry after 2 s‡ |
| Fetch: the server has no such file (404 or 410) | `MSG_FETCH_NOT_FOUND` | Yes | Next entry after 2 s‡ |
| Fetch: the file does not match HVSC's record | `MSG_FETCH_MISMATCH` | Yes | Next entry after 2 s‡ |
| Fetch: no connection, timeout, certificate refused, or a server error | `MSG_FETCH_NETWORK` | No | Next present entry after 2 s‡ (SPEC-143) |
| Fetch: too large, the card is full, or the file cannot be written | `MSG_FETCH_TOO_LARGE`, `MSG_CARD_FULL`, `MSG_CANNOT_WRITE` | No | Next present entry after 2 s‡ (SPEC-143) |
| An absent entry that may not be requested: within SPEC-143's 60 s‡, or with HVSC's notice not accepted (SPEC-152) | None; in the second case the notice `MSG_NOTICE_NEEDED` | No | Passed over at once |
| A playlist entry is missing (part `playlists`) | `MSG_ENTRY_MISSING` | No | Passed over at once, and the message is not shown |
| Sound cannot be opened | `MSG_NO_SOUND` | No | Stays Failed |

In every case but the two that are passed over, the player is in Failed, within 1 s‡ of the failure being
known, and the now-playing screen and the mini player show the message. Attended, it stays there; the two
that are passed over cannot be met attended, except the missing entry, which then shows its message. After five‡ bad files in a row, unattended
playback stops: → Idle with the notice `MSG_STOPPED_BAD_FILES`. An entry that plays resets the count.
- Covers: REQ-019
- Priority: Must
- Oracle: a queue of a good tune, five truncated fixtures and another good one, started at the first with a
  2-second length, plays the first and ends Idle with that notice after the fifth bad file; a queue of good,
  bad, good plays both good ones; a bad file chosen with Enter leaves the player Failed and does not advance;
  each row of the table that has a message gives its identifier in the screen description (the missing
  entry's with the part `playlists`).
- Level: host-unit, simulator

**SPEC-070 From the engine to the loudspeaker.** In the software backend, the render loop asks the engine for
about 5 ms of samples at a time and writes them, as 16-bit stereo, into a ring buffer of 8,192‡ frames. It
keeps the buffer at least half full and waits when it is full. The audio callback copies what it needs from
the buffer; when the buffer has less, it plays what there is and fills the rest with silence. Output for a
subtune starts when 2,048‡ frames are in the buffer. The **underrun counter** adds the frames filled with
silence only while the backend is playing, after output for the subtune has started, before its last sample,
and not during or within one second after skipping. On `stop()`, a new `load()` and restart, what is in the
buffer is thrown away and the sink goes silent within one callback. On `pause()` the buffer keeps its
contents. At a song's end every sample in the buffer is played. In tests the render loop is a function the
test calls; on the device it runs on the render thread.
- Covers: REQ-020, REQ-050, REQ-017
- Priority: Must
- Oracle: with the fake engine and sink, the frames received equal the frames produced, in order, across a
  pause and resume; with the engine made slower than real time the counter rises by exactly the frames not
  delivered, and does not rise during the start of a subtune, a pause, a stop or the end of a song; a
  simulator script plays a fixture with the real engine and a recording sink while it opens three folders
  and the settings screen, saves the samples, then plays the same fixture again for the same time and
  presses nothing: the counter stays 0 and the two saved recordings are equal (`expect-same`).
  On the device `key_to_sound_ms_max` is at most 500 ms for present tunes.
- Level: host-unit, simulator, device

**SPEC-071 The same tune renders the same.** With the settings of SPEC-067, rendering a given subtune twice
gives the same samples. For the fixture tunes and both engines, the first ten seconds rendered by the app
equal, value for value, the first ten seconds of a WAV written at test time by the `sidplayfp` program,
version 3.2.3, built by the test framework from its release archive (SHA-256
`083a21a80e5da57a0cef15451f326ea3dbb4c28e0b453daefad0814070a1a440`) against the same two libraries, and run
with an empty home folder as:

```
sidplayfp --delay=0 -s -rr -f48000 -t10 --residfp -wOUT.wav TUNE.sid
```

with `--sidlite` for the light engine, `-f44100` for the other rate, and `-oN` for subtune N.
- Covers: REQ-163, REQ-056
- Priority: Must
- Oracle: one test renders a fixture twice and compares the two; another makes the WAV and compares the app's
  frames with it, for each fixture tune, engine and rate. Nothing is compared with stored sample values, so
  the result does not depend on the compiler or the processor.
- Level: host-integration

**SPEC-072 Choosing the engine automatically.** When the app first runs, and again whenever its version or the
setting `sample_rate` has changed, the long worker measures the two engines once the browser has been showing
for 3 s: for each engine it renders the built-in one-SID fixture tune for 0.5 s without timing, then three
runs of 1 s each, timed through the `Clock` seam (the real clock on the device), and keeps the slowest run.
The two **speed factors** (seconds of sound per second taken) are stored in the cache folder as `speed.ini`,
two lines `accurate = F` and `light = F`, with the version and `sample_rate` they were measured for. With `engine = auto`, a tune that uses *n* SID chips
is played with reSIDfp when its factor divided by *n* is at least 2.5, and with sidlite otherwise. Until the
measurement is done, `auto` means sidlite.
- Covers: REQ-050, REQ-051
- Priority: Must
- Oracle: with stored factors:

  | Accurate factor | 1 SID | 2 SID | 3 SID |
  |---|---|---|---|
  | 8.0 | reSIDfp | reSIDfp | reSIDfp |
  | 5.5 | reSIDfp | reSIDfp | sidlite |
  | 2.6 | reSIDfp | sidlite | sidlite |
  | 2.4 | sidlite | sidlite | sidlite |

  On the device the results file holds both factors, the engine used for each benchmark tune and each tune's
  `render_load_percent`; the benchmark set plays for 30 minutes with `underrun_frames` at 0 and
  `cpu_percent` at most 60 %.
- Level: host-unit, device

**SPEC-073 Sample rate.** The setting `sample_rate` is 44100 or 48000, and starts as 48000. The engine renders
at this rate and the stream is opened at this rate; if the system runs at another, the system converts. A
change takes effect when the next tune is loaded.
- Covers: REQ-052
- Priority: Should
- Oracle: with each value, the fake sink is opened at that rate and receives that many frames per second of
  song.
- Level: host-unit

**SPEC-074 Muting voices and switching the filter.** Each of the three voices of each SID chip in use can be
switched off and on, and so can each chip's filter, while a tune plays. The change takes effect within one
render chunk. The choices hold for the tune: across its subtunes and a restart. Loading another tune puts
everything on again. The now-playing screen shows them (SPEC-096).
- Covers: REQ-040, REQ-041
- Priority: Should
- Oracle: with voice 1 muted from the first sample, ten seconds of a fixture equal the `sidplayfp` program's
  WAV made with `-u1` added to SPEC-071's command; with the filter off, the WAV made with `-nf`; after next
  subtune the mute is still on, and after the next tune nothing is muted.
- Level: host-integration, host-unit

**SPEC-075 Tunes for more than one SID chip.** A tune's header says how many chips it uses and at which
addresses, and the engine is given as many. The library's mixer is set for stereo, which places two chips
left and right and three left, centre and right; a one-chip tune sounds the same in both channels.
- Covers: REQ-045
- Priority: Should
- Oracle: ten seconds of the two-SID and three-SID fixtures equal the `sidplayfp` program's WAVs of them
  (SPEC-071's command); the screen description of the now-playing screen gives `chips` as 2 and 3.
- Level: host-integration, simulator

**SPEC-076 Volume.** Volume up and volume down read the system volume through `SystemVolume` and set it to the
nearest multiple of 5 % above or below, never above 100 % or below 0 %. Mute toggles the system's mute. The
notice `MSG_VOLUME` shows the new level, or "Muted". The real `SystemVolume` runs, on the quick worker and
without a shell, `pactl get-sink-volume @DEFAULT_SINK@`, `pactl set-sink-volume @DEFAULT_SINK@ N%`,
`pactl get-sink-mute @DEFAULT_SINK@` and `pactl set-sink-mute @DEFAULT_SINK@ toggle`. If `pactl` is missing or
fails, the notice is `MSG_VOLUME_FAILED`.
- Covers: REQ-042, REQ-127
- Priority: Must
- Oracle: from 42 %, up asks the fake for 45 % and down for 40 %; at 100 % up asks for nothing; the notice
  is in the screen description; with a `pactl` stand-in on the path, the real `SystemVolume` runs exactly
  those argument lists. On the device the level heard changes and the launcher shows the same level.
- Level: host-unit, simulator, host-integration, device

**SPEC-077 Forcing the chip and the clock.** The settings `sid_model` (`auto`, `6581`, `8580`) and `clock`
(`auto`, `pal`, `ntsc`) start as `auto`, which follows each tune's header. Another value forces it for every
tune.
- Covers: REQ-046
- Priority: Could
- Oracle: with `sid_model = 8580`, ten seconds of a fixture marked 6581 equal the `sidplayfp` program's WAV
  made with `-mnf` added; with `clock = ntsc`, the WAV made with `-vnf` added.
- Level: host-integration

**SPEC-078 When sound cannot be opened, or goes away.** If the sink cannot be opened when a tune is to play,
the load fails with `MSG_NO_SOUND`; the rest of the app keeps working, and the next attempt to play tries
again. If the sink reports that the device was lost or changed while playing, the software backend reports
`device_lost`, closes the stream and opens it again, up to five times at intervals of 200 ms‡; rendering
waits meanwhile, so no part of the tune is skipped, and no underruns are counted. When it succeeds it reports
`device_back` and the player stays Playing. If it does not, it reports `device_gone`, and the player pauses
and raises the notice `MSG_NO_SOUND`; Play/Pause then tries to open the sink again.
- Covers: REQ-044, REQ-043
- Priority: Must
- Oracle: with the fake sink refusing to open, play ends in Failed with that message and the browser still
  answers keys; when it accepts, play works; with the fake sink dropping the device for 300 ms mid-tune, the
  frames received are complete and in order and the player is Playing; dropping it for good leaves the
  player Paused with that notice, and Play/Pause after the fake accepts again carries on from the same
  sample. On the device, plugging and unplugging headphones twice leaves the tune playing with a gap
  under 1 s‡.
- Level: host-unit, simulator, device

**SPEC-079 When it cannot keep up.** If the underrun counter rises in three‡ separate seconds within ten
seconds, the player acts, once for each tune loaded:

| `engine` | What happens |
|---|---|
| `auto`, playing with reSIDfp | The subtune is loaded again from its beginning with sidlite, and the notice `MSG_SWITCHED_LIGHT` says so. For the rest of this run, `auto` uses sidlite for tunes with that many chips or more |
| `accurate` | The notice `MSG_TOO_HEAVY` offers the light engine: pressing L while it shows, which is 10 s‡ from when it appears, loads the subtune again from its beginning with sidlite, for this tune only |
| Playing with sidlite already | The notice `MSG_CANNOT_KEEP_UP` |

- Covers: REQ-053
- Priority: Should
- Oracle: with the fake backend reporting underruns in three separate seconds: `auto` loads the subtune
  again with the light engine and shows the first notice within 10 s‡ of the fake clock; `accurate` shows
  the second, and L switches while it shows; once it has gone L does nothing; a second burst on the same
  tune does nothing.
- Level: host-unit, simulator

**SPEC-080 The voice display.** Thirty times a second at most, the app reads the chosen chip's registers from
the backend. For each of its three voices the now-playing screen draws a bar whose height in pixels is the
high byte of the voice's frequency register divided by 8, drawn bright when the voice's gate bit is set and
dim when it is not.
- Covers: REQ-034
- Priority: Could
- Oracle: with the fake backend returning frequency high bytes of 80, 160 and 240 and gates on, off, on, the
  screen description's `voices` gives heights 10, 20 and 30 and `lit` true, false, true; while paused, two
  screen descriptions a second apart are equal.
- Level: simulator

## 6. Screens and keys

**SPEC-090 The screens, and how one leads to another.** The screens are: the browser (or the empty screen of
SPEC-048), now playing, Search, Playlists, Playlist, Settings, HVSC and About; and, only while a device test
asks for it, the self-test screen (SPEC-260), which the test opens over the browser and closes by itself.
The open screens form a
**stack** with the browser at the bottom. A key that opens a screen puts it on top; if that screen is already
in the stack, the app returns to it and closes those above it. A short ESC closes the top screen; on the
browser it is the "parent folder" key. So no screen is ever in the stack twice, and the stack is never
deeper than six.

| Screen | Opened by |
|---|---|
| Now playing | P on any screen, while no text is being typed and no panel or help is open; key 4 in the browser; playing a tune from the browser, Search or a playlist |
| Search | Key 5 in the browser |
| Playlists | Key 6 in the browser |
| Playlist | Enter in Playlists |
| Settings | TAB on any screen, while no text is being typed and no panel or help is open. From HVSC and About that returns to Settings, by the rule above; in Settings itself TAB does nothing |
| HVSC | Key 5 in Settings; "Get HVSC" on the empty screen |
| About | Key 8 in Settings |

Help (SPEC-107) and panels (SPEC-114) open over a screen and are not part of the stack.
- Covers: REQ-060, REQ-062
- Priority: Must
- Oracle: a simulator script opens every screen by every way in this table and checks the screen
  description's `screen` and `stack` after each step; from the stack browser, Search, now playing, Settings,
  HVSC, four short presses of ESC reach the browser, and with a panel and help open as well, six do; TAB in
  HVSC and in About gives the stack that Settings had, and TAB in Settings changes nothing. With the
  `playlists` part (SPEC-115): from the deepest stack (browser, Playlists, Playlist, now playing, Settings,
  HVSC) five presses reach the browser.
- Level: simulator

**SPEC-091 The layout.** Every screen is 320×170 and has three bands:

| Band | Rows of pixels | Holds |
|---|---|---|
| Top line | 0 to 15 | Left: the screen's title, or in the browser the folder's path with its start cut off if too long, then the list position. Right: the mini player (SPEC-109) |
| Content | 16 to 135 | Lists have rows 20 pixels high: six rows, or five and the detail line. A notice (SPEC-113) covers the last row while it shows |
| Action bar | 136 to 169 | Five labels centred at x = 49, 105, 161, 217 and 273, each with a tick below it pointing at its key, for the keys 4, 5, 6, 7 and 8. While text is being typed it shows "Enter: OK" and "ESC: cancel" instead |

All text is the 12-pixel font, except the title on the now-playing screen, which is the 14-pixel font. The
theme is dark: light text on a near-black background, the highlight a filled bar.
- Covers: REQ-061, REQ-031
- Priority: Must
- Oracle: for every screen, the screen description's `boxes` give each action label a box centred on its x
  (within one pixel), no two boxes of one band overlap, and every box lies inside its band.
- Level: simulator

**SPEC-092 Keys that work everywhere.** The key map has three groups of keys that are not tied to one place:

| Group | Works | Keys |
|---|---|---|
| `always` | In every place, also while text is being typed | Play or pause, previous and next tune, the volume and brightness keys, help, the screenshot key, ESC and held ESC |
| `lists` | Wherever no text is being typed, panels and help included | Up, Down, Page Up, Page Down, Home, End |
| `screens` | On a screen, while no text is being typed and no panel or help is open | TAB, Space, P, L |

A panel takes the keys (SPEC-114), so a `screens` key pressed while a panel or help is open does nothing. A
place can switch a `screens` key off with the action `nothing`, as Settings does for TAB.
- Covers: REQ-061, REQ-060
- Priority: Must
- Oracle: a simulator script sends each `always` key that is built on every screen, in a panel and in the
  text prompt, each `lists` key on every screen and in a list panel, and each `screens` key on every screen,
  and checks its effect in the screen description; with a `confirm` panel open, TAB, Space, P and L leave
  the description unchanged; in the text prompt, P, L and Space are typed and do nothing else.
- Level: simulator

**SPEC-093 Arrows, lists and the action bar.** Where no text is being typed, the letters F, X, Z and C do
exactly what the Up, Down, Left and Right key codes do. Enter confirms. The keys 4 to 8 trigger the action
whose label is above them, and a key with no label does nothing. A letter or digit that the key map does not
name for the screen does nothing.
- Covers: REQ-061, REQ-005
- Priority: Must
- Oracle: on the browser, the letter X and the Down key code each move the highlight one row; 9 and Q change
  nothing in the screen description; each of 4 to 8 does what its label says.
- Level: simulator

**SPEC-094 The key map is one file.** [`keymap.tsv`](keymap.tsv) is part of this specification. It has a row
for every key in every screen and panel: where, key, action, the label shown in the action bar, the part it
belongs to if it is not always built (SPEC-115), and the words shown in help. The app's key handling, action bar and help are built from this file at build time,
and nothing else in the source maps keys to actions. The repository's check reads the file and fails if: a
place has two rows for one key; a key 4 to 8 has no label, or another key has one; a label is longer than
seven characters; a key name, a place or a part is not one this document has; a key is in two of the
groups of SPEC-092; a place has a row for a key of `always`; a place where no text is typed has a row for a
key of `lists`, other than Up and Down on the now-playing screen, which step through tunes there; or a
screen has a row for a key of `screens` with any action but `nothing`. The key step of SPEC-260 is the one
place where no group applies.
- Covers: REQ-063, REQ-061
- Priority: Must
- Oracle: the check passes on the file and fails on a copy with each of those faults; a host-unit test reads
  the same file and checks that the key map compiled into `core` has exactly its rows.
- Level: host-unit

**SPEC-095 The browser.** Up and Down move; Enter or Right opens a folder or plays a tune; Left goes to the
parent. Keys 4 to 8 are "Playing", "Search", "Lists", "Fav" and "Add". "Playing" has no label when the player
has no current entry; "Fav" and "Add" have none when a folder is highlighted. Playing a tune opens the
now-playing screen.
- Covers: REQ-002, REQ-061, REQ-100, REQ-101
- Priority: Must
- Oracle: the screen description's `actions` are as stated for a tune, for a folder and with nothing loaded;
  a script exercises each key and checks the screen and the player's state.
- Level: simulator

**SPEC-096 The now-playing screen.** The content band shows six lines:

| Line | Holds |
|---|---|
| 1 | The title, in the 14-pixel font |
| 2 | The author |
| 3 | The released line |
| 4 | "n of m" for the subtune, elapsed and total time as m:ss, and a progress bar. With no known length the total is "--:--" and the bar is empty |
| 5 | The chip model, "PAL" or "NTSC", "1 SID", "2 SID" or "3 SID", and the engine's name ("accurate" or "light") |
| 6 | The state, then "Chip n" when the tune has more than one, "Voices 1 2 3" with a muted voice's number struck through, "Filter off" when it is off, a star for a favourite, and marks for repeat-all and shuffle |

What the lines hold in each state:

| State | Lines 1 to 3 | Line 4 | Line 5 | State shown |
|---|---|---|---|---|
| Idle, no current entry | "Nothing is playing" on line 1 | empty | empty | empty |
| Idle, with a current entry | As when it last played | Subtune, 0:00 and the total | As when it last played | "Stopped" |
| Waiting, Fetching | The file's name on line 1; lines 2 and 3 empty | The number of subtunes and the first length, from the listing | empty | "Fetching…" |
| Loading | The file's name | empty | empty | "Loading…" |
| Playing, Paused | From the tune's header | Live | Live | "Playing", "Paused" |
| Failed | The file's name | empty | empty | The failure's message |

The chip model and clock shown are the ones in effect: the header's, the library's default when the header
does not say, or the forced one followed by "(forced)". With several chips, line 5 shows the first chip's
model. Keys are in the key map: Left and Right for subtunes, Up and Down for tunes, Enter to play or pause;
keys 4 to 8 are "Browse", "Stop", "Mode", "Fav" and "More". These parts are Should or Could and may be absent
(SPEC-115): Backspace to restart; 1, 2, 3, 9 and 0 for voices, filter and chip, with their part of line 6;
"Mode", which steps `when_song_ends` and shows `MSG_MODE`; "Fav"; "More", whose panel offers "Add to
playlist", "Repeat all", "Shuffle" and "Notes"; holding Right for 400 ms or more to skip ahead, in which case
releasing it does not change the subtune.
- Covers: REQ-030, REQ-010, REQ-011, REQ-012, REQ-040, REQ-041, REQ-015, REQ-016
- Priority: Must
- Oracle: with the fixture "Commando Test" playing, the screen description's `player` and `lines` equal the
  values in the fixture's header and database entry, with the subtune as "1 of 1"; for each row of the state
  table but Loading, a script brings the player to that state and compares the lines (Loading, which settling
  passes at once, is checked at the unit level); "Stop" leaves the player Idle with
  "Stopped". Part `voices`: with voice 2 muted and the filter off, line 6 shows both.
- Level: simulator

**SPEC-097 Notes from STIL.** "Notes" opens a text panel with the entry for the playing tune from
`DOCUMENTS/STIL.txt`, read by the quick worker and converted from Latin-1: the part for the file, then the
part for the playing subtune. With no STIL file or no entry it says "No notes for this tune".
- Covers: REQ-033
- Priority: Could
- Oracle: for a fixture tune with an entry in a fixture STIL file, the panel's text in the screen description
  equals the entry; an entry with "é" shows that letter.
- Level: host-unit, simulator

**SPEC-098 Text that does not fit.** Text from tune files is converted from Windows-1252 to Unicode before it
is drawn; a byte that Windows-1252 leaves undefined becomes "?". In a list row and in the top line, text too
long for its box ends in "…". On the now-playing screen, lines 1 to 3 scroll sideways when too long, pausing
1 s at each end. No text is drawn outside its box.
- Covers: REQ-031
- Priority: Must
- Oracle: the fixture tune titled "Hülsbeck Tëst" with a 32-character author gives a screen description
  whose lines hold those strings exactly; for every box of every screen in the description, either `text_w`
  is at most `w` or `clipped` is true, and a box of the now-playing screen's lines 1 to 3 whose text is too
  long scrolls: its `text_x` differs between two dumps a second apart.
- Level: simulator

**SPEC-099 Typing.** A text field is in use in the Search field and in a text prompt. While it is:

- A key becomes a character by this map, from the keyboard's key codes (CON-06). A letter key gives its
  lower-case letter, or its capital while Shift (code 42) is down. Digits and Space give themselves. The
  `sym` layer arrives as its own codes: 26 `!`, 27 `@`, 39 `#`, 40 `$`, 41 `%`, 43 `^`, 51 `&`, 52 `*`,
  53 `(`, 94 `)`, 55 `~`, 69 `` ` ``, 70 `_`, 71 `-`, 72 `+`, 73 `=`, 74 `[`, 75 `]`, 76 `{`, 77 `}`,
  79 `;`, 80 `:`, 81 `'`, 82 `"`, 83 `<`, 85 `>`, 86 `\`, 89 `|`, 90 `,`, 91 `.`, 92 `/`, 93 `?`.
- Backspace removes the character before the cursor and Delete the one after it. Left, Right, Home and End
  move the cursor, and arrive only as their key codes. Enter accepts and ESC cancels.
- TAB, Up and Down do nothing in a prompt. In the Search field, Down moves to the results.
- A field holds at most 64 characters in Search, 40 for a playlist's name, 255 for a path or an address, and
  5 for a length. A key that would exceed the limit does nothing.
A prompt opens with the cursor at the end of the text it holds (FIND-024).
- Covers: REQ-064
- Priority: Must
- Oracle: sending the key codes for "hub 1985" puts exactly that text in the Search field; the codes for F,
  X, Z and C are typed; a sequence using Shift and the `sym` codes for `~/C64_Music.2` gives that text; the
  65th character is not added.
- Level: simulator, host-unit

**SPEC-100 Search.** The Search screen has a text field above a result list, and opens with the field in use.
In the key map the screen is two places: `search-field` while the field is in use, and `search-results`
while the list is. Enter runs the search (SPEC-162) and moves to the list; with an empty field Enter does
nothing. Up from the
first result returns to the field, as does "Edit". ESC, in the field or the list, closes the screen. In the
list, Enter plays the result, and the queue becomes the results. Keys 4 to 8 are "Edit", "Folder", "Filter",
"Fav" and "Add". The top line shows the number of results, or "No results". While the index is being built it
shows "Indexing n %" and searches what has been indexed so far.
- Covers: REQ-090, REQ-064
- Priority: Must
- Oracle: a simulator script types a query on the fixture collection and gets the expected rows in the
  screen description, plays one, opens one in the browser at its folder with the tune highlighted, and gets
  "No results" for a query that matches nothing.
- Level: simulator

**SPEC-101 Settings.** The Settings screen is a list, each row showing a setting's name and value. For a
setting with a fixed set of values, Left and Right step through them and Enter steps forward. For one that is
a path, an address or a time, Enter opens a text prompt holding the current value; a value that is not
allowed gives the notice `MSG_INVALID_VALUE` and leaves the prompt open. A change is saved at once (SPEC-130).
Keys 4, 5 and 8 are "Back", "HVSC" and "About". The rows, in order, each always built or belonging to a part
(SPEC-115):

| Row | Setting | Part |
|---|---|---|
| Collection folder | `collection` | Always |
| Engine | `engine` | Always |
| When a song ends | `when_song_ends` | `modes` |
| Default length | `default_length` | Always |
| Repeat all | `repeat_all` | `modes` |
| Shuffle | `shuffle` | `shuffle` |
| Sample rate | `sample_rate` | `rate` |
| SID model | `sid_model` | `force` |
| Clock | `clock` | `force` |
| KERNAL ROM, BASIC ROM, Character ROM | `rom_kernal`, `rom_basic`, `rom_chargen` | `roms` |
| Resume on start | `resume_on_start` | `resume` |
| Screen off while playing | `screen_off_after` | `screen-off` |
| Address for single files | `url_files` | Always |
| Address for the complete archive | `url_archive` | `download-all` |

Stepping a value with Left or Right stops at the first and last values; it does not wrap (FIND-024).
- Covers: REQ-070, REQ-051, REQ-052, REQ-083, REQ-018, REQ-060
- Priority: Must
- Oracle: a simulator script changes every row that is present, by Left, Right or the prompt as its kind
  needs, restarts the app, and reads the same values in the screen description; `0:02` for the default
  length is refused with the notice.
- Level: simulator

**SPEC-102 The HVSC screen.** The HVSC screen shows a status block and, under it, a list of things to do,
chosen with Up, Down and Enter. The status block has: "HVSC #n", "HVSC #n, complete" when the collection holds that release complete
(`hvsc_complete_release`, FIND-031), "HVSC, release not known" or "HVSC is not set up"; how many tunes HVSC
lists and how many are on the device; the index's state; and while a job runs, its step, a bar, and its
figures (SPEC-147). The list has, in this order, the rows that apply:

| Row | Shown when | Does |
|---|---|---|
| Get HVSC | HVSC is not set up | SPEC-140 |
| Check for a newer release | HVSC is set up (SPEC-140) | SPEC-148 |
| Update to #m | A check has found a newer release | SPEC-148 |
| Download everything | HVSC is set up, its release is known, and SPEC-145 allows it | SPEC-144 |
| Finish the download | A complete download was cut short while moving files (SPEC-144); in place of the row above, whatever SPEC-145 says | SPEC-144 |
| Rebuild the index | Always | SPEC-161 |

When SPEC-145 does not allow the complete download, its explanation is shown as text under the list. While a
job runs the list is replaced by the progress, key 5 is "Stop", and Enter does nothing. Key 4, "Back", and
ESC close the screen and leave the job running. The first row is highlighted when the screen opens, so from
Settings the keys 5, Enter, Enter set HVSC up.
- Covers: REQ-080, REQ-084, REQ-089, REQ-081, REQ-062
- Priority: Must
- Oracle: the screen description's `rows` equal the table's for each combination of its conditions; from
  Settings, with HVSC not set up and the fake `HttpClient` serving the fixture documents, the three keys 5,
  Enter, Enter end with the notice `MSG_HVSC_READY`; with a download held open by the fake, ESC returns to
  Settings and the job's progress is still reported by the job mark of SPEC-147.
- Level: simulator

**SPEC-103 About.** The About screen shows, as text that Up and Down scroll: "SidPlayZero" and its version;
"libsidplayfp 3.1.2" and "libresidfp 1.3.0", read from the libraries; "GPL-3.0-or-later" and where the
licence is installed; the address of the source code. Key 4 is "Back"; key 5, "Notice", opens a text panel
with SPEC-152's notice, followed, once HVSC is set up, by the copyright section of the root's
`DOCUMENTS/HVSC.txt`. `sidplayzero --version` prints the first two of those lines.
- Covers: REQ-148, REQ-126, REQ-085
- Priority: Must
- Oracle: the screen description's text holds each of those lines, and its version equals the version in the
  package's control file and in `app-builder.json`; "Notice" opens a panel whose text begins with SPEC-152's.
- Level: simulator, host-package

**SPEC-104 Brightness.** The brightness keys read the screen's brightness through `Backlight` and set it to
the nearest multiple of 10 % above or below, never below 10 % or above 100 %, and the notice `MSG_BRIGHTNESS`
shows the level. When `Backlight` cannot change it, the notice is `MSG_BRIGHTNESS_UNAVAILABLE`. The real
`Backlight` uses the first folder under `/sys/class/backlight` whose `brightness` file the app may write.
Up at 100 % asks for nothing, as down at 10 % does (FIND-024).
- Covers: REQ-129
- Priority: Should
- Oracle: with the fake at 50 %, up asks for 60 % and down for 40 %; at 10 % down asks for nothing; with a
  fake that cannot change, the second notice shows. On the device the keys change the screen's brightness, or
  the results file records that the app may not.
- Level: host-unit, simulator, device

**SPEC-106 Leaving.** A press of ESC released within 500 ms‡ is "back", and acts when it is released. When
ESC has been held for 500 ms‡ the notice `MSG_HOLD_TO_LEAVE` appears at once, in place of any notice showing,
whatever is waiting (FIND-021). At 1,000 ms‡ the app begins SPEC-201's
exit. Releasing between the two removes the notice and does nothing else.
- Covers: REQ-062
- Priority: Must
- Oracle: with the fake clock: a 200 ms press goes back; a 700 ms press shows and removes the notice and
  leaves the screen description otherwise unchanged; a 1,000 ms press ends the program with status 0. On the
  device, holding ESC returns to the launcher before the launcher's own 3-second limit.
- Level: simulator, device

**SPEC-107 Help.** `fn`+H opens a panel listing the keys of the place under it: first that place's rows of
the key map; then the `screens` rows, if the place is a screen and has not switched the key off; then the
`lists` rows, if no text is being typed there; then the `always` rows. Each line is the key's name on the
device (SPEC-111) and the row's help words. A row whose action is `nothing`, and a row whose part is not
built (SPEC-115), is left out. Up and Down scroll it; `fn`+H, Enter or ESC closes it.
A help line is the key's name as it is pressed on the device (SPEC-111's last column, such as `fn+X`, or
the key's own name), then the words of its action (FIND-024).
- Covers: REQ-063
- Priority: Must
- Oracle: for every place, the help lines in the screen description are exactly those this rule gives from
  `keymap.tsv`, read by the test itself, with the same words: so help in Settings has no line for TAB, and
  help over a `confirm` panel has none for TAB, Space, P or L.
- Level: simulator

**SPEC-108 Screenshots.** Print Screen writes what LVGL has drawn, 320×170, as a PNG named
`sidplayzero_YYYYMMDD_HHMMSS_mmm.png` in `~/Pictures/Screenshots`, creating the folder if needed, and shows
the notice `MSG_SCREENSHOT_SAVED` with the path. If it cannot be written the notice is
`MSG_SCREENSHOT_FAILED`. The same function makes the screenshots of the simulator tests.
- Covers: REQ-065
- Priority: Should
- Oracle: after the key, a PNG of 320×170 with that name pattern exists in the folder and decodes to the
  same pixels as the simulator's own `shot` of that moment; with the folder made unwritable, the second
  notice shows.
- Level: simulator

**SPEC-109 The mini player.** On every screen but now playing, the right part of the top line shows: nothing
when the player has no current entry; a mark for the state (playing, paused, stopped, fetching or loading,
failed), the tune's title cut to fit (its file name when the title is not known), and the elapsed time while
playing or paused.
- Covers: REQ-032
- Priority: Should
- Oracle: the screen descriptions of the browser, Search and Settings during playback each have `mini` with
  the state, the title and the fake clock's elapsed time; in Failed it has the failed mark.
- Level: simulator

**SPEC-110 No pointer.** The device build creates no pointer or touch input. Every screen and panel takes its
keys from the key map, so nothing needs anything but the keyboard.
- Covers: REQ-060
- Priority: Must
- Oracle: the simulator tests reach every screen and change every setting by key codes alone; an
  integration test of the device's input set-up, which is `platform` code, run against a made-up list of
  input devices, creates one keypad and nothing else.
- Level: simulator, host-integration

**SPEC-111 Key names and key codes.** Keys have these names, used in the key map and the test scripts, and on
the device they are these Linux key codes:

| Name | Code | On the device |
|---|---|---|
| Esc, Tab, Enter, Backspace, Space | 1, 15, 28, 14, 57 | Their own keys |
| A to Z, 0 to 9 | The usual codes: Q 16 … P 25, A 30 … L 38, Z 44 … M 50; 1 to 9 are 2 to 10 and 0 is 11 | Their own keys |
| Shift | 42 | `shift` |
| Up, Down, Left, Right | 103, 108, 105, 106 | `fn`+F, `fn`+X, `fn`+Z, `fn`+C |
| Home, End, PageUp, PageDown | 102, 107, 104, 109 | `fn`+K, `fn`+N, `fn`+L, `fn`+M |
| Delete | 111 | `fn`+Backspace |
| PlayPause, Previous, Next | 164, 165, 163 | `fn`+Q, `fn`+W, `fn`+E |
| Mute, VolumeDown, VolumeUp | 113, 114, 115 | `fn`+A, `fn`+S, `fn`+D |
| BrightnessDown, BrightnessUp | 224, 225 | `fn`+U, `fn`+I |
| Help, PrintScreen | 138, 99 | `fn`+H, `fn`+J |

`EscHold` and `RightHold` are the Esc and Right keys held as SPEC-106 and SPEC-096 say; `Char` is any key
that SPEC-099 turns into a character; `Any` is any key at all. A key code that this table and SPEC-099 do
not name is ignored: the keyboard also sends Ctrl (29), Alt (56), the `sym` and `fn` keys themselves (383
and 389), Insert, and F1 to F12. On the device, keys are read from the evdev keyboard without grabbing
it: the path in `SIDPLAYZERO_KEYBOARD`, else `/dev/input/by-path/platform-3f804000.i2c-event`, else the first
device under `/dev/input` that reports the keys A to Z. Only press and release events are used; the
keyboard's own repeat events are ignored. In the simulator, the desktop's keys are mapped to the same names,
with F1 to F10 standing for PlayPause, Previous, Next, Mute, VolumeDown, VolumeUp, BrightnessDown,
BrightnessUp, Help and PrintScreen in that order. Both feed one function in `core`, the only place where a
key becomes an action.
- Covers: REQ-061, REQ-127, REQ-143
- Priority: Must
- Oracle: a host-unit test feeds that function press and release events with the codes of this table and of
  SPEC-099, written into the test from this document, and gets the expected key names and characters; the
  device build's source never uses `EVIOCGRAB` (a check searches for it); on the device, SPEC-260's key step
  records the code of each key the tester presses and compares it with this table.
- Level: host-unit, device

**SPEC-112 How fast the screen answers.** Every key event is stamped when it is read, and again when the first
screen update after it has been flushed. The differences are kept as a histogram and written out by SPEC-241.
- Covers: REQ-054
- Priority: Must
- Oracle: on the device, during the 30-minute benchmark with SPEC-260's key script, `key_to_screen_ms_p95` is
  at most 100 ms.
- Level: device

**SPEC-113 Notices.** A notice is one line of text over the last row of the content band, shown for 3 s‡;
`MSG_TOO_HEAVY`, which offers a key, shows for 10 s‡. It takes no keys: every key does what it would do
without it. The answer to a key the user has just pressed (a volume level, "Added to Favourites", a
brightness, "Muted") shows at once, in place of the notice showing; that notice, if it was itself the
answer to a key, is done with, and otherwise it waits its turn again (FIND-025). Other notices wait their
turn (all but `MSG_HOLD_TO_LEAVE`, SPEC-106): one at a time, in the order raised, at most five waiting (the oldest is
dropped). A notice with the same identifier as the one showing replaces it at once and starts its time
again, and one with the same identifier as a waiting one replaces that, so five quick presses of a volume
key end with the last level showing. Everything that happens by itself (a download finishing or failing,
the index being ready, a file that could not be read at start) is told by a notice, and never opens a panel
or changes the screen, with two exceptions: the browser taking the empty screen's place (SPEC-048), and the
browser opening when HVSC has been set up (SPEC-140). Both wait until no panel and no help is open.
- Covers: REQ-019, REQ-044, REQ-081
- Priority: Must
- Oracle: with three different notices raised at once, the screen description shows them one after another,
  3 s‡ of the fake clock each; five volume presses within a second leave one notice, with the last level; a
  key pressed while one shows has its usual effect; a fake download that finishes while a text prompt is
  open leaves the prompt and its text as they were and raises the notice.
- Level: simulator

**SPEC-114 Panels.** A panel takes the keys until it is closed. Only a key press opens one; nothing that
happens by itself does. One panel can be open at a time, and help can be open over it. The kinds:

| Panel | Opened by | Keys | Always built |
|---|---|---|---|
| `confirm` | "Stop" a download; "Delete" | Enter is yes, ESC is no | Yes |
| `prompt` | A text setting; "New", "Rename" | SPEC-099 | Yes |
| `text` | "Notice"; "Notes", the report after an import | Up, Down scroll; Enter or ESC closes | Yes |
| `notice-panel` | The first thing that would use the network (SPEC-152) | Up, Down scroll; Enter accepts; ESC cancels | Yes |
| `more-playing` | "More" on the now-playing screen | Up, Down, Enter, ESC | With any of `playlists`, `modes`, `shuffle`, `notes` |
| `pick-playlist`, `more-playlist`, `import` | "Add"; "More" in a playlist; "Import" | Up, Down, Enter, ESC | Part `playlists` |
| `filter` | "Filter" | Up, Down choose a line; Left, Right change it; Enter applies; ESC cancels | Part `filters` |

- Covers: REQ-060, REQ-062
- Priority: Must
- Oracle: for each kind that is always built, a simulator script opens it, checks `panel.id` in the screen
  description, and closes it with each of its closing keys; while a panel is open, the keys 4 to 8 do
  nothing. The other kinds are checked the same way by tests of their parts.
- Level: simulator

**SPEC-115 Parts that may be left out.** Version 1.0 may be released without some of its Should and Could
features. Those that can be left out are the **parts** of this table, each behind a build switch of its
name. A Should or Could item that the table does not name has no switch and is built with the rest.

| Part | Priority | What belongs to it |
|---|---|---|
| `detail` | Should | The detail line (SPEC-046) |
| `mini` | Should | The mini player (SPEC-109) |
| `modes` | Should | The values of `when_song_ends` other than `subtunes`, `repeat_all`, the "Mode" key and their Settings rows (SPEC-065) |
| `shuffle` | Could | `shuffle` and its rows (SPEC-065) |
| `restart` | Should | Backspace on the now-playing screen (SPEC-066) |
| `skip` | Could | Holding Right to skip ahead (SPEC-066) |
| `voices` | Should | The keys 1, 2, 3, 9 and 0 on the now-playing screen and their part of line 6 (SPEC-074) |
| `bars` | Could | The voice display (SPEC-080) |
| `notes` | Could | STIL notes (SPEC-097) |
| `rate`, `force`, `roms` | Should, Could, Should | The setting and Settings row for the sample rate (SPEC-073); for the chip model and clock (SPEC-077); and for the three ROM files (SPEC-068), without which a tune that needs a ROM still says so and is skipped |
| `too-slow` | Should | SPEC-079 and the L key |
| `brightness`, `screenshot` | Should | SPEC-104 and SPEC-108 |
| `playlists` | Should | SPEC-170 to SPEC-175 and SPEC-178: the Playlists and Playlist screens, the "Lists", "Fav" and "Add" keys, the `pick-playlist`, `more-playlist` and `import` panels |
| `import-more`, `export` | Could | SPEC-176 and SPEC-177; SPEC-179 |
| `filters` | Could | SPEC-164, the "Filter" key and the `filter` panel |
| `resume`, `screen-off` | Could | SPEC-135; SPEC-207 |
| `releases` | Should | SPEC-148 and its two rows of the HVSC screen |
| `download-all` | Should | SPEC-007, SPEC-144, SPEC-145, SPEC-149, the "Download everything" row and the Settings row for `url_archive` |
| `usbsid` | Could | SPEC-191 and SPEC-192; off unless asked for |

The key map names the part of each key that is not always built, and SPEC-101 the part of each Settings
row. When a part is off: its keys do nothing and have no label, and help leaves them out; its rows are
absent from Settings, the HVSC screen and "More"; its setting keeps its default; its part of a screen is not drawn; and a "More" panel with no rows
left has no key. Every test names the parts it needs, in a comment line `parts: NAME …`, and is skipped when
one of them is off. A Must item's oracle rests only on what is always built: where an oracle in this
document reaches into a part, it says "part" and the name, and that clause is a test of its own. The build
option `SIDPLAYZERO_PARTS` is `all`, the default, which is every part but `usbsid`; `must`, for none of them;
or a list of names.
- Covers: REQ-160
- Priority: Must
- Oracle: the unit and simulator levels pass with `SIDPLAYZERO_PARTS=must`, running only tests that name no
  part, and with `all`, running every test; a check fails if a test without a `parts:` line uses a key,
  row or panel that the key map or this table gives to a part.
- Level: host-unit, simulator

**SPEC-116 Messages.** Every message is in this table, which is also a table in `core`. Tests name a message
by its identifier. "State" messages are shown on the now-playing screen and in the mini player while the
player is Failed; "notice" messages by SPEC-113; "text" on the screen or in the panel that asks.

| Identifier | Kind | Words |
|---|---|---|
| `MSG_INVALID_SID` | state | Not a valid SID file |
| `MSG_ENGINE_REFUSED` | state | This tune cannot be played |
| `MSG_NEEDS_BASIC` | state | This tune needs the C64 BASIC ROM |
| `MSG_FETCH_NOT_FOUND` | state | Not on HVSC's site. Check for a newer HVSC release |
| `MSG_FETCH_MISMATCH` | state | The download does not match HVSC's record. Check for a newer HVSC release |
| `MSG_FETCH_NETWORK` | state | Cannot fetch: no network |
| `MSG_FETCH_TOO_LARGE` | state | Cannot fetch: the file is too large |
| `MSG_CARD_FULL` | state | The card is full |
| `MSG_CANNOT_WRITE` | state | The collection folder cannot be written |
| `MSG_NO_SOUND` | state; a notice in SPEC-078 | No sound device |
| `MSG_USBSID_MISSING` | state; a notice in SPEC-191 | USBSID-Pico is not connected |
| `MSG_ENTRY_MISSING` | state | This tune is not on the device and not in HVSC |
| `MSG_NOTICE_NEEDED` | notice | Not fetched: play a tune that is not on the device to read HVSC's notice first |
| `MSG_STOPPED_BAD_FILES` | notice | Stopped: several files in a row could not be played |
| `MSG_REST_ABSENT` | notice | The rest of this list is not on the device |
| `MSG_FOLDER_UNREADABLE` | notice | This folder cannot be read |
| `MSG_ROM_IGNORED` | notice | A ROM file was ignored: not found, or the wrong size |
| `MSG_SETTINGS_RESET` | notice | Some settings could not be read and were set to their defaults |
| `MSG_LENGTHS_UNREADABLE` | notice | HVSC's song lengths could not be read |
| `MSG_VOLUME` | notice | "Volume n %", or "Muted" |
| `MSG_VOLUME_FAILED` | notice | The volume could not be changed |
| `MSG_BRIGHTNESS` | notice | Brightness n % |
| `MSG_BRIGHTNESS_UNAVAILABLE` | notice | The brightness cannot be changed on this device |
| `MSG_MODE` | notice | "When a song ends:" and the new value in words |
| `MSG_SWITCHED_LIGHT` | notice | Switched to the light engine for this tune |
| `MSG_TOO_HEAVY` | notice | Too heavy for the accurate engine. Press L for the light engine |
| `MSG_CANNOT_KEEP_UP` | notice | This tune is too heavy for this device |
| `MSG_HOLD_TO_LEAVE` | notice | Keep holding ESC to leave |
| `MSG_SCREENSHOT_SAVED`, `MSG_SCREENSHOT_FAILED` | notice | "Saved" and the path; The screenshot could not be saved |
| `MSG_FAV_ADDED`, `MSG_FAV_REMOVED` | notice | Added to Favourites; Removed from Favourites |
| `MSG_ADDED`, `MSG_ALREADY_THERE` | notice | "Added to" and the playlist's name; Already in that playlist |
| `MSG_NAME_TAKEN`, `MSG_INVALID_VALUE` | notice | There is already a playlist with that name; That value is not allowed |
| `MSG_FILE_TOO_LARGE`, `MSG_LIST_CUT` | notice | That file is too large to read; Only the first 20,000 entries were read |
| `MSG_EXPORTED` | notice | "Exported to" and the path |
| `MSG_FETCHED_SOME` | notice | "Fetched n." and, if any remain, "m remain" |
| `MSG_HVSC_READY`, `MSG_HVSC_FAILED` | notice | HVSC #n is ready; "HVSC could not be set up:" and the step that failed |
| `MSG_HVSC_NEWEST`, `MSG_HVSC_NEWER` | notice | HVSC #n is the newest release; HVSC #m is available |
| `MSG_DOWNLOAD_DONE`, `MSG_DOWNLOAD_FAILED` | notice | All of HVSC is on the device; "The download failed:" and the step |
| `MSG_UNPACKING` | notice | HVSC is being unpacked. Playing is off until it is done |
| `MSG_NEED_ROOM` | notice; text in SPEC-145 | Not enough room: n MiB needed, m MiB free |
| `MSG_NEED_7ZIP`, `MSG_NEED_MEMORY`, `MSG_NEED_WRITE` | text | SPEC-145's other three explanations |
| `MSG_CHECK_FAILED` | notice | Could not check for a newer release |
| `MSG_IMPORT_NOTHING` | text | Nothing in that file could be read as a playlist |
| `MSG_INDEX_READY` | notice | The search index is ready |
| `MSG_SELFTEST` | notice | Self-test running: leave the device alone |

Besides `n` and `m`, a message names what it is about as `path`, `name` or `step` (FIND-024).
- Covers: REQ-019, REQ-044, REQ-007
- Priority: Must
- Oracle: a host-unit test fails if `core` can raise a message that is not in its table, if two rows share
  an identifier, or if the table in `core` differs from this one (the test holds a copy of these rows).
- Level: host-unit

## 7. Settings and what is remembered

**SPEC-130 The settings file.** Settings are in `settings.ini` in the configuration folder (SPEC-134): lines of
`key = value`, with empty lines and lines starting with `#` ignored, in UTF-8. A setting that is missing has
its default. The file is written through SPEC-132 each time a setting changes, keeping the lines it did not
understand.

| Key | Values | Default |
|---|---|---|
| `collection` | A path | `~/C64Music` |
| `engine` | `auto`, `accurate`, `light` | `auto` |
| `sample_rate` | `44100`, `48000` | `48000` |
| `when_song_ends` | `subtunes`, `next_tune`, `repeat`, `stop` | `subtunes` |
| `repeat_all`, `shuffle` | `on`, `off` | `off` |
| `default_length` | `0`, or `m:ss` from `0:05` to `59:59` | `3:30` |
| `sid_model` | `auto`, `6581`, `8580` | `auto` |
| `clock` | `auto`, `pal`, `ntsc` | `auto` |
| `rom_kernal`, `rom_basic`, `rom_chargen` | A path, or empty | empty |
| `resume_on_start` | `on`, `off` | `off` |
| `screen_off_after` | `0` for never, or 10 to 3600 seconds | `0` |
| `url_files` | An `https://` address | `https://hvsc.c64.org/download/C64Music` |
| `url_archive` | An `https://` address containing `{release}` | `https://hvsc.brona.dk/HVSC/HVSC_{release}-all-of-them.7z` |

Every key has a row in Settings (SPEC-101), so every setting can be changed from the keyboard.
- Covers: REQ-070, REQ-001, REQ-083, REQ-060
- Priority: Must
- Oracle: a host-unit test sets each key to each value (and to the ends of each range), reloads, and reads it
  back; a file with only one key gives defaults for the rest; a file with a comment and an unknown key keeps
  both when saved again.
- Level: host-unit

**SPEC-131 The state file.** `state.ini` in the state folder holds what is remembered between runs, in the
same form as the settings:

| Key | Holds |
|---|---|
| `browser_folder`, `browser_entry` | The folder open in the browser, as a path under the root, and the highlighted entry's name |
| `current_path`, `current_subtune` | The player's current entry |
| `queue_kind`, `queue_source`, `queue_place` | `folder`, `search` or `playlist`; the folder's path, the query or the playlist's name; and the place in the queue |
| `notice_accepted` | `on` once the user has accepted SPEC-152's notice |
| `hvsc_complete_release` | The release of HVSC the collection holds complete, after a complete download (SPEC-144); removed by a set-up (SPEC-140), which brings the documents only; absent otherwise (FIND-031) |
| `backlight_saved` | The brightness to restore, while the app has the screen off (SPEC-207) |

It is written through SPEC-132 when any of these has changed and two seconds‡ have passed since the first
change not yet saved, and at exit. On start the browser opens `browser_folder` with `browser_entry`
highlighted; if the folder is neither on the device nor in the listing it opens the nearest parent that is,
and the root if none is. The current entry is restored as the player's current entry, Idle. The queue is
made again from its source: the folder's tunes, the playlist's entries, or, for `search`, the one current
entry alone.
`queue_place` counts from 1 (FIND-024).
- Covers: REQ-071, REQ-121
- Priority: Must
- Oracle: stopping three levels down in the fixture tree and starting again gives the same folder and
  highlight in the screen description, and the same current entry; with the folder removed, the parent
  opens; a highlight moved once every second for 20 s of the fake clock is saved at least every 2 s‡.
- Level: host-unit, simulator

**SPEC-132 Writing a file safely.** Every file the app writes for keeping (settings, state, playlists, the
index, the listing, fetched tunes, documents) is written to a new file in the same folder, named as the
target with `.tmp-sidplayzero` added and a dot in front, flushed to the card with `fsync`, renamed over the
target, and the folder flushed too. At start the app removes files with that ending from its own folders and,
as it lists them, from the collection's folders. A partial download is not such a file: it is named
`NAME.part`, with `NAME.part.meta` beside it, in the cache folder, and is removed when it is 7 days‡ old.
- Covers: REQ-073
- Priority: Must
- Oracle: with a `Files` fake that fails at every possible operation of a save in turn, the file read back
  is always complete and is either the old or the new contents; an integration test kills the real program
  with SIGKILL at 20 random moments while a script makes it save, and after each kill every kept file loads.
- Level: host-unit, host-integration

**SPEC-133 Settings that cannot be read.** A key the app does not know is kept and otherwise ignored. A line
that is neither empty, a comment nor `key = value`, a known key given twice, and a value that is not allowed for
its key are faults. A key given twice takes the value of its last line; for the other two the setting
concerned keeps its default. So is a file that is not
valid UTF-8 or is larger than 64 KiB, which is not used at all; for such a `settings.ini` the notice is
`MSG_SETTINGS_RESET`, not SPEC-204's `MSG_FILE_TOO_LARGE` (FIND-021). When a file had a fault, it is copied to
`settings.ini.bad`, or `settings.ini.bad.2` and so on if that exists, before anything is written, and the
notice `MSG_SETTINGS_RESET` is shown once at start. A file larger than 64 KiB is not read at all: it is
renamed to that name instead of copied, so its bytes are kept as they were and nothing larger than the
limit is ever read (FIND-027). `state.ini` is treated the same way, without the copy and
the notice.
- Covers: REQ-074
- Priority: Must
- Oracle: started with a `settings.ini` of random bytes, the simulator starts, shows the notice, and
  `settings.ini.bad` holds the original bytes; a file with only comments and an unknown key gives no notice
  and no copy; an empty file gives defaults and no notice; started twice with a faulty file that is not
  saved in between, the second copy is `settings.ini.bad.2`.
- Level: host-unit, simulator

**SPEC-134 Folders.** With `NAME` standing for `sidplayzero`:

| What | Folder |
|---|---|
| Settings | `$XDG_CONFIG_HOME/NAME`, else `~/.config/NAME` |
| Playlists, with the `import` and `export` folders inside, as `playlists/import` and `playlists/export` | `$XDG_DATA_HOME/NAME/playlists`, else `~/.local/share/NAME/playlists` |
| State, and the measurements of SPEC-241 | `$XDG_STATE_HOME/NAME`, else `~/.local/state/NAME` |
| The index, the listing, the speed factors, partial downloads | `$XDG_CACHE_HOME/NAME`, else `~/.cache/NAME` |

Each is created, for the user only, when first needed. The package has no scripts that could touch them.
- Covers: REQ-075, REQ-070
- Priority: Must
- Oracle: with each variable set, the app's files appear under it; the package has no maintainer scripts
  (SPEC-220), so removing it cannot delete them. On the device, after `apt remove`, the settings, playlists
  and collection are still there.
- Level: host-unit, host-package, device

**SPEC-135 Carrying on at start.** With `resume_on_start = on`, once the browser is showing, the app plays the
restored current entry from its beginning, with its queue, if it is present. An absent entry is not fetched
for this.
- Covers: REQ-072
- Priority: Could
- Oracle: with the setting on and a recorded present tune, the fake backend is loaded with that subtune after
  start; with it off, or with an absent tune, nothing is loaded and no request is made.
- Level: host-unit

## 8. HVSC

**SPEC-140 Setting HVSC up.** **HVSC is set up** when the collection root has a song length database that
gave a listing (SPEC-142); its **release** is known when `hv_sids.txt` names one (SPEC-148). A **job** is one
of four things the user starts and can watch and stop: setting HVSC up, updating it (SPEC-148), the
complete download (SPEC-144) and "Fetch missing" (SPEC-174). One job runs at a time; while one runs, the
rows that would start another are not shown. Building the index and measuring the engines are not jobs:
they run by themselves and cannot be stopped.

"Get HVSC", from the empty screen or the HVSC screen, runs these steps as a job on the long worker, shown on
the HVSC screen:

1. The notice gate (SPEC-152).
2. Check that the card has room (SPEC-146).
3. Download five documents from `url_files` into a new folder in the cache, with progress (SPEC-147):
   `DOCUMENTS/Songlengths.md5`, `DOCUMENTS/STIL.txt`, `DOCUMENTS/HVSC.txt`, `DOCUMENTS/BUGlist.txt` and
   `DOCUMENTS/hv_sids.txt`.
4. Check them: the song length file must give a listing with at least one tune (SPEC-142), and `hv_sids.txt`
   must name a release (SPEC-148).
5. Move them into `DOCUMENTS` under the collection root, creating the folders, each through SPEC-132, the
   song length file last.
6. Build the listing and start the index (SPEC-161). Raise the notice `MSG_HVSC_READY`.

If the user is on the HVSC screen or the empty screen when step 6 ends, the browser opens at the root, once
no panel or help is open; otherwise the screen is left alone. If any step fails, what was there before is untouched, the notice
`MSG_HVSC_FAILED` names the step, and the row can be chosen again. From the empty screen this is two key
presses, Enter and Enter on the notice; from Settings, three (SPEC-102).
- Covers: REQ-080, REQ-085, REQ-081
- Priority: Must
- Oracle: with a fake `HttpClient` serving fixture documents, the steps end with the fixture collection in
  the browser and a chosen absent tune playing; the request log is exactly the five documents; with a
  download failing, the root is unchanged and the notice names step 3. On the device, from an empty home
  folder, it ends within two minutes‡ with HVSC listed and a tune playing.
- Level: host-unit, simulator, device

**SPEC-141 Fetching a tune when it is played.** To play an absent entry the app requests `url_files` followed
by the tune's HVSC path, saving to a temporary file in the folder the tune belongs in (SPEC-132), after
creating that folder. When the file has arrived its MD5 must equal the one the listing
gives for that path, and then it must pass SPEC-203's check; the MD5 is compared first, so an answer that is
no tune at all fails with `MSG_FETCH_MISMATCH` (FIND-021). Then it is renamed to its name, the tune becomes present, it is added to the
index, and it is played. Otherwise it is deleted and the fetch has failed (SPEC-069). Rules:

- Fetches run on the fetch worker (SPEC-022), one at a time. A request for the tune to be played cancels a
  fetch-ahead or an earlier such request in flight, unless that is for the same tune, which it then takes
  over. A file of "Fetch missing" in flight is left to finish first (SPEC-174).
- A tune chosen with Enter is requested at once. A tune reached by a next or previous key is requested once
  it has been the current entry for 700 ms‡, the Waiting state, so stepping past tunes fetches nothing.
- The **fetch-ahead**: while a song plays unattended to its end, the entry that SPEC-065 will play next, if
  it is absent, is requested when the playing song has 10 s‡ or less to go, and nothing further ahead. It
  does not touch the player's state. If it is done before the song ends, the entry is simply present when
  its turn comes. If it is still running when the song ends, the player goes to Fetching and waits for that
  same request. If it has failed, nothing happens while the present song plays; at the song's end the entry
  fails with that failure, by SPEC-069 and SPEC-143, without a second request. A fetch-ahead is not started
  when the song has no length, during SPEC-143's 60 s‡, or while HVSC's notice is not accepted.
- Unattended requests are limited to six in any minute‡; beyond that the player waits in Waiting for the
  next one allowed.
- Before a request the card must have 8 MiB‡ free, else the fetch fails with `MSG_CARD_FULL`.
A fetch-ahead counts towards the six requests a minute, and one beyond the limit is not started (FIND-024).
- Covers: REQ-088, REQ-124, REQ-086, REQ-082
- Priority: Must
- Oracle: with the fake `HttpClient` and `Clock`: playing an absent tune with Enter makes one request at
  once, for exactly that path, and the file appears at its HVSC path and plays; playing it again makes none;
  an answer with other bytes fails with `MSG_FETCH_MISMATCH` and leaves no file; "next" pressed 50 times
  within five seconds makes one request, 700 ms‡ after the last press, for the entry landed on. With a
  30-second present tune followed by an absent one: the request is made when 10 s‡ remain and not before;
  answered in time, the second tune starts with no gap in the player's states (Playing, Loading, Playing);
  held back past the song's end, the player is Fetching and there is still one request; pressing Enter on a
  third tune meanwhile cancels it. With two-second songs and a queue of absent tunes after a present one, no
  more than six requests are made in any minute‡; with 7 MiB free no request is made.
- Level: host-unit

**SPEC-142 The listing.** HVSC's song length database is a text file with CRLF or LF line endings: a first
line `[Database]`, then for each tune a line starting `; ` that holds its HVSC path, and a line of the form
`MD5=length length …` with 32 hexadecimal digits and one length for each subtune, each `m:ss`, or `m:ss` and
a point with one to three digits of a second (`3:55.594`, `0:03.5`, `0:02.70`; FIND-028).
The app's own reader, on the long worker, turns it into `listing.bin` in the cache: every path with its MD5
and all its lengths in milliseconds, in the order of SPEC-042, with a table of where each folder starts and a
table sorted by MD5. A path that is not an HVSC path (SPEC-154), a line that fits neither form, and an MD5 line with no path
before it are skipped and counted; if more than one line in a hundred is skipped, or no tune is found, the
file is refused with `MSG_LENGTHS_UNREADABLE` and there is no listing. The listing is built again when the
database's size or modification time differs from what it records. The quick worker reads folders, lengths
and MD5s from it; it is mapped through `Files` (SPEC-021), not loaded.
- Covers: REQ-088, REQ-003, REQ-013, REQ-122
- Priority: Must
- Oracle: the listing built from the fixture database has one record per entry, in order, with every length
  to the millisecond, from files with either kind of line ending; a database with one bad line in 200 is
  used and one with two in 100 is refused; when the test names a folder holding HVSC #85's own documents
  (SPEC-252), the listing has 61,157 tunes in 1,981 folders and 87,868 lengths.
- Level: host-unit, host-integration

**SPEC-143 Absent tunes that cannot be fetched.** When an unattended fetch, or a fetch-ahead, fails for one of
the reasons SPEC-069 marks "SPEC-143", the player shows the failure for 2 s‡, as for any failed entry, and
then moves to the next entry in the queue that is present. For 60 s‡ from that failure, unattended playback
passes over absent entries at once, without a request. A tune chosen by a key is always requested, and when that fetch fails the player stays in Failed.
If the queue has no further present entry, playback ends: → Idle with the notice `MSG_REST_ABSENT`. With
repeat-all on, "further" goes round the whole queue once, so a queue whose only present entry is the one
that just played plays it again.
Only an unattended fetch or a fetch-ahead that fails starts the 60 s‡; a failed fetch of a tune chosen with
Enter does not (FIND-024).
- Covers: REQ-088, REQ-086
- Priority: Must
- Oracle: with the fake network down and 30-second songs, the queue present, absent, absent, present,
  started at the first, plays the first and then the fourth after one failed request, made when 10 s‡
  remained; the queue present, absent, absent ends in Idle with that notice after one request (part `modes`:
  with repeat-all on it plays the first again); within the 60 s‡, Enter on an absent tune still makes a request, and
  its failure leaves the player Failed. With HVSC's notice not accepted, the same queues make no request at
  all and show `MSG_NOTICE_NEEDED` once.
- Level: host-unit

**SPEC-144 The complete download.** "Download everything" is offered by SPEC-102 when SPEC-145 allows it. It
runs as a job on the long worker and keeps a marker file in the cache, `install.state`, naming the step it is
in:

1. The notice gate (SPEC-152). Find the release number: the one HVSC is set up with.
2. Check memory and room (SPEC-145, SPEC-146). Stop the player. Until the job ends, anything that would play
   a tune gives the notice `MSG_UNPACKING` instead.
3. Download `url_archive`, with `{release}` replaced, to the cache, with progress (SPEC-147).
4. List the archive with `7z l -slt` and check every entry (SPEC-149).
5. Unpack with `7z x -mmt1` into the **staging folder**, `.sidplayzero-staging` beside the collection root,
   which is removed first if it exists. The `7z` process runs in its own process group and is ended if the
   job is stopped or the app exits.
6. Check the result: no links or special files, and a song length file that gives a listing.
7. Move it into place. First the listing in use is copied to `listing.old` in the cache. Then every file of
   the staging folder is renamed over its place under the root, the `DOCUMENTS` folder last. Then each file
   that `listing.old` names and the new song length database does not is removed, if its MD5 is still the
   one `listing.old` gives. Files the user put there, or changed, are never removed. From here on the job cannot be stopped: "Stop" has no label.
8. Build the listing and the index again. Delete the archive, the staging folder, `listing.old` and the
   marker. Set `hvsc_complete_release` in `state.ini` to the release (FIND-031). Raise the notice
   `MSG_DOWNLOAD_DONE`.

A failure or a stop in steps 1 to 6 leaves the root exactly as it was, removes the staging folder and the
marker, and raises `MSG_DOWNLOAD_FAILED`; a partial archive is kept for SPEC-147. A failure in step 7 or 8
(the card filling up, a file that cannot be renamed) raises `MSG_DOWNLOAD_FAILED`, keeps the marker, the
staging folder and `listing.old`, and switches playing on again; what has been moved stays moved. At start,
a marker naming step 1 to 6 means the clean-up; a marker naming step 7 or 8 means those steps are taken up
again from where they stopped, which is safe because each rename either has happened or has not, and
`listing.old` still says which files were HVSC's. Until that has succeeded, the HVSC screen offers "Finish
the download" in place of "Download everything". It does the same, and it is offered whatever SPEC-145's
conditions say, because finishing only renames files: it needs neither the memory nor the room.
- Covers: REQ-089, REQ-081, REQ-082
- Priority: Should
- Oracle: with the fake `HttpClient`, a real `7z` and a small 7-Zip fixture archive, the steps end with the
  fixture tree under the root and every tune present; stopping at each of steps 3 to 6 leaves the root's
  contents, compared file by file, unchanged and no staging folder; a file the test planted in the root
  survives step 7; the program killed during step 7 and started again ends with the same tree as an
  uninterrupted run, with the files only the old release had gone and the planted file still there; a rename
  made to fail in step 7 by the `Files` fake gives the notice, and "Finish the download" then ends with the
  same tree; Enter on a tune during the job gives the notice and loads nothing.
- Level: host-unit, host-integration

**SPEC-145 When the complete download is offered.** It is offered when all of these hold, checked when the
HVSC screen opens and again when the row is chosen:

| Condition | If not, this text is shown under the list |
|---|---|
| HVSC is set up and its release is known (SPEC-140) | (the row is simply absent) |
| `Unpacker` is available: a `7z`, `7zz` or `7za` program is on the path | `MSG_NEED_7ZIP`: "Install the 7zip package to download everything." |
| `Memory` reports available memory plus free swap of at least 450 MiB‡ | `MSG_NEED_MEMORY`: "This device does not have enough free memory to unpack HVSC (n MiB free, 450 MiB needed)." |
| The card has at least 700 MiB‡ free | `MSG_NEED_ROOM` |
| The folder holding the collection root can be written | `MSG_NEED_WRITE`: "The folder that holds the collection cannot be written." |

Each text is followed by: "Tunes are fetched as you play them. To have everything, copy a C64Music folder
into" and the root.
- Covers: REQ-089
- Priority: Should
- Oracle: with the fake `Memory` at 449 MiB the row is absent and the memory text is in the screen
  description with the number; at 450 MiB, with an `Unpacker` and 700 MiB free, it is offered; each other
  condition, failed alone, gives its text. On the device the results file records what each condition found
  and, if all held and the tester chose to run it, that the complete download finished.
- Level: host-unit, simulator, device

**SPEC-146 Room on the card.** Before a download job the app compares what it is about to write with the free
space on the file system holding the destination. For SPEC-140 that is 16 MiB. For SPEC-144 it is 700 MiB‡
before the archive has been listed, and after step 4 the archive's size plus the sizes of its entries, each
rounded up to the file system's block size, plus a tenth, plus 64 MiB. If there is less, the job stops with
the notice `MSG_NEED_ROOM` giving both figures in MiB.
- Covers: REQ-082
- Priority: Must
- Oracle: with the `Files` fake reporting one MiB less than needed, no download request is made and the
  notice shows the two numbers; with exactly enough, the download starts.
- Level: host-unit

**SPEC-147 Progress, stopping and carrying on.** While a job runs, the HVSC screen shows its step in words,
bytes done and total, and a bar; while `7z` unpacks it shows the number of files written so far. On every
other screen the right part of the top line begins with the **job mark**, an arrow and the job's percentage,
until the job ends, which a notice tells. The job mark is always built: it does not belong to the mini
player's part. "Stop" (key 5 on the HVSC screen) asks
"Stop the download?" in a `confirm` panel, and Enter stops the job. A document is downloaded whole each time.
For the archive, a partial file is kept (SPEC-132) with the server's `ETag` and `Last-Modified`: the next
attempt asks for the rest with a `Range` request and those values as its condition when the server gave
both, and starts again from the beginning when it gave neither, when they no longer match, or when the
server answers without honouring the range.
- Covers: REQ-081
- Priority: Must
- Oracle: with the fake `HttpClient`: the screen description's `job` gives the step, done and total while a
  download is held open, on the HVSC screen and, with the job mark in `mini`, on others; an archive download cut at 50 % and tried again
  requests bytes from the cut and ends with a file equal to the whole; with a changed `ETag`, or none, it
  requests from 0; stopping leaves nothing in the cache but the partial file and its `.meta`.
- Level: host-unit, simulator

**SPEC-148 Releases.** The release number is the first number after the word "Release" in the root's
`DOCUMENTS/hv_sids.txt`, a file of two lines that HVSC keeps for this purpose. The HVSC screen shows it as
"HVSC #n". "Check for a newer release" requests that file from `url_files` and compares: a higher number
adds the row "Update to #m" and raises `MSG_HVSC_NEWER`; otherwise `MSG_HVSC_NEWEST`; and if the file cannot
be fetched or names no release, `MSG_CHECK_FAILED`. "Update" runs SPEC-140's steps 2 to 6 as a job. When the collection holds the release it is set up with
complete (`hvsc_complete_release`) and SPEC-145 allows the complete download at that moment, "Update" runs
the complete download of SPEC-144 for the newer release instead, which brings the documents too and leaves
the collection complete for it; when SPEC-145 does not allow it, "Update" runs steps 2 to 6 as above,
`hvsc_complete_release` is removed, and the HVSC screen offers "Download everything" again whenever SPEC-145
allows it (FIND-031). Afterwards, for present tunes:

- a tune whose path the new listing no longer has, and whose MD5 it has at another path, is moved there;
- a tune whose path and MD5 the new listing does not have stays where it is, as a tune of the user's own;
- a tune whose path the new listing has with another MD5 is fetched again when next played, but only if its
  file still has the MD5 the old listing gave for it. If that fetch fails, the file on the device is played.

Nothing checks for releases unless the user asks.
- Covers: REQ-084, REQ-086
- Priority: Should
- Oracle: with fixture documents for release 84 set up and a fake server offering 85: the check reports 85;
  after updating, the browser lists what 85 lists; a tune 85 moved is present at its new path with no
  request; a tune whose bytes changed is requested once when played; one the test had replaced with other
  bytes is not requested; a playlist entry whose folder was renamed still plays (SPEC-173).
- Level: host-unit

**SPEC-149 Unpacking safely.** The archive is refused, and nothing is unpacked, if any entry in `7z`'s
listing: has a path that starts with `/`, contains an empty part, a `.` or `..` part or a backslash; is a
link or a special file by anything the listing says of it (its attributes, its mode, a `Symbolic Link` or
`Hard Link` value) or is of a kind the reader does not know; or lies outside the one top-level folder
`C64Music`. It is also refused if there are more than 200,000 entries or their sizes add up to more than
2 GiB, if `7z` does not report the archive as of type 7z (it is listed and unpacked with `-t7z`), or if the
listing is longer than SPEC-204 allows; `7z` runs in a process group of its own, with a time limit, and is
stopped with the app (FIND-026). Of what is unpacked, only the paths HVSC's rules allow (SPEC-154) and
HVSC's own documents are moved into the collection; nothing else is (FIND-026). After unpacking, the
staging folder is walked without following links, and any link or special file found makes the job fail. The
reader of `7z`'s listing is the app's own and is one of SPEC-205's fuzz targets.
- Covers: REQ-123, REQ-122
- Priority: Should
- Oracle: fixture archives, one for each trick (an absolute path, a `..` part, a link pointing outside, two
  top-level folders, too many entries, too large in total), are each refused by the real `7z` path, and
  afterwards no file exists outside the staging folder and the staging folder is gone.
- Level: host-integration

**SPEC-150 Rules for every request.** Every request is HTTPS, with the server's certificate and name checked
against the system's certificates. Redirects are followed at most five times and only to HTTPS. A request is
abandoned if it cannot connect within 15 s, or if it runs slower than 1 KiB/s for 30 s. A download stops,
and fails, past its limit: 1 MiB for a tune, 32 MiB for a document, 512 MiB for the archive. The request
names the app and its version as its user agent and sends nothing else about the user or the device: no
cookies, no identifiers. What is downloaded is only ever read as data; none of it is run. A test build, and
only a test build, also accepts `http://127.0.0.1` and a certificate authority named by the test.
- Covers: REQ-124, REQ-083
- Priority: Must
- Oracle: the real `HttpClient` against a local test server: a self-signed certificate is refused; a
  redirect to `http://` is refused; an endless answer is cut at the limit; the request as the server
  received it has the user agent and no cookie; with `url_files` and `url_archive` pointed at the local
  server, the server's log is the complete list of requests the app made. The release build's rules refuse
  `http://127.0.0.1` (a test applies them through `spz_tool fetch --release-rules`, FIND-020).
- Level: host-integration

**SPEC-151 The network is used only when asked.** The app makes a request only for: SPEC-140; SPEC-141, for a
tune the user is playing or that unattended playback is about to reach; SPEC-144; SPEC-148's check and
update, and the fetch it allows when a changed tune is played; and SPEC-174. Starting, browsing, searching,
playing present tunes that the listing agrees with, and changing settings make none.
- Covers: REQ-086, REQ-124
- Priority: Must
- Oracle: a scripted session starts the app, browses, searches, plays present tunes and changes every
  setting, with a `HttpClient` that fails the test if it is called. Part `playlists`: the same session also
  edits a playlist (FIND-022).
- Level: host-unit, simulator

**SPEC-152 The notice, and the gate.** While `notice_accepted` is off, the app makes no request of any kind.
Whatever would make the first one (SPEC-140, SPEC-141, SPEC-144, SPEC-148 or SPEC-174) first opens the
`notice-panel` with this text, and goes on only when Enter accepts it, which sets `notice_accepted`. ESC
cancels what was asked for. Only a key press opens the panel: unattended playback that reaches an absent
tune while the notice is not accepted does not fetch it and does not ask (SPEC-143).

> SidPlayZero can fetch music from the High Voltage SID Collection (HVSC), a free collection kept by the
> HVSC crew at hvsc.c64.org. HVSC says of its tunes: "All of the tunes available in the High Voltage SID
> Collection (HVSC) are copyrighted." and "Use of the SIDs in this collection for any purpose other than
> private enjoyment is subject to relevant copyright law." SidPlayZero downloads files to this device only
> when you ask, from the addresses in Settings: HVSC's own site, and the mirror its site links to, unless
> you change them. It does not contain HVSC's files and does not pass them on.

The About screen shows the same text at any time (SPEC-103).
- Covers: REQ-085, REQ-148
- Priority: Must
- Oracle: with `notice_accepted` off, each of the five ways in opens the panel and makes no request; ESC
  leaves `notice_accepted` off and makes none; Enter makes the request that was asked for; the panel's text
  in the screen description equals this text.
- Level: host-unit, simulator

**SPEC-153 No music in the package or the repository.** A check walks the repository and the built `.deb` and
fails on any file whose first four bytes are `PSID` or `RSID`, unless it is under `tests/fixtures/` and named
in that folder's manifest, and on any file of exactly 8,192 or 4,096 bytes whose SHA-1 is that of a C64
KERNAL (901227-01, -02 or -03), BASIC (901226-01) or character ROM (901225-01), as MAME's source publishes
them. The fixtures are written by a generator in the repository (SPEC-252).
- Covers: REQ-087
- Priority: Must
- Oracle: the check passes on the real tree and fails when a tune that is not in the manifest is added.
- Level: host-unit, host-package

**SPEC-154 Paths are checked wherever they come in.** Two rules, a strict one for where the app writes and a
wider one for what it only reads:

| | **HVSC path** | **Collection path** |
|---|---|---|
| Used for | Every path taken from the song length database, and every path a fetched file is written to | Every other path of a tune inside the collection: one the browser found on the device, a playlist's or an imported file's entry, the saved state |
| Must | Start with `/`; have no empty part and no part that is `.` or `..`; end in `.sid` in any case | The same |
| Length | At most 255 characters | At most 1,024 bytes |
| Characters | Only the letters A to Z and a to z, digits, `_`, `-`, `.` and `/` | Any valid UTF-8 but control characters and the backslash |

A path that fails its rule is refused where it comes in: the line of the database is skipped (SPEC-142), the
playlist entry becomes a missing one, the saved state's path is dropped. The app writes a tune under the
collection root only at an HVSC path that the listing names. A collection path is only ever joined to the
root and read. A playlist's `file://` entries name files outside the collection; they too are only read.
So the user's own tunes may have spaces and accents in their names, and can be played, made favourites and
put in playlists like any other; they are never fetched.
- Covers: REQ-122, REQ-123, REQ-006
- Priority: Must
- Oracle: a table of paths (with `..`, a doubled slash, a backslash, a control character, 256 characters, no
  `.sid`) is each refused by the listing reader, and the same table without the length row by the state reader
  and, part `playlists`, by the playlist reader and the importers (FIND-022); a path with a space is refused by the listing reader and
  accepted by the others; with the fake `Files`, no write outside the root, and none at a path the listing
  does not name, is attempted in any of those tests. A tune of the test's own at `/My Rips/Cool Söng.sid`
  is listed, plays, and is restored as the current entry after a restart. Part `playlists`: it becomes a
  favourite and is still in Favourites after a restart.
- Level: host-unit, simulator

## 9. Searching

**SPEC-160 The index.** The index is one file, `index.bin`, in the cache folder. Its header holds a format
number, the collection root, and the size and modification time of the song length database it was built
from. Then one record per tune: its path (an HVSC path, or a path under the root for a tune HVSC does not
list); whether it is present; its file's size and modification time when it is; and, for present tunes, the
title, author and released line from the file's header, the chip model, the clock, the year (the first
four-digit number in the released line), and, for a tune HVSC does not list, the MD5 of its file. With each
record goes its **search text**: the path, title, author and released line, in lower case, with accented
letters reduced to their base letters and every character that is not a letter or digit turned into a space.
- Covers: REQ-091, REQ-102
- Priority: Must
- Oracle: an index built from the fixture collection, read back, has one record per tune with the expected
  fields; the search text of "/MUSICIANS/H/Hubbard_Rob/Commando.sid" contains the words "hubbard", "rob" and
  "commando"; a tune of the user's own has its MD5.
- Level: host-unit

**SPEC-161 Building the index, and keeping it true.** The long worker builds the index when there is none,
when its format number, root or database signature differ from the present ones, or when the user chooses
"Rebuild the index". Step one writes a record for every tune in the listing, without opening any tune, which
makes every path searchable. Step two walks the folders of the root that are on the device, and for each
present tune whose record is missing or whose size or modification time differs, reads its header (and the
whole file, for its MD5, if HVSC does not list it); a tune no longer there becomes absent again if the listing has it, and its record is removed if not
(FIND-021). It handles
200‡ files at a time, pausing 100 ms‡ between batches while a tune is playing, and rewrites the file through
SPEC-132 after every 2,000‡ tunes, so an interrupted build continues from there. Step two also runs at every
start, and for one folder whenever the browser lists it and finds a difference. A tune that is fetched is
added at once. Progress is shown on the Search and HVSC screens as a percentage, and the notice
`MSG_INDEX_READY` tells when a build asked for by the user or made after set-up is done.
- Covers: REQ-091, REQ-017
- Priority: Must
- Oracle: for a generated collection of 61,157 tunes: the build completes; interrupted after 10,000 tunes and
  started again, it reads only the remaining files (counted by the `Files` fake) and the result equals an
  uninterrupted build's; a search during the build returns matches from what is done; a tune added to the
  root, changed, and removed is found, updated and gone after the next start's step two, with only that file
  read; changing the root rebuilds. On the device, the results file holds the number of present tunes,
  the time step two took, and the underrun count during it.
- Level: host-unit, device

**SPEC-162 What a search matches.** The query is treated like search text and split at spaces into words. A
tune matches when every word of the query is the beginning of some word in the tune's search text. Results
are in order of path, folder by folder in SPEC-042's order, and at most 500‡ are returned; the count shown is the true number of matches. A query
with no word gives no search. A search is one pass over the index and runs on the quick worker, limited to
2 s‡, after which it returns what it has.
- Covers: REQ-090
- Priority: Must
- Oracle: on the fixture collection a table of queries, including "hub 1985", "1985 hub", "HUB", "huls" for
  "Hülsbeck" and a word that matches only a path, each give exactly the expected tunes; an absent tune is
  found by a word from its path and not by its title. On the device a two-word query over an index built
  from HVSC #85's listing returns within 500 ms (`search_ms_max`).
- Level: host-unit, device

**SPEC-164 Filters.** The `filter` panel has four lines, each changed with Left and Right: chip model (any,
6581, 8580); clock (any, PAL, NTSC); first year and last year (each "any", or a year from 1982 to the
present one). Enter applies them and ESC leaves them as they were. Applied, they limit the results to tunes
that fit, a tune with no year fitting only "any"; absent tunes, whose headers are not known, are left out
while any filter is set. The top line shows that a filter is set.
- Covers: REQ-092
- Priority: Could
- Oracle: each filter alone, and all together, remove exactly the fixture tunes that do not match.
- Level: host-unit, simulator

**SPEC-165 Searching HVSC's site.** Not built in version 1.0. REQ-093 depends on a search address on HVSC's
site that its maintainers have not offered for other programs, and whose answers carry no path to fetch a
tune by; how it would work is left to be specified if they agree. Until then the app never requests any
address but those of SPEC-151.
- Covers: REQ-093
- Priority: Could
- Oracle: SPEC-151's and SPEC-150's oracles, whose request logs hold no address under `/api/`.
- Level: host-unit

## 10. Playlists and favourites

**SPEC-170 A playlist's file.** A playlist is a UTF-8 text file in the playlists folder (SPEC-134). Its name
is the playlist's name with every character other than letters, digits, space, `-` and `_` replaced by `_`,
then `.m3u8`, with a number added if that name is taken. It is an extended M3U file that other players can
read, with two kinds of line of its own:

```
#EXTM3U
#PLAYLIST:Loading tunes
#EXTINF:235,Rob Hubbard - Commando
#SIDTUNE:subtune=1 md5=6d019ecba831a9f853675aac29a61c10
/MUSICIANS/H/Hubbard_Rob/Commando.sid
```

`#PLAYLIST:` holds the name as the user typed it, and is what the app shows. Each entry is its tune's line,
preceded by an optional `#EXTINF:` (the length in seconds and "author - title", when known) and an optional
`#SIDTUNE:` with `subtune=N`, `md5=` and the 32 digits of the file's MD5 when known, or both. A tune inside
the collection is written as its path under the root (a collection path, SPEC-154). A tune outside it is
written as `file://` followed by its full path, as it is. Without `subtune=` the entry means the tune's
default subtune. Other `#` lines are kept with the entry they come before, and move with it.
A playlist file ends each line, and the file, with a line feed; a length is written in whole seconds, cut
down; an entry with no subtune number has its default subtune's length (FIND-024).
- Covers: REQ-107, REQ-102
- Priority: Should
- Oracle: a playlist written by hand from this description, with and without the optional lines, loads and
  plays the expected subtunes; saved again after its entries are reordered, each unknown `#` line is still
  before its entry.
- Level: host-unit

**SPEC-171 Favourites.** Favourites are the playlist named "Favourites", which always exists, is listed
first, and cannot be renamed or deleted (those two keys have no label on it). "Fav" in the browser and in
Search acts on the entry "this tune, no subtune number"; on the now-playing screen it acts on "this tune,
the playing subtune". It adds the entry if it is not there and removes it if it is, with the notice
`MSG_FAV_ADDED` or `MSG_FAV_REMOVED`. A tune has a star in the browser and in search results when Favourites
has any entry for it, and on the now-playing screen when it has one for the playing subtune, or one without
a number while the default subtune plays.
- Covers: REQ-100
- Priority: Should
- Oracle: marking a fixture tune twice in the browser adds then removes one entry; marking subtune 3 on the
  now-playing screen adds an entry with `subtune=3`, the star then shows in the browser, and "Fav" in the
  browser adds a second entry without a number; after a restart the stars and the file are the same.
- Level: host-unit, simulator

**SPEC-172 Making and changing playlists.** The Playlists screen lists the playlists with their number of
entries; Enter opens one. Its keys are "Back", "New", "Rename", "Delete" and "Import". A name is 1 to 40
characters with no control characters, must differ from every other playlist's name without regard to case,
and cannot be "Favourites"; a name that is taken gives `MSG_NAME_TAKEN`. "Delete" asks first. The Playlist
screen lists the entries: the title, or the file's name when the title is not known, the subtune number if
the entry has one, and a mark for absent or missing (SPEC-173). Enter plays from that entry (SPEC-062). Its
keys are "Back", "Up", "Down", "Remove" and "More", whose panel offers "Fetch missing" (SPEC-174) and
"Export" (SPEC-179). "Add", in the browser, Search and the now-playing "More" panel, opens `pick-playlist`,
which lists the playlists and "New playlist…"; Enter adds the entry: the playing subtune from the
now-playing screen, no subtune number from the browser or Search, and the notice `MSG_ADDED` confirms it.
Adding an entry that is already there adds nothing and gives `MSG_ALREADY_THERE`. Every change is saved at once through SPEC-132. An empty playlist
shows "(empty)".
- Covers: REQ-101, REQ-064
- Priority: Should
- Oracle: a simulator script makes a playlist, adds three fixture tunes, moves the third to the top, plays
  it and sees the three play in the new order (2-second lengths, `when_song_ends = subtunes`), renames it,
  and deletes it; the file on disk after each step is as SPEC-170 describes; a second playlist with the
  same name in other case is refused.
- Level: host-unit, simulator

**SPEC-173 Finding an entry's tune.** An entry is resolved in this order: a file at its path; else, if it has
an MD5, a tune with that MD5 at another path, found in the listing or, for the user's own tunes, in the
index, and the entry's path is corrected the next time the playlist is saved; else, if the listing has its
path, the tune is **absent**; else the entry is **missing**. An absent entry is fetched when it is played
(SPEC-141). A missing entry stays in the playlist, is shown with its mark, and is skipped by playback.
- Covers: REQ-102, REQ-103
- Priority: Should
- Oracle: after a fixture folder is renamed in the collection and the listing, every entry that pointed into
  it resolves and plays; one of the user's own tunes, moved, is found by its MD5; a playlist of present,
  missing, present plays the first and third and still holds three entries afterwards.
- Level: host-unit

**SPEC-174 Fetching for a playlist.** "Fetch missing" fetches the playlist's absent entries one after
another, as a job on the fetch worker, with a pause of 250 ms‡ between them, showing "n of m". A tune that
is to be played meanwhile is fetched before the job's next file. It fetches at
most 200‡ in one go and ends with the notice `MSG_FETCHED_SOME`. It passes the notice gate first (SPEC-152)
and checks room as SPEC-141 does. "Stop", as in SPEC-147, ends it after the file in flight. It stops at the
first failure for lack of network.
- Covers: REQ-104
- Priority: Should
- Oracle: with the fake `HttpClient`: a playlist with three absent entries makes three requests, in order,
  and all three become present; one with 250 makes 200 and the notice says 50 remain; the request log holds
  only those paths; Enter on another absent tune while the job runs puts that tune's request next in the
  log.
- Level: host-unit

**SPEC-175 Importing: where from, and M3U.** The `import` panel lists the files ending in `.m3u`, `.m3u8`,
`.js2`, `.json` or `.spl` in the `import` folder (SPEC-134) and in the collection root. Enter imports one
into a new playlist. Its name is the file's `#PLAYLIST:` line when it has one, and otherwise the file's name
without its ending; either way control characters are removed, and while the name is taken or is
"Favourites", " 2", " 3" and so on is added. The name is cut so that, with what is added, it is at most 40
characters. The file is only read. An M3U or M3U8 file is
read as UTF-8 if it is valid UTF-8 and as Latin-1 if not, with either kind of line ending. Each line that is
not empty and does not start with `#` is an entry. Its subtune comes from the first of these that applies:
a `#SIDTUNE:` line before it; a `#PHOSPHOR:` line before it holding `song=N`; a `{N}` at the start of the
line; an ending `?N` on the line; an ending `/tune_NNN.sid` after a name ending in `.sid`. The same entry
twice is kept twice. These are the dialects, each as its player writes it:

| Player | An entry |
|---|---|
| VICE's VSID | `#EXTM3U`, comment lines, then `/home/u/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid` |
| sidplaywx | A first line `#`, then one full path per line |
| Phosphor | `#PHOSPHOR:song=8` then `MUSICIANS/C/Clarke_Peter/Bubble_Bobble.sid` |
| SIDPLAY for macOS | `#EXTINF:235,Rob Hubbard - Commando` then a full path, in Latin-1 |
| Audacious | `file:///home/u/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid?2`, with `%20` for a space |
| MPD | `MUSICIANS/H/Hubbard_Rob/Commando.sid/tune_002.sid` |
| A leading `{N}`, as Winamp's SID plug-in writes | `{3}C:\C64Music\MUSICIANS\H\Hubbard_Rob\Commando.sid` |

A line starting `file://` has that removed and its `%XX` sequences decoded.
- Covers: REQ-105
- Priority: Should
- Oracle: one fixture file for each row of the table, written exactly so, imports with the expected tunes,
  subtunes and order; a file with the same entry twice gives two entries; a file `mix.m3u` holding
  `#PLAYLIST:Favourites` becomes the playlist "Favourites 2", and one without that line becomes "mix".
- Level: host-unit

**SPEC-176 Importing: JSidplay2.** A `.js2` file is one tune per line; a leading `<HVSC>` is removed. A
`.json` file is an array of objects, and each object's `filename` is a tune, as in
`[{"filename":"/C64Music/MUSICIANS/T/TheK/Tune.sid"}]`; anything else in the file is ignored. Neither
carries subtunes.
- Covers: REQ-105
- Priority: Could
- Oracle: fixture files of both kinds import with the expected tunes in order; a `.json` file that is not an
  array of objects imports nothing, makes no playlist, and shows `MSG_IMPORT_NOTHING`.
- Level: host-unit

**SPEC-177 Importing: Sidplay/w.** A `.spl` file has a line `[SIDPLAY/Windows Playlist]` and a `Format=` line.
In format 1, each numbered section (`[1]`, `[2]`, …) has `Filename=` and `Subsong=`. In format 2, each entry
is a block of lines beginning with `file=`, with `subtune=` among them. A `BaseDir=` line is put in front of
file names that are not full paths. Names of keys are read without regard to case; keys the app does not
know, among them the play time, are ignored. The two forms:

```
[SIDPLAY/Windows Playlist]          [SIDPLAY/Windows Playlist]
Format=1                            Format=2
BaseDir=C:\c64music\                Basedir=Top100
[1]                                 file=Wizball.sid
Filename=barrett\trojanwa.sid       name=Wizball
Subsong=1                           subtune=1
Time=79                             time=6
```

- Covers: REQ-105
- Priority: Could
- Oracle: fixture files written as these two samples, with fixture tunes' names, each import with the
  expected tunes and subtunes.
- Level: host-unit

**SPEC-178 Matching an imported path to a tune.** Backslashes become slashes, before SPEC-154's rule is
applied, so an importer never meets a backslash (FIND-021). Then the first of these that
finds a tune in the listing or under the root is used: the part after `/C64Music/`, if the path has one; the
path as it stands, as an HVSC path; the path with a `/` put in front; the path taken relative to the folder
of the file being imported; the same four again without regard to case; the file's name alone, if exactly
one tune in the listing has that name. A path that passes none, or fails SPEC-154's rule for a collection
path, becomes a missing entry
holding the original text. After an import a `text` panel says how many entries were imported and lists
those not found.
- Covers: REQ-105, REQ-103
- Priority: Should
- Oracle: a fixture file with one entry for each of those forms, and one that matches nothing, imports with
  every form resolved to the expected tune and the last reported as not found and kept as missing.
- Level: host-unit

**SPEC-179 Exporting.** "Export" writes the playlist to the `export` folder (SPEC-134) under the name
SPEC-170 gives it, replacing a file of that name: `#EXTM3U`, `#PLAYLIST:`, then for each entry an `#EXTINF:`
line when the title is known and the HVSC path, with `?N` added when the entry has a subtune number. The
notice `MSG_EXPORTED` gives the path.
- Covers: REQ-106
- Priority: Could
- Oracle: exporting a fixture playlist and importing the file gives a playlist with the same tunes, subtune
  numbers and order.
- Level: host-unit

## 11. Real SID chips

**SPEC-191 USBSID-Pico.** Not built in version 1.0 unless the part `usbsid` is asked for (SPEC-115), which
waits for the licence wording of the driver's `usbsid_protocol.h` to be cleared with its author (R9,
FIND-007). When on, libsidplayfp is built with its USBSID backend, the package depends on `libusb-1.0-0`, and
the setting `output` (`software`, the default, or `usbsid`) appears in Settings. With `usbsid`, a second
`SoundBackend` is used: the engine runs in step with the real clock and drives the device, and nothing goes
to `AudioSink`. Its `can()` reports no skip and no registers. Subtunes, song lengths and SPEC-065 work
unchanged, because they belong to the player. If the device is not there the load fails with
`MSG_USBSID_MISSING`; if it goes away the backend reports `device_gone`, and the player pauses and raises the
same message as a notice.
- Covers: REQ-111
- Priority: Could
- Oracle: the player's host-unit tests (SPEC-060 to SPEC-069) pass against a fake `SoundBackend` whose
  `can()` is the hardware's; a fake that reports the device gone gives the message. On the device, with the
  part built, the checklist plays a tune on a USBSID-Pico, steps through its subtunes, and unplugs it.
- Level: host-unit, device

**SPEC-192 Permission to use the device.** With the part `usbsid` built, the package installs
`/usr/lib/udev/rules.d/60-sidplayzero.rules`, holding one rule that gives the logged-in user access to USB
device `cafe:4011` by tagging it `uaccess`. The rule runs no program.
- Covers: REQ-112, REQ-125
- Priority: Could
- Oracle: with the option on, the store's install-path check accepts the file, and a check finds no `RUN`,
  `PROGRAM` or `IMPORT` in it. On the device, the app opens the USBSID-Pico as the normal user.
- Level: host-package, device

## 12. Starting, stopping and staying safe

**SPEC-200 Starting.** The app starts in this order:

1. Take the lock: a file `lock` in the state folder, held with `flock` while the app runs. If another
   SidPlayZero holds it, print "SidPlayZero is already running" and exit with status 1, touching nothing.
2. Read `settings.ini` and `state.ini` (SPEC-133). Restore the screen's brightness if `backlight_saved` is set
   (SPEC-207).
3. Set up the display and the keyboard.
4. Show the browser at the remembered folder (SPEC-131), or the empty screen (SPEC-048).
5. Only then, on the workers, in this order: the clean-up of SPEC-202; SPEC-144's marker, before anything
   reads the song length database; a check of the listing and the index (SPEC-142, SPEC-161); and SPEC-072's
   measurement if it is due.

Nothing in this order uses the network. The **start time** runs from the start of the process, as the system
records it, to the first screen update flushed after the browser has its first screenful, and is at most
2 s when the app has run before.
- Covers: REQ-055, REQ-086
- Priority: Must
- Oracle: the simulator, started with a `HttpClient` that fails the test if it is called, shows the browser
  and answers a key; a second copy of the real program, started while the first runs, exits with status 1
  and the first carries on. On the device `start_ms`, for each of five starts from the launcher with
  networking off, HVSC set up and the caches in place, is at most 2,000‡.
- Level: simulator, host-integration, device

**SPEC-201 Stopping.** A held ESC (SPEC-106), SIGTERM and SIGINT all do the same, in this order: stop taking
keys; stop the sound backend, which silences and closes the stream; put the screen's brightness back if the
app had turned it off (SPEC-207); write `state.ini` (SPEC-132); tell the workers to stop, which cuts a
download short and keeps its partial file, ends a `7z` child and its process group, and keeps the index as
last saved; write the measurements if they were asked for (SPEC-241); release the lock; exit with status 0.
This takes at most 1 s. A second signal during it ends the process at once.
- Covers: REQ-120, REQ-127
- Priority: Must
- Oracle: an integration test starts the real program with a script that plays a fixture tune, sends
  SIGTERM, and sees it end with status 0 within 1 s of the real clock, `state.ini` naming that tune, and no
  child process left; the same with SIGINT, and with a fake download held open. On the device `exit_ms` is
  at most 1,000‡ and a held ESC brings the launcher back.
- Level: host-integration, simulator, device

**SPEC-202 After being killed.** At start the app removes its temporary files (SPEC-132) and partial downloads
past their age; throws away and builds again any cache file (`index.bin`, `listing.bin`, the speed factors)
whose header does not fit or that is shorter than its header says; acts on `install.state` as SPEC-144 says;
and restores the brightness as SPEC-207 says. Nothing else needs repair, because of SPEC-132.
- Covers: REQ-121
- Priority: Must
- Oracle: an integration test starts the real program, without a window and on the real clock, with a script
  that moves the highlight ten times a second and plays a fixture tune; it sends SIGKILL at a random moment
  in the first four seconds, and starts the program again, 50 times. Every start reaches the browser, and
  the folder and entry restored are ones the script's own log shows as highlighted in the 10 s‡ before the
  kill.
- Level: host-integration

**SPEC-203 A tune file is checked before the engine sees it.** A file is given to the engine only if all of
these hold; otherwise the tune fails with `MSG_INVALID_SID`. The same check is made on a fetched file before
it is stored.

| Check | Rule |
|---|---|
| Size | At most 1 MiB, and larger than its header |
| First four bytes | `PSID` or `RSID` |
| Version (bytes 4 and 5, high byte first, as all the header's numbers are) | 1 to 4; an `RSID` file, 2 to 4 |
| Data offset (bytes 6 and 7) | 0x76 for version 1 and 0x7C otherwise |
| Number of songs (bytes 14 and 15) | 1 to 256 |
| Default song (bytes 16 and 17) | 0 to the number of songs; 0 is read as 1 |

- Covers: REQ-122, REQ-126
- Priority: Must
- Oracle: the fixture set of bad tunes, one breaking each row of the table, plus an empty file and one of
  random bytes, is each refused with that message, and the fake backend's `load` is never called for them;
  in the integration level the same files, and the good fixtures cut short at every length from 0 to their
  full size, are given to the real backend under AddressSanitizer with no report.
- Level: host-unit, host-integration

**SPEC-204 Other files are read with limits.** A file is read at most this far, and a larger one is refused
with `MSG_FILE_TOO_LARGE` (the song length database with `MSG_LENGTHS_UNREADABLE`):

| File | Limit |
|---|---|
| `settings.ini`, `state.ini`, `hv_sids.txt` | 64 KiB |
| A playlist, or a file being imported | 4 MiB |
| `HVSC.txt`, `BUGlist.txt` | 1 MiB |
| `STIL.txt` | 16 MiB |
| The song length database | 32 MiB |
| The listing that `7z` prints of an archive | 64 MiB |

A line longer than 4,096 bytes is cut there. A zero byte ends its line. A playlist holds at most 20,000
entries; the rest of the file is not read, and the notice `MSG_LIST_CUT` says so. Handling any such file
takes at most 2 s‡ before the app answers keys again, because it is read on a worker.
A line longer than its limit is cut there and the rest of it dropped; a zero byte ends its line in the same
way (FIND-024).
- Covers: REQ-122
- Priority: Must
- Oracle: fixture files one byte past each limit, with a million-character line and with zero bytes inside,
  load or are refused as described, each within 2 s‡ on the host, under AddressSanitizer.
- Level: host-unit

**SPEC-205 Fuzzing.** Each reader of a file that comes from outside has a fuzz target: the tune check
(SPEC-203); the playlist reader and each importer (SPEC-170, SPEC-175 to SPEC-178); the song length reader
(SPEC-142); the STIL reader; the release reader (SPEC-148); the settings and state reader (SPEC-133); the
reader of `7z`'s listing (SPEC-149); and the readers of `index.bin` and `listing.bin`. `tools/test fuzz`
builds them with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer and runs each for 15 s‡ from the
starting files in `tests/fuzz/corpus/`, failing on a crash, a sanitizer report, or an input that takes
longer than 2 s‡. One more target gives arbitrary bytes that pass SPEC-203 to the real backend and renders
one second. That is thirteen targets, a little over three minutes of running.
- Covers: REQ-122
- Priority: Must
- Oracle: the command's status; and a deliberately broken reader, built by a test of the framework itself,
  makes it fail.
- Level: host-integration

**SPEC-206 An ordinary user's program.** The app never asks for more rights: it has no setuid file, runs no
`sudo`, and installs no service and no script that runs at installation. It needs only what the stock image
gives its user: the framebuffer, the keyboard, sound and the home folder. What it may not use it does
without: the brightness (SPEC-104) and the USB device (SPEC-192) are the two such things.
- Covers: REQ-125, REQ-127
- Priority: Must
- Oracle: the store's package checks pass; the `.deb` has no maintainer scripts and no file with a setuid or
  setgid bit; the source runs no `sudo`, `su` or `pkexec` (a check searches for them). On the device the app
  runs from the launcher, and the results file gives `user_groups` and what each of the framebuffer, the
  keyboard, sound and the backlight allowed.
- Level: host-package, device

**SPEC-207 The screen while playing.** Whatever the system does to the screen, rendering and output do not
depend on the screen or on drawing. With `screen_off_after` above 0, when that many seconds pass without a
key while a tune plays, the app writes the present brightness to `backlight_saved` in `state.ini`, at once,
and then turns the backlight off through `Backlight`. Any key turns it back on and does nothing else. The
brightness is also put back when playback pauses or stops and when the app exits, and `backlight_saved` is
then cleared. If the app was killed while the screen was off, the next start finds `backlight_saved` and
puts the brightness back. When `Backlight` cannot change the brightness, the setting's row reads "Not
available on this device" and the setting stays 0.
The seconds count from the last key pressed; a key that turns the screen back on also clears
`backlight_saved` (FIND-024).
- Covers: REQ-128, REQ-127
- Priority: Could
- Oracle: with the fake `Backlight` at 70 % and the setting at 10: after 10 s of the fake clock the fake is
  at 0 and `state.ini` holds 70; a key sets it to 70 and has no other effect on the screen description;
  pausing restores it; a start with `backlight_saved = 70` sets the fake to 70 and clears the key; with a
  fake that cannot change, the row shows the words. On the device, a tune left playing for two minutes with
  the setting at 0 plays to its end with `underrun_frames` at 0, and the tester records what the screen did.
- Level: host-unit, simulator, device

## 13. The package

**SPEC-220 What is installed.** The `.deb` installs these files and nothing else:

| File | What it is |
|---|---|
| `/usr/bin/sidplayzero` | The program |
| `/usr/share/APPLaunch/applications/sidplayzero.desktop` | The launcher's entry |
| `/usr/share/APPLaunch/share/images/sidplayzero.png`, `sidplayzero_80.png`, `sidplayzero_100.png` | The launcher's icons |
| `/usr/share/doc/sidplayzero/LICENSE`, `THIRD-PARTY-NOTICES.md`, `README.md` | Licence and notices |
| `/usr/lib/udev/rules.d/60-sidplayzero.rules` | Only with SPEC-192 |

It has no maintainer scripts and no configuration files. The package's version is the app's version followed
by `-1`, the revision of the packaging, and its file is `sidplayzero_<that version>_arm64.deb`, for example
`sidplayzero_1.0.0-1_arm64.deb`. That whole version is what REQ-140's `<version>` stands for.
- Covers: REQ-140, REQ-142, REQ-125
- Priority: Must
- Oracle: `dpkg-deb -c` on the built package lists exactly these paths and their folders; `dpkg-deb -e` finds
  only `control` and `md5sums`; the file's name is made of the control fields `Package`, `Version` and
  `Architecture`.
- Level: host-package

**SPEC-221 What it depends on.** `Depends`: `libc6`, `libstdc++6`, `libgcc-s1`, `libcurl4t64`,
`ca-certificates`, `libpulse0`, `pulseaudio-utils`. `Recommends`: `7zip`. With SPEC-191 on, `Depends` gains
`libusb-1.0-0`. All are in Debian trixie's main archive. `Architecture` is `arm64`. `Maintainer` comes from
the environment variable `SIDPLAYZERO_MAINTAINER` when the package is built. It has no default in the
repository, and a package build without it fails with a message that says so. The tests build with
`SidPlayZero test build <build@sidplayzero.test>`; `.test` is a name reserved for testing that can never be
a real address.
- Covers: REQ-141, REQ-143
- Priority: Must
- Oracle: a check reads the package's `Depends` and `Recommends` and finds each name in a copy of trixie's
  main arm64 package index fetched for the test; every library `readelf -d` lists as needed by the program
  belongs to a package in `Depends`; installing the package in a clean Debian trixie arm64 container, run
  under emulation with only the main archive enabled, succeeds and `sidplayzero --version` runs there; the
  package build without a maintainer fails.
- Level: host-package

**SPEC-222 The launcher's entry.** The `.desktop` file has `Type=Application`, `Name=SidPlayZero`,
`Exec=/usr/bin/sidplayzero`, `Icon=share/images/sidplayzero.png` and `Terminal=false`. The icons have the
sizes of the app template's: `sidplayzero.png` and `sidplayzero_100.png` are 100 pixels square and
`sidplayzero_80.png` is 80.
- Covers: REQ-140
- Priority: Must
- Oracle: the file in the package has exactly these lines, and the three images have those sizes. On the
  device the launcher shows the name and the icon, starts the app, and returns when it exits.
- Level: host-package, device

**SPEC-223 The store's manifest.** `app-builder.json` has `app_name` "SidPlayZero", `package_name`
"sidplayzero", the version, and a `store` section with these fields, which are what the store's own check
asks for:

| Field | Value |
|---|---|
| `summary` | One line of at most 80 characters |
| `description` | A few paragraphs, among them that the app contains no music and what HVSC is |
| `categories` | `["Media"]` |
| `license` | `GPL-3.0-or-later` |
| `source_repo` | The address of the source repository |
| `author` | `display_name`: "The SidPlayZero authors" |
| `share_code` | `SIDZ`: exactly four letters or digits |
| `icon` | A square PNG of 256 pixels, kept apart from the launcher's icons |
| `screenshots` | Three to six PNGs of exactly 320×170, chosen from the approved images of SPEC-255 |
| `permissions` | The seven declarations, all `false` but `network`, and `additional_hardware` too when SPEC-191 is on |

Until the screenshots exist, in Phase 5, this item's test is on the waiting list of SPEC-256, for want of
them and for no other reason.
- Covers: REQ-145
- Priority: Should
- Oracle: the package level writes the store's `meta.json` from the manifest as the store's publishing tool
  does (the title from `app_name`; the fields above; the icon as its bare file name; each screenshot as
  `screenshots/` and its file name) and runs the store's `store_meta_policy.py`, at commit `21ea24cf` of
  `CardputerZero/packages`, on it: no error is reported. Copies of the manifest with a summary of 81
  characters, a share code of five, a store icon of 100 pixels and a screenshot 321 pixels wide each get an
  error. The publishing tool itself is not run.
- Level: host-package

**SPEC-224 The store's checks run on every build.** The package level runs the store's
`install_path_policy.py`, at commit `21ea24cf` of `CardputerZero/packages`, on the built `.deb`, and makes
the other checks that repository's `validate-pr.yml` workflow makes of a package at that commit: the control
fields `Package`, `Version` and `Architecture` are present and well formed, no service runs as root, the
`.desktop` entry is in place, and the maintainer is not a placeholder. The commit is named in one file in
the repository and is moved forward by hand.
- Covers: REQ-142
- Priority: Must
- Oracle: on the package the tests build, the checks report nothing; on a copy to which a test has added a
  file `/usr/bin/other` the install-path check reports it, and on one whose maintainer is
  `Your Name <you@example.com>` the maintainer check does.
- Level: host-package

**SPEC-225 Nothing of one person's.** `tools/check_repo.py` looks through every tracked file, and through the
files and the program of the built package, for: private network addresses (IPv4 and IPv6); the private
terms kept in the `PRIVATE_TERMS` secret, matched as whole words without regard to case; and paths under
`/home/`, `/Users/` or `/root/`. The fixtures of SPEC-175 use `/home/u/`, which is allowed, and so are the
words of this document. Both builds use `-ffile-prefix-map` (SPEC-001), so the program holds no path of the
machine that built it. Everything that differs between devices comes from the environment variables
`HVSC_BASE`, `SIDPLAYZERO_KEYBOARD` and the XDG variables, or from `settings.ini`.
- Covers: REQ-143
- Priority: Must
- Oracle: the check passes on the real tree and the real package; it fails when a file with a private term,
  a private address or a path under `/home/` other than `/home/u/` is added, and when the program is built
  without the prefix map.
- Level: host-unit, host-package

**SPEC-226 Nothing publishes.** `tools/check_repo.py` fails if any workflow, script or build file contains
`czdev login` or `czdev publish`, other than in the documents that state this rule.
- Covers: REQ-146
- Priority: Must
- Oracle: the check passes on the real tree and fails when such a line is added to a workflow.
- Level: host-unit

**SPEC-227 Size.** The built `.deb` is at most 20 MB‡, and the package level fails when it is larger.
- Covers: REQ-147
- Priority: Should
- Oracle: the check's status, and the size it prints.
- Level: host-package

**SPEC-229 Installing and removing.** On a stock system image,
`sudo apt install ./sidplayzero_<version>_arm64.deb` installs the package and what it depends on, and
`sudo apt remove sidplayzero` removes every file of SPEC-220 and nothing else.
- Covers: REQ-140, REQ-141, REQ-075
- Priority: Must
- Oracle: on the device: install, start from the launcher, play a tune, remove; afterwards `dpkg -L` reports
  that the package is not installed, none of SPEC-220's files exists, and the settings, playlists and
  collection are still there.
- Level: device

## 14. Budgets and how they are measured

**SPEC-240 The budgets.** These are the numbers the tests hold the app to, with the measurement that decides
each. A budget marked ‡ is a proposal until the device run has measured it (SPEC-243).

| Budget | Value | From | Measurement (SPEC-241) | How it is produced |
|---|---|---|---|---|
| First screenful of a folder listed before | 300 ms | REQ-003 | `folder_again_ms_max` | The key script opens `/MUSICIANS/B/Bayliss_Richard` and `/MUSICIANS/S` a second time |
| First screenful of a folder not listed before | 1 s | REQ-003 | `folder_first_ms_max` | The key script opens them the first time after a start |
| Rows per second with an arrow held | 20 or more | REQ-003 | `rows_per_second_min` | The key script holds Down for 10 s in the first of those folders |
| Longest pause while it is held | 100 ms | REQ-003 | `row_pause_ms_max` | The same hold |
| Memory in use | 64 MB‡ | REQ-003 | `peak_memory_kb` | The process's peak resident size at the end of the benchmark |
| Detail line after the highlight rests | 200 ms‡ | REQ-004 | `detail_ms_max` | The key script, on present tunes |
| Key to first samples, tune present | 500 ms | REQ-020 | `key_to_sound_ms_max` | Each benchmark tune's start; time spent fetching is not counted |
| Underruns in 30 minutes of the benchmark set | 0 | REQ-050 | `underrun_frames` | SPEC-070's counter |
| Processor use while playing | 60 % of one core | REQ-050 | `cpu_percent` | The process's processor time, all threads, over the benchmark's wall time |
| Key to screen, 95 of 100 | 100 ms | REQ-054 | `key_to_screen_ms_p95` | SPEC-112, over the key script |
| Start to a browser that answers | 2 s | REQ-055 | `start_ms` | SPEC-200, five starts |
| Exit after being told | 1 s | REQ-120 | `exit_ms` | SPEC-201 |
| Search, two words | 500 ms | REQ-090 | `search_ms_max` | The key script's queries, over HVSC #85's listing |
| Index, step one, HVSC #85's listing | 60 s | REQ-091 | `index_step_one_s` | SPEC-161, at the first build |
| Index, step two | 200 tunes a second‡ or more | REQ-091 | `index_tunes_per_second` | SPEC-161; decided only when at least 1,000 tunes are present, else recorded as not measured |
| Underruns while the index is built | 0 | REQ-091 | `index_underrun_frames` | A benchmark tune playing during the build |
| Setting HVSC up | 2 minutes‡ | REQ-080 | `hvsc_setup_s` | SPEC-140, from Enter on the notice to the notice that it is ready |
| A bad file handled | 2 s‡ | REQ-122 | (the host tests' own clocks) | SPEC-204, SPEC-205 |

REQ-091's five minutes for a complete collection is the same as 200 tunes a second over its 61,157 tunes,
which is the form a device that fetches on demand can be measured in.
- Covers: REQ-003, REQ-020, REQ-050, REQ-054, REQ-055, REQ-090, REQ-091
- Priority: Must
- Oracle: every measurement named in the table is a key of the results file from the device run, and each
  is within its budget or has a finding raised (SPEC-243); a host-unit test reads this table from the
  document and fails if a key it names is not one SPEC-241 writes.
- Level: host-unit, device

**SPEC-241 Measurements the app takes of itself.** Started with `--metrics FILE`, the app writes a text file
of `key = value` lines when it exits; while a device test asks for it, it appends them to `selftest.results`
as they are taken (SPEC-260). The keys are those of SPEC-240's table and these:

| Key | Holds |
|---|---|
| `app_version`, `engine_library` | The two version lines of SPEC-103 |
| `key_to_screen_ms_p50`, `key_to_screen_ms_max` | With the `_p95` figure, from SPEC-112 |
| `speed_factor_accurate`, `speed_factor_light` | SPEC-072's two measurements |
| `bench_N_engine`, `bench_N_render_load_percent`, for N from 1 to 4 | For each tune of SPEC-242: the engine used, and the time spent rendering it as a share of the time it played |
| `audio_backend`, `audio_rate`, `audio_format` | What miniaudio opened |
| `index_tunes`, `index_step_two_s` | How many tunes were present, and how long step two took |
| `render_repeatable`, `render_hash_accurate`, `render_hash_light` | Whether ten seconds of the built-in tune rendered twice gave the same samples, and their SHA-256 for each engine |
| `lengths_commando` | The first two lengths the listing gives for `/MUSICIANS/H/Hubbard_Rob/Commando.sid` |
| `key_codes_wrong` | The keys whose codes differed from SPEC-111 in the key step, or `none` |
| `key_answer` | Each answer of the key step as it is given: the name, then `right` or `wrong` (FIND-029) |
| `memory_check_mib`, `unpacker`, `card_free_mib`, `download_everything_offered` | What SPEC-145's conditions found |
| `backlight_writable`, `framebuffer`, `keyboard` | What the app was allowed to open |
| `user_groups` | The groups of the user it ran as, without the group named after the user |

Nothing else is written. No value holds a name of a person, a device or a network, or an address.
Which keys a run off the device writes, and the form of each value, are as `src/core/metrics.cpp` has
them: `start_ms`, `exit_ms`, `app_version`, `engine_library`, `memory_check_mib`, `unpacker`,
`card_free_mib`, `download_everything_offered` and `user_groups` at every run, the `audio_`,
`lengths_commando`, `backlight_writable`, `framebuffer` and `keyboard` keys on the device only, and the
rest when they are taken; `unpacker` is `yes` or `no`, and the two version keys carry their names (FIND-024).
- Covers: REQ-050, REQ-054, REQ-055, REQ-162
- Priority: Must
- Oracle: a simulator run with `--metrics` writes a file whose keys are exactly those of the two tables that
  apply off the device, each with a value of the right form (a number, `yes` or `no`, or one of a stated set
  of words); a host-unit test of the `user_groups` line, given a user name and a group list containing it,
  leaves it out.
- Level: host-unit, simulator, device

**SPEC-242 The benchmark set.** The device run plays four tunes from HVSC, one after another, round and round
for 30 minutes, each for its own length or two minutes‡, whichever is shorter:

| N | Kind | HVSC path |
|---|---|---|
| 1 | An ordinary tune | `/MUSICIANS/H/Hubbard_Rob/Commando.sid` |
| 2 | One that plays samples | `/MUSICIANS/G/Galway_Martin/Arkanoid.sid` |
| 3 | Two SID chips | `/MUSICIANS/J/Jammer/Antipop_2SID.sid` |
| 4 | Three SID chips | `/DEMOS/A-F/Devils_ReSIDence_3SID.sid` |

They are not in the repository or the package. The tester sets HVSC up on the device, and the self-test
fetches the four through the app before the clock starts, as SPEC-141 does for any tune. The set is played
with the default settings.
- Covers: REQ-050, REQ-087
- Priority: Must
- Oracle: the results file gives, for each N, the engine used and the render load, and `underrun_frames` and
  `cpu_percent` for the whole run.
- Level: device

**SPEC-243 What the device run settles.** After the device run, every ‡ in this document and in the
requirements is either confirmed by its measurement or raised as a finding in `findings.md`, to be resolved
in the document it belongs to. The same run settles what Phase 1 left unknown: the system's sample rate and
format; the user's groups and what they allow (R8); what the screen does while an app runs, and whether the
app may change the brightness (R7); whether the device passes SPEC-145's conditions (R1); the two engines'
speeds (R2, with SPEC-262); and whether the device's rendering is repeatable (CON-20).
- Covers: REQ-050, REQ-164
- Priority: Must
- Oracle: `findings.md`, after the device run, holds a confirmation or a finding for each row of SPEC-240
  and each of those open points, each naming the line of the results file it rests on.
- Level: device

## 15. Tests

**SPEC-250 The levels, and one command each.** The script `tools/test` takes one word. Each command ends
with status 0 only if everything it ran passed, and prints the names of what failed.

| Command | Level | What it builds and runs | Time on the CI machine‡ |
|---|---|---|---|
| `tools/test unit` | host-unit | `core` and the fakes, with the unit tests (SPEC-251), plain and again with AddressSanitizer and UndefinedBehaviorSanitizer; the same once more with only the Must parts (SPEC-115); then `tools/check_repo.py` and the tests of the checker itself | 4 minutes |
| `tools/test integration` | host-integration | The real engine against the `sidplayfp` program (SPEC-071); the real program started, signalled and killed; a local HTTPS server; a real `7z`; stand-ins for `pactl` and `7z` that record their arguments; and the simulator tests once more under ThreadSanitizer | 15 minutes |
| `tools/test fuzz` | host-integration | SPEC-205 | 8 minutes |
| `tools/test package` | host-package | The `cp0-cross-package` build, then every check of section 13 on the `.deb`, with the install in an emulated arm64 container | 20 minutes |
| `tools/test host` | | The four above, in that order | |
| `tools/test simulator` | simulator | The `linux-x86-64` build and the tests of SPEC-253, with all parts and again with only the Must parts (SPEC-115) | 8 minutes |
| `tools/test device` | device | Builds the `.deb` and prints what to copy to the device and what to run there (SPEC-260) | One sitting of about an hour |

So REQ-161's three levels have the commands `tools/test host`, `tools/test simulator` and
`tools/test device`. Each command says what it needs installed and stops with that list if something is
missing; the list is in `CONTRIBUTING.md`, with the commands.
- Covers: REQ-160, REQ-161
- Priority: Must
- Oracle: on a clean checkout each command runs; a deliberately failing test at each level makes its
  command, and `tools/test host` for the host levels, end with a status other than 0 and name that test.
- Level: host-unit, host-integration, host-package, simulator, device

**SPEC-251 The host-unit level.** Unit tests are C++ programs using the doctest framework (one header, MIT),
linked with `core` and the fakes of SPEC-021, and with nothing else: no LVGL, no libsidplayfp, no sound
device, no network, and no clock but the fake one. Everything `core` does can be tested here, because `core`
holds the player, the software backend's render loop, the readers, the MD5 function, the key handling and
the message table (SPEC-020).
- Covers: REQ-160
- Priority: Must
- Oracle: the unit tests' link line names only `core`, the fakes and the C++ library (a check reads it), and
  the level passes inside a sandbox with no network and no sound device.
- Level: host-unit

**SPEC-252 Fixtures are made, not collected.** `tools/make_fixtures.py` writes everything under
`tests/fixtures/` and a manifest of it. The tunes are original: each is a few dozen bytes of 6502 code,
listed in the generator, that sets up a voice and changes its pitch on every call, so each subtune sounds
different and renders the same every time. The set has:

- a one-subtune tune titled "Commando Test" by "Test Author", and a three-subtune tune with default 2;
- two-SID and three-SID tunes; tunes marked 6581 and 8580, PAL and NTSC, and one marked for either;
- a tune marked as needing BASIC; a tune titled "Hülsbeck Tëst" with 32-character fields; and seven tunes
  whose title, author and released fields between them hold every printable Windows-1252 character;
- the bad tunes of SPEC-203;
- a song length database, a STIL file, an `HVSC.txt`, a `BUGlist.txt` and an `hv_sids.txt` for them, written
  in HVSC's forms as observed in release 85: CRLF line endings; `[Database]`, `; /path` and `md5=lengths`
  lines with lengths as `m:ss` and with one and three digits after the point (FIND-028); STIL in Latin-1; `HVSC.txt` with "Release 85" on its
  fifth line after leading spaces; and `hv_sids.txt` as the two lines `# stub for old updaters` and
  `Release 85`;
- small collections laid out like HVSC, and the same for a release 84 with one tune moved and one changed;
- playlists and import files as SPEC-170 and SPEC-175 to SPEC-177 show them; the archives of SPEC-149 and a
  good one; the limit files of SPEC-204;
- and, into a temporary folder when a test asks, a collection of 61,157 tunes in 1,981 folders shaped like
  HVSC #85, with a folder of 1,298 tunes and one of 207 folders.

Because the project writes both these files and their readers, one more test checks the readers against
HVSC itself: when the environment variable `SIDPLAYZERO_REAL_HVSC` names a folder holding HVSC's own
`DOCUMENTS`, kept outside the repository, the integration level reads them and checks the figures of
SPEC-142's oracle and the two lengths of `Commando.sid`, 3:55.594 and 1:01.288. Without the variable that
test is reported as skipped.
- Covers: REQ-087, REQ-160
- Priority: Must
- Oracle: running the generator twice gives identical files; SPEC-153's check accepts them; the real engine
  loads every good tune and the `sidplayfp` program plays each.
- Level: host-unit, host-integration

**SPEC-253 The simulator level.** The desktop build, and only a build made with the option
`SIDPLAYZERO_TEST_HOOKS`, takes these options:

```
sidplayzero --headless --script FILE --out DIR [--fakes DIR] [--backend software|fake]
            [--engine real|fake] [--clock fake|real] [--seed N] [--metrics FILE]
```

With `--headless` it opens no window and no sound device: LVGL draws into memory and sound goes to a
recording sink. With `--backend fake` the player drives the fake `SoundBackend` of SPEC-025 and no engine
runs at all; otherwise it drives the software backend, with the real engine unless `--engine fake` is given.
Time is the fake clock unless `--clock real` is given; then `advance` and `hold` wait that long in real time,
the sink takes its frames in real time, and only `settle` waits for the app to be settled. The runner gives
every script a new, empty home folder and sets `HOME`, the XDG variables and `HVSC_BASE` into it. The fakes
are set up from the `--fakes` folder:

| In the folder | Sets up |
|---|---|
| `home/` | Copied into the home folder before the start: a collection, settings, playlists |
| `http.tsv` | The fake `HttpClient`: lines of URL, status, the file with the body, and options (`stall`, `cut=BYTES`, `etag=TEXT`, `error=connect`, `error=timeout`, `error=tls`). A URL not listed answers 404. Without this file, any request fails the run |
| `fakes.ini` | Starting values: `memory.available_mib`, `memory.swap_free_mib`, `files.free_mib`, `files.list_delay_ms`, `files.root_writable`, `volume.level`, `volume.works`, `backlight.level`, `backlight.writable`, `unpacker.available`, `unpacker.tree`, `engine.speed` |

A script is lines of these commands; a line starting with `#` is a comment:

| Command | Does |
|---|---|
| `key NAME` | Presses and releases a key, by its name in SPEC-111 |
| `press NAME`, `release NAME` | One half of that |
| `hold NAME MS` | Presses, moves the clock on by MS, releases |
| `type TEXT` | Types the text, one key press for each character (SPEC-099) |
| `advance MS` | Moves the fake clock on, in steps of 10 ms. The recording sink takes exactly the frames that much time holds |
| `settle` | Waits until the app is settled (below) |
| `fake NAME VALUE` | Changes a value of `fakes.ini` while running; also `fake http release URL`, `fake http down`, `fake http up`, `fake sink drop MS` |
| `fake backend EVENT` | With `--backend fake`: makes the fake report `song_ended`, `device_lost`, `device_back`, `device_gone`, `failed REASON` or `underrun SECONDS`, or sets what it returns with `registers CHIP REGISTER VALUE` |
| `put FROM PATH`, `remove PATH`, `readonly PATH`, `writable PATH` | Copies a file or folder of the fakes folder to PATH under the home folder; deletes PATH; takes away or gives back the right to write there |
| `restart` | Leaves as `quit` does, then starts the app again with the same options, home folder and fakes, whose logs carry on |
| `dump NAME` | Writes the screen description (SPEC-254) to `NAME.json` in the `--out` folder |
| `shot NAME` | Writes the screen to `NAME.png` there (SPEC-255) |
| `expect WHAT IS VALUE` | Compares a value of the screen description. WHAT is a path into it such as `title`, `rows[2].text`, `notice.id` or `player.state`; IS is `=`, `!=`, `contains`, `matches` (a regular expression), `<` or `>` |
| `expect-file PATH exists`, `absent`, `equals OTHER`, `contains TEXT` | Looks at a file under the home folder |
| `expect-requests COUNT`, `expect-request N URL` | Looks at the fake `HttpClient`'s log |
| `expect-samples equals FILE`, `expect-samples frames IS N` | Looks at what the recording sink received |
| `save-samples NAME`, `expect-same NAME NAME` | Writes what the sink received since the last `save-samples`, or the start, to `NAME.raw` in the `--out` folder; checks that two such files are equal byte for byte |
| `expect-log NAME contains TEXT` | Looks at the log of the fake `SystemVolume`, `Backlight` or `Unpacker` |
| `quit` | Leaves as a held ESC does; the exit status must be 0 |

The app is **settled** when: no message is waiting for the UI thread; each of the three workers is idle, or
its job is waiting for the fake clock or for an answer the fake is holding back; the render loop is waiting; and LVGL
has drawn everything it had to. Every `key`, `hold`, `type`, `advance`, `fake`, `put`, `remove` and `restart`
waits for that before the next line runs, so a script sees the same thing on every run. If the app has not settled within 10 s of real
time the script fails. A failed `expect` ends the run with status 1 and writes the line, what was expected,
what was found, the screen description and the screen to the `--out` folder; a script that cannot be read
ends it with status 2.

A simulator test is a script in `tests/simulator/`, with comment lines at its top naming its SPEC items, its
parts (SPEC-115), its fakes folder and its options. For the few things a script cannot ask, such as whether
a font has a glyph (SPEC-010), the level also has test programs in C++ that link the `ui` part and LVGL with
no display. One more test starts the windowed build under a virtual display and checks that it draws a
frame and leaves on a held ESC.
- Covers: REQ-160, REQ-149
- Priority: Must
- Oracle: scripts written from this item alone behave as it says: one that reads the empty screen's
  description passes; the same with a wrong expected title fails with status 1 and writes its report; one
  with an unknown command fails with status 2; one that changes a setting, uses `restart` and reads the
  setting back passes; one that `put`s a tune into the empty root sees the browser. Two runs of one script
  give byte-identical dumps and screens.
- Level: simulator

**SPEC-254 The screen description.** `dump` and `expect` read one JSON object, made by one function from the
screens' own state and from LVGL's objects. A field that does not apply is `null`.

| Field | Holds |
|---|---|
| `screen`, `stack` | The top screen and the whole stack (SPEC-090): `browser`, `empty`, `playing`, `search`, `playlists`, `playlist`, `settings`, `hvsc`, `about`, `selftest` |
| `place` | The place of the key map whose keys apply now: a screen's name, `search-field` or `search-results`, a panel's name, `help` or `key-step` |
| `title`, `position` | The top line's left part: a screen's title, or in the browser the folder's path, `/` at the root; and "n/total" |
| `mini` | The top line's right part: `job` for the job mark, and, with the part `mini`, `state`, `title` and `elapsed` |
| `rows` | The rows on the screen, top to bottom: each with `text`, `kind` (`folder`, `tune`, `entry`, `setting`, `action`), `value` for a setting, `marks` (any of `absent`, `fetching`, `missing`, `favourite`, `playing`) and `highlighted` |
| `list` | `total` entries, the index of the `first` row shown, and `objects`, the number of LVGL objects the list holds |
| `detail` | The detail line |
| `actions` | Five strings, the labels over the keys 4 to 8, empty where a key has none |
| `typing`, `fields` | Whether text is being typed, and each text field's `text` and `cursor` |
| `panel` | The open panel: `id` (SPEC-114), `title`, `text`, `rows`, `fields` |
| `help` | The lines of the help panel, when it is open |
| `notice` | The notice showing: `id` (SPEC-116) and `text` |
| `player` | `state`, `message`, `path`, `subtune`, `subtunes`, `elapsed_ms`, `length_ms`, `engine`, `chips`, `model`, `clock`, `muted`, `filter`, `chip`, `mode`, `underrun_frames` |
| `lines` | The text lines of the now-playing, HVSC, About and self-test screens, as drawn |
| `voices` | For each bar of SPEC-080: `height` and `lit` |
| `job` | The running job: `kind`, `step`, `text`, `done`, `total` |
| `text` | All the text on the screen, in reading order |
| `boxes` | For each label drawn: `id`, `band`, `x`, `y`, `w`, `h`, `text`, `text_x` (where its text starts, which moves while it scrolls), `text_w` (the width the whole text needs) and `clipped` (whether it was cut to fit) |

The forms of the values:

| Value | Form |
|---|---|
| `player.state`, `mini.state` | `idle`, `waiting`, `fetching`, `loading`, `playing`, `paused` or `failed` |
| `player.message`, `notice.id` | An identifier of SPEC-116 |
| `player.path` | The collection path, or the `file://` form for a tune outside the collection |
| `player.subtune`, `subtunes`, `chips`, `chip` | Numbers, counted from 1 |
| `player.model`, `clock`, `engine` | `"6581"` or `"8580"`; `"PAL"` or `"NTSC"`; `"accurate"` or `"light"` |
| `player.muted`, `filter` | For each chip: three true-or-false values, true for a muted voice; and one, true for a filter that is on |
| `player.mode` | The value of `when_song_ends` |
| `mini.elapsed`, and times in `lines` and `detail` | Text as `m:ss` |
| `rows[].text` | A folder's name; a tune's file name with its ending; a playlist entry's title, or file name, as SPEC-172 shows it; a setting's name, with its value in `value`; an action's words |
| `position`, `list.first` | Text as "n/total", n counted from 1; `first` counted from 0 |
| `job.kind`, `job.step` | `setup`, `update`, `download`, `fetch`, or `selftest` during SPEC-260's benchmark; the number of the step in the job's item |
| `job.done`, `job.total` | Bytes, or files for "Fetch missing" and while unpacking |

On the empty screen, `rows` holds one row of kind `action`, "Get HVSC", and `actions` are five empty
strings. Where another item's oracle names a field, this is the field it means. Where it names a thing on
the screen that has no field of its own ("No results", "Indexing n %", that a filter is set, a row of the
Playlists screen, the now-playing star), a test reads it from `text`; the number of search results is
`list.total`, and the Search field or a prompt's field is `fields[0]`. `help` holds every line of the help,
not only those in view; `player` is filled on every screen, with `state` `idle` when nothing is loaded; and
`boxes[].text` is the text as drawn. A field is added only by changing this item and its schema (FIND-023).
- Covers: REQ-160
- Priority: Must
- Oracle: the description of the browser on the fixture collection, and of the now-playing screen playing
  "Commando Test" with the fake backend, equal two JSON files written by hand from these tables and those
  screens' items, in every field but four: `boxes` and `list.objects`, which no hand can write before the
  screens exist and which other oracles check by what must hold of them (SPEC-091, SPEC-043, SPEC-098); and
  `lines` and `text`, of which the test checks that they contain each value those items name; every dump
  in the simulator level passes the JSON schema kept beside the tests, which allows no field and no form
  these tables do not name.
- Level: simulator

**SPEC-255 Reference images.** A screen can only be drawn once it is built, so no oracle in this document
rests on an image alone: each says what the screen description must hold. Images come afterwards. In Phase 5
the `shot` of each screen is shown to the owner; one that is approved is stored as `tests/golden/NAME.png`,
and from then on `shot NAME` compares the screen with it, pixel for pixel, failing with the screen it got
and a picture of the difference. A `shot` with no stored image writes its file and is reported as "no
reference yet", without failing. The store's screenshots (SPEC-223) are chosen from the approved images.
- Covers: REQ-160, REQ-148
- Priority: Must
- Oracle: with a stored image, a `shot` equal to it passes and one that differs in a single pixel fails and
  writes the difference; with none it is reported and passes. At the end of Phase 5 every screen and panel
  of SPEC-090 and SPEC-114 has an approved image, the About screen's among them.
- Level: simulator

**SPEC-256 Every item has a test, and every test an item.** From Phase 4 on, each test carries the
identifiers of the SPEC items it checks, in its name or in a comment line of the form `SPEC-nnn`.
`tools/check_repo.py` fails if: a requirement has no SPEC item; a SPEC item names a requirement that does
not exist; an item lacks one of its four lines, or names a level that SPEC-250 does not have; `keymap.tsv`
breaks one of SPEC-094's rules; and, from Phase 4 on, if
a SPEC item has no test at each of its levels other than the device, or a test names an item that does not
exist. For the device level, each item with that level must be named by a line of the checklist or a key of
the results file (SPEC-260).

Tests are written before the code they test, so for a time they fail, and that must not hide a real fault
or block every change. The file `tests/waiting.tsv` is the **waiting list**: a test's name, the reason
(`code`, for code that Phase 5 has yet to write, or `images`, for images that SPEC-255 has yet to see
approved), and the words of the failure expected. `tools/test` counts a listed test as in order when it
fails with those words, and as a fault when it fails otherwise or when it passes, so a test that starts to
pass must be taken off the list in the same change. At the Phase 4 gate every test is either passing or on
the list; at the Phase 5 gate the list is empty.
- Covers: REQ-164
- Priority: Must
- Oracle: the check passes on the real documents; a test of the checker itself gives it copies with each of
  those faults in turn and sees each refused. A test of `tools/test` itself shows that a listed test failing
  as expected leaves the status 0, and that one failing with other words, or passing, does not.
- Level: host-unit

**SPEC-257 Checks on every pull request.** A GitHub Actions workflow runs `tools/test host` and
`tools/test simulator` on every pull request and on every push to `main`, as separate jobs behind the one
job, "All checks passed", that the repository requires. The device level is never run there, and the
workflow holds no secret but `PRIVATE_TERMS`. Until the test framework exists, in Phase 3, the workflow runs
`tools/check_repo.py` alone.
- Covers: REQ-161
- Priority: Should
- Oracle: `tools/check_repo.py` reads the workflow and fails if a job is not among those "All checks passed"
  waits for, or if a step uses a secret other than `PRIVATE_TERMS`. Once, at the Phase 3 gate, a pull request
  with a deliberately failing unit test is shown to end with a failed "All checks passed".
- Level: host-unit

**SPEC-258 The results file holds nothing private.** The device script builds the results file from the
app's measurements (SPEC-241) and the checklist's answers, and nothing else. Before writing it, it replaces
with `[removed]` every whole word, compared without regard to case, that equals the device's host name or
the user's name, and anything with the form of an IPv4 address, an IPv6 address or a network card's
address; it then says how many replacements it made. It never deletes the file.
- Covers: REQ-162, REQ-143
- Priority: Must
- Oracle: an integration test runs that step of the script on a text holding a host name, a user name
  inside and outside longer words, and one address of each kind: only the whole words and the addresses are
  replaced, and the count is right. The real results file passes `tools/check_repo.py`'s check for private
  details.
- Level: host-integration, device

**SPEC-260 The device level.** The device level is one script, `tools/device-run.sh`, and one checklist,
`docs/device-checklist.md`, done in one sitting of about an hour with the tester at the device.

*Who runs what.* The tester runs the script over SSH as the device's normal user, the one the launcher
starts apps as; it uses `sudo` for `apt install` and `apt remove` and for nothing else, and refuses to run
as root. The app is always started by the tester from the launcher, never from the script's shell, so it
runs as the launcher's user with that user's sound. After the first start the script looks for the app's
results in its own state folder; if they are not there, the app ran as another user, and the script stops
and says so.

*How they talk.* The script writes the **request file**, `selftest.request`, in the app's state folder: one
line, `step = NAME`. (A build with the test hooks of SPEC-253 also reads a line `benchmark_seconds = N`.) While that file exists, the app appends `key = value` lines to `selftest.results`
beside it: each measurement of SPEC-240 and SPEC-241 when it is taken, whatever the step (so `start_ms` at
every start, and `hvsc_setup_s` when a set-up ends); at every exit, `exit_ms` and the keys of SPEC-241 that
belong to no step; and `step_done = NAME` when a step ends. A key written again is a newer value, except `start_ms` and `exit_ms`,
of which every line is kept. The script changes the request before each of the tester's steps and waits for
`step_done`, or for the tester's answer. The steps:

| `step` | What the app does at its next start |
|---|---|
| `none` | Runs as usual, with no step of its own; measurements are recorded as they are taken |
| `keys` | Opens the self-test screen with the **key step**: it names one key at a time, from SPEC-111's table and the characters `/ . _ : ~ -`, and records the code that arrives. In this step every key is only recorded; a held ESC still leaves. It writes `key_codes_wrong` (the order and the rest are below) |
| `engines` | Opens the self-test screen; measures the two engines (SPEC-072) and renders the built-in tune twice with each; writes the speed factors and the three `render_` keys |
| `benchmark` | Opens the self-test screen while it fetches the benchmark set, if that is absent. Then it closes that screen and plays the set for 30 minutes on the app's ordinary screens, while the **key script** presses keys just as a user would, through the same code as real keys; then it builds the index again while a tune plays. It writes the keys of SPEC-240 that it produces, which are all but `start_ms`, `exit_ms` and `hvsc_setup_s`, and the `bench_` keys, and opens the self-test screen again |

*The key step.* It asks only for what could differ on a device, 25 keys, one at a time (FIND-029): the 19
names of SPEC-111's table that are pressed with `fn` (Up, Down, Left, Right, Home, End, PageUp, PageDown,
Delete, PlayPause, Previous, Next, Mute, VolumeDown, VolumeUp, BrightnessDown, BrightnessUp, Help,
PrintScreen, in that order), and then the six characters `/ . _ : ~ -`, which come from the `sym` layer
(SPEC-099), in that order. The screen's last line reads "Key n/25: NAME"; a character is named by itself.
The first press of a key that SPEC-111 or SPEC-099 names answers the key asked for, and the step goes on to
the next: the answer is right if its code is the one SPEC-111 gives for the name, or the one SPEC-099 gives
for the character, and wrong otherwise. A code that neither names is ignored, as it is everywhere. Each
answer is written to `selftest.results` as it is given, as `key_answer = NAME right|wrong`; a key step that
is started again begins at the first key with no answer, so leaving and starting the app again loses
nothing. After the 25th the step is done. `key_codes_wrong` holds the names whose last answer was wrong, in
the order asked and with a space between them, or `none`.

The self-test screen is opened over the browser, by the test and not by a key, and closed by it or by ESC.
It shows the step and its progress as lines of text and, when a step is done, "Done. Hold ESC to leave." It
has no keys of its own: the groups of the key map apply. The key step is the place `key-step`, where no
group applies. While the benchmark plays, the top line carries the job mark (SPEC-147) with the share of
the 30 minutes done, and the notice `MSG_SELFTEST` shows at its start.

*The order of the sitting.* The script prints each instruction and waits:

| Step | Who | What | Settles |
|---|---|---|---|
| 1 | Script | Installs the `.deb`; records the package's file list; writes the request, `keys` | SPEC-220, SPEC-229 |
| 2 | Tester | Starts SidPlayZero from the launcher and presses each key the screen asks for; leaves with a held ESC | SPEC-111, SPEC-099, SPEC-222, SPEC-106 |
| 3 | App | Request `engines`: the tester starts the app and waits for "Done" | SPEC-072, SPEC-071 |
| 4 | Tester | Request `none`. With an empty collection folder: Enter, Enter sets HVSC up; plays `Commando.sid`; says whether sound comes from the loudspeaker and whether the screen shows "Commando", "Rob Hubbard" and 3:55 | SPEC-140, SPEC-141, SPEC-046, SPEC-063, SPEC-070 |
| 5 | Tester | If the HVSC screen offers "Download everything" and the tester chooses to: runs it to its end. The steps after this then measure a complete collection | SPEC-145, SPEC-144 |
| 6 | Tester | Turns networking off, which ends the SSH session; starts the app and leaves with a held ESC, five times; turns networking on, connects again and runs the script again, which carries on (FIND-030) | SPEC-200, SPEC-201 |
| 7 | Tester | Volume keys: the level heard changes; brightness keys; headphones in and out twice | SPEC-076, SPEC-104, SPEC-078 |
| 8 | App | Request `benchmark`: the tester starts the app and leaves the device alone for about 35 minutes | SPEC-242, SPEC-072, SPEC-112, SPEC-043, SPEC-161, SPEC-162 |
| 9 | Tester | Request `none`. Leaves a tune playing for two minutes without touching the device, and says what the screen did | SPEC-207 |
| 10 | Tester | Leaves the app: the launcher answers at once, its key sounds play, and its volume and the brightness are as they were | SPEC-201, REQ-127 |
| 11 | Script | Removes the package; checks that the settings, playlists and collection are still there; gathers the results file (SPEC-258) and removes the request and the app's results | SPEC-229, SPEC-134 |

The five starts of step 6 come after HVSC is set up and its caches exist, as REQ-055 means them. The
script keeps the step it has reached in `sitting.state` beside it: run again with the same package, it
carries on from that step, so a step that ends the SSH session, or a sitting that stops, loses nothing;
`--restart` begins again from step 1 (FIND-030). The key
script, built into the app, repeats this round during step 8, starting one every 20 s‡: from the root, open
and leave the two folders of SPEC-240; rest on three present tunes for the detail line; open Search, type
"hub 1985" and then "galway ark", and go back; open and close Settings; and, in the first round and every
fifth after it, hold Down for 10 s in the first of the two folders. The
checklist has one line for each tester's step, answered yes, no, or in the words asked for, and the script
puts the answers in the results file. In the results file `start_ms` and `exit_ms` are lists, in order.
- Covers: REQ-162, REQ-160, REQ-043, REQ-127
- Priority: Must
- Oracle: a simulator script with `step = keys` in its home folder sees `screen = selftest` and
  `place = key-step`, feeds the keys asked for with one wrong, and finds that key's name in
  `key_codes_wrong`; with `step = benchmark`, the fake backend and `benchmark_seconds = 30`, it shows the
  browser and Search in turn while the key script runs, and ends on the self-test screen with every key the
  benchmark step writes in `selftest.results`, and `step_done = benchmark`; with `step = none`, two starts
  and exits leave two `start_ms` and two `exit_ms` lines and one set of the keys that belong to no step. A host-integration test runs `tools/device-run.sh`
  against a stand-in that plays the app's and the tester's parts through files, and gets a results file with
  an answer for each checklist line; run as root, the script refuses. The results file from the reference
  device has all of SPEC-240's and SPEC-241's keys and every answer.
- Level: host-integration, simulator, device

**SPEC-262 The engines are measured on the device before their thresholds are relied on.** The number in
SPEC-072, 2.5‡, and the default it leads to are proposals until both engines of the version the app ships
have been timed on the device. For that, the repository builds a small program, `sidplayzero-bench`, from
the same libraries, for arm64 and linked statically so that it runs on the stock image with nothing
installed. It renders its built-in tune set up for one, two and three chips, with each engine, at 44,100 and
48,000 Hz, ten seconds each after a second of warming up, and prints the twelve speed factors with the
processor's model and clock speed. It is built by `tools/engine-bench/build.sh`, and
`tools/device-probe.sh --engine-bench PATH` runs it and puts its lines in the probe's results file. When the figures are in, SPEC-072's threshold and table, SPEC-004's default and
REQ-050's budget are confirmed, or a finding is raised and they are changed before Phase 5 begins.
- Covers: REQ-051, REQ-050
- Priority: Must
- Oracle: the bench program, built for the host, prints twelve factors greater than 0 in the stated form; the
  arm64 build is a static program (`file` says so) and runs under emulation in the package level. The
  probe's results file from the reference device holds the twelve lines, and `findings.md` records the
  outcome against SPEC-072.
- Level: host-integration, host-package, device
