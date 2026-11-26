# SidPlayZero

A SID player for the [M5Stack CardputerZero](https://docs.m5stack.com/en/CardputerZero), a pocket Linux
computer with a 320×170 screen and a keyboard. It browses a collection of Commodore 64 music on the device,
normally the [High Voltage SID Collection](https://hvsc.c64.org) (HVSC), and plays the tunes with the
[sidplayfp](https://github.com/libsidplayfp/libsidplayfp) engine, all from the device's own keys.

## Where it stands

**The app is not finished, and there is no release to install yet.** What can be built today is a stub: a
program that starts, draws one screen and leaves again. The tests of the whole design are written and fail
against it, as they should until the app is written to pass them.

| Done | Not done yet |
|---|---|
| What the app has to do ([requirements](docs/requirements.md)) | The app itself |
| How it does it ([the design](docs/spec.md), reviewed independently) | A release |
| The machinery its tests run on, and the tests of every item of the design | |

Everything below under "Using it" describes the app as its design has it. It is there so that the plan can
be read in one place, and it will be corrected to what the app really does when the app exists.

## Using it

### What you need

- An M5Stack CardputerZero with its stock system image (Debian 13, arm64).
- Wi-Fi, if the app is to fetch music itself. It plays what is on the device without a network.

### Installing and removing

SidPlayZero comes as one package. Copy it to the device and install it there:

```bash
sudo apt install ./sidplayzero_<version>_arm64.deb
```

It then appears in the launcher as **SidPlayZero**. To remove it:

```bash
sudo apt remove sidplayzero
```

Removing it leaves your music, playlists and settings where they are.

### Music

The package contains no music. SidPlayZero looks for a collection in `~/C64Music`, or in the folder that the
environment variable `HVSC_BASE` names. There are two ways to fill it:

- **From inside the app.** With an empty collection, the first screen offers "Get HVSC". It shows HVSC's
  notice about the tunes' copyright, then downloads HVSC's lists (about 9 MB). After that every tune of
  HVSC is in the browser, and a tune is fetched from HVSC's site the first time it is played.
- **By hand.** Copy a `C64Music` folder, or any folder tree of `.sid` files, to `~/C64Music`.

### Keys

| Key | Does |
|---|---|
| `fn`+F, X, Z, C (or F, X, Z, C alone, outside text entry) | Up, down, left, right |
| Enter | Open the folder, play the tune, confirm |
| ESC | Back. Held for a second: leave SidPlayZero |
| 4, 5, 6, 7, 8 | The five actions named along the bottom of the screen |
| P | The now-playing screen |
| TAB | Settings |
| `fn`+H | Help: the keys of the screen you are on |
| `fn`+Q, W, E | Play or pause, previous tune, next tune |
| `fn`+A, S, D | Mute, volume down, volume up |

Every key on every screen is in [`docs/keymap.tsv`](docs/keymap.tsv).

### Where it keeps things

| What | Where |
|---|---|
| Settings | `~/.config/sidplayzero/settings.ini` |
| Playlists and favourites, as `.m3u8` files other players can read | `~/.local/share/sidplayzero/playlists/` |
| What it remembers between runs | `~/.local/state/sidplayzero/` |
| What it can make again (the search index, partial downloads) | `~/.cache/sidplayzero/` |

Tunes that need the Commodore 64's own ROMs play only if you have the ROM files and name them in Settings;
the app ships none.

## Building it

The app is being written; what builds today is the app as far as it goes. On Debian or Ubuntu you need
`cmake` 3.31 or newer, `ninja-build`, `g++`, `make`, `git`, `libsdl2-dev` and `libcurl4-openssl-dev`.

For a desktop, in a window of the device's size:

```bash
cmake --workflow --preset linux-x86-64
```

```bash
build/linux-x86-64/src/sidplayzero
```

Hold ESC for a second to leave it.

For the device, as a package (this also needs `g++-aarch64-linux-gnu`, and downloads the device's system
libraries, 435 MB, the first time):

```bash
SIDPLAYZERO_MAINTAINER="Your Name <you@your.domain>" cmake --workflow --preset cp0-cross-package
```

The package is then in `dist/`. Everything the build fetches from outside is named, with its version and
checksum, in [`cmake/pins.cmake`](cmake/pins.cmake).

## Documents

| Document | What it is |
|---|---|
| [`docs/requirements.md`](docs/requirements.md) | What the app has to do, and the facts about the device that this rests on |
| [`docs/spec.md`](docs/spec.md) | The design: decisions, structure, screens and keys, states, limits, and how each item is tested |
| [`docs/keymap.tsv`](docs/keymap.tsv) | Every key on every screen; part of the design |
| [`docs/spec-review.md`](docs/spec-review.md) | The independent review of the design, and what was done about each point |
| [`docs/findings.md`](docs/findings.md) | Things that turned out differently from what an earlier document said |
| [`docs/device-checklist.md`](docs/device-checklist.md) | The tests that are done by hand, at the device |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | How the project is worked on: its phases and gates, the tests and how to run them, the repository's checks |

## No music in this repository

SidPlayZero does not contain HVSC, any other tunes, or the Commodore 64's ROMs, and never will. The tunes in
HVSC are the copyright of their authors. The app downloads them to the device only when its user asks.

SidPlayZero is not part of HVSC or of M5Stack, and neither has endorsed it.

## Licence

SidPlayZero is free software, licensed under the GNU General Public License, version 3 or later. The full
text is in [`LICENSE`](LICENSE).
