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
#   ./device-run.sh PACKAGE.deb           the whole sitting of docs/device-checklist.md, about an hour.
#                                         Run again with the same package, it carries on from the step
#                                         it had reached, with the answers already given (FIND-030).
#   ./device-run.sh --restart PACKAGE.deb the whole sitting, begun again from step 1.
#   ./device-run.sh --only 4,5,8 PACKAGE.deb
#                                         only the steps named, with the install of step 1 and the
#                                         removal and results of step 11; carried on as above (FIND-038).
#   ./device-run.sh --scrub FILE          takes private details out of FILE, as it does for its own results
#
# It installs the package with sudo, and removes it again at the end; it uses sudo for nothing else, and
# refuses to run as root. The app is always started by you, from the launcher. The script and the app
# talk through two files in the app's state folder: the script writes selftest.request, and the app
# appends what it measures to selftest.results.
#
# It writes one results file in the current folder. Host names, user names and network addresses are
# replaced in it by [removed] (SPEC-258). Read it before you send it all the same.
#
# During the sitting it keeps the step it has reached, the answers given and the checks made in
# sitting.state, beside this script, so that a sitting whose SSH session ends (step 6 turns networking
# off) loses nothing. It removes that file when the sitting is done.

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
SITTING_STATE="$(cd "$(dirname "$0")" && pwd)/sitting.state"
ONLY=""            # the steps --only names, as " 4 5 8 "; empty for the whole sitting
PACKAGE_SUM=""      # the package's SHA-256: a sitting carries on only with the same package
REACHED=1           # the step of the sitting reached
CHECKED=" "         # the steps the app has done that have been counted, between spaces

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

# answered NAME: whether the results file's checklist already has a line for NAME.
answered() { printf '%s' "$ANSWERS" | grep -q "^$1 = "; }

# ask ID "question": prints the question, reads one line, and keeps "ID = answer" for the results file.
# A question answered already in this sitting is not asked again.
ask() {
  local id="$1" question="$2" answer
  answered "check_$id" && return 0
  say ""
  say "[$id] $question"
  printf '> '
  IFS= read -r answer || answer=""
  [ -n "$answer" ] || answer="(no answer)"
  ANSWERS="${ANSWERS}check_${id} = ${answer}"$'\n'
  save_state
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
  # The file that was checked, and no other: apt-get takes a path with a slash in it as a package file.
  local path="$deb"
  case "$path" in
    */*) ;;
    *) path="./$path" ;;
  esac
  say "Installing $(basename "$deb") (this asks for your password if sudo needs it)."
  $SUDO apt-get install -y "$path" >/dev/null 2>&1 || stop "the package could not be installed"
  rm -f "$RESULTS"
}

remove_package() {
  $SUDO apt-get remove -y sidplayzero >/dev/null 2>&1 || say "The package could not be removed; remove it by hand: sudo apt remove sidplayzero"
  rm -f "$REQUEST" "$RESULTS"
}

# --- where the sitting has got to (FIND-030) -------------------------------------------------------------

package_sum() {
  sha256sum "$1" 2>/dev/null | cut -d' ' -f1
}

# save_state: writes the step reached, the checks counted and the answers given to sitting.state.
save_state() {
  local temporary="$SITTING_STATE.new"
  {
    say "# SidPlayZero device sitting: where it has got to. Run device-run.sh again with the same package to"
    say "# carry on, or with --restart to begin again."
    say "package = $PACKAGE_SUM"
    say "step = $REACHED"
    say "only =$ONLY"
    say "passed = $PASSED"
    say "faults = $FAULTS"
    say "checked =$CHECKED"
    printf '%s' "$ANSWERS" | sed 's/^/answer: /'
  } > "$temporary" && mv -f "$temporary" "$SITTING_STATE"
}

# load_state: takes up the sitting that sitting.state describes, if it is one with this package. False if
# there is none to take up.
load_state() {
  [ -f "$SITTING_STATE" ] || return 1
  local line package="" step="" only="" passed=0 faults=0 checked=" " answers=""
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      "package = "*) package="${line#package = }" ;;
      "step = "*) step="${line#step = }" ;;
      "only ="*) only="${line#only =}" ;;
      "passed = "*) passed="${line#passed = }" ;;
      "faults = "*) faults="${line#faults = }" ;;
      "checked ="*) checked="${line#checked =}" ;;
      "answer: "*) answers="${answers}${line#answer: }"$'\n' ;;
    esac
  done < "$SITTING_STATE"
  if [ "$package" != "$PACKAGE_SUM" ]; then
    say "The sitting in $SITTING_STATE was with another package: beginning again from step 1."
    return 1
  fi
  if [ "$only" != "$ONLY" ]; then
    say "The sitting in $SITTING_STATE was of other steps: beginning again from step 1."
    return 1
  fi
  case "$step$passed$faults" in
    ''|*[!0-9]*) say "$SITTING_STATE cannot be read: beginning again from step 1."; return 1 ;;
  esac
  REACHED="$step" PASSED="$passed" FAULTS="$faults" CHECKED="$checked" ANSWERS="$answers"
  return 0
}

# reach N: true if step N is still to do, or is the step the sitting had reached, and records that the
# sitting has reached it; false for a step done in an earlier run, and for one --only leaves out. Steps 1
# and 11, the install and the removal, are never left out.
reach() {
  [ "$1" -ge "$REACHED" ] || return 1
  if [ -n "$ONLY" ] && [ "$1" -ne 1 ] && [ "$1" -ne 11 ]; then
    case "$ONLY" in *" $1 "*) ;; *) return 1 ;; esac
  fi
  REACHED="$1"
  save_state
  return 0
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
      # Of key_answer, the key step's answers, the last answer of each key counts (FIND-029).
      grep -vE '^(start_ms|exit_ms) = ' "$RESULTS" | awk -F' = ' '{ k = $1; if (k == "key_answer") { split($2, w, " "); k = k " " w[1] }
          value[k] = $0; if (!(k in seen)) { seen[k] = 1; order[++n] = k } }
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
# the app to say the step is done. A step counted already in this sitting is not asked for again.
step_with_app() {
  local step="$1" instruction="$2" seconds="$3"
  case "$CHECKED" in *" $step "*) return 0 ;; esac
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
  CHECKED="$CHECKED$step "
  save_state
}

sitting() {
  local deb="$1" restart="$2" out resumed=0
  out="sidplayzero-device-$(date +%Y%m%d-%H%M%S).txt"
  refuse_root
  [ -f "$deb" ] || stop "there is no file $deb"
  PACKAGE_SUM="$(package_sum "$deb")"
  [ -n "$PACKAGE_SUM" ] || stop "the package $deb could not be read"
  if [ "$restart" = yes ]; then
    rm -f "$SITTING_STATE"
  elif load_state; then
    resumed="$REACHED"
    say "Carrying on the sitting from step $REACHED, with the answers already given ($SITTING_STATE)."
    say "To begin again from step 1 instead: $0 --restart $deb"
  fi

  # 1
  if reach 1; then
    install_package "$deb"
    answered package_files || ANSWERS="${ANSWERS}package_files = $(dpkg -L sidplayzero 2>/dev/null | grep -c .)"$'\n'
  fi
  # 2
  if reach 2; then
    ask launcher_shows_app "Step 2. Does the launcher show SidPlayZero, with its icon? (yes/no)"
    # 25 keys, found with fn and sym: three times the usual wait. Started again, the app's key step carries
    # on at the first key with no answer (FIND-029).
    step_with_app keys "Start SidPlayZero from the launcher. Press each key the screen asks for, then hold ESC to leave." "$((WAIT * 3))"
    if ! [ -f "$RESULTS" ]; then
      remove_package
      rm -f "$SITTING_STATE"
      stop "the app wrote no results to $STATE: the launcher starts apps as another user than the one running this script"
    fi
    ask esc_hold_returns "Did holding ESC bring the launcher back, within about a second? (yes/no)"
  fi
  # 3
  if reach 3; then
    step_with_app engines "Step 3. Start SidPlayZero again and wait until it says Done. Then hold ESC to leave." 600
  fi
  # 4
  if reach 4; then
    request none
    answered check_sound_from_loudspeaker ||
      pause "Step 4. Start SidPlayZero. With an empty collection folder: Enter, Enter sets HVSC up. Then play MUSICIANS/H/Hubbard_Rob/Commando.sid."
    ask sound_from_loudspeaker "Does sound come from the loudspeaker? (yes/no)"
    ask commando_shown "Does the screen show Commando, Rob Hubbard and 3:55? (yes/no)"
  fi
  # 5
  if reach 5; then
    ask download_everything "Step 5. Press Tab for Settings, then 5 for the HVSC screen: is \"Download everything\" offered? If it is and you want to, run it to its end now. (not offered / offered, not run / ran to its end / failed)"
  fi
  # 6: turning networking off ends the SSH session, and this script with it (FIND-030).
  if reach 6; then
    if [ "$resumed" -eq 6 ]; then
      pause "Step 6, carried on. If you have not yet done its five starts with networking off, do them now: turn networking off, start SidPlayZero and leave it with a held ESC, five times, and turn networking on again."
    else
      pause "Step 6. Turn networking off: that ends this SSH session, and this script with it. Start SidPlayZero and leave it with a held ESC, five times. Then turn networking on, connect again, and run this script again just as before ($0 $deb): it carries on from here. (If this session is still there afterwards, press Enter here instead.)"
    fi
  fi
  # 7
  if reach 7; then
    ask volume_keys "Step 7. While a tune plays: do fn+S and fn+D change the level you hear, and fn+A mute it? (yes/no)"
    ask brightness_keys "Do fn+U and fn+I change the screen's brightness? (yes/no/the app says it may not)"
    ask headphones "Plug headphones in and out twice. Does the tune keep playing, with gaps under a second? (yes/no)"
  fi
  # 8
  if reach 8; then
    step_with_app benchmark "Step 8. Start SidPlayZero and leave the device alone for about 35 minutes, until it says Done. Then hold ESC to leave." 2700
  fi
  # 9
  if reach 9; then
    request none
    ask screen_after_two_minutes "Step 9. Start SidPlayZero, play a tune, and do not touch the device for two minutes. What did the screen do? (stayed on / dimmed / went dark)"
    ask tune_played_on "Did the tune play on all the while? (yes/no)"
  fi
  # 10
  if reach 10; then
    ask launcher_answers "Step 10. Hold ESC to leave. Does the launcher answer keys at once? (yes/no)"
    ask launcher_sounds "Do the launcher's key sounds play? (yes/no)"
    ask levels_as_before "Are the launcher's volume and the brightness what they were before? (yes/no)"
  fi
  # 11
  reach 11
  if [ -n "$ONLY" ]; then
    gather "$out" "steps${ONLY% } of the sitting"
  else
    gather "$out" "the whole sitting"
  fi
  local config="${XDG_CONFIG_HOME:-$HOME/.config}/sidplayzero" kept=yes removed=yes
  remove_package
  [ -d "$config" ] || kept=no
  [ ! -e /usr/bin/sidplayzero ] || removed=no
  say "user_files_kept_after_removal = $kept" >> "$out"
  say "package_removed_cleanly = $removed" >> "$out"
  rm -f "$SITTING_STATE"
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
  --restart)
    [ $# -eq 2 ] || stop "usage: device-run.sh --restart PACKAGE.deb"
    sitting "$2" yes
    ;;
  --only)
    [ $# -eq 3 ] || stop "usage: device-run.sh --only STEPS PACKAGE.deb, with STEPS such as 4,5,8"
    case "$2" in ''|*[!0-9,]*|,*|*,|*,,*) stop "--only takes step numbers from 2 to 10 with commas between, such as 4,5,8" ;; esac
    ONLY=" "
    for n in ${2//,/ }; do
      { [ "$n" -ge 2 ] && [ "$n" -le 10 ]; } || stop "--only takes steps 2 to 10; 1 and 11 are always done"
      case "$ONLY" in *" $n "*) ;; *) ONLY="$ONLY$n " ;; esac
    done
    sitting "$3" no
    ;;
  -*)
    stop "unknown option: $1"
    ;;
  *)
    [ $# -eq 1 ] || stop "usage: device-run.sh PACKAGE.deb"
    sitting "$1" no
    ;;
esac
