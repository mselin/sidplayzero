// SPDX-License-Identifier: GPL-3.0-or-later
// The real Engine (SPEC-021): libsidplayfp 3.1.2 with reSIDfp or sidlite, linked into the program
// (SPEC-002, SPEC-003). This file and real_engine.cpp are the only ones that name the library.
//
// How a tune is set up is SPEC-067: the sample rate asked for; the SID model and the clock from the tune's
// header, or 6581 and PAL when it says "unknown" or "any", unless the settings force one (SPEC-077);
// reSIDfp or sidlite; the library's "resample and interpolate"; a power-on delay of 0; the mixer in stereo
// (SPEC-075). reSIDfp's filter is set as the `sidplayfp` program sets it with an empty home folder, so
// that the two render the same (SPEC-071).
//
// Threads: load() and render() are called by one thread at a time, never together: in the app, load() on
// the quick worker (the player's load job, through the software backend, which keeps render() waiting
// meanwhile) and render() on the render thread; in the tools and tests, by whoever holds the engine.
// set_muted(), set_filter(), registers() and chips() may be called on any thread at the same time: a mute
// or a filter is taken up at the start of the next render() (within one render chunk, SPEC-074), and
// registers() gives the values as they were at the end of the last render().
#pragma once

#include <memory>

#include "spz/engine.h"

namespace spz::platform {

class RealEngine : public Engine {
public:
    RealEngine();
    ~RealEngine() override;
    RealEngine(const RealEngine&) = delete;
    RealEngine& operator=(const RealEngine&) = delete;

    // Fails with "MSG_INVALID_SID" when the tune does not pass check_tune() (SPEC-203: the engine checks
    // it itself, whoever calls), with "MSG_ENGINE_REFUSED" when the library refuses it, and with
    // "MSG_NEEDS_BASIC" when the tune needs the BASIC ROM and the settings have none (SPEC-068).
    bool load(const std::vector<std::uint8_t>& tune, int subtune, const EngineSettings& settings,
              std::string* why) override;
    // Always gives `frames` frames once a tune is loaded, unless the library reports an error; 0 if none is.
    std::size_t render(std::int16_t* out, std::size_t frames) override;
    int chips() const override;
    void set_muted(int chip, int voice, bool muted) override;
    void set_filter(int chip, bool on) override;
    bool registers(int chip, std::uint8_t out[kRegisters]) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// "libsidplayfp 3.1.2": the library built in, as `sidplayzero --version` names it (SPEC-002).
const char* engine_library_version();
// "libresidfp 1.3.0": the reSIDfp library built in with it (SPEC-002, SPEC-103).
const char* residfp_library_version();

// The sanitizer the engine library was compiled with: "address", "thread", or "" for none. The library is
// compiled with the flags of the build it is in (cmake/engine.cmake).
const char* engine_sanitizer();

}  // namespace spz::platform
