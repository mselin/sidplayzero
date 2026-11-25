#!/usr/bin/env bash
#
# device-probe.sh: collects the facts about a CardputerZero that SidPlayZero's requirements
# and specification depend on, and writes them to one text file to send back.
#
# It changes nothing on the device: no installs, no settings, no sudo. The only files it
# writes are the results file, in the current directory, and temporary WAV renders in
# /dev/shm, which it deletes again.
#
# It leaves out the hostname, the user name, IP addresses and Wi-Fi names on purpose.
# Do read the results file before you share it all the same.
#
#   ./device-probe.sh              facts only, a few seconds
#   ./device-probe.sh --bench      also times sidplayfp renders (needs the sidplayfp package
#                                  and a SID collection, or --fetch); about two minutes
#   ./device-probe.sh --bench --fetch
#                                  downloads three test tunes from hvsc.c64.org to /dev/shm
#                                  when there is no collection on the device, and deletes them
#   ./device-probe.sh --unpack-test
#                                  finds out whether this device can unpack the complete HVSC
#                                  archive. It downloads the archive (81 MB) next to the results
#                                  file, has 7z test it without writing any files, records the
#                                  memory and time that took, and deletes the archive. Needs the
#                                  7zip package. Expect several minutes during which the device is
#                                  short of memory and slow; stop other apps first.
#   ./device-probe.sh --engine-bench ./sidplayzero-bench
#                                  times the two engines the app will ship (libsidplayfp 3.1.2),
#                                  for tunes with one, two and three SID chips. The program is
#                                  built by tools/engine-bench/build.sh and copied to the device
#                                  beforehand; it needs nothing installed, reads no tune file and
#                                  uses no network. About three minutes, longer on a slow device.
#
# The options can be combined. The collection is looked for in $HVSC_BASE, then in ~/C64Music.

set -u

BENCH=0
FETCH=0
UNPACK=0
ENGINE_BENCH=""
while [ $# -gt 0 ]; do
  case "$1" in
    --bench) BENCH=1 ;;
    --fetch) FETCH=1 ;;
    --unpack-test) UNPACK=1 ;;
    --engine-bench)
      [ $# -ge 2 ] || { echo "--engine-bench needs the path of the sidplayzero-bench program" >&2; exit 2; }
      ENGINE_BENCH="$2"; shift ;;
    -h|--help) sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
if [ -n "$ENGINE_BENCH" ] && [ ! -x "$ENGINE_BENCH" ]; then
  echo "$ENGINE_BENCH is not a program that can be run (chmod +x it first)" >&2; exit 2
fi

OUT="sidplayzero-probe-$(date +%Y%m%d-%H%M%S).txt"
KEYBOARD=/dev/input/by-path/platform-3f804000.i2c-event
HVSC_URL=https://hvsc.c64.org/download/C64Music
ARCHIVE_URL=https://hvsc.brona.dk/HVSC/HVSC_85-all-of-them.7z
# One ordinary tune, one that plays samples, one for two SID chips.
TUNES="MUSICIANS/H/Hubbard_Rob/Commando.sid MUSICIANS/G/Galway_Martin/Arkanoid.sid MUSICIANS/J/Jammer/Antipop_2SID.sid"

section() { printf '\n## %s\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
# Runs a command and prints its output, or a note that it is missing or failed. Never stops the script.
run() {
  if ! have "$1"; then echo "($1: not installed)"; return 0; fi
  "$@" 2>&1 || echo "($1: exit $?)"
}
access() { # access <path>: which of read/write this user has
  local p="$1" r=no w=no
  [ -e "$p" ] || { echo "$p: missing"; return 0; }
  [ -r "$p" ] && r=yes
  [ -w "$p" ] && w=yes
  echo "$p: read=$r write=$w ($(stat -c '%A %U:%G' "$p" 2>/dev/null | sed "s/ $(id -un):/ <me>:/"))"
}

probe() {
  echo "# SidPlayZero device probe"
  echo "probe_version: 2"
  echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"

  section "System"
  grep -E '^(PRETTY_NAME|VERSION_ID|VERSION_CODENAME|ID)=' /etc/os-release 2>/dev/null
  echo "kernel: $(uname -srm)"
  echo "model: $( (cat /proc/device-tree/model 2>/dev/null || echo unknown) | tr -d '\0')"
  echo "cpus: $(nproc 2>/dev/null)"
  grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree)' /proc/meminfo
  echo "swap devices: $(tail -n +2 /proc/swaps 2>/dev/null | awk '{print $1 " " $2 " " $3 "kB"}' | tr '\n' ';')"
  echo "cpu_max_khz: $(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq 2>/dev/null || echo unknown)"
  echo "cpu_governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  echo "home_filesystem: $(df -h --output=fstype,size,avail "$HOME" 2>/dev/null | tail -1)"
  echo "tmpfs_dev_shm: $(df -h --output=size,avail /dev/shm 2>/dev/null | tail -1)"

  section "User and session"
  echo "uid_is_root: $([ "$(id -u)" = 0 ] && echo yes || echo no)"
  echo "groups: $(id -Gn | tr ' ' '\n' | grep -v -x "$(id -un)" | sort | tr '\n' ' ')"
  echo "XDG_RUNTIME_DIR set: $([ -n "${XDG_RUNTIME_DIR:-}" ] && echo yes || echo no)"
  echo "XDG_CONFIG_HOME set: $([ -n "${XDG_CONFIG_HOME:-}" ] && echo yes || echo no)"
  echo "launcher user service: $(run systemctl --user is-active APPLaunch.service | tail -1)"
  echo "launcher dark_time: $(grep -o '"dark_time"[^,}]*' "$HOME/.config/cardputerzero/config.json" 2>/dev/null || echo 'not set (default 30)')"

  section "Packages (installed and candidate versions)"
  for p in sidplayfp libsidplayfp6 libsidplayfp-dev libstilview0 libusb-1.0-0 libpulse0 libasound2t64 \
           libarchive13t64 7zip p7zip-full libcurl4t64 libsqlite3-0 pipewire pipewire-pulse wireplumber pulseaudio-utils; do
    printf '%-18s %s\n' "$p" "$(apt-cache policy "$p" 2>/dev/null | awk '/Installed:/{i=$2} /Candidate:/{c=$2} END{print "installed=" (i?i:"?") " candidate=" (c?c:"?")}')"
  done
  echo "launcher packages:"
  dpkg-query -W -f='  ${Package} ${Version}\n' 2>/dev/null | grep -i -E 'applaunch|cardputer|m5stack' || echo "  (none found)"

  section "Audio"
  run pactl info | grep -E '^(Server Name|Server Version|Default Sink|Default Sample Specification|Default Channel Map)'
  echo "sinks:"; run pactl list short sinks | sed 's/^/  /'
  echo "cards (ALSA):"; sed 's/^/  /' /proc/asound/cards 2>/dev/null || echo "  (none)"
  echo "mixer controls on card 0:"; run amixer -c 0 scontrols | sed 's/^/  /' | head -40
  echo "sink volume: $(run pactl get-sink-volume @DEFAULT_SINK@ | head -1)"
  echo "sink mute: $(run pactl get-sink-mute @DEFAULT_SINK@ | head -1)"

  section "Keyboard and input"
  access "$KEYBOARD"
  awk '/^N: Name=/{n=$0} /^H: Handlers=/{print "  " n "  " $0}' /proc/bus/input/devices 2>/dev/null

  section "Display and backlight"
  access /dev/fb0
  echo "fb0 size: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null) bpp: $(cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null)"
  for b in /sys/class/backlight/*; do
    [ -d "$b" ] || { echo "backlight: none"; continue; }
    echo "backlight $(basename "$b"): brightness=$(cat "$b/brightness" 2>/dev/null) max=$(cat "$b/max_brightness" 2>/dev/null)"
    access "$b/brightness"
  done

  section "USB"
  if have lsusb; then lsusb | sed 's/^/  /'; else echo "(lsusb: not installed)"; fi
  echo "USBSID-Pico (cafe:4011) attached: $(have lsusb && lsusb -d cafe:4011 >/dev/null 2>&1 && echo yes || echo no)"

  section "SID collection"
  local base="${HVSC_BASE:-$HOME/C64Music}"
  echo "HVSC_BASE set: $([ -n "${HVSC_BASE:-}" ] && echo yes || echo no)"
  if [ -d "$base" ]; then
    echo "collection: present"
    echo "version file: $(tr -d '\r' < "$base/DOCUMENTS/hv_sids.txt" 2>/dev/null | head -2 | tr '\n' ' ')"
    local t0 t1 n
    t0=$(date +%s.%N); n=$(find "$base" -type f -iname '*.sid' 2>/dev/null | wc -l); t1=$(date +%s.%N)
    echo "sid files: $n"
    echo "walk seconds (find, cache state unknown): $(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')"
    echo "size on disk: $(du -sh "$base" 2>/dev/null | cut -f1)"
    echo "songlengths: $(stat -c '%s bytes' "$base/DOCUMENTS/Songlengths.md5" 2>/dev/null || echo missing)"
  else
    echo "collection: not found in \$HVSC_BASE or ~/C64Music"
  fi

  [ "$BENCH" = 1 ] && bench "$base"
  [ -n "$ENGINE_BENCH" ] && engine_bench
  [ "$UNPACK" = 1 ] && unpack_test
  echo
  echo "# end of probe"
}

# Times sidplayfp rendering 60 seconds of each test tune to a WAV in /dev/shm, per engine and
# sample rate, and renders one of them twice to see whether the output is the same both times:
# once as sidplayfp does it by default, with a random power-on delay, and once with the delay fixed.
bench() {
  local base="$1" work=/dev/shm/sidplayzero-probe.$$ fetched=0
  section "Render benchmark (sidplayfp -w, 60 s of each tune)"
  if ! have sidplayfp; then
    echo "skipped: sidplayfp is not installed (sudo apt install sidplayfp)"; return 0
  fi
  mkdir -p "$work" || { echo "skipped: cannot write to /dev/shm"; return 0; }
  echo "sidplayfp options: $(sidplayfp --help 2>&1 | grep -o -E -- '--(resid|residfp|sidlite|usbsid|exsid)\b' | sort -u | tr '\n' ' ')"
  local t f
  for t in $TUNES; do
    f="$base/$t"
    if [ ! -f "$f" ] && [ "$FETCH" = 1 ] && have curl; then
      f="$work/$(basename "$t")"
      if curl -fsSL --max-time 60 -o "$f" "$HVSC_URL/$t"; then fetched=1; else rm -f "$f"; fi
    fi
    [ -f "$f" ] || { echo "$t: not available (no collection; try --fetch)"; continue; }
    local engine rate start end secs
    for engine in resid residfp sidlite; do
      sidplayfp --help 2>&1 | grep -q -- "--$engine\b" || continue
      for rate in 44100 48000; do
        start=$(date +%s.%N)
        sidplayfp "--$engine" "-f$rate" -t60 "-w$work/out.wav" "$f" >/dev/null 2>&1
        end=$(date +%s.%N)
        secs=$(awk -v a="$start" -v b="$end" 'BEGIN{printf "%.2f", b-a}')
        echo "$(basename "$t") engine=$engine rate=$rate render_seconds=$secs realtime_factor=$(awk -v s="$secs" 'BEGIN{if (s>0) printf "%.2f", 60/s; else print "?"}') wav_bytes=$(stat -c %s "$work/out.wav" 2>/dev/null || echo 0)"
      done
    done
  done
  f="$base/${TUNES%% *}"; [ -f "$f" ] || f="$work/$(basename "${TUNES%% *}")"
  if [ -f "$f" ]; then
    local a b delay
    for delay in "" "--delay=0"; do
      rm -f "$work/a.wav" "$work/b.wav"
      # shellcheck disable=SC2086  # $delay is empty or one option
      sidplayfp $delay -t20 "-w$work/a.wav" "$f" >/dev/null 2>&1; a=$(sha256sum "$work/a.wav" 2>/dev/null | cut -c1-16)
      # shellcheck disable=SC2086
      sidplayfp $delay -t20 "-w$work/b.wav" "$f" >/dev/null 2>&1; b=$(sha256sum "$work/b.wav" 2>/dev/null | cut -c1-16)
      echo "same output twice (default engine, 20 s, ${delay:-random delay}): $([ -n "$a" ] && [ "$a" = "$b" ] && echo yes || echo no) ($a $b)"
    done
  fi
  echo "test tunes downloaded for this run: $([ "$fetched" = 1 ] && echo yes || echo no)"
  rm -rf "$work"
}

# Has 7z test the complete HVSC archive, which takes the same memory as unpacking it, and records
# what that needed. Nothing is unpacked to the card; the archive itself is deleted afterwards.
unpack_test() {
  section "Unpack test (7z t on the complete HVSC archive)"
  local z="" f="./sidplayzero-probe-archive.$$.7z" log="/dev/shm/sidplayzero-probe-7z.$$"
  for c in 7z 7zz 7za; do have "$c" && { z="$c"; break; }; done
  [ -n "$z" ] || { echo "skipped: no 7z program (sudo apt install 7zip)"; return 0; }
  have curl || { echo "skipped: curl is not installed"; return 0; }
  echo "7z program: $z"
  echo "before: $(grep -E '^(MemAvailable|SwapFree)' /proc/meminfo | tr -s ' ' | tr '\n' ' ')"
  local t0 t1
  t0=$(date +%s.%N)
  if ! curl -fsSL --max-time 1800 -o "$f" "$ARCHIVE_URL"; then echo "download failed"; rm -f "$f"; return 0; fi
  t1=$(date +%s.%N)
  echo "download: $(stat -c %s "$f") bytes in $(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.0f", b-a}') s"
  if [ -x /usr/bin/time ]; then
    /usr/bin/time -v "$z" t -mmt1 -bso0 -bsp0 "$f" > "$log" 2>&1
    echo "7z exit status: $?"
    grep -E 'Maximum resident set size|Elapsed \(wall clock\)|Major \(requiring I/O\) page faults|Swaps' "$log" | sed 's/^[[:space:]]*/  /'
  else
    t0=$(date +%s.%N); "$z" t -mmt1 -bso0 -bsp0 "$f" > "$log" 2>&1; echo "7z exit status: $?"; t1=$(date +%s.%N)
    echo "  elapsed seconds: $(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.0f", b-a}') (install the 'time' package for memory figures)"
  fi
  grep -i -E 'error|cannot|killed' "$log" | head -5 | sed 's/^/  /'
  echo "after: $(grep -E '^(MemAvailable|SwapFree)' /proc/meminfo | tr -s ' ' | tr '\n' ' ')"
  if dmesg >/dev/null 2>&1; then
    echo "out-of-memory kills in the kernel log: $(dmesg | grep -c -i 'out of memory')"
  else
    echo "out-of-memory kills in the kernel log: unknown (reading the kernel log is not permitted)"
  fi
  rm -f "$f" "$log"
}

# Times the engines the app will ship, with the program built by tools/engine-bench/build.sh.
# Its lines are "engine_bench_<engine>_<chips>sid_<rate> = <seconds of sound per second of processor time>".
engine_bench() {
  section "Engine benchmark (libsidplayfp 3.1.2, both engines; SPEC-262)"
  echo "before: $(grep -E '^(MemAvailable)' /proc/meminfo | tr -s ' ') load=$(cut -d' ' -f1-3 /proc/loadavg)"
  "$ENGINE_BENCH" 2>&1 || echo "(engine benchmark: exit $?)"
  echo "temperature after: $( (cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo unknown) )"
}

probe > "$OUT" 2>&1
echo "Wrote $OUT ($(wc -l < "$OUT") lines). Read it, then send it back."
