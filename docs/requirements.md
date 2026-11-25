# SidPlayZero requirements

| | |
|---|---|
| Status | **Draft 1, awaiting the owner's approval.** This is the Phase 1 artifact. No later phase starts before it is approved. |
| Date | 25 November 2026 |
| Next artifact | `docs/spec.md` (Phase 2), which traces every SPEC item to the REQ ids here |

## 1. What SidPlayZero is

SidPlayZero is a native app for the M5Stack CardputerZero. It browses a collection of Commodore 64 SID
music kept on the device, normally the High Voltage SID Collection (HVSC), and plays the tunes with the
sidplayfp engine. Everything is done from the device's own keyboard on its 320×170 screen. It replaces
running `sidplayfp` in a terminal.

The app is generic. Nothing about one person's device, network or accounts is built into it, so it can be
offered in the CardputerZero app store later. Publishing there is a separate step that happens only when the
owner asks for it.

## 2. How to read this document

- **REQ-nnn** is a requirement. Each has a rationale (why), a priority and an acceptance criterion (how a
  test or a person can tell it is met).
- **CON-nn** is a platform constraint: a fact about the device, its system or the things the app depends on.
  Constraints are not up for approval, but several requirements rest on them, so each says where it comes
  from and how sure it is.
- **Priority** uses four levels. All of them are proposals until the owner confirms them (section 7).

  | Priority | Meaning |
  |---|---|
  | Must | Version 1.0 is not done without it |
  | Should | Planned for 1.0, and the first to be cut if something has to give |
  | Could | Wanted, built only if it fits once the Musts and Shoulds are done |
  | Won't | Deliberately left out of 1.0 |

- A number marked **‡** is a proposed budget. It is a sensible starting point and has not been measured on
  the device yet. The specification fixes it after measurement; if it has to change, that is a finding
  against this document and comes back here.
- **Reference device** means the owner's CardputerZero. Its host name and user name are kept out of this
  repository on purpose.

Terms used below:

| Term | Meaning |
|---|---|
| Tune | One `.sid` file |
| Subtune | One of the songs inside a tune. HVSC #85 has 61,157 tunes holding 87,868 subtunes |
| Collection | A folder tree of tunes. For HVSC its root is the `C64Music` folder, with `DEMOS`, `GAMES`, `MUSICIANS` and `DOCUMENTS` inside |
| HVSC path | A tune's path inside the collection, starting with `/`, such as `/MUSICIANS/H/Hubbard_Rob/Commando.sid` |
| Engine | The code that emulates the C64 and its SID chip and produces the sound |
| Launcher | APPLaunch, the CardputerZero home screen that starts apps |

## 3. Platform constraints

How sure each one is:

- **Run**: I ran it in this project and saw the result.
- **Source**: read in the source code or data that the device actually uses.
- **Documented**: stated in the vendor's documentation, not checked beyond that.
- **Unknown**: not settled yet. `tools/device-probe.sh` or a look at the device settles it (section 7).

| Id | Constraint | Where it comes from | How sure |
|---|---|---|---|
| CON-01 | Hardware: Raspberry Pi CM0 (four Cortex-A53 cores at 1 GHz, 512 MB RAM), 1.9" LCD of 320×170, 46-key keyboard, ES8389 audio codec with an AW8737A speaker amplifier, 1 W speaker, 3.5 mm jack with plug detection, microSD storage, USB with a physical host/device switch, 1750 mAh battery | M5Stack product page | Documented |
| CON-02 | The system image is Debian 13 "trixie" for arm64, built with Raspberry Pi's image builder | M5Stack's image builder (`RELEASE=trixie`, image name `cardputerzero-trixie-arm64`) | Source; the probe confirms it on the device |
| CON-03 | An app is an ordinary program. The launcher lists it from a `.desktop` file in `/usr/share/APPLaunch/applications/`, starts it as a child process in its own process group, as the normal user and not as root, then stops drawing and waits for it to end. The app is not loaded into the launcher, so it may use threads and blocking calls freely | Launcher source (`cp0_external_app_runner.cpp`) and its packaging guide | Source |
| CON-04 | Leaving an app: the launcher keeps watching ESC while the app runs. Held for 0.5 s it shows a hint, held for 3 s it sends SIGTERM to the app's process group, and 3 s after that SIGKILL. Apps are expected to treat a short ESC as "back" and a held ESC as "exit" themselves. (The launcher's older packaging guide says SIGINT after 5 s; the code says otherwise) | Launcher source (`cp0_esc_exit_policy.hpp`), key specification, usage guide | Source |
| CON-05 | The app draws the whole 320×170 screen itself, through the Linux framebuffer `/dev/fb0` | App template | Source; the user's access to `/dev/fb0` is Unknown |
| CON-06 | Keys arrive from one Linux input device that apps share without grabbing it. The keyboard driver works out the `sym`, `shift` and `fn` layers and the press-once, press-twice and hold behaviour of those keys, so the app receives finished key codes: `fn`+F/X/Z/C are Up/Down/Left/Right, `fn`+Q/W/E are Play-Pause/Previous/Next, `fn`+A/S/D are Mute/Volume down/Volume up, `fn`+U/I are brightness, `fn`+H is Help, `fn`+J is Print Screen, `fn`+K/N are Home/End, `fn`+L/M are Page Up/Page Down | Keyboard driver and device tree keymaps, key specification | Source |
| CON-07 | Key conventions that apps are asked to follow: the number keys 4 to 8 sit under the screen and trigger five actions drawn along its bottom edge (icon centres at x = 49, 105, 161, 217 and 273, y = 153, 22×22 pixels); Enter confirms; a short ESC goes back; F/X/Z/C work as arrows without `fn` wherever text is not being typed; `fn`+H shows help. The stock apps also use TAB for a menu or settings | cardputer.cc key specification, M5Stack usage guide | Documented |
| CON-08 | Sound goes through PipeWire with its PulseAudio-compatible server, run for the logged-in user. The sound card is `ES8389-Audio`. Plugging in headphones switches the output in the system; the app does not route anything. The stock Music app plays through the PulseAudio client library and changes the system volume in 5 % steps | Image builder, device tree, Music app | Source; the sink's sample rate and format are Unknown |
| CON-09 | While an app runs, the launcher pauses its own key handling. So the volume, mute and media keys do nothing unless the app handles them | Launcher source (`keyboard_pause`) | Source, by inference; to be confirmed on the device |
| CON-10 | The launcher locks its home screen after a period without key presses (30 s by default, setting "DarkTime"). Whether the screen dims or locks while another app is running is not known | Usage guide, launcher source | Unknown |
| CON-11 | The supported way to build an app is the CardputerZero Template: C++17, LVGL 9.5.0, CMake 3.31 or newer, a desktop simulator built with SDL2, a cross build for arm64 against M5Stack's sysroot, and a `.deb` made by CPack. I built the template and ran its simulator without a display: synthetic key presses reached it, and its own screenshot function wrote 320×170 PNGs | Template; built and run on 25 November 2026 | Run |
| CON-12 | The `czdev` tool now only scaffolds and publishes. Its emulator was removed, and the manifest fields `runtime`, `entry` and `lvgl_version` are no longer read. The desktop simulator is part of the template instead | AppBuilder documentation | Source |
| CON-13 | Engine versions: Debian trixie has libsidplayfp 2.12.0 and sidplayfp 2.12.0, with the reSID and reSIDfp engines and no USBSID support. USBSID-Pico support arrived in 2.16.0. Upstream is at 3.1.2 (25 October 2026): reSID is gone, there is a new light engine called sidlite, reSIDfp is a separate library, the default sample rate is 48 kHz, and 3.1.2 fixes an out-of-bounds read, CVE-2026-111148 | Debian package indexes for arm64, upstream release notes | Source |
| CON-14 | The stock image does not include libsidplayfp. M5Stack's sysroot does include the PulseAudio and ALSA client libraries, libusb, libcurl, libarchive, liblzma, SQLite and fmt | Sysroot 0.0.4 file list | Source; the installed image may differ, the probe confirms |
| CON-15 | HVSC: release #85 of 28 June 2026 has 61,157 tunes in 1,981 folders, the largest holding 1,298 tunes. There are two releases a year. The complete collection is a 7-Zip archive of 80.8 MB on a mirror that HVSC's site links to; it unpacks to one `C64Music` folder of roughly 395 MB. Update packs are about 3.3 MB but need HVSC's own update tool, which applies a script, must be run once and in order, and has no arm64 build. HVSC's site also serves every file on its own at `https://hvsc.c64.org/download/C64Music/` followed by the HVSC path. Song lengths are in `DOCUMENTS/Songlengths.md5` (5.2 MB, one entry per tune, looked up by the MD5 of the whole file, lengths as `mm:ss` or `mm:ss.SSS` per subtune). Paths use only letters, digits, `_`, `-` and `.`, at most 94 characters, and no two differ only by case. Folder names do change between releases, sometimes only in case | hvsc.c64.org and HVSC's own documents, fetched 25 November 2026; the song length file, measured | Source. The archive's inner layout, compression method and unpacked size are from third parties, because the mirror could not be reached from here |
| CON-16 | HVSC's terms: the collection is free of charge, and every tune remains the copyright of its author or publisher. "Use of the SIDs in this collection for any purpose other than private enjoyment is subject to relevant copyright law." Nothing grants a right to redistribute it inside other software | HVSC's copyright notice and disclaimer | Source |
| CON-17 | App store rules: an arm64 `.deb`; the package name in lowercase letters, digits, `.`, `+` and `-`; files only in places named after the package (`/usr/bin/sidplayzero…`, `/usr/share/sidplayzero/`, `/usr/lib/sidplayzero/`, `/etc/sidplayzero…`) plus `/usr/share/APPLaunch/` and `/usr/share/doc/`; a udev rule is allowed if named after the package and free of `RUN` and `PROGRAM`; no setuid files, no device files, no service that runs as root; an SPDX licence id; seven yes-or-no permission declarations; one to six screenshots of exactly 320×170; a first release is reviewed by a person and needs a video of the app on a real device. Depending on Debian packages is normal: 179 of the 191 apps in the store do. GPL apps are accepted: 26 of the 191 use a GPL-3 licence. There is no SID player in the store today | The store's validator scripts and its registry, read 25 November 2026 | Source |
| CON-18 | Settings belong in `$HOME/.config/<app>/`. The system fonts are DejaVu Sans, JetBrains Mono and Noto CJK; 12 pixels or larger is recommended | cardputer.cc development guidelines | Documented |
| CON-19 | USBSID-Pico is a USB device with id `cafe:4011`. A program needs permission to open it, which on Linux is given by a udev rule. It works only when the CardputerZero's USB switch is set to host mode | USBSID-Pico repository, M5Stack product page | Source; not tried |
| CON-20 | `sidplayfp -w` does not write the same WAV twice by default, because the engine starts with a random power-on delay. With the delay fixed (`--delay=0`) repeated renders are identical byte for byte, with both reSID and reSIDfp | Tried with sidplayfp 2.6.2 on x86-64 | Run; to be repeated with the version and processor the app ships on |
| CON-21 | Licences of what the app would be built from: the engine library is GPL-2.0-or-later (122 of its 126 GPL source files say "or any later version"), so it can be part of a GPL-3 program. The four exceptions are its exSID hardware backend, which is GPL version 2 only and so must be left out of a GPL-3 build. The USBSID-Pico driver the library embeds is GPL-3.0-or-later, which makes GPL-3 the only GPL version a build with USBSID can have; one header of that driver, `usbsid_protocol.h`, carries "version 2" wording that contradicts the rest. LVGL, fmt and the app template are MIT | The source files' own licence headers | Source |

## 4. Requirements

### 4.1 The collection and browsing it

**REQ-001 Where the collection is.** The app uses the collection at `$HVSC_BASE` when that is set, otherwise
the folder chosen in its settings, otherwise `~/C64Music`.
- Why: `HVSC_BASE` is what sidplayfp users already have; the launcher starts apps without it, so there must
  be a setting too.
- Priority: Must
- Acceptance: started with `HVSC_BASE` pointing at a test tree, the browser's top level shows that tree's
  folders. Started without it and with no setting, it shows `~/C64Music`. Changing the setting changes the
  browser without restarting the app.

**REQ-002 Browse by folder.** The browser shows one folder at a time, folders before tunes, each sorted
without regard to case. Enter opens a folder or plays a tune, and back returns to the parent with the folder
just left highlighted.
- Why: this is how HVSC is organised and how people find tunes in it.
- Priority: Must
- Acceptance: for a test tree, the order shown matches a list written by hand; after opening a folder and
  going back, that folder is highlighted.

**REQ-003 A full HVSC stays quick.** With all of HVSC #85 installed, opening any folder shows its first
screenful within 300 ms‡ when the folder has been read before and within 1 s‡ when it has not. Holding an
arrow key moves the highlight at 20 rows per second‡ or more with no pause longer than 100 ms‡. The app uses
no more than 64 MB‡ of memory while doing so.
- Why: 61,157 tunes on a microSD card and 512 MB of RAM shared with the system (CON-01, CON-15). Long stalls
  were the first thing asked to be avoided.
- Priority: Must
- Acceptance: on the reference device, timed on the largest folders of HVSC #85 (`/MUSICIANS/B/Bayliss_Richard`,
  1,298 tunes, and `/MUSICIANS/S`, 207 folders), the times and the memory figure hold. On the host the same
  test runs against a generated tree of the same shape.

**REQ-004 See what a tune is before playing it.** For the highlighted tune the browser shows its title,
author and length, taken from the file and the song length database.
- Why: many file names are abbreviations, and several subtunes hide behind one name.
- Priority: Should
- Acceptance: highlighting `Commando.sid` in a test collection shows "Commando", "Rob Hubbard" and the length
  of its default subtune within 200 ms‡.

**REQ-005 Move through long lists.** Home and End jump to the first and last entry, Page Up and Page Down move
a screenful, and the list shows where the highlight is (for example "214/1298").
- Why: a folder with 1,298 entries is not usable one row at a time.
- Priority: Must
- Acceptance: each key lands on the expected row in a 1,298-entry test folder, and the position indicator
  is right.

**REQ-006 Any folder of SID files works.** The collection does not have to be HVSC. Any folder tree containing
`.sid` files can be browsed and played. Files that are not tunes are not listed.
- Why: people have their own rips, and the app should be useful before HVSC has been downloaded.
- Priority: Must
- Acceptance: a folder with three `.sid` files, a text file and no `DOCUMENTS` folder lists the three tunes and
  plays them, using the default length.

**REQ-007 Nothing there yet.** When the collection folder is missing or empty, the app says so in plain words,
shows where it looked, and offers the ways to get a collection: download HVSC (REQ-080) or copy one to the
folder.
- Why: this is what a new user sees first.
- Priority: Must
- Acceptance: started with an empty home folder, the first screen names the folder and offers the download; no
  empty list and no error code.

### 4.2 Playing

**REQ-010 Play, pause, stop.** A tune can be started, paused and resumed at the same position, and stopped.
- Why: the basic controls of any player. Pause matters more than usual, because a SID tune cannot be rewound.
- Priority: Must
- Acceptance: against a fake engine, the player's state follows the keys exactly; after pause and resume no
  samples are missing or repeated.

**REQ-011 Subtunes.** A tune starts at the subtune its file names as the default. Next and previous subtune
step through the others.
- Why: HVSC's 61,157 tunes hold 87,868 songs, so many tunes have several. The file's default is the one
  meant to be heard first.
- Priority: Must
- Acceptance: for a test tune with 3 subtunes and default 2, play starts at 2, next gives 3, previous gives 2
  then 1, and stepping past either end follows REQ-014.

**REQ-012 Next and previous tune.** Next and previous move to the neighbouring tune in whatever is being played
from: the folder, the search results or the playlist.
- Why: stepping through a composer's folder is the usual way of listening to HVSC.
- Priority: Must
- Acceptance: in a three-tune folder, next from the second plays the third; in a playlist it follows the
  playlist's order, not the folder's.

**REQ-013 Song lengths.** The length of each subtune comes from the collection's `DOCUMENTS/Songlengths.md5`,
looked up by the MD5 of the tune file. When a tune is not in it, or there is no such file, a default length
from the settings applies (3 minutes 30 seconds unless changed; 0 means play until told otherwise).
- Why: without lengths SID tunes never end. Looking up by MD5 keeps working when HVSC renames a folder (CON-15).
- Priority: Must
- Acceptance: for `Commando.sid` the app reports 3:55.594 for subtune 1 and 1:01.288 for subtune 2, as HVSC #85
  lists them; a tune missing from the file gets the default; a damaged database file gives a message and the
  default, not a crash.

**REQ-014 Carry on by itself.** When a subtune reaches its length, playback moves on. What "on" means is a
setting with at least: the tune's remaining subtunes and then the next tune; straight to the next tune;
repeat this subtune; stop.
- Why: listening without touching the device is the main use.
- Priority: Must for moving on at all, Should for the four choices
- Acceptance: with a fake engine and a 2-second test length, each setting produces the expected sequence of
  subtunes and tunes; the last tune of a folder ends in "stop" unless repeat is on.

**REQ-015 Start again and skip ahead.** The current subtune can be restarted from the beginning. Holding a key
plays it faster than real time to skip ahead.
- Why: SID tunes cannot be rewound, only replayed; skipping ahead is what sidplayfp offers.
- Priority: Should for restart, Could for skipping ahead
- Acceptance: restart sets the elapsed time to 0:00 and plays; skipping ahead advances the elapsed time at
  four times real time‡ or more and returns to normal speed on release.

**REQ-016 Play modes.** Repeat one, repeat all and shuffle can be switched on for folders and playlists.
- Why: shuffle was listed as wanted at the start. Repeat costs almost nothing once REQ-014 exists.
- Priority: Should for repeat, Could for shuffle
- Acceptance: with shuffle on and a fixed seed in the test, ten tunes play once each in the seeded order
  before any repeats.

**REQ-017 Music keeps playing while you look around.** Playback continues while the user browses other
folders, searches, edits playlists or changes settings.
- Why: finding the next tune while one plays is how a music player is used, and it is what `sidplayfp` in a
  terminal cannot do.
- Priority: Must
- Acceptance: with a tune playing, moving through three folders and opening settings does not interrupt the
  sample stream (no underrun counted, REQ-050).

**REQ-018 Tunes that need the C64's ROMs.** Tunes that play without the C64's BASIC and KERNAL ROMs play.
A tune that needs a ROM the app does not have says so and is skipped when playing unattended. The paths of
ROM files can be given in the settings. The app ships no ROMs.
- Why: the ROMs are copyrighted and upstream does not ship them either; most tunes do not need them.
- Priority: Must for the message and the skip, Should for the ROM settings
- Acceptance: a BASIC tune without ROMs shows "needs the BASIC ROM" and the next tune plays; with ROM paths
  set, the same tune plays.

**REQ-019 Files that will not play.** A file that cannot be loaded gives a message within 1 s‡ and does not
crash or hang the app. Unattended, playback moves to the next tune, and after five‡ failures in a row it stops
and says why.
- Why: collections contain files the engine rejects. An unattended player must not stop at the first, nor run
  through thousands of them.
- Priority: Must
- Acceptance: a folder of five truncated files followed by a good one stops with a message after the fifth; a
  folder with one bad file between two good ones plays both good ones.

**REQ-020 Quick to start.** Sound starts within 500 ms‡ of choosing a tune.
- Why: on a small keyboard, a delay after choosing a tune looks like a key press that did not register.
- Priority: Should
- Acceptance: on the reference device, the time from the key press to the first samples handed to the sound
  system, logged by the app, is within the budget for the benchmark tunes of REQ-050.

### 4.3 The now-playing view

**REQ-030 What is playing.** The now-playing view shows the tune's title, author and released line, the
subtune as "n of m", elapsed and total time, the SID model (6581 or 8580), PAL or NTSC, how many SID chips the
tune uses, and whether it is playing, paused or stopped.
- Why: these are the fields asked for at the start, and what sidplayfp prints.
- Priority: Must
- Acceptance: a simulator screenshot of a known test tune matches a reference image, and each field equals the
  value in the tune's header.

**REQ-031 Text is shown properly.** Text from tune files, which is in the Windows-1252 character set, is shown
with the right characters. A line too long for the screen scrolls or ends in "…" and never runs over another
field.
- Why: 32-character fields and names like "Hülsbeck" on a 320-pixel screen.
- Priority: Must
- Acceptance: a test tune with "Hülsbeck" and a 32-character title shows both correctly in a simulator
  screenshot.

**REQ-032 Always know what is playing.** Every other screen shows, in one line, the tune being played and its
elapsed time.
- Why: the now-playing view is not always on the screen (REQ-017).
- Priority: Should
- Acceptance: simulator screenshots of the browser, search and settings screens during playback each contain
  the line.

**REQ-033 Notes about the tune.** The entry for the tune in HVSC's `STIL.txt` (title, artist and comments) can
be read in the app.
- Why: STIL holds the stories behind the tunes: what a tune covers, where it was used, the composer's remarks.
  It comes with HVSC.
- Priority: Could
- Acceptance: for a tune with a STIL entry, the text shown equals the entry, converted from Latin-1.

**REQ-034 Something to look at.** The now-playing view shows what the three voices are doing, as level bars or
a waveform.
- Why: not asked for. It is what people expect of a SID player, and it shows at a glance that sound is being
  made.
- Priority: Could
- Acceptance: the display changes with the tune and stops moving when paused; it costs no underruns (REQ-050).

### 4.4 Sound controls and output

**REQ-040 Mute voices.** Each voice of each SID chip can be switched off and on while playing, and the view
shows which are off.
- Why: asked for at the start. It is how people study how a tune is put together.
- Priority: Should
- Acceptance: with voice 1 off, the rendered output equals a reference render made with the same voice muted.

**REQ-041 Filter on and off.** The SID filter emulation can be switched off and on.
- Why: asked for at the start. The filter is the most disputed part of SID emulation, and some tunes sound
  better without it.
- Priority: Should
- Acceptance: output with the filter off equals a reference render made with the filter off.

**REQ-042 Volume.** `fn`+S and `fn`+D lower and raise the system volume in 5 % steps and `fn`+A mutes and
unmutes it, on every screen. The app shows the new level briefly.
- Why: the system leaves these keys to the app while it runs (CON-09), and 5 % is what the stock Music app
  does (CON-08).
- Priority: Must
- Acceptance: against a fake sound system, each key requests the expected change; on the reference device the
  level heard changes and the launcher shows the same level afterwards.

**REQ-043 Speaker and headphones.** Sound comes out of the speaker, or the headphones when they are plugged
in. Plugging or unplugging while a tune plays does not stop the app and needs no restart; any gap is shorter
than 1 s‡.
- Why: the system does the switching (CON-08), and the app must survive it.
- Priority: Must
- Acceptance: on-device checklist: plug and unplug twice during a tune; sound follows each time and the
  elapsed time keeps counting.

**REQ-044 When the sound system is not there.** If sound cannot be opened, or goes away, the app says so,
keeps working as a browser, and tries again when asked to play.
- Why: the sound system is a separate service that can be restarting or absent, and the app must not hang
  waiting for it.
- Priority: Must
- Acceptance: with the fake sound system refusing to open, play shows the message and the app stays usable;
  when it accepts again, play works.

**REQ-045 Tunes for two and three SID chips.** Tunes written for two or three SID chips play with all chips
sounding.
- Why: HVSC #85 has 337 two-SID and 27 three-SID tunes.
- Priority: Should
- Acceptance: a two-SID test tune's output equals a reference render of it.

**REQ-046 Override the chip and the clock.** The SID model and the PAL or NTSC clock normally follow the tune
file and can be forced in the settings.
- Why: some tunes are marked for the wrong chip, and listeners have preferences.
- Priority: Could
- Acceptance: forcing 8580 on a 6581 tune gives output equal to a reference render made that way.

### 4.5 Engine, quality and speed

**REQ-050 Plays in real time on the device.** With the default settings, the reference device plays without
dropouts: no underruns in 30 minutes over a fixed benchmark set of an ordinary tune, a tune that plays
samples, a two-SID tune and a three-SID tune. The app's processor use averages no more than 60 %‡ of one core.
- Why: the default engine of sidplayfp is accurate and heavy, and the processor is modest (CON-01).
- Priority: Must
- Acceptance: the app counts underruns and time spent rendering and can write both to a file; the on-device
  run plays the benchmark set and the results file shows zero underruns and the processor figure within
  budget.

**REQ-051 Choice of engine.** The settings offer a light engine and the most accurate one the engine library
provides. The default is whichever meets REQ-050 with the best sound.
- Why: which engines exist depends on the library version (CON-13); the choice of version is made in the
  specification.
- Priority: Must
- Acceptance: both settings play the benchmark tunes on the host; the on-device run records the speed of each,
  and the default is the one the specification names from those figures.

**REQ-052 Sample rate.** The sample rate can be set, with at least 44,100 and 48,000 Hz. The default matches
what the device's sound system runs at.
- Why: asked for at the start. A rate different from the sound system's makes the system convert it, which
  costs processor time.
- Priority: Should
- Acceptance: each setting produces output at that rate, checked on the host from the rendered stream.

**REQ-053 Say when it cannot keep up.** If dropouts keep happening, the app tells the user and offers the
lighter engine, and does not just stutter.
- Why: a heavy tune on the accurate engine is the likely way REQ-050 fails in daily use, and the user should
  be told what helps.
- Priority: Should
- Acceptance: with a fake engine made too slow, the message appears within 10 s‡ and accepting it switches
  engine.

**REQ-054 The screen keeps up.** While a tune plays, a key press shows its effect within 100 ms‡ in 95 of 100
cases.
- Why: rendering sound competes with the screen for a slow processor. A player that plays well and ignores
  keys is not usable.
- Priority: Must
- Acceptance: on the reference device, the app logs the time from each key event to the next finished screen
  update during the benchmark; 95 % are within the budget.

**REQ-055 Quick to open.** From choosing the app in the launcher to a browser that responds takes 2 s‡ or
less when the app has run before. Starting never waits for the network.
- Why: the launcher shows nothing while an app starts (CON-03), so a slow start looks like a hang.
- Priority: Must
- Acceptance: timed on the reference device with networking off; the app logs its own start-up time to the
  results file.

### 4.6 Keys and getting around

**REQ-056 The engine is sidplayfp's.** Tunes are played by the sidplayfp engine, libsidplayfp. Another
engine is used only if the specification shows it is better for this device, and that change needs the
owner's approval.
- Why: asked for directly: "it should be using sidplayfp as player, if possible, unless there is a better
  player option". No better option was found: it is the engine most SID players are built on, and from
  version 3.0 it includes a light engine meant for slow processors (CON-13).
- Priority: Must
- Acceptance: the About screen names libsidplayfp and its version, and the app's renders equal sidplayfp's
  for the same tune and settings (REQ-163).

**REQ-060 Keyboard only.** Every function can be reached with the device's own 46 keys. Nothing needs a
pointer, a touch screen or another keyboard.
- Why: the device has no touch screen and no pointer (CON-01).
- Priority: Must
- Acceptance: the simulator tests reach every screen and every setting using only key codes the device
  produces (CON-06).

**REQ-061 Keys work the way CardputerZero apps work.** The app follows the conventions of CON-06 and CON-07:
arrows with `fn` and, outside text entry, F/X/Z/C without it; Enter to confirm; a short ESC to go back; keys
4 to 8 for the five actions labelled along the bottom of the screen; `fn`+Q, `fn`+W and `fn`+E for play-pause,
previous and next on every screen; `fn`+H for help; Home, End, Page Up and Page Down in lists.
- Why: asked for directly, and it is what makes the app feel like it belongs on the device.
- Priority: Must
- Acceptance: the specification's key map lists every key on every screen; a simulator test presses each one
  and checks the result; none of the conventions above is used for something else.

**REQ-062 Leaving.** Holding ESC leaves the app from any screen. The app shows that this is about to happen
and exits by itself before the launcher's 3-second limit (CON-04). Pressing ESC repeatedly always reaches the
top screen.
- Why: the launcher kills an app that does not leave in time (CON-04), and nobody should ever be stuck in a
  screen.
- Priority: Must
- Acceptance: from the deepest screen in the specification, repeated short presses reach the top; holding ESC
  for 1 s‡ shows the hint and exits with status 0 before 3 s.

**REQ-063 Help on every screen.** `fn`+H shows the keys that work on the current screen and closes the same
way.
- Why: the conventions ask for it (CON-07), and the screen has no room to label every key.
- Priority: Must
- Acceptance: each screen's help lists exactly the keys in the specification's key map for that screen; a test
  compares the two.

**REQ-064 Typing.** Where text is typed (search, playlist names) the letter keys type, `sym` and `shift` give
symbols and capitals, Backspace deletes, and the arrows need `fn`.
- Why: search and playlist names need text, and the same keys are arrows elsewhere (CON-07).
- Priority: Must, if search or playlists are in
- Acceptance: typing "hub 1985" with the device's key codes produces that text in the search field in a
  simulator test.

**REQ-065 Screenshots.** `fn`+J saves the screen as a 320×170 PNG in `~/Pictures/Screenshots`.
- Why: the template does this, the store needs such screenshots (CON-17), and the simulator tests use the same
  function.
- Priority: Should
- Acceptance: after the key press a PNG of 320×170 exists in the folder and the app says where it is.

### 4.7 Settings and what is remembered

**REQ-070 Settings are kept.** Settings are stored in `$XDG_CONFIG_HOME/sidplayzero/`, or
`~/.config/sidplayzero/` when that variable is not set, and are in force again at the next start.
- Why: asked for at the start.
- Priority: Must
- Acceptance: change each setting, stop the app, start it: each has the value set.

**REQ-071 Back where you were.** On starting, the browser opens the folder that was open last, with the same
tune highlighted. If that folder no longer exists, it opens the nearest parent that does.
- Why: asked for at the start. Finding one's place again among 61,157 tunes is slow.
- Priority: Must
- Acceptance: stop the app three levels down in a test tree and start it: same folder and highlight. Remove
  the folder and start it: the parent opens.

**REQ-072 Carry on playing.** On starting, the app can resume the tune that was playing when it was closed,
from its start, if a setting says so. The setting is off unless the user turns it on.
- Why: convenient for some people and startling for others, so it is off unless asked for.
- Priority: Could
- Acceptance: with the setting on, the tune and subtune that were playing start again.

**REQ-073 Never left half-written.** Settings, playlists and the remembered position are written so that
stopping the app at any instant, even with SIGKILL or a power cut, leaves either the old file or the new one,
complete.
- Why: the launcher kills apps that do not exit in time (CON-04), and the device has a hard power switch.
- Priority: Must
- Acceptance: a test kills the app at random moments during 200 saves; every file read back afterwards is
  complete and valid.

**REQ-074 Bad settings do not stop it starting.** A settings file that is damaged, or written by a newer
version, makes the app start with defaults for whatever it cannot read and say so. The damaged file is kept
under another name.
- Why: a settings file can be damaged by a power cut or a slip while editing it, and an app that will not
  start cannot be repaired from the device.
- Priority: Must
- Acceptance: start with a file of random bytes: the app starts, shows the notice, and the original bytes are
  in a file beside it.

**REQ-075 The user's files are the user's.** Playlists and favourites are stored under
`$XDG_DATA_HOME/sidplayzero/` (or `~/.local/share/sidplayzero/`), and things that can be rebuilt, such as the
search index, under `$XDG_CACHE_HOME/sidplayzero/` (or `~/.cache/sidplayzero/`). Removing the package deletes
none of them, and none of the collection.
- Why: reinstalling or upgrading must not cost anyone their playlists, or a 395 MB download.
- Priority: Must
- Acceptance: after `apt remove sidplayzero` the settings, playlists and collection are still there.

### 4.8 Getting HVSC onto the device

**REQ-080 Download HVSC from inside the app.** From the empty-collection screen or the settings, the user can
fetch the complete current HVSC with no more than three‡ key presses and without a terminal. The app
downloads the archive, checks it arrived whole, unpacks it and puts it in the collection folder.
- Why: asked for directly: "downloading HVSC locally should be easy".
- Priority: Must
- Acceptance: on the reference device with an empty home folder and Wi-Fi, the steps on the screen end with
  HVSC browsable and playing. On the host, the same code runs against a small test archive served locally.
- Open point: whether the archive can be unpacked within the device's memory is not known yet (R1 in
  section 6). If it cannot, this requirement comes back here to be changed.

**REQ-081 Progress, cancel, try again.** The download shows what it is doing and how far it has got, and can
be cancelled at any point. A download that was interrupted can be continued or started again cleanly. Until
the new collection is complete, the old one stays in place and usable.
- Why: 81 MB over Wi-Fi and 61,157 files to unpack take minutes on this device, and Wi-Fi drops out.
- Priority: Must
- Acceptance: cancelling at 50 % leaves the previous collection untouched and no leftover files larger than
  the partial download; cutting the network mid-way and retrying completes the download.

**REQ-082 Check there is room.** Before downloading, the app checks that the storage has room for the archive
and the unpacked collection together, with a margin, and says how much is needed if not.
- Why: a full microSD card can stop the whole system from working.
- Priority: Must
- Acceptance: with a test file system too small, the download does not start and the message names the
  amount needed and the amount free.

**REQ-083 Where it downloads from can be changed.** The addresses for the complete archive and for single
files are settings, with HVSC's own site and the mirror it links to as defaults.
- Why: the archive is on a mirror that HVSC does not run (CON-15), and addresses change.
- Priority: Must
- Acceptance: pointing the settings at a local test server makes all downloads go there and nowhere else.

**REQ-084 Newer releases.** The app shows which HVSC release is installed. When asked, it checks whether a
newer one exists and can install it by downloading the complete archive again. It does not apply HVSC's
update packs.
- Why: two releases a year; the update packs need HVSC's own tool, which is not available for the device
  (CON-15).
- Priority: Should
- Acceptance: with release 84 installed and a test server offering 85, "check for a newer release" reports 85
  and installing it leaves 85 in place; playlists still resolve (REQ-102).

**REQ-085 HVSC's terms are shown.** Before the first download the app shows where the collection comes from
and HVSC's copyright notice, and the user confirms.
- Why: the tunes are other people's copyrighted work, offered for private enjoyment (CON-16).
- Priority: Must
- Acceptance: the download does not start until the notice has been shown and confirmed; the notice can be
  read again from the About screen.

**REQ-086 Works without a network.** Everything except downloading works with no network. The app contacts
the network only when the user starts a download, a fetch or a check, and never by itself.
- Why: a pocket computer is often offline, and an app that goes online by itself owes its users and the store
  an explanation.
- Priority: Must
- Acceptance: a host test runs every other feature with networking forbidden and counts zero attempts to
  connect; starting the app makes no connection.

**REQ-087 No music and no ROMs in the package.** The package contains no part of HVSC, no other tunes and no
C64 ROMs. Test fixtures in the repository are tunes written for this project or generated by it.
- Why: decided at the start, and CON-16.
- Priority: Must
- Acceptance: a check of the built `.deb` and of the repository finds no `.sid` file that is not one of the
  project's own fixtures, and no ROM images.

### 4.9 Searching

**REQ-090 Search the collection.** The user can type words and get the tunes whose title, author, released
line or path contain words starting with them, in any order and without regard to case, as HVSC's own site
search does ("hub 1985" finds Rob Hubbard's tunes of 1985). A result can be played, or opened in the browser
at its folder. The search works without a network.
- Why: asked for directly: "search and play from HVSC".
- Priority: Must
- Acceptance: on a test collection of known tunes, a table of queries gives exactly the expected results; on
  the reference device with HVSC #85, results for a two-word query appear within 500 ms‡.

**REQ-091 The index builds itself.** What the search needs is built in the background from the tunes
themselves, with progress shown. Browsing and playing work meanwhile. The index is kept between runs, rebuilt
when the collection changes, and building it resumes if it was interrupted. On the reference device the
first build of HVSC #85 takes 5 minutes‡ or less and causes no underruns in a tune playing at the same time.
- Why: reading 61,157 files takes minutes on a microSD card. It cannot be done at every start, or while the
  user waits.
- Priority: Must, if REQ-090 is
- Acceptance: on the host, an index of a generated 61,157-tune tree is built, interrupted, resumed and
  checked against the tree; on the device the build time and the underrun count go into the results file.

**REQ-092 Narrow the results.** Results can be limited by SID model, by PAL or NTSC and by year.
- Why: HVSC's own search offers these filters.
- Priority: Could
- Acceptance: each filter removes exactly the non-matching tunes of the test collection.

**REQ-093 Search HVSC online.** With a network, the user can search HVSC's site for tunes that are not on the
device and fetch one (REQ-104).
- Why: it would let the app be useful without the full collection. It depends on a search address on HVSC's
  site that is not documented or offered for other software to use.
- Priority: Could, and only if HVSC's maintainers agree
- Acceptance: against a test server that answers as HVSC's site does, a search lists the tunes and fetching
  one makes it playable.

### 4.10 Playlists and favourites

**REQ-100 Favourites.** One key press marks or unmarks the current tune, with its subtune, as a favourite. The
favourites are a list that can be played like any other.
- Why: listed as wanted at the start. It is the simplest playlist and the one most people use.
- Priority: Should
- Acceptance: marking a tune adds it once; the mark shows in the browser and in the now-playing view; the list
  survives a restart.

**REQ-101 Playlists.** The user can make playlists, name, rename and delete them, add the highlighted or
playing tune and subtune to one, remove entries and change their order, and play a playlist with the play
modes of REQ-016.
- Why: asked for directly.
- Priority: Should
- Acceptance: a simulator test makes a playlist of three tunes, reorders it, plays it through and deletes it,
  using keys only.

**REQ-102 Entries point at tunes; they do not copy them.** An entry refers to a tune by its HVSC path and
subtune, and also records the MD5 of the file. If the path no longer exists, the tune is found again by its
MD5. A tune outside the collection is referred to by its full path.
- Why: an HVSC path means the same on every device, so playlists can be shared; HVSC renames folders between
  releases (CON-15).
- Priority: Should
- Acceptance: after a folder in the test collection is renamed, every entry of a playlist that pointed into it
  still plays.

**REQ-103 Missing tunes do not break a playlist.** An entry whose tune is not on the device is shown as
missing and skipped when playing. It stays in the playlist.
- Why: playlists outlive the collection they were made with: releases change, and playlists get shared.
- Priority: Must, if playlists are in
- Acceptance: a playlist of a present, a missing and a present tune plays the two present ones and shows the
  middle one as missing.

**REQ-104 Fetch single tunes.** A missing entry that has an HVSC path can be downloaded by itself and put in
its place in the collection folder. "Fetch what is missing" does this for a whole playlist, one file at a
time, up to a limit of 200‡ files per request. The app never downloads tunes the user has not asked for.
- Why: asked for directly: playlists that download single files, or just link to files in the collection.
  HVSC's site serves single files (CON-15); fetching them in bulk would be misuse of it.
- Priority: Should
- Acceptance: against a local test server, fetching a missing entry makes it playable and stores it at its
  HVSC path; a playlist with 250 missing entries fetches 200 and says so; the server's log shows only the
  files asked for.

**REQ-105 Import playlists from other SID players.** The app can read playlists that other players write and
turn them into its own:
- M3U and M3U8 as written by VICE's VSID, sidplaywx, Phosphor, SIDPLAY for macOS, Audacious and MPD,
  including the ways those players mark a subtune (`#PHOSPHOR:song=N`, a trailing `?N`, `/tune_NNN.sid`,
  a leading `{N}`);
- JSidplay2 favourites (`.js2`, and its `.json` export);
- Sidplay/w and SIDPLAY 2/W playlists (`.spl`), which carry the subtune and play time.

A path is matched to a tune in the collection whether it is absolute, relative to the playlist file, or an
HVSC path with or without `C64Music` in front, and with `/` or `\` between its parts. Entries that cannot be
matched are listed for the user and kept as missing entries. The file being imported is never changed.
- Why: asked for as something to check. The check found that sidplayfp itself has no playlists, that there is
  no single SID playlist format, and that these three cover nearly everything in use. The formats left out are
  in section 5.
- Priority: Should for M3U and M3U8, Could for the other two
- Acceptance: a sample file of each kind, taken from the format's own documentation or written by the player,
  imports into a playlist with the expected tunes, subtunes and order; a file with one unknown path imports
  the rest and reports the one.

**REQ-106 Export.** A playlist can be written as an M3U8 file of HVSC paths.
- Why: so that a playlist made on the device can be used in other players.
- Priority: Could
- Acceptance: exporting a playlist and importing the file gives the same playlist.

**REQ-107 Playlists are plain text.** A playlist is stored as a text file that a person can read and edit,
in a format the project documents.
- Why: so playlists can be made on another computer, kept in version control and repaired by hand.
- Priority: Should
- Acceptance: a playlist written by hand from the format's description loads and plays.

### 4.11 Real SID chips: USBSID-Pico

**REQ-110 Room for other ways of making sound.** The part of the app that produces sound can be replaced by
one that drives SID hardware, without changing the browser, the playlists or the screens.
- Why: "possibly support USBSID" was asked for. Keeping the door open costs little now; adding it later to a
  design that assumed software sound would cost a lot.
- Priority: Should
- Acceptance: the specification defines the boundary, and the host tests run the whole player against a fake
  on the far side of it.

**REQ-111 Play on a USBSID-Pico.** With a USBSID-Pico attached and chosen in the settings, tunes play on it.
Subtunes, song lengths and moving on by itself work as usual. Unplugging it during a tune stops playback with
a message and does not crash the app. Software sound stays the default.
- Why: asked for as a possibility. It needs an engine library of version 2.16 or newer, which Debian trixie
  does not have (CON-13), so it decides how the library is shipped.
- Priority: Could
- Acceptance: on the reference device with a USBSID-Pico in the USB-A port, the on-device checklist plays a
  tune on it, steps through subtunes, and survives unplugging.

**REQ-112 No root for the hardware.** The package gives the logged-in user access to the USBSID-Pico with a
udev rule that runs no programs. The app never asks for a password.
- Why: store rules (CON-17) and plain good practice.
- Priority: Could, with REQ-111
- Acceptance: after installing the package and plugging the device in, the app opens it as the normal user.

### 4.12 Starting, stopping, and staying safe

**REQ-120 Stops when told.** On SIGTERM or SIGINT, as on a held ESC, the app stops the sound, saves its state
and exits within 1 s‡.
- Why: the launcher sends SIGTERM and kills what has not gone 3 s later (CON-04).
- Priority: Must
- Acceptance: a test sends SIGTERM during playback: the process exits with status 0 inside the budget and the
  saved state is current.

**REQ-121 Survives being killed.** After being killed at any moment, the app starts normally, and what it
remembers is at most 10 s‡ old.
- Why: the launcher kills apps (CON-04), and the device has a power switch that cuts power (CON-01).
- Priority: Must
- Acceptance: a test sends SIGKILL at random moments during use and restarts the app 50 times; every start
  succeeds and restores a position at most 10 s old.

**REQ-122 Bad files do no harm.** Tune files, playlists, song length and STIL files and archives that are
truncated, oversized or deliberately malformed do not crash the app, do not make it hang for more than 2 s‡,
and do not make it write anywhere it should not.
- Why: the files come from the internet, and the engine has had a memory-safety bug in exactly this area
  (CON-13).
- Priority: Must
- Acceptance: a set of damaged files of each kind, kept in the repository, is loaded by the host tests under a
  memory checker with no error, crash or timeout.

**REQ-123 Unpacking is contained.** When unpacking an archive the app refuses entries with absolute paths,
with `..` in them, and links and device files. It stops if the total size or the number of files goes beyond
a limit. It unpacks only into a new folder of its own and moves the result into place when complete.
- Why: an archive from the network is untrusted, and unpacking is the classic way for a file to be written
  where it should not be.
- Priority: Must, with REQ-080
- Acceptance: test archives with each of those tricks are rejected with nothing written outside the new
  folder.

**REQ-124 Downloads are safe.** Downloads use HTTPS with the certificate checked, follow redirects only to
HTTPS addresses, and stop at a size limit. Nothing downloaded is ever run. The app sends no information about
the user or the device and has no telemetry.
- Why: the download runs on someone's own device, on networks the app knows nothing about.
- Priority: Must
- Acceptance: a test server with a bad certificate, a redirect to plain HTTP and an endless file is refused in
  each case; a capture of a download shows the request and nothing else.

**REQ-125 Ordinary user only.** The app runs as the normal user. The package has no setuid files and no
service, and nothing in it runs as root after installation.
- Why: the store rejects anything else (CON-17).
- Priority: Must
- Acceptance: the store's own package checks pass on the `.deb`; the app starts and works as a user who is in
  no special groups beyond those of the stock image.

**REQ-126 An engine without known holes.** The engine library the app ships with, or depends on, has no known
unfixed vulnerability in reading tune files. The About screen shows the library's version.
- Why: CVE-2026-111148 is fixed in upstream 3.1.2; whether the 2.12.0 in Debian trixie is affected has not
  been checked yet (R3).
- Priority: Must
- Acceptance: the specification names the library version and how it reaches the device, with the evidence
  that the known issues do not apply to it; the About screen shows that version.

**REQ-127 A good neighbour.** The app does not take the keyboard for itself alone, releases the sound device
when it exits, and leaves the system's volume, brightness and other settings as the user set them.
- Why: the launcher and other apps share the keyboard and the sound system with it (CON-06, CON-08).
- Priority: Must
- Acceptance: on-device checklist: after leaving the app the launcher works at once, its key sounds play, and
  the volume and brightness are what they were.

**REQ-128 Dark screen, music on.** If the system dims or locks the screen, the music keeps playing. If the
system does not dim the screen while the app runs, the app can darken it itself after a time without key
presses, and any key brings it back.
- Why: asked for as a possibility. What the system does here is not known yet (CON-10), nor whether an app may
  change the backlight.
- Priority: Could
- Acceptance: on-device checklist: leave a tune playing for two minutes; it plays to its end whatever the
  screen does.

### 4.13 The package and how it is offered

**REQ-140 One package that installs cleanly.** The result is one `.deb` for arm64. On a stock system image,
`sudo apt install ./sidplayzero_<version>_arm64.deb` installs it together with anything it depends on, the
launcher shows it as "SidPlayZero" with its icon, and it starts. Removing it removes everything it installed.
- Why: the delivery asked for at the start: a `.deb` and how to install it.
- Priority: Must
- Acceptance: on the reference device, install, start from the launcher, remove; `dpkg -L` listed nothing
  outside the places allowed by CON-17 and nothing is left behind except the user's files (REQ-075).

**REQ-141 Depends only on what Debian has.** Anything the package depends on is in Debian trixie's main
archive. Anything else it needs is inside the package, under folders named after it.
- Why: the device installs dependencies from Debian; the store has no other source.
- Priority: Must
- Acceptance: installing on a fresh trixie arm64 system with only Debian's main archive enabled succeeds.

**REQ-142 Ready for the store from the first build.** The package passes the store's install-path and
package checks, which run in the project's own checks on every change.
- Why: fitting the layout now is free; changing it after people have installed the app is not.
- Priority: Must
- Acceptance: the store's validator scripts, run on each built `.deb`, report nothing.

**REQ-143 Generic.** Nothing in the package or the repository names a particular person's device, user
account, host name or network. Anything that differs between devices comes from a setting or an environment
variable.
- Why: asked for directly, and the repository is public.
- Priority: Must
- Acceptance: a check on every change searches the repository and the package for a list of private terms
  kept outside the repository, and for private network addresses, and finds none.

**REQ-144 Licence.** The app is released under the GNU General Public License, version 3 or later
(`GPL-3.0-or-later`). The package includes the licence text and the notices of everything built into it.
Every source file says which licence it is under. Nothing that is GPL version 2 only is built into the app.
- Why: the owner's decision of 25 November 2026: GPL-3 if possible. It is possible, and it is the only GPL
  version that also fits the USBSID driver (CON-21).
- Priority: Must
- Acceptance: the licence file and third-party notices are in `/usr/share/doc/sidplayzero/`; a check finds an
  SPDX line in every source file; the engine library is built without its exSID backend; a check of the
  licences of everything linked in finds none that GPL-3 cannot be combined with.

**REQ-145 Everything the store asks for is prepared.** The manifest's store section is complete and valid:
title, summary of 80 characters or fewer, description, category, the seven permission declarations (network:
yes; additional hardware: yes if REQ-111 is in), icon, and screenshots taken from the simulator.
- Why: publishing is a possible last step. Preparing for it now keeps that step small.
- Priority: Should
- Acceptance: the store's metadata check passes on the manifest.

**REQ-146 Nothing is published without being asked.** The project's scripts and automation never log in to
the store or publish to it. That happens only by hand, when the owner asks for it.
- Why: decided at the start.
- Priority: Must
- Acceptance: no workflow or script in the repository calls `czdev login` or `czdev publish`; a check enforces
  it.

**REQ-147 Small.** The `.deb` is no larger than 20 MB‡.
- Why: the store's web upload stops at 80 MB, and downloads to the device are slow.
- Priority: Should
- Acceptance: the size of each built package is checked.

**REQ-148 About.** An About screen shows the app's version, the engine library and its version, the licences,
where the source code is, and HVSC's notice (REQ-085).
- Why: users and store reviewers need to see what they are running and on what terms, and the GPL asks that
  the licence and the source be offered.
- Priority: Must
- Acceptance: a simulator screenshot of the About screen shows the same version as the package.

**REQ-149 Runs on a desktop too.** The same app builds and runs on a Linux desktop in a window of the
device's size, for development and for the tests.
- Why: CON-11; it is what makes most of the testing possible without the device.
- Priority: Must
- Acceptance: one documented command builds it and one runs it on a Linux machine with no CardputerZero
  attached.

### 4.14 Knowing that it works

**REQ-160 Three levels of tests.** The app is tested at three levels: on the host, where the logic runs
against a fake engine, a fake sound system, test folders and a local test server; in the simulator, where the
real screens at 320×170 are driven by key presses and compared by screenshot, with no display needed; and on
the device, for speed, sound and the real keyboard.
- Why: asked for at the start. Most faults can be found without the device, and the rest need a person
  present.
- Priority: Must
- Acceptance: each level exists and can run one passing and one deliberately failing test (this is the Phase 3
  gate).

**REQ-161 One command each.** Each level runs with one command. The host and simulator levels run
automatically on every proposed change.
- Why: tests that are awkward to run do not get run.
- Priority: Must for the commands, Should for running them automatically (section 7)
- Acceptance: the commands are in the README and do what it says on a clean checkout.

**REQ-162 The device run is one sitting.** The tests on the device are one script and a short checklist that
a person runs once, at the device. The script writes a results file that holds no host name, user name or
network address.
- Why: the owner must be present for anything that runs on the device, so it has to be worth one visit.
- Priority: Must
- Acceptance: the results file from a run on the reference device contains every measurement the
  specification asks for and passes the private-terms check of REQ-143.

**REQ-163 The same tune always renders the same.** With the same tune and settings the app produces the same
samples every time, so that a test can compare its output with a stored reference.
- Why: it is the only practical way for a test to know that sound is right. It needs the engine's power-on
  delay fixed (CON-20).
- Priority: Must
- Acceptance: a host test renders a fixture tune twice and compares the bytes; a second test compares them
  with a reference made by `sidplayfp -w` with the same settings.

**REQ-164 Everything traces back.** Every requirement here is covered by at least one item of the
specification, and every such item by at least one test. A script checks this.
- Why: it is how the documents stay a true description of the app.
- Priority: Must
- Acceptance: the script fails when a REQ has no SPEC item or a SPEC item has no test, and passes on the real
  documents.

## 5. Left out of version 1.0

| What | Why |
|---|---|
| Playing on after leaving the app | The launcher ends an app's whole process group when the user leaves it (CON-04). It would need a separate background service, a different design and a store permission |
| Applying HVSC's update packs | They need HVSC's own update tool, which runs a script, must be run exactly once and in order, and has no build for the device. Downloading the complete archive again is 81 MB twice a year (REQ-084) |
| HVSC or C64 ROMs inside the package | Copyright (CON-16); decided at the start |
| Reading SIDPLAY for macOS playlists in its own format | A binary Apple archive format. That player can export M3U, which REQ-105 reads |
| Reading foobar2000 `.fpl` and DeaDBeeF `.dbpl` playlists | Binary formats without a published description |
| Importing from DeepSID and ACID 64 Player Pro | DeepSID keeps playlists on its server and has no export. No playlist format was found for ACID 64 |
| PLS and XSPF playlists | No SID player was found that writes them |
| Other SID hardware: exSID, HardSID, SIDBlaster, network SID devices | Not asked for. REQ-110 leaves the door open |
| Music formats other than SID | A different app |
| Compute's Sidplayer files (`.mus`, `.str`) | HVSC contains none. The engine can play them, so this is easy to add later |
| Saving a tune as a WAV file | `sidplayfp -w` does it already, and it was not asked for |
| Languages other than English | Can be added later; the store accepts translated listings at any time |

## 6. Open points for the specification

These are not for the owner to decide. They are what Phase 2 has to settle, listed here so the risks are in
view when the priorities are set.

| Id | Open point | How it gets settled |
|---|---|---|
| R1 | Can the device unpack the complete HVSC archive? It is a "solid" 7-Zip archive; some of HVSC's archives have used compression methods that need a lot of memory or that some unpackers lack | Reach the mirror, inspect the archive, unpack it with the candidate library under a 256 MB limit. The fall-backs are an external `7z` program as a dependency, or fetching single files |
| R2 | How fast are the engines on the device? | The benchmark part of `tools/device-probe.sh` |
| R3 | Which engine library, and how does it reach the device: Debian's 2.12.0 as a dependency (has reSID; no USBSID; not checked for CVE-2026-111148), or a pinned newer release built into the package (has USBSID and the fixes; reSID is replaced by sidlite)? | Decided in the specification from R2 and from whether REQ-111 is in |
| R4 | Link the engine library, or drive the `sidplayfp` program? | The evidence so far favours linking: the app is an ordinary process and may use threads (CON-03), and only the library lets the app fix the power-on delay (CON-20), mute voices and switch subtunes directly. The specification records the choice |
| R5 | How to hand sound to the system: PulseAudio client library, ALSA, or a wrapper such as miniaudio (which the stock Music app and newer sidplayfp use) | Specification, checked against the sink format from the probe |
| R6 | How long does indexing 61,157 tunes on a microSD card take, and in what form is the index kept? | A prototype measured on the device |
| R7 | Does the screen dim or lock while an app runs, and may an app change the backlight? | Probe and a look at the device |
| R8 | Do the stock user's groups allow the framebuffer, the keyboard and USB devices? | Probe |
| R9 | One header of the USBSID-Pico driver says "GPL version 2" while the rest of the driver says "version 3 or later" (CON-21). Taken literally the two cannot be combined | Ask the driver's author to make the header match before REQ-111 ships. It does not affect a build without USBSID |

## 7. What the owner needs to decide

Already decided, and built into the requirements above:

| Decision | Where it shows |
|---|---|
| The app is called SidPlayZero; its package and program are `sidplayzero` | REQ-140 |
| It is tested locally first: host, then simulator, then the device. Nothing is published unless asked for | REQ-146, REQ-160 |
| No HVSC and no ROMs in the package. The collection is in `~/C64Music` unless `HVSC_BASE` or a setting says otherwise | REQ-001, REQ-087 |
| The engine is sidplayfp's unless something better is shown | REQ-056 |
| The licence is GPL-3 | REQ-144 |
| The source is public, at github.com/mselin/sidplayzero | REQ-143 |

Still open:

1. **Priorities.** Are the proposed priorities right? In particular the set of Musts, which defines 1.0:
   browsing, playing with song lengths, the now-playing view, volume and output, real-time speed, the key
   conventions, remembered settings and position, downloading HVSC in the app, local search, the safety
   requirements, and the package. Playlists, favourites, single-file fetching, playlist import, voice mute
   and the filter switch are proposed as Should. USBSID-Pico, STIL notes, the voice display and online search
   are proposed as Could.
2. **Licence wording.** I have written GPL-3 as `GPL-3.0-or-later`, the form most GPL apps in the store use
   and the one the USBSID driver uses. Say if you want `GPL-3.0-only` instead.
3. **USBSID-Pico.** Is there one to test with? If REQ-111 is to be in 1.0, it settles R3 in favour of
   building a newer engine library into the package, and R9 has to be cleared with the driver's author.
4. **Playlists from other players.** Which players do you have playlists from? Real files make better test
   fixtures than samples from documentation, and they would settle which formats are worth the Could.
5. **Playing on after leaving the app** is proposed as left out (section 5). Agreed?
6. **Online search of HVSC** (REQ-093) uses an undocumented address on HVSC's site. Leave it at Could and ask
   HVSC's maintainers first, or drop it?
7. **The device probe.** Please run `tools/device-probe.sh --bench --fetch` on the device once and send back
   the file it writes. It changes nothing, and it settles CON-02, CON-05, CON-08, CON-14, R2, R7 (in part) and
   R8. It needs the `sidplayfp` package for the benchmark (`sudo apt install sidplayfp`). While you are there:
   start any third-party app, leave it for a minute, and note whether the screen dims or locks (CON-10).
8. **Automatic checks.** Should the host and simulator tests run on GitHub Actions for every pull request?
   I recommend yes.
9. **The device's name.** I have kept the reference device's host name and user name out of this repository,
   since it is public. Say if that is not what you want.

## 8. Sources

Read on 25 November 2026.

| What | Where | Version |
|---|---|---|
| Device hardware, usage guide, image and ADB guides | docs.m5stack.com: `/en/CardputerZero`, `/en/compute/cardputerzero/usage`, `/image`, `/adb` | as of that date |
| Development guidelines, key specification, submission guide | cardputer.cc: `docs/en/CP0_dev.md`, `CP0_keys.md`, `CP0_submit.md` | as of that date |
| Store registry | cardputer.cc: `generated/registry.json` | 191 apps |
| App template | github.com/CardputerZero/Template | commit `477fa6d` |
| AppBuilder and `czdev` | github.com/m5stack/CardputerZero-AppBuilder | commit `042356c` |
| Launcher | github.com/CardputerZero/launcher | commit `d59f159` |
| Store validators | github.com/CardputerZero/packages | commit `21ea24c` |
| Image builder | github.com/CardputerZero/pi-gen | commit `3b0e6f6` |
| Stock Music app | github.com/CardputerZero/Music | commit `b4ad7cb` |
| Keyboard driver and device tree | github.com/m5stack/m5stack-linux-dtoverlays | commit `a3fd3e8` |
| Sysroot | CardputerZero/M5CardputerZero-UserDemo release `v0.0.4`, `sdk_bsp.tar.gz` | 0.0.4 |
| Engine library and player | github.com/libsidplayfp/libsidplayfp, github.com/libsidplayfp/sidplayfp | commits `932a602`, `176ff06` |
| Debian package versions | deb.debian.org: `dists/{bookworm,trixie,forky,sid}/main/binary-arm64/Packages.xz` | as of that date |
| USBSID-Pico | github.com/LouDnl/USBSID-Pico | as of that date |
| HVSC | hvsc.c64.org: downloads page, `download/C64Music/DOCUMENTS/` | release #85 |
| Playlist formats | Source code and documentation of VICE, sidplaywx, Phosphor, SIDPLAY for macOS, Audacious, MPD, JSidplay2, Sidplay/w and DeepSID | as of that date |
