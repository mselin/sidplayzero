// SPDX-License-Identifier: GPL-3.0-or-later
// The volume keys (SPEC-076): what a press asks of the system's volume, and the notice it leads to.
#pragma once

#include <string>

#include "spz/system.h"

namespace spz {

enum class VolumeKey { Up, Down, Mute };

const int kVolumeStep = 5;      // per cent

struct VolumeOutcome {
    std::string notice;         // "MSG_VOLUME", or "MSG_VOLUME_FAILED" if the volume could not be read or set
    int level = 0;              // with MSG_VOLUME: the level now, in per cent
    bool muted = false;         // with MSG_VOLUME: the notice says "Muted" instead of the level
};

// Does what a volume key does, through `volume`. Called on the quick worker (SPEC-022).
// Up and Down read the level and set it to the nearest multiple of 5 % above or below it, never above
// 100 % or below 0 %; where there is no such level, nothing is set. Mute toggles the system's mute.
VolumeOutcome press_volume_key(SystemVolume* volume, VolumeKey key);

}  // namespace spz
