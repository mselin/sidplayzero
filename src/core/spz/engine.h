// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: the SID engine (SPEC-021). The real one is libsidplayfp, in `platform`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spz {

enum class EngineKind { Accurate, Light };          // reSIDfp, sidlite (SPEC-004)
enum class SidModel { FromTune, Mos6581, Mos8580 }; // SPEC-077
enum class C64Clock { FromTune, Pal, Ntsc };

struct EngineSettings {
    EngineKind kind = EngineKind::Light;
    unsigned sample_rate = 48000;
    SidModel model = SidModel::FromTune;
    C64Clock clock = C64Clock::FromTune;
    std::vector<std::uint8_t> kernal, basic, chargen;  // empty when not set (SPEC-068)
};

class Engine {
public:
    static const int kRegisters = 32;

    virtual ~Engine() = default;

    // Prepares a subtune (counted from 1) of a tune given as its file's bytes. On failure says why.
    virtual bool load(const std::vector<std::uint8_t>& tune, int subtune, const EngineSettings& settings,
                      std::string* why) = 0;
    // Renders up to `frames` stereo frames (two 16-bit samples each) and returns how many it wrote.
    virtual std::size_t render(std::int16_t* out, std::size_t frames) = 0;
    // How many SID chips the loaded tune uses: 1 to 3.
    virtual int chips() const = 0;
    virtual void set_muted(int chip, int voice, bool muted) = 0;
    virtual void set_filter(int chip, bool on) = 0;
    // The values last written to a chip's registers. False if this engine cannot say.
    virtual bool registers(int chip, std::uint8_t out[kRegisters]) = 0;
};

}  // namespace spz
