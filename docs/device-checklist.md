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

The script keeps the step it has reached, and the answers given, in `sitting.state` beside it. Run again
with the same package, it carries on from that step and does not ask again what was answered; so a step
that ends the SSH session (step 6), or a sitting that stops for any other reason, loses nothing.
`./device-run.sh --restart PACKAGE.deb` begins again from step 1. The key step, too, carries on at the
first key with no answer when SidPlayZero is started again.

| Name | Step | What the tester does, or is asked |
|---|---|---|
| | 1 | Nothing: the script installs the package |
| `launcher_shows_app` | 2 | Does the launcher show SidPlayZero, with its icon? |
| | 2 | Start SidPlayZero from the launcher. Press each key the screen asks for, then hold ESC to leave |
| `esc_hold_returns` | 2 | Did holding ESC bring the launcher back, within about a second? |
| | 3 | Start it again and wait until it says Done. Hold ESC to leave |
| | 4 | Start it. With an empty collection folder: Enter, Enter sets HVSC up. Play `MUSICIANS/H/Hubbard_Rob/Commando.sid` |
| `sound_from_loudspeaker` | 4 | Does sound come from the loudspeaker? |
| `commando_shown` | 4 | Does the screen show Commando, Rob Hubbard and 3:55? |
| `download_everything` | 5 | On the HVSC screen: is "Download everything" offered? If it is and you want to, run it to its end |
| | 6 | Turn networking off, which ends the SSH session and the script with it. Start SidPlayZero and leave it with a held ESC, five times. Turn networking on, connect again and run the script again just as before: it carries on |
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

## Which items of the specification the sitting settles

Each item of the specification that names the device level is settled by an answer above, by a line the
script writes itself, or by a measurement the app writes to the results file (`spec.md`, SPEC-240 and
SPEC-241). The repository's check holds this table to the specification: an item with the device level
that is missing here is reported.

| Item | Settled by |
|---|---|
| SPEC-005 | `audio_backend` reads PulseAudio |
| SPEC-043 | `folder_first_ms_max`, `folder_again_ms_max`, `rows_per_second_min`, `row_pause_ms_max`, `peak_memory_kb` |
| SPEC-046, SPEC-063 | `commando_shown`; `lengths_commando` reads 3:55.594 1:01.288 |
| SPEC-070 | `sound_from_loudspeaker`; `key_to_sound_ms_max` |
| SPEC-072, SPEC-242 | `speed_factor_accurate`, `speed_factor_light`, the `bench_N_` lines, `underrun_frames`, `cpu_percent` |
| SPEC-076 | `volume_keys`; `levels_as_before` |
| SPEC-078 | `headphones` |
| SPEC-104 | `brightness_keys`; `backlight_writable` |
| SPEC-106, SPEC-201 | `esc_hold_returns`; `exit_ms` |
| SPEC-111 | `key_codes_wrong` reads none |
| SPEC-112 | `key_to_screen_ms_p95` |
| SPEC-134, SPEC-229 | `package_files`, `package_removed_cleanly`, `user_files_kept_after_removal` |
| SPEC-140 | step 4 done; `hvsc_setup_s` |
| SPEC-145 | `download_everything`; `memory_check_mib`, `unpacker`, `card_free_mib`, `download_everything_offered` |
| SPEC-161 | `index_tunes`, `index_step_two_s`, `index_tunes_per_second`, `index_underrun_frames` |
| SPEC-162 | `search_ms_max` |
| SPEC-191, SPEC-192 | Only when the part `usbsid` is built, which it is not by default: a tune plays on a USBSID-Pico, its subtunes step, and unplugging it pauses with a notice, all as the normal user. These lines join the sitting when that part does |
| SPEC-200 | `start_ms`, five values |
| SPEC-206 | `launcher_shows_app`; `user_groups`, `framebuffer`, `keyboard`, `backlight_writable` |
| SPEC-207 | `screen_after_two_minutes`, `tune_played_on` |
| SPEC-222 | `launcher_shows_app`, `launcher_answers`, `launcher_sounds` |
| SPEC-240, SPEC-241, SPEC-260 | Every key of the two items' tables is in the results file, and every question above has an answer |
| SPEC-243 | After the sitting: a confirmation or a finding in `findings.md` for each budget, each naming its line of the results file |
| SPEC-250 | `./device-run.sh --prove`: one check passes and one fails as intended |
| SPEC-258 | `private_details_removed`; the results file passes the repository's check for private details |
| SPEC-262 | The twelve `engine_bench_` lines of `tools/device-probe.sh --engine-bench` |

## The results file

The script writes one file in the folder it was run from. It holds what the app measured, the answers
above, and how many checks passed. Host names, user names and network addresses are replaced in it by
`[removed]`. Read it before you send it all the same.
