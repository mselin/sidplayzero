// SPDX-License-Identifier: GPL-3.0-or-later
// Which engine plays a tune (SPEC-004, SPEC-072): the choice, the built-in tune the engines are measured
// with, the measurement, and the file the speed factors are kept in.
#include "spz/engine_choice.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <system_error>
#include <vector>

#include "spz/path_text.h"
#include "spz/safe_file.h"
#include "spz/text.h"

namespace spz {

namespace {

const char kSpeedFile[] = "speed.ini";
const std::size_t kSpeedFileLimit = 4096;   // the file is a few lines; anything longer is not ours

using path_text::join;
using text::trim;

std::string number_text(double value) {
    char buffer[64];
    const std::to_chars_result made = std::to_chars(buffer, buffer + sizeof buffer, value);
    if (made.ec != std::errc()) return "0";
    return std::string(buffer, made.ptr);
}

// A number as the file holds it: the whole value, and nothing after it.
bool read_number(const std::string& text, double* out) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result read = std::from_chars(first, last, *out);
    return read.ec == std::errc() && read.ptr == last;
}

bool read_whole(const std::string& text, long long* out) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result read = std::from_chars(first, last, *out);
    return read.ec == std::errc() && read.ptr == last;
}

// The PSID file of the built-in tune: a version 2 header and 37 bytes of the app's own code. Its init
// routine starts voice 1 of the chip on a sawtooth; its play routine moves the voice's pitch on at each
// call, so that there is something to render all the time.
Bytes make_builtin_tune() {
    Bytes tune(0x7C, 0);
    auto put_word = [&tune](std::size_t at, unsigned value) {
        tune[at] = static_cast<std::uint8_t>(value >> 8);
        tune[at + 1] = static_cast<std::uint8_t>(value & 0xFF);
    };
    auto put_text = [&tune](std::size_t at, const char* text) {
        for (std::size_t i = 0; text[i] != 0 && i < 32; ++i) tune[at + i] = static_cast<std::uint8_t>(text[i]);
    };
    tune[0] = 'P';
    tune[1] = 'S';
    tune[2] = 'I';
    tune[3] = 'D';
    put_word(0x04, 2);          // version
    put_word(0x06, 0x7C);       // data offset
    put_word(0x08, 0);          // load address: the first two bytes of the data
    put_word(0x0A, 0x1000);     // init
    put_word(0x0C, 0x101A);     // play
    put_word(0x0E, 1);          // songs
    put_word(0x10, 1);          // default song
    put_text(0x16, "SidPlayZero speed test");
    put_text(0x36, "SidPlayZero");
    put_text(0x56, "2026 SidPlayZero");
    put_word(0x76, 0x14);       // flags: PAL, 6581
    const std::uint8_t data[] = {
        0x00, 0x10,             // loaded at $1000
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,   // init: volume 15
        0xA9, 0x09, 0x8D, 0x05, 0xD4,   //       attack and decay
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,   //       sustain and release
        0xA9, 0x11, 0x8D, 0x01, 0xD4,   //       frequency, high byte
        0xA9, 0x21, 0x8D, 0x04, 0xD4,   //       sawtooth, gate on
        0x60,                           //       RTS
        0xEE, 0x24, 0x10,               // play: INC $1024
        0xAD, 0x24, 0x10,               //       LDA $1024
        0x8D, 0x01, 0xD4,               //       STA $D401
        0x60,                           //       RTS
        0x00,                           // $1024: the counter
    };
    tune.insert(tune.end(), data, data + sizeof data);
    return tune;
}

// Renders `frames` frames, a chunk at a time. False if the engine stops giving any.
bool render_for(Engine* engine, std::size_t frames, std::vector<std::int16_t>* buffer) {
    const std::size_t chunk = buffer->size() / 2;
    while (frames > 0) {
        const std::size_t asked = frames < chunk ? frames : chunk;
        const std::size_t got = engine->render(buffer->data(), asked);
        if (got == 0) return false;
        frames -= got < frames ? got : frames;
    }
    return true;
}

}  // namespace

EngineKind choose_engine(EngineChoice choice, const SpeedFactors& factors, int chips) {
    switch (choice) {
        case EngineChoice::Accurate: return EngineKind::Accurate;
        case EngineChoice::Light: return EngineKind::Light;
        case EngineChoice::Auto: break;
    }
    if (!factors.measured) return EngineKind::Light;
    if (chips < 1) chips = 1;
    return factors.accurate / chips >= kAccurateFactorNeeded ? EngineKind::Accurate : EngineKind::Light;
}

const Bytes& builtin_tune() {
    static const Bytes tune = make_builtin_tune();
    return tune;
}

double measure_engine(Engine* engine, EngineKind kind, int sample_rate, Clock* clock) {
    if (engine == nullptr || clock == nullptr || sample_rate <= 0) return 0;
    EngineSettings settings;
    settings.kind = kind;
    settings.sample_rate = static_cast<unsigned>(sample_rate);
    std::string why;
    if (!engine->load(builtin_tune(), 1, settings, &why)) return 0;
    const std::size_t second = static_cast<std::size_t>(sample_rate);
    std::vector<std::int16_t> buffer(2 * (second / 100 > 0 ? second / 100 : 1));     // 10 ms at a time
    if (!render_for(engine, second / 2, &buffer)) return 0;                         // warming up, untimed
    std::int64_t slowest = 0;
    for (int run = 0; run < 3; ++run) {
        const std::int64_t began = clock->now_ms();
        if (!render_for(engine, second, &buffer)) return 0;
        const std::int64_t took = clock->now_ms() - began;
        if (took > slowest) slowest = took;
    }
    if (slowest < 1) slowest = 1;       // faster than the clock can tell: as fast as it can tell
    return 1000.0 / static_cast<double>(slowest);
}

bool save_speed_factors(Files* files, const std::string& cache_folder, const SpeedFactors& factors,
                        const std::string& app_version, int sample_rate) {
    if (files == nullptr) return false;
    const std::string text = "# SidPlayZero: how fast each engine renders on this device (SPEC-072)\n"
                             "version = " + app_version + "\n"
                             "sample_rate = " + std::to_string(sample_rate) + "\n"
                             "accurate = " + number_text(factors.accurate) + "\n"
                             "light = " + number_text(factors.light) + "\n";
    const Bytes bytes(text.begin(), text.end());
    return safe_write(files, join(cache_folder, kSpeedFile), bytes) == FileStatus::Ok;
}

SpeedFactors load_speed_factors(Files* files, const std::string& cache_folder, const std::string& app_version,
                                int sample_rate) {
    SpeedFactors none;
    if (files == nullptr) return none;
    Bytes bytes;
    if (files->read(join(cache_folder, kSpeedFile), kSpeedFileLimit, &bytes) != FileStatus::Ok) return none;
    // Every line ends in a line feed: a file cut short anywhere loses at least its last one (SPEC-202).
    if (bytes.empty() || bytes.back() != '\n') return none;
    std::map<std::string, std::string> values;
    std::size_t at = 0;
    while (at < bytes.size()) {
        std::size_t end = at;
        while (end < bytes.size() && bytes[end] != '\n') ++end;
        std::string line(reinterpret_cast<const char*>(bytes.data()) + at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find('\0') != std::string::npos) return none;
        const std::string bare = trim(line);
        if (bare.empty() || bare[0] == '#') continue;
        const std::size_t equals = bare.find('=');
        if (equals == std::string::npos) return none;
        values[trim(bare.substr(0, equals))] = trim(bare.substr(equals + 1));
    }
    const char* const keys[] = {"version", "sample_rate", "accurate", "light"};
    for (const char* key : keys) {
        if (values.count(key) == 0) return none;
    }
    long long rate = 0;
    if (values["version"] != app_version || !read_whole(values["sample_rate"], &rate) || rate != sample_rate) return none;
    SpeedFactors factors;
    if (!read_number(values["accurate"], &factors.accurate) || !read_number(values["light"], &factors.light)) return none;
    if (!std::isfinite(factors.accurate) || !std::isfinite(factors.light) || factors.accurate <= 0 || factors.light <= 0) {
        return none;
    }
    factors.measured = true;
    return factors;
}

}  // namespace spz
