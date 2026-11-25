// SPDX-License-Identifier: GPL-3.0-or-later
// Seams to the rest of the system (SPEC-021): the unpacker, the volume, the backlight, memory.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace spz {

struct ArchiveEntry {
    std::string path;
    std::uint64_t size = 0;
    bool is_folder = false;
    bool is_link = false;
    bool is_special = false;
};

// Runs the system's 7z (SPEC-007).
class Unpacker {
public:
    // Called with the number of files written so far. Return false to stop.
    using Progress = std::function<bool(std::uint64_t files)>;

    virtual ~Unpacker() = default;
    virtual bool available() = 0;
    // The archive's entries. The app checks them before anything is unpacked (SPEC-149).
    virtual bool list(const std::string& archive, std::vector<ArchiveEntry>* out) = 0;
    virtual bool unpack(const std::string& archive, const std::string& into, const Progress& progress) = 0;
};

// The system's volume and mute (SPEC-076).
class SystemVolume {
public:
    virtual ~SystemVolume() = default;
    virtual bool get(int* percent, bool* muted) = 0;
    virtual bool set(int percent) = 0;
    virtual bool toggle_mute() = 0;
};

// The screen's brightness (SPEC-104, SPEC-207).
class Backlight {
public:
    virtual ~Backlight() = default;
    virtual bool can_change() = 0;
    virtual bool get(int* percent) = 0;
    virtual bool set(int percent) = 0;
};

// Free memory (SPEC-145).
class Memory {
public:
    virtual ~Memory() = default;
    virtual std::uint64_t available_mib() = 0;
    virtual std::uint64_t swap_free_mib() = 0;
};

}  // namespace spz
