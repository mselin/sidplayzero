// SPDX-License-Identifier: GPL-3.0-or-later
//
// sidplayzero-bench: how fast do the two engines of libsidplayfp render on this machine?
//
// This is a measuring tool for the specification (SPEC-262), not part of the app. It renders a small
// tune of its own, set up for one, two and three SID chips, with each engine (reSIDfp, the accurate
// one, and sidlite, the light one) at 44,100 and 48,000 Hz, and prints how many seconds of sound each
// renders per second of processor time ("speed factor"). It reads no file but /proc and /sys, writes
// nothing, and uses no network.
//
// The tune is original: a few dozen bytes of 6502 code written below, so no music has to be shipped.
// With a path as its first argument it times that tune file instead.

#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/builders/sidlite.h>
#include <sidplayfp/sidplayfp.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

const unsigned kWarmUpSeconds = 1;
const unsigned kTimedSeconds = 10;

// Appends a 6502 "LDA #value; STA address" pair.
void set(std::vector<uint8_t>& code, uint16_t address, uint8_t value) {
    const uint8_t bytes[] = {0xA9, value, 0x8D, static_cast<uint8_t>(address & 0xFF), static_cast<uint8_t>(address >> 8)};
    code.insert(code.end(), bytes, bytes + sizeof bytes);
}

// Appends "STA address".
void store(std::vector<uint8_t>& code, uint16_t address) {
    const uint8_t bytes[] = {0x8D, static_cast<uint8_t>(address & 0xFF), static_cast<uint8_t>(address >> 8)};
    code.insert(code.end(), bytes, bytes + sizeof bytes);
}

// A PSID file for `chips` SID chips (1 to 3) at $D400, $D420 and $D440. Fifty times a second its play
// routine moves the pitch of all three voices of each chip, gates them on and off, and sweeps the
// filter, with every voice routed through the filter: all of each chip is kept busy.
std::vector<uint8_t> make_tune(int chips) {
    const uint16_t load = 0x1000;
    std::vector<uint8_t> code;
    // init: LDA #0 ; STA $FB ; RTS
    const uint8_t init[] = {0xA9, 0x00, 0x85, 0xFB, 0x60};
    code.insert(code.end(), init, init + sizeof init);
    const uint16_t play = static_cast<uint16_t>(load + code.size());
    code.push_back(0xE6); code.push_back(0xFB);                       // INC $FB
    for (int chip = 0; chip < chips; ++chip) {
        const uint16_t base = static_cast<uint16_t>(0xD400 + 0x20 * chip);
        for (int voice = 0; voice < 3; ++voice) {
            const uint16_t v = static_cast<uint16_t>(base + 7 * voice);
            code.push_back(0xA5); code.push_back(0xFB);               // LDA $FB
            for (int shift = 0; shift < voice; ++shift) code.push_back(0x0A);  // ASL: a different pitch per voice
            store(code, static_cast<uint16_t>(v + 1));                // frequency, high byte
            store(code, static_cast<uint16_t>(v + 2));                // pulse width, low byte
            set(code, static_cast<uint16_t>(v + 3), 0x08);            // pulse width, high bits
            set(code, static_cast<uint16_t>(v + 5), 0x09);            // attack and decay
            set(code, static_cast<uint16_t>(v + 6), 0xF8);            // sustain and release
            code.push_back(0xA5); code.push_back(0xFB);               // LDA $FB
            const uint8_t gate[] = {0x4A, 0x4A, 0x4A, 0x4A, 0x29, 0x01};  // four LSR, AND #1: the gate changes every 16 calls
            code.insert(code.end(), gate, gate + sizeof gate);
            code.push_back(0x09);                                     // ORA #waveform
            code.push_back(voice == 0 ? 0x40 : voice == 1 ? 0x20 : 0x10);  // pulse, sawtooth, triangle
            store(code, static_cast<uint16_t>(v + 4));                // control register
        }
        code.push_back(0xA5); code.push_back(0xFB);                   // LDA $FB
        store(code, static_cast<uint16_t>(base + 0x16));              // filter cut-off, high byte
        set(code, static_cast<uint16_t>(base + 0x17), 0xF7);          // full resonance, all three voices filtered
        set(code, static_cast<uint16_t>(base + 0x18), 0x1F);          // low-pass, volume 15
    }
    code.push_back(0x60);                                             // RTS

    std::vector<uint8_t> file(0x7C, 0);
    const char magic[] = {'P', 'S', 'I', 'D'};
    for (int i = 0; i < 4; ++i) file[i] = static_cast<uint8_t>(magic[i]);
    const uint8_t version = chips == 1 ? 2 : chips == 2 ? 3 : 4;
    file[0x05] = version;                                             // version
    file[0x07] = 0x7C;                                                // data offset
    file[0x08] = static_cast<uint8_t>(load >> 8); file[0x09] = static_cast<uint8_t>(load & 0xFF);
    file[0x0A] = static_cast<uint8_t>(load >> 8); file[0x0B] = static_cast<uint8_t>(load & 0xFF);  // init
    file[0x0C] = static_cast<uint8_t>(play >> 8); file[0x0D] = static_cast<uint8_t>(play & 0xFF);
    file[0x0F] = 1;                                                   // one song
    file[0x11] = 1;                                                   // start song
    const std::string title = "Engine benchmark", author = "SidPlayZero", released = "2026 SidPlayZero";
    for (size_t i = 0; i < title.size(); ++i) file[0x16 + i] = static_cast<uint8_t>(title[i]);
    for (size_t i = 0; i < author.size(); ++i) file[0x36 + i] = static_cast<uint8_t>(author[i]);
    for (size_t i = 0; i < released.size(); ++i) file[0x56 + i] = static_cast<uint8_t>(released[i]);
    // Flags: PAL (bits 2-3 = 01) and a 6581 for each chip (bits 4-5, 6-7 and 8-9 = 01).
    uint16_t flags = 0x0004 | 0x0010;
    if (chips >= 2) { flags |= 0x0040; file[0x7A] = 0x42; }           // second chip at $D420
    if (chips >= 3) { flags |= 0x0100; file[0x7B] = 0x44; }           // third chip at $D440
    file[0x76] = static_cast<uint8_t>(flags >> 8); file[0x77] = static_cast<uint8_t>(flags & 0xFF);
    file.insert(file.end(), code.begin(), code.end());
    return file;
}

// Seconds of processor time this thread has used: rendering is one thread, and wall time would count
// whatever else the machine was doing.
double cpu_seconds() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Renders `seconds` of the tune; returns false on an engine error.
bool render(sidplayfp& player, std::vector<short>& buffer, unsigned rate, unsigned seconds) {
    size_t frames = 0;
    while (frames < static_cast<size_t>(rate) * seconds) {
        const int produced = player.play(2000);                       // run the C64 for 2,000 cycles
        if (produced < 0) return false;
        frames += player.mix(buffer.data(), static_cast<unsigned>(produced));
    }
    return true;
}

// Returns the speed factor, or a negative number if the tune could not be played.
double measure(const std::vector<uint8_t>& bytes, bool accurate, unsigned rate, int* chips_found) {
    SidTune tune(bytes.data(), static_cast<unsigned>(bytes.size()));
    if (!tune.getStatus()) return -1;
    tune.selectSong(0);
    if (chips_found) *chips_found = tune.getInfo()->sidChips();
    sidplayfp player;
    std::unique_ptr<sidbuilder> builder;
    if (accurate) builder.reset(new ReSIDfpBuilder("residfp"));
    else builder.reset(new SIDLiteBuilder("sidlite"));
    SidConfig config = player.config();
    config.frequency = rate;
    config.sidEmulation = builder.get();
    config.powerOnDelay = 0;
    config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;          // the library's default, and the app's
    if (!player.config(config) || !player.load(&tune)) return -1;
    player.initMixer(true);                                           // stereo, as the app plays
    std::vector<short> buffer(static_cast<size_t>(rate) * 2);         // room for a second of stereo
    if (!render(player, buffer, rate, kWarmUpSeconds)) return -1;
    const double before = cpu_seconds();
    if (!render(player, buffer, rate, kTimedSeconds)) return -1;
    const double taken = cpu_seconds() - before;
    return taken > 0 ? kTimedSeconds / taken : -1;
}

std::string first_line_value(const char* path, const std::string& key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (key.empty()) return line;
        if (line.compare(0, key.size(), key) == 0) {
            const size_t colon = line.find(':');
            if (colon != std::string::npos) return line.substr(line.find_first_not_of(" \t", colon + 1));
        }
    }
    return "unknown";
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("engine_bench_version = 1\n");
    std::printf("engine_bench_library = libsidplayfp %s\n", sidplayfp().info().version());
    std::string model = first_line_value("/proc/cpuinfo", "model name");
    if (model == "unknown") model = first_line_value("/proc/cpuinfo", "Model");
    if (model == "unknown") model = first_line_value("/proc/cpuinfo", "CPU part");
    std::printf("engine_bench_cpu = %s\n", model.c_str());
    std::printf("engine_bench_cpu_khz = %s\n", first_line_value("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "").c_str());
    std::printf("engine_bench_governor = %s\n", first_line_value("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "").c_str());

    if (argc > 1) {                                                   // a tune file of the user's own
        std::ifstream in(argv[1], std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (int accurate = 1; accurate >= 0; --accurate) {
            int chips = 0;
            const double factor = measure(bytes, accurate != 0, 48000, &chips);
            if (factor < 0) { std::fprintf(stderr, "could not play that file\n"); return 1; }
            std::printf("engine_bench_file_%s_%dsid_48000 = %.2f\n", accurate ? "accurate" : "light", chips, factor);
        }
        return 0;
    }

    int failures = 0;
    for (int accurate = 1; accurate >= 0; --accurate) {
        for (int chips = 1; chips <= 3; ++chips) {
            const std::vector<uint8_t> tune = make_tune(chips);
            for (unsigned rate : {44100u, 48000u}) {
                int found = 0;
                const double factor = measure(tune, accurate != 0, rate, &found);
                if (factor < 0 || found != chips) {
                    std::printf("engine_bench_%s_%dsid_%u = failed\n", accurate ? "accurate" : "light", chips, rate);
                    ++failures;
                } else {
                    std::printf("engine_bench_%s_%dsid_%u = %.2f\n", accurate ? "accurate" : "light", chips, rate, factor);
                }
                std::fflush(stdout);
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
