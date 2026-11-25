// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: whatever makes the sound (SPEC-025). The player knows nothing else about sound.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "spz/engine.h"

namespace spz {

// What a backend is able to do. A hardware backend cannot skip or read registers (SPEC-191).
struct BackendAbilities {
    bool mute = false;
    bool filter = false;
    bool skip = false;
    bool registers = false;
};

// The backend's events, delivered on the UI thread.
class SoundBackendListener {
public:
    virtual ~SoundBackendListener() = default;
    virtual void song_ended() = 0;                         // the length set by set_length() has been played
    virtual void device_lost() = 0;                        // output stopped; the backend is trying again
    virtual void device_back() = 0;                        // output carries on, nothing skipped
    virtual void device_gone() = 0;                        // it did not come back (SPEC-078)
    virtual void failed(const std::string& reason) = 0;    // a message identifier of SPEC-116
};

class SoundBackend {
public:
    virtual ~SoundBackend() = default;

    virtual void set_listener(SoundBackendListener* listener) = 0;

    // Prepares a subtune, counted from 1. On failure gives a message identifier.
    virtual bool load(const std::vector<std::uint8_t>& tune, int subtune, const EngineSettings& settings,
                      std::string* why) = 0;
    virtual void start() = 0;    // output begins at the first sample
    virtual void pause() = 0;    // stops at a known sample
    virtual void resume() = 0;   // carries on from it: none lost, none repeated
    virtual void stop() = 0;     // unloads; what was buffered is thrown away

    // "song_ended" after exactly this many samples of the subtune; 0 means never.
    virtual void set_length(std::uint64_t samples) = 0;
    // Samples of the subtune produced so far.
    virtual std::uint64_t position() const = 0;

    virtual void set_muted(int chip, int voice, bool muted) = 0;
    virtual void set_filter(int chip, bool on) = 0;
    virtual void set_skip(bool on) = 0;                    // SPEC-066
    virtual bool registers(int chip, std::uint8_t out[Engine::kRegisters]) = 0;
    virtual int chips() const = 0;
    virtual BackendAbilities can() const = 0;
    // Frames the backend could not deliver in time (SPEC-070).
    virtual std::uint64_t underruns() const = 0;
};

}  // namespace spz
