// SPDX-License-Identifier: GPL-3.0-or-later
// The real seams of spz/system.h (SPEC-021): the `7z` program behind Unpacker (SPEC-007, SPEC-149), `pactl`
// behind SystemVolume (SPEC-076), /sys/class/backlight behind Backlight (SPEC-104) and /proc/meminfo
// behind Memory (SPEC-145). Each runs its program from an argument list, never through a shell.
#pragma once

#include <atomic>
#include <string>

#include "spz/system.h"

namespace spz::platform {

// `7z`, `7zz` or `7za`, the first found on PATH. list() runs `7z l -slt -t7z` and reads what it prints with
// the app's own reader (read_7z_listing, spz/complete_download.h), up to SPEC-204's 64 MiB (a longer
// listing fails), and stops it once it has named more entries than SPEC-149 allows; unpack() runs
// `7z x -t7z -mmt1` into the folder. Both run 7z in a process group of its own (stopped with the app,
// stop_all_children), with a time limit, and unpack() kills it if the progress function says stop.
class SevenZipUnpacker : public Unpacker {
public:
    bool available() override;
    bool list(const std::string& archive, std::vector<ArchiveEntry>* out) override;
    bool unpack(const std::string& archive, const std::string& into, const Progress& progress) override;

    // The program it would run: a full path, or "" if there is none.
    static std::string program();
};

// `pactl` on the default sink, with a limit of 2 s on each run (SPEC-022's quick worker).
class PactlVolume : public SystemVolume {
public:
    bool get(int* percent, bool* muted) override;
    bool set(int percent) override;
    bool toggle_mute() override;
};

// The first folder under `/sys/class/backlight` (or the folder given) whose `brightness` file the app may
// write. Levels are per cent of its `max_brightness`.
class SysfsBacklight : public Backlight {
public:
    explicit SysfsBacklight(std::string folder = "/sys/class/backlight");
    bool can_change() override;
    bool get(int* percent) override;
    bool set(int percent) override;

private:
    bool find();
    std::string folder_;
    std::string device_;   // the chosen device's folder, once found
};

// MemAvailable and SwapFree of /proc/meminfo, in MiB.
class ProcMemory : public Memory {
public:
    std::uint64_t available_mib() override;
    std::uint64_t swap_free_mib() override;
};

}  // namespace spz::platform
