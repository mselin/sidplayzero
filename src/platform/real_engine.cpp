// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/real_engine.h"

#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/builders/sidlite.h>
#include <sidplayfp/sidbuilder.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/sidversion.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#ifndef SPZ_ENGINE_SANITIZER
#define SPZ_ENGINE_SANITIZER ""
#endif

namespace spz::platform {

namespace {

const int kMaxChips = 3;
const int kVoices = 3;
// The library is run this many cycles at a time, as the `sidplayfp` program runs it.
const unsigned kCyclesPerPlay = 2000;

#define SPZ_STRINGIFY2(x) #x
#define SPZ_STRINGIFY(x) SPZ_STRINGIFY2(x)
const char kLibraryVersion[] = "libsidplayfp " SPZ_STRINGIFY(LIBSIDPLAYFP_VERSION_MAJ) "." SPZ_STRINGIFY(
    LIBSIDPLAYFP_VERSION_MIN) "." SPZ_STRINGIFY(LIBSIDPLAYFP_VERSION_LEV);

}  // namespace

struct RealEngine::Impl {
    std::unique_ptr<sidplayfp> player;
    std::unique_ptr<sidbuilder> builder;
    std::unique_ptr<SidTune> tune;
    std::vector<std::uint8_t> kernal, basic, chargen;   // the library keeps pointers to these

    std::vector<short> chunk;     // what one play() and mix() give
    std::vector<short> pending;   // samples rendered and not yet handed out, left and right in turn
    std::size_t pending_at = 0;
    bool loaded = false;
    bool failed = false;

    // Asked for on any thread, taken up by render().
    std::atomic<bool> muted[kMaxChips][kVoices];
    std::atomic<bool> filter[kMaxChips];
    std::atomic<unsigned> asked{0};     // bumped by every change, so render() knows to look
    unsigned applied = 0;

    // Given out on any thread.
    std::atomic<int> chip_count{1};
    std::atomic<std::uint8_t> regs[kMaxChips][Engine::kRegisters];

    Impl() {
        for (int chip = 0; chip < kMaxChips; ++chip) {
            for (int voice = 0; voice < kVoices; ++voice) muted[chip][voice].store(false);
            filter[chip].store(true);
            for (int r = 0; r < Engine::kRegisters; ++r) regs[chip][r].store(0);
        }
    }

    void apply_controls() {
        const unsigned now = asked.load(std::memory_order_acquire);
        if (now == applied) return;
        applied = now;
        for (unsigned chip = 0; chip < kMaxChips; ++chip) {
            for (unsigned voice = 0; voice < kVoices; ++voice) {
                player->mute(chip, voice, muted[chip][voice].load(std::memory_order_relaxed));
            }
            player->filter(chip, filter[chip].load(std::memory_order_relaxed));
        }
    }

    void keep_registers() {
        const int count = chip_count.load(std::memory_order_relaxed);
        std::uint8_t values[Engine::kRegisters];
        for (int chip = 0; chip < count; ++chip) {
            if (!player->getSidStatus(static_cast<unsigned>(chip), values)) continue;
            for (int r = 0; r < Engine::kRegisters; ++r) regs[chip][r].store(values[r], std::memory_order_relaxed);
        }
    }
};

RealEngine::RealEngine() : impl_(new Impl) {}
RealEngine::~RealEngine() {
    if (impl_->player) impl_->player->load(nullptr);
}

bool RealEngine::load(const std::vector<std::uint8_t>& tune, int subtune, const EngineSettings& settings,
                      std::string* why) {
    Impl& d = *impl_;
    auto refuse = [&](const char* reason) {
        d.loaded = false;
        if (d.player) d.player->load(nullptr);
        d.player.reset();
        d.builder.reset();
        d.tune.reset();
        if (why) *why = reason;
        return false;
    };

    // A new player and a new chip emulation for each tune, as the `sidplayfp` program makes them, so that
    // nothing of the tune before carries over (SPEC-071).
    if (d.player) d.player->load(nullptr);
    d.player.reset();
    d.builder.reset();
    d.loaded = false;
    d.failed = false;
    d.pending.clear();
    d.pending_at = 0;

    if (tune.empty() || tune.size() > 0xffffffffu) return refuse("MSG_ENGINE_REFUSED");
    d.tune.reset(new SidTune(tune.data(), static_cast<uint_least32_t>(tune.size())));
    if (!d.tune->getStatus()) return refuse("MSG_ENGINE_REFUSED");
    d.tune->selectSong(subtune > 0 ? static_cast<unsigned>(subtune) : 0u);
    const SidTuneInfo* info = d.tune->getInfo();
    if (info == nullptr) return refuse("MSG_ENGINE_REFUSED");

    // The ROMs, when set and of the right size (SPEC-068); the player has already checked them.
    d.kernal = settings.kernal.size() == 8192 ? settings.kernal : std::vector<std::uint8_t>();
    d.basic = settings.basic.size() == 8192 ? settings.basic : std::vector<std::uint8_t>();
    d.chargen = settings.chargen.size() == 4096 ? settings.chargen : std::vector<std::uint8_t>();
    if (info->compatibility() == SidTuneInfo::COMPATIBILITY_BASIC && d.basic.empty()) {
        return refuse("MSG_NEEDS_BASIC");
    }

    // The same steps, in the same order, as the `sidplayfp` program takes them (SPEC-071): the settings
    // first, with no tune and no chips, so that the tune is first set up with a power-on delay of 0; then
    // the tune; then the chip emulation.
    d.player.reset(new sidplayfp);
    SidConfig config = d.player->config();
    config.frequency = settings.sample_rate;
    config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
    config.powerOnDelay = 0;
    config.ciaModel = SidConfig::MOS6526;
    config.digiBoost = false;
    config.defaultSidModel = settings.model == SidModel::Mos8580 ? SidConfig::MOS8580 : SidConfig::MOS6581;
    config.forceSidModel = settings.model != SidModel::FromTune;
    config.defaultC64Model = settings.clock == C64Clock::Ntsc ? SidConfig::NTSC : SidConfig::PAL;
    config.forceC64Model = settings.clock != C64Clock::FromTune;
    config.sidEmulation = nullptr;
    if (!d.player->config(config)) return refuse("MSG_ENGINE_REFUSED");
    d.player->setRoms(d.kernal.empty() ? nullptr : d.kernal.data(), d.basic.empty() ? nullptr : d.basic.data(),
                      d.chargen.empty() ? nullptr : d.chargen.data());
    if (!d.player->load(d.tune.get())) return refuse("MSG_ENGINE_REFUSED");

    if (settings.kind == EngineKind::Accurate) {
        auto* residfp = new ReSIDfpBuilder("SidPlayZero");
        d.builder.reset(residfp);
        // What the `sidplayfp` program sets with no configuration file of its own.
        residfp->combinedWaveformsStrength(SidConfig::AVERAGE);
        residfp->filter6581Range(0.5);
        residfp->filter6581Curve(0.5);
        residfp->filter8580Curve(0.5);
        residfp->enableOld6581caps(false);
        residfp->dacLeakage(1.0);
        residfp->offset6581(1.0);
        residfp->dcbRes(0.0);
    } else {
        d.builder.reset(new SIDLiteBuilder("SidPlayZero"));
    }
    config.sidEmulation = d.builder.get();
    if (!d.player->config(config)) return refuse("MSG_ENGINE_REFUSED");

    // Everything on: a new tune has no voice muted and every filter on (SPEC-074).
    for (int chip = 0; chip < kMaxChips; ++chip) {
        for (int voice = 0; voice < kVoices; ++voice) d.muted[chip][voice].store(false);
        d.filter[chip].store(true);
        for (int r = 0; r < kRegisters; ++r) d.regs[chip][r].store(0);
    }
    for (unsigned chip = 0; chip < kMaxChips; ++chip) d.player->filter(chip, true);
    d.player->initMixer(true);
    for (unsigned chip = 0; chip < kMaxChips; ++chip) {
        for (unsigned voice = 0; voice < kVoices; ++voice) d.player->mute(chip, voice, false);
    }
    d.applied = d.asked.load();

    const int chips = static_cast<int>(d.player->installedSIDs());
    d.chip_count.store(std::max(1, std::min(kMaxChips, chips)));
    const int size = d.player->getBufSize(kCyclesPerPlay);
    d.chunk.assign(static_cast<std::size_t>(std::max(size, 0)) * 2 + 64, 0);
    d.pending.reserve(d.chunk.size() * 2);
    d.loaded = true;
    return true;
}

std::size_t RealEngine::render(std::int16_t* out, std::size_t frames) {
    Impl& d = *impl_;
    if (!d.loaded || d.failed) return 0;
    d.apply_controls();
    std::size_t done = 0;
    while (done < frames) {
        if (d.pending_at < d.pending.size()) {
            const std::size_t have = (d.pending.size() - d.pending_at) / 2;
            const std::size_t take = std::min(have, frames - done);
            std::memcpy(out + 2 * done, d.pending.data() + d.pending_at, take * 2 * sizeof(std::int16_t));
            d.pending_at += take * 2;
            done += take;
            continue;
        }
        d.pending.clear();
        d.pending_at = 0;
        const int samples = d.player->play(kCyclesPerPlay);
        if (samples < 0) {
            d.failed = true;
            break;
        }
        if (samples == 0) continue;
        const unsigned mixed = d.player->mix(d.chunk.data(), static_cast<unsigned>(samples));
        d.pending.insert(d.pending.end(), d.chunk.begin(), d.chunk.begin() + mixed);
    }
    d.keep_registers();
    return done;
}

int RealEngine::chips() const { return impl_->chip_count.load(std::memory_order_relaxed); }

void RealEngine::set_muted(int chip, int voice, bool muted) {
    if (chip < 0 || chip >= kMaxChips || voice < 0 || voice >= kVoices) return;
    impl_->muted[chip][voice].store(muted, std::memory_order_relaxed);
    impl_->asked.fetch_add(1, std::memory_order_release);
}

void RealEngine::set_filter(int chip, bool on) {
    if (chip < 0 || chip >= kMaxChips) return;
    impl_->filter[chip].store(on, std::memory_order_relaxed);
    impl_->asked.fetch_add(1, std::memory_order_release);
}

bool RealEngine::registers(int chip, std::uint8_t out[kRegisters]) {
    if (chip < 0 || chip >= chips()) return false;
    for (int r = 0; r < kRegisters; ++r) out[r] = impl_->regs[chip][r].load(std::memory_order_relaxed);
    return true;
}

const char* engine_library_version() { return kLibraryVersion; }

const char* engine_sanitizer() { return SPZ_ENGINE_SANITIZER; }

}  // namespace spz::platform
