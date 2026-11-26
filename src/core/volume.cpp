// SPDX-License-Identifier: GPL-3.0-or-later
// The volume keys (SPEC-076).
#include "spz/volume.h"

namespace spz {

namespace {

VolumeOutcome failed() {
    VolumeOutcome outcome;
    outcome.notice = "MSG_VOLUME_FAILED";
    return outcome;
}

VolumeOutcome shown(int level, bool muted) {
    VolumeOutcome outcome;
    outcome.notice = "MSG_VOLUME";
    outcome.level = level;
    outcome.muted = muted;
    return outcome;
}

}  // namespace

VolumeOutcome press_volume_key(SystemVolume* volume, VolumeKey key) {
    if (volume == nullptr) return failed();
    int level = 0;
    bool muted = false;
    if (!volume->get(&level, &muted)) return failed();
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    if (key == VolumeKey::Mute) {
        if (!volume->toggle_mute()) return failed();
        int now = level;
        bool now_muted = !muted;
        if (!volume->get(&now, &now_muted)) {     // the toggle was done; say what it should have led to
            now = level;
            now_muted = !muted;
        }
        return shown(now, now_muted);
    }

    // The nearest multiple of 5 % above or below, never past 100 % or 0 %.
    int target = level;
    if (key == VolumeKey::Up) {
        target = (level / kVolumeStep + 1) * kVolumeStep;
        if (target > 100) target = 100;
    } else {
        target = ((level + kVolumeStep - 1) / kVolumeStep - 1) * kVolumeStep;
        if (target < 0) target = 0;
    }
    if (target == level) return shown(level, muted);   // already at the end: nothing to set
    if (!volume->set(target)) return failed();
    return shown(target, muted);
}

}  // namespace spz
