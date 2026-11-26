#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# device-run.sh: the device level of SidPlayZero's tests (docs/spec.md, SPEC-260), for one sitting at a
# CardputerZero with its tester present. Copy this file and the .deb to the device and run it there, over
# SSH, as the device's normal user: the one the launcher starts apps as.
#
#   ./device-run.sh --prove PACKAGE.deb   the device level's two canaries (docs/test-framework-tasks.md,
#                                         TF-081): one check that must hold and one built to fail.
#                                         About two minutes, and one start of the app from the launcher.
#   ./device-run.sh PACKAGE.deb           the whole sitting, about an hour. It needs the app itself, which
#                                         does not exist before Phase 5.
#   ./device-run.sh --scrub FILE          takes private details out of FILE, as it does for its own results
#
# It installs the package with sudo, and removes it again at the end; it uses sudo for nothing else, and
# refuses to run as root. The app is always started by you, from the launcher. The script and the app
# talk through two files in the app's state folder: the script writes selftest.request, and the app
# appends what it measures to selftest.results.
#
# It writes one results file in the current folder. Host names, user names and network addresses are
# replaced in it by [removed] (SPEC-258). Read it before you send it all the same.

set -u

SUDO="${SPZ_DEVICE_SUDO:-sudo}"
WAIT="${SPZ_DEVICE_WAIT:-180}"          # seconds to wait for the app to answer a request
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/sidplayzero"
REQUEST="$STATE/selftest.request"
RESULTS="$STATE/selftest.results"
ANSWERS=""
PASSED=0
FAILED_AS_INTENDED=0
FAULTS=0

say() { printf '%s\n' "$*"; }
stop() { say "device-run: $*" >&2; exit 2; }

# --- private details (SPEC-258) ----------------------------------------------------------------------

# scrub FILE: replaces, in place, every whole word that is this machine's host name or this user's name,
# and everything with the form of a network address, by [removed]. Prints how many it replaced.
scrub() {
  local file="$1" host user count=0 pattern n
  host="$(hostname 2>/dev/null || true)"
  user="$(id -un 2>/dev/null || true)"
  local ipv4='([0-9]{1,3}\.){3}[0-9]{1,3}'
  local mac='([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}'
  local ipv6full='([0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4}'
  local ipv6short='([0-9A-Fa-f]{1,4}:){0,6}[0-9A-Fa-f]{0,4}::([0-9A-Fa-f]{1,4}:){0,6}[0-9A-Fa-f]{0,4}'
  for word in "$host" "${host%%.*}" "$user"; do
    [ -n "$word" ] || continue
    pattern="$(printf '%s' "$word" | sed 's/[][\.*^$/+?(){}|]/\\&/g')"
    # Twice: two matches that share the character between them are not both found in one pass.
    for _ in 1 2; do
      n="$(grep -oiE "(^|[^[:alnum:]_])${pattern}([^[:alnum:]_]|\$)" "$file" | wc -l)"
      count=$((count + n))
      sed -i -E "s/(^|[^[:alnum:]_])${pattern}([^[:alnum:]_]|\$)/\1[removed]\2/Ig" "$file"
    done
  done
  # Each address pattern, with the number of the group that holds what follows it: the patterns have
  # groups of their own, and sed counts them all.
  local entry after
  for entry in "4 $mac" "4 $ipv6full" "5 $ipv6short" "4 $ipv4"; do
    after="${entry%% *}"
    pattern="${entry#* }"
    n="$(grep -oE "(^|[^[:alnum:]:.])(${pattern})([^[:alnum:]:.]|\$)" "$file" | wc -l)"
    count=$((count + n))
    sed -i -E "s/(^|[^[:alnum:]:.])(${pattern})([^[:alnum:]:.]|\$)/\1[removed]\\${after}/g" "$file"
  done
  say "$count"
}

# --- talking to the tester and to the app ----------------------------------------------------------------

# ask ID "question": prints the question, reads one line, and keeps "ID = answer" for the results file.
ask() {
  local id="$1" question="$2" answer
  say ""
  say "[$id] $question"
  printf '> '
  IFS= read -r answer || answer=""
  [ -n "$answer" ] || answer="(no answer)"
  ANSWERS="${ANSWERS}check_${id} = ${answer}"$'\n'
}

# pause "instruction": prints it and waits for Enter.
pause() {
  say ""
  say "$1"
  printf 'Press Enter here when that is done. '
  IFS= read -r _ || true
}

request() {  # request STEP: asks the app for a step at its next start (SPEC-260)
  mkdir -p "$STATE"
  printf 'step = %s\n' "$1" > "$REQUEST"
}

# wait_for PATTERN SECONDS: waits until a line of the app's results matches. False when the time is up.
wait_for() {
  local pattern="$1" seconds="$2" waited=0
  while [ "$waited" -lt "$seconds" ]; do
    [ -f "$RESULTS" ] && grep -qE "$pattern" "$RESULTS" && return 0
    sleep 1
    waited=$((waited + 1))
  done
  return 1
}

results_have() { [ -f "$RESULTS" ] && grep -qE "$1" "$RESULTS"; }

# --- the package -----------------------------------------------------------------------------------------

refuse_root() {
  [ "$(id -u)" -ne 0 ] || stop "run this as the device's normal user, not as root: the app is started by the launcher as that user, and its results are looked for in that user's folders"
}

install_package() {
  local deb="$1"
  [ -f "$deb" ] || stop "there is no file $deb"
  say "Installing $(basename "$deb") (this asks for your password if sudo needs it)."
  $SUDO apt-get install -y "./$(basename "$deb")" >/dev/null 2>&1 || stop "the package could not be installed"
  rm -f "$RESULTS"
}

remove_package() {
  $SUDO apt-get remove -y sidplayzero >/dev/null 2>&1 || say "The package could not be removed; remove it by hand: sudo apt remove sidplayzero"
  rm -f "$REQUEST" "$RESULTS"
}

# gather FILE "heading": writes the results file from the app's results and the tester's answers.
gather() {
  local out="$1"
  {
    say "# SidPlayZero device run: $2"
    say "run_version = 1"
    say "date_utc = $(date -u +%Y-%m-%d)"
    say "model = $( (tr -d '\0' < /proc/device-tree/model) 2>/dev/null || say unknown)"
    say ""
    say "## What the app measured"
    if [ -f "$RESULTS" ]; then
      # start_ms and exit_ms are kept as lists, in order; of any other key the last value counts.
      for key in start_ms exit_ms; do
        say "$key = $(sed -n "s/^$key = //p" "$RESULTS" | tr '\n' ' ' | sed 's/ $//')"
      done
      grep -vE '^(start_ms|exit_ms) = ' "$RESULTS" | awk -F' = ' '{ value[$1] = $0; if (!($1 in seen)) { seen[$1] = 1; order[++n] = $1 } }
        END { for (i = 1; i <= n; i++) print value[order[i]] }'
    else
      say "(the app wrote no results)"
    fi
    say ""
    say "## The checklist"
    printf '%s' "$ANSWERS"
    say ""
    say "## Checks"
    say "checks_passed = $PASSED"
    say "checks_failed_as_intended = $FAILED_AS_INTENDED"
    say "checks_not_in_order = $FAULTS"
  } > "$out"
  local removed
  removed="$(scrub "$out")"
  say "private_details_removed = $removed" >> "$out"
  say ""
  say "Wrote $out ($removed private detail(s) replaced by [removed]). Read it, then send it back."
}

# --- the proof: two canaries (TF-081) ------------------------------------------------------------------

prove() {
  local deb="$1" out
  out="sidplayzero-device-prove-$(date +%Y%m%d-%H%M%S).txt"
  refuse_root
  install_package "$deb"
  request none
  pause "Now start SidPlayZero from the launcher. When its screen shows, hold ESC until the launcher is back."
  if ! wait_for '^start_ms = [0-9]+$' "$WAIT"; then
    remove_package
    stop "the app wrote no results to $STATE. Either it was not started, or the launcher starts apps as another user than the one running this script"
  fi

  say ""
  say "Canary 1, which must hold: the app recorded its start time."
  if results_have '^start_ms = [0-9]+$'; then
    say "  passed"
    PASSED=$((PASSED + 1))
  else
    say "  NOT IN ORDER: no start time"
    FAULTS=$((FAULTS + 1))
  fi

  say "Canary 2, which is built to fail: the app says it is version 0.0.0-not-this."
  if results_have '^app_version = 0\.0\.0-not-this$'; then
    say "  NOT IN ORDER: it passed, and it must not"
    FAULTS=$((FAULTS + 1))
  else
    say "  failed, as intended (the app says: $(sed -n 's/^app_version = //p' "$RESULTS" | tail -1))"
    FAILED_AS_INTENDED=$((FAILED_AS_INTENDED + 1))
  fi

  gather "$out" "the device level's canaries"
  remove_package
  say ""
  if [ "$PASSED" -eq 1 ] && [ "$FAILED_AS_INTENDED" -eq 1 ] && [ "$FAULTS" -eq 0 ]; then
    say "Device level: in order. One check passed and one failed as intended."
    return 0
  fi
  say "Device level: NOT IN ORDER."
  return 1
}

# --- the whole sitting (SPEC-260) ------------------------------------------------------------------------

# step_with_app STEP "instruction" SECONDS: asks the app for a step, has the tester start it, and waits for
# the app to say the step is done.
step_with_app() {
  local step="$1" instruction="$2" seconds="$3"
  request "$step"
  say ""
  say "$instruction"
  if wait_for "^step_done = $step\$" "$seconds"; then
    say "  the app has done: $step"
    PASSED=$((PASSED + 1))
  else
    say "  NOT IN ORDER: the app did not finish '$step' within $seconds s"
    FAULTS=$((FAULTS + 1))
  fi
}

sitting() {
  local deb="$1" out
  out="sidplayzero-device-$(date +%Y%m%d-%H%M%S).txt"
  refuse_root

  # 1
  install_package "$deb"
  ANSWERS="${ANSWERS}package_files = $(dpkg -L sidplayzero 2>/dev/null | grep -c .)"$'\n'
  # 2
  ask launcher_shows_app "Step 2. Does the launcher show SidPlayZero, with its icon? (yes/no)"
  step_with_app keys "Start SidPlayZero from the launcher. Press each key the screen asks for, then hold ESC to leave." "$WAIT"
  if ! [ -f "$RESULTS" ]; then
    remove_package
    stop "the app wrote no results to $STATE: the launcher starts apps as another user than the one running this script"
  fi
  ask esc_hold_returns "Did holding ESC bring the launcher back, within about a second? (yes/no)"
  # 3
  step_with_app engines "Step 3. Start SidPlayZero again and wait until it says Done. Then hold ESC to leave." 600
  # 4
  request none
  pause "Step 4. Start SidPlayZero. With an empty collection folder: Enter, Enter sets HVSC up. Then play MUSICIANS/H/Hubbard_Rob/Commando.sid."
  ask sound_from_loudspeaker "Does sound come from the loudspeaker? (yes/no)"
  ask commando_shown "Does the screen show Commando, Rob Hubbard and 3:55? (yes/no)"
  # 5
  ask download_everything "On the HVSC screen: is \"Download everything\" offered? If it is and you want to, run it to its end now. (not offered / offered, not run / ran to its end / failed)"
  # 6
  pause "Step 6. Turn networking off. Start SidPlayZero and leave it with a held ESC, five times. Then turn networking on again."
  # 7
  ask volume_keys "Step 7. While a tune plays: do fn+S and fn+D change the level you hear, and fn+A mute it? (yes/no)"
  ask brightness_keys "Do fn+U and fn+I change the screen's brightness? (yes/no/the app says it may not)"
  ask headphones "Plug headphones in and out twice. Does the tune keep playing, with gaps under a second? (yes/no)"
  # 8
  step_with_app benchmark "Step 8. Start SidPlayZero and leave the device alone for about 35 minutes, until it says Done. Then hold ESC to leave." 2700
  # 9
  request none
  ask screen_after_two_minutes "Step 9. Start SidPlayZero, play a tune, and do not touch the device for two minutes. What did the screen do? (stayed on / dimmed / went dark)"
  ask tune_played_on "Did the tune play on all the while? (yes/no)"
  # 10
  ask launcher_answers "Step 10. Hold ESC to leave. Does the launcher answer keys at once? (yes/no)"
  ask launcher_sounds "Do the launcher's key sounds play? (yes/no)"
  ask levels_as_before "Are the launcher's volume and the brightness what they were before? (yes/no)"
  # 11
  gather "$out" "the whole sitting"
  local config="${XDG_CONFIG_HOME:-$HOME/.config}/sidplayzero" kept=yes removed=yes
  remove_package
  [ -d "$config" ] || kept=no
  [ ! -e /usr/bin/sidplayzero ] || removed=no
  say "user_files_kept_after_removal = $kept" >> "$out"
  say "package_removed_cleanly = $removed" >> "$out"
  say ""
  if [ "$FAULTS" -eq 0 ]; then
    say "The sitting is done. What it found is in $out."
    return 0
  fi
  say "The sitting is done, with $FAULTS step(s) not in order. See $out."
  return 1
}

case "${1:-}" in
  --scrub)
    if [ $# -ne 2 ] || [ ! -f "$2" ]; then stop "usage: device-run.sh --scrub FILE"; fi
    scrub "$2"
    ;;
  --prove)
    [ $# -eq 2 ] || stop "usage: device-run.sh --prove PACKAGE.deb"
    prove "$2"
    ;;
  -h|--help|"")
    sed -n '4,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    ;;
  -*)
    stop "unknown option: $1"
    ;;
  *)
    [ $# -eq 1 ] || stop "usage: device-run.sh PACKAGE.deb"
    sitting "$1"
    ;;
esac
