// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 fuzz: bytes that pass SPEC-203's check, given to the real engine and rendered for one second
//
// The one target that is not a reader of `core`: what the tune check lets through goes to libsidplayfp,
// built with the same sanitizers (cmake/engine.cmake), through the real Engine of `platform`. The input is
// used as it is when it passes the check. So that every input reaches the engine, it is also made into a
// tune whose header is mended to pass the check: the input fills the header from its eighth byte on (the
// addresses, the songs, the speed, the texts and the flags), what the check asks of the magic, the version,
// the data offset and the songs is set, and the rest of the input is the tune's data. That tune is loaded
// and rendered for 100 ms as it is, and then, with addresses the library accepts, for one second.
//
// A second is rendered with sidlite, the engine `auto` plays with until the engines' speed has been
// measured (SPEC-072), and then 100 ms with reSIDfp: under these sanitizers reSIDfp renders a second in
// about two, which is all the time an input may take (SPEC-205).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "platform/real_engine.h"
#include "spz/tune.h"

namespace {

const std::size_t kHeader = 0x7C;

void render(const std::vector<std::uint8_t>& tune, bool accurate, int chunks) {
    spz::platform::RealEngine engine;
    spz::EngineSettings settings;
    settings.kind = accurate ? spz::EngineKind::Accurate : spz::EngineKind::Light;
    settings.sample_rate = 48000;
    std::string why;
    if (!engine.load(tune, 0, settings, &why)) return;
    std::vector<std::int16_t> out(2 * 960);
    for (int chunk = 0; chunk < chunks; ++chunk) {      // chunks of 20 ms
        if (engine.render(out.data(), 960) == 0) break;
    }
    std::uint8_t registers[spz::Engine::kRegisters];
    for (int chip = 0; chip < engine.chips(); ++chip) engine.registers(chip, registers);
}

// One second with sidlite; with `accurate`, 100 ms with reSIDfp as well.
void render_one_second(const std::vector<std::uint8_t>& tune, bool accurate) {
    if (!spz::check_tune(tune).empty()) return;
    render(tune, false, 50);
    if (accurate) render(tune, true, 5);
}

// A plain tune: PSID version 2, one song, load address 0x1000, an init and a play routine that only return.
std::vector<std::uint8_t> plain_tune(std::uint8_t flags) {
    std::vector<std::uint8_t> tune(kHeader, 0);
    std::memcpy(tune.data(), "PSID", 4);
    tune[5] = 2;
    tune[7] = static_cast<std::uint8_t>(kHeader);
    tune[8] = 0x10;                       // load address 0x1000
    tune[10] = 0x10;                      // init 0x1000
    tune[12] = 0x10;                      // play 0x1001
    tune[13] = 0x01;
    tune[15] = 1;                         // one song
    tune[17] = 1;
    tune[0x77] = flags;                   // the SID model
    tune.push_back(0x60);                 // RTS
    tune.push_back(0x60);
    return tune;
}

}  // namespace

// reSIDfp works out its filters' and its resampler's tables once in a process, which under the sanitizers
// takes seconds: it is done here, before the inputs are timed, for both chip models and both clocks.
extern "C" int LLVMFuzzerInitialize(int*, char***) {
    for (const int flags : {0x14, 0x18, 0x24, 0x28}) {
        render(plain_tune(static_cast<std::uint8_t>(flags)), true, 1);
        render(plain_tune(static_cast<std::uint8_t>(flags)), false, 1);
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const std::vector<std::uint8_t> asis(data, data + size);
    render_one_second(asis, true);

    // The same bytes behind a header mended to pass the check.
    std::vector<std::uint8_t> tune(kHeader, 0);
    std::memcpy(tune.data(), "PSID", 4);
    const std::size_t header_part = size > 8 ? (size < kHeader ? size : kHeader) : 8;
    for (std::size_t i = 8; i < header_part; ++i) tune[i] = data[i];
    tune[4] = 0;
    tune[5] = static_cast<std::uint8_t>(2 + (size > 5 ? data[5] % 3 : 0));   // version 2 to 4
    tune[6] = 0;
    tune[7] = static_cast<std::uint8_t>(kHeader);
    const unsigned songs = 1 + ((static_cast<unsigned>(tune[14]) << 8 | tune[15]) % 256);
    tune[14] = static_cast<std::uint8_t>(songs >> 8);
    tune[15] = static_cast<std::uint8_t>(songs & 0xff);
    const unsigned start = (static_cast<unsigned>(tune[16]) << 8 | tune[17]) % (songs + 1);
    tune[16] = static_cast<std::uint8_t>(start >> 8);
    tune[17] = static_cast<std::uint8_t>(start & 0xff);
    if (size > kHeader) tune.insert(tune.end(), data + kHeader, data + size);
    tune.push_back(0x60);   // at least one byte of data: RTS
    if (spz::check_tune(tune).empty()) render(tune, false, 5);

    // And once more with addresses the library takes, so that the input's bytes run as the tune's code:
    // loaded at 0x1000, init there, play at 0x1003.
    tune[8] = 0x10;
    tune[9] = 0x00;
    tune[10] = 0x10;
    tune[11] = 0x00;
    tune[12] = 0x10;
    tune[13] = 0x03;
    render_one_second(tune, true);
    return 0;
}
