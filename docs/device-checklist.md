# SidPlayZero device checklist

The device level of the tests (`spec.md`, SPEC-260) is one script, `tools/device-run.sh`, and this
checklist, done in one sitting of about an hour with the tester at the device. The script prints each
instruction and each question below, waits, and puts the answers in its results file. This page is the
same list, to read beforehand.

Before the sitting:

- Build the package and copy it and the script to the device: `tools/test device` says how.
- Run the script over SSH as the device's normal user, the one the launcher starts apps as. It uses `sudo`
  to install the package and to remove it at the end, and for nothing else.
- The app is always started by hand, from the launcher.

## The two canaries (Phase 3)

`./device-run.sh --prove PACKAGE.deb` is the device level's proof that it notices a failing check. It
installs the package, asks for one start of the app from the launcher and a held ESC, makes one check that
must hold and one that is built to fail, and removes the package again. About two minutes.

## The whole sitting (from Phase 5 on)

`./device-run.sh PACKAGE.deb`. The answer to each question is recorded under the name in the first column.

| Name | Step | What the tester does, or is asked |
|---|---|---|
| | 1 | Nothing: the script installs the package |
| | 2 | Start SidPlayZero from the launcher. Press each key the screen asks for, then hold ESC to leave |
| | 3 | Start it again and wait until it says Done. Hold ESC to leave |
| | 4 | Start it. With an empty collection folder: Enter, Enter sets HVSC up. Play `MUSICIANS/H/Hubbard_Rob/Commando.sid` |
| `sound_from_loudspeaker` | 4 | Does sound come from the loudspeaker? |
| `commando_shown` | 4 | Does the screen show Commando, Rob Hubbard and 3:55? |
| `download_everything` | 5 | On the HVSC screen: is "Download everything" offered? If it is and you want to, run it to its end |
| | 6 | Turn networking off. Start SidPlayZero and leave it with a held ESC, five times. Turn networking on |
| `volume_keys` | 7 | While a tune plays: do `fn`+S and `fn`+D change the level you hear, and `fn`+A mute it? |
| `brightness_keys` | 7 | Do `fn`+U and `fn`+I change the screen's brightness? |
| `headphones` | 7 | Plug headphones in and out twice. Does the tune keep playing, with gaps under a second? |
| | 8 | Start it and leave the device alone for about 35 minutes, until it says Done. Hold ESC to leave |
| `screen_after_two_minutes` | 9 | Start it, play a tune, and do not touch the device for two minutes. What did the screen do? |
| `tune_played_on` | 9 | Did the tune play on all the while? |
| `launcher_answers` | 10 | Hold ESC to leave. Does the launcher answer keys at once? |
| `launcher_sounds` | 10 | Do the launcher's key sounds play? |
| `levels_as_before` | 10 | Are the launcher's volume and the brightness what they were before? |
| | 11 | Nothing: the script removes the package, checks that your settings are still there, and writes the results file |

## The results file

The script writes one file in the folder it was run from. It holds what the app measured, the answers
above, and how many checks passed. Host names, user names and network addresses are replaced in it by
`[removed]`. Read it before you send it all the same.
