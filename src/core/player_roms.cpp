// SPDX-License-Identifier: GPL-3.0-or-later
// The ROM files the settings may name (SPEC-068).
#include "spz/player_roms.h"

#include <utility>

namespace spz {

namespace {

// Reads one ROM file into `out` if the setting names one of exactly `size` bytes. False if a file was
// named and could not be used.
bool read_rom(Files* files, const std::string& name, std::size_t size, Bytes* out) {
    out->clear();
    if (name.empty()) return true;
    Bytes bytes;
    if (files == nullptr || files->read(name, size, &bytes) != FileStatus::Ok || bytes.size() != size) return false;
    *out = std::move(bytes);
    return true;
}

}  // namespace

RomsRead read_roms(Files* files, const Settings& settings) {
    RomsRead read;
    const bool kernal = read_rom(files, settings.rom_kernal, kKernalRomSize, &read.roms.kernal);
    const bool basic = read_rom(files, settings.rom_basic, kBasicRomSize, &read.roms.basic);
    const bool chargen = read_rom(files, settings.rom_chargen, kChargenRomSize, &read.roms.chargen);
    read.ignored = !kernal || !basic || !chargen;
    return read;
}

}  // namespace spz
