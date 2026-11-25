// SPDX-License-Identifier: GPL-3.0-or-later
// Which engine plays a tune, and how it is set: the setting and the measurement (SPEC-004, SPEC-072), what
// the player asks of the engine for each tune (SPEC-067, SPEC-077), and what it does when the engine
// cannot keep up (SPEC-079). Against the fake SoundBackend, Engine, Clock and Files.
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/engine_choice.h"
#include "spz/player.h"
#include "spz/tune.h"
#include "support.h"

using spz::EngineChoice;
using spz::EngineKind;
using spz::PlayerState;
using namespace spz::test;

namespace {

spz::SpeedFactors measured(double accurate, double light = 150.0) {
    spz::SpeedFactors factors;
    factors.measured = true;
    factors.accurate = accurate;
    factors.light = light;
    return factors;
}

const EngineKind A = EngineKind::Accurate;      // reSIDfp
const EngineKind L = EngineKind::Light;         // sidlite

// SPEC-072's table: the accurate engine's factor, and the engine for one, two and three SID chips.
struct Row {
    double factor;
    EngineKind chips[3];
};
const Row kTable[] = {
    {8.0, {A, A, A}},
    {5.5, {A, A, L}},
    {2.6, {A, L, L}},
    {2.4, {L, L, L}},
};

const char* const kChipTunes[3] = {kChips1, kChips2, kChips3};

}  // namespace

// ---- SPEC-072, SPEC-004: the choice --------------------------------------------------------------------------------

TEST_CASE("SPEC-072 with `auto`, a tune of n SID chips is played with reSIDfp when the stored factor divided by n is at least 2.5: the item's table") {
    for (const Row& row : kTable) {
        for (int chips = 1; chips <= 3; ++chips) {
            CAPTURE(row.factor);
            CAPTURE(chips);
            CHECK(spz::choose_engine(EngineChoice::Auto, measured(row.factor), chips) == row.chips[chips - 1]);
        }
    }
}

TEST_CASE("SPEC-072 the threshold is 2.5 for each chip, and reSIDfp is chosen at exactly that") {
    CHECK(spz::kAccurateFactorNeeded == 2.5);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(2.5), 1) == A);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(2.49), 1) == L);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(5.0), 2) == A);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(4.99), 2) == L);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(7.5), 3) == A);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(7.49), 3) == L);
    // The light engine's own factor plays no part.
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(8.0, 1.0), 1) == A);
    CHECK(spz::choose_engine(EngineChoice::Auto, measured(2.0, 500.0), 1) == L);
}

TEST_CASE("SPEC-072 until the measurement is done, `auto` means sidlite") {
    const spz::SpeedFactors nothing_yet;
    REQUIRE_FALSE(nothing_yet.measured);
    for (int chips = 1; chips <= 3; ++chips) CHECK(spz::choose_engine(EngineChoice::Auto, nothing_yet, chips) == L);
}

TEST_CASE("SPEC-004 `accurate` is reSIDfp and `light` is sidlite, whatever was measured and however many chips") {
    for (const double factor : {8.0, 2.4, 0.1}) {
        for (int chips = 1; chips <= 3; ++chips) {
            CHECK(spz::choose_engine(EngineChoice::Accurate, measured(factor), chips) == A);
            CHECK(spz::choose_engine(EngineChoice::Light, measured(factor), chips) == L);
        }
    }
    const spz::SpeedFactors nothing_yet;
    CHECK(spz::choose_engine(EngineChoice::Accurate, nothing_yet, 3) == A);
    CHECK(spz::choose_engine(EngineChoice::Light, nothing_yet, 1) == L);
    CHECK(spz::Settings().engine == EngineChoice::Auto);    // the default
}

TEST_CASE("SPEC-004 SPEC-072 with a stored speed measurement, each setting gives the expected engine for the one-, two- and three-SID fixture tunes") {
    for (const Row& row : kTable) {
        CAPTURE(row.factor);
        PlayerFixture f;
        // The measurement as the app keeps it: stored in the cache folder, and read back at a start.
        const std::string cache = f.home.file("cache");
        std::filesystem::create_directories(cache);
        REQUIRE(spz::save_speed_factors(&f.files, cache, measured(row.factor), "1.0.0", 48000));
        const spz::SpeedFactors stored = spz::load_speed_factors(&f.files, cache, "1.0.0", 48000);
        REQUIRE(stored.measured);
        f.player->set_speed_factors(stored);
        for (int chips = 1; chips <= 3; ++chips) {
            CAPTURE(chips);
            const char* tune = kChipTunes[chips - 1];
            f.settings.engine = EngineChoice::Auto;
            f.play({tune}, 0);
            REQUIRE(f.state() == PlayerState::Playing);
            CHECK(f.status().chips == chips);
            CHECK(f.backend.loaded_settings.kind == row.chips[chips - 1]);
            CHECK(f.status().engine == row.chips[chips - 1]);   // what the now-playing screen names
            f.settings.engine = EngineChoice::Accurate;
            f.play({tune}, 0);
            CHECK(f.backend.loaded_settings.kind == A);
            CHECK(f.status().engine == A);
            f.settings.engine = EngineChoice::Light;
            f.play({tune}, 0);
            CHECK(f.backend.loaded_settings.kind == L);
            CHECK(f.status().engine == L);
        }
    }
}

TEST_CASE("SPEC-004 SPEC-072 a player that has been given no measurement plays `auto` with sidlite") {
    PlayerFixture f;
    for (const char* tune : kChipTunes) {
        f.play({tune}, 0);
        CHECK(f.backend.loaded_settings.kind == L);
        CHECK(f.status().engine == L);
    }
}

// ---- SPEC-072: the stored factors, and the measurement ---------------------------------------------------------------

TEST_CASE("SPEC-072 the two speed factors are kept in the cache folder and read back as they were") {
    TempFolder cache;
    spz::fake::Files files;
    REQUIRE(spz::save_speed_factors(&files, cache.path.string(), measured(17.25, 141.5), "1.0.0", 48000));
    CHECK(files.count("write") >= 1);                       // through the Files seam
    CHECK_FALSE(std::filesystem::is_empty(cache.path));
    const spz::SpeedFactors back = spz::load_speed_factors(&files, cache.path.string(), "1.0.0", 48000);
    CHECK(back.measured);
    CHECK(back.accurate == doctest::Approx(17.25));
    CHECK(back.light == doctest::Approx(141.5));
}

TEST_CASE("SPEC-072 a new measurement is due when nothing is stored, and when the app's version or the sample rate has changed") {
    TempFolder cache;
    spz::fake::Files files;
    CHECK_FALSE(spz::load_speed_factors(&files, cache.path.string(), "1.0.0", 48000).measured);    // first run
    REQUIRE(spz::save_speed_factors(&files, cache.path.string(), measured(17.25), "1.0.0", 48000));
    CHECK(spz::load_speed_factors(&files, cache.path.string(), "1.0.0", 48000).measured);
    CHECK_FALSE(spz::load_speed_factors(&files, cache.path.string(), "1.0.1", 48000).measured);
    CHECK_FALSE(spz::load_speed_factors(&files, cache.path.string(), "1.0.0", 44100).measured);
    // Measured again for the other rate, that is what is stored from then on.
    REQUIRE(spz::save_speed_factors(&files, cache.path.string(), measured(19.0), "1.0.0", 44100));
    CHECK(spz::load_speed_factors(&files, cache.path.string(), "1.0.0", 44100).accurate == doctest::Approx(19.0));
}

TEST_CASE("SPEC-072 SPEC-202 stored factors that are cut short are thrown away: a new measurement is due") {
    TempFolder cache;
    spz::fake::Files files;
    REQUIRE(spz::save_speed_factors(&files, cache.path.string(), measured(17.25), "1.0.0", 48000));
    int cut = 0;
    for (const auto& file : std::filesystem::directory_iterator(cache.path)) {
        const auto size = std::filesystem::file_size(file.path());
        REQUIRE(size > 1);
        std::filesystem::resize_file(file.path(), size / 2);
        ++cut;
    }
    REQUIRE(cut >= 1);
    CHECK_FALSE(spz::load_speed_factors(&files, cache.path.string(), "1.0.0", 48000).measured);
}

namespace {

// An engine that takes time, on the fake clock: rendering a second of sound moves the clock on by the
// cost of the stretch it falls in. The stretches are counted in frames rendered since the engine was
// made, over however many loads: half a second of warming up, then three runs of a second each.
struct TimedEngine : spz::fake::Engine {
    spz::fake::Clock* clock = nullptr;
    unsigned rate = 48000;
    std::int64_t cost_ms[4] = {50, 100, 250, 125};          // for the warm-up, and for each run's second
    std::uint64_t total = 0;                                // frames rendered, over all loads
    std::uint64_t owed = 0;                                 // cost not yet a whole millisecond, in ms x rate

    std::size_t render(std::int16_t* out, std::size_t frames) override {
        const std::size_t done = spz::fake::Engine::render(out, frames);
        for (std::size_t i = 0; i < done; ++i) {
            const std::uint64_t at = total + i;
            const std::uint64_t stretch = at < rate / 2 ? 0 : std::min<std::uint64_t>(3, 1 + (at - rate / 2) / rate);
            owed += static_cast<std::uint64_t>(cost_ms[stretch]) * (stretch == 0 ? 2 : 1);
        }
        total += done;
        clock->advance(static_cast<std::int64_t>(owed / rate));
        owed %= rate;
        return done;
    }
};

}  // namespace

TEST_CASE("SPEC-072 an engine is measured by rendering the built-in tune for 0.5 s untimed and then three runs of 1 s, and the slowest run gives the factor") {
    spz::fake::Clock clock;
    TimedEngine engine;
    engine.clock = &clock;
    const double factor = spz::measure_engine(&engine, EngineKind::Accurate, 48000, &clock);
    // The runs took 100, 250 and 125 ms for a second of sound each: the slowest is 4 times real time.
    CHECK(factor == doctest::Approx(4.0));
    CHECK(engine.total == 48000u / 2 + 3 * 48000u);         // 3.5 s of sound, and no more
    CHECK(engine.loads >= 1);
    CHECK(engine.loaded_settings.kind == EngineKind::Accurate);
    CHECK(engine.loaded_settings.sample_rate == 48000);
    CHECK(clock.now_ms() == 50 + 100 + 250 + 125);
}

TEST_CASE("SPEC-072 the measurement is made at the sample rate in use, and for the engine asked for") {
    spz::fake::Clock clock;
    TimedEngine engine;
    engine.clock = &clock;
    engine.rate = 44100;
    engine.cost_ms[1] = 20;
    engine.cost_ms[2] = 10;
    engine.cost_ms[3] = 5;
    const double factor = spz::measure_engine(&engine, EngineKind::Light, 44100, &clock);
    CHECK(factor == doctest::Approx(50.0));                 // the slowest of 50, 100 and 200 times real time
    CHECK(engine.total == 44100u / 2 + 3 * 44100u);
    CHECK(engine.loaded_settings.kind == EngineKind::Light);
    CHECK(engine.loaded_settings.sample_rate == 44100);
}

TEST_CASE("SPEC-072 the built-in tune the engines are measured with is a valid tune for one SID chip") {
    const spz::Bytes& tune = spz::builtin_tune();
    CHECK(spz::check_tune(tune) == "");
    spz::TuneInfo info;
    REQUIRE(spz::read_tune_info(tune, &info));
    CHECK(info.chips == 1);
    CHECK(info.songs >= 1);
    CHECK_FALSE(info.needs_basic);
    CHECK(tune.size() < 1024);                              // a few dozen bytes of code, not somebody's music
}

// ---- SPEC-067, SPEC-077: what the engine is told for a tune ----------------------------------------------------------

TEST_CASE("SPEC-067 for each tune the backend is given the sample rate, the engine, and the model and clock left to the tune's header") {
    PlayerFixture f;
    f.play({kOne}, 0);
    const spz::EngineSettings& given = f.backend.loaded_settings;
    CHECK(given.sample_rate == 48000);                      // where `sample_rate` starts (SPEC-073)
    CHECK(given.model == spz::SidModel::FromTune);          // `sid_model` and `clock` start as auto (SPEC-077)
    CHECK(given.clock == spz::C64Clock::FromTune);
    CHECK(given.kernal.empty());
    CHECK(given.basic.empty());
    CHECK(given.chargen.empty());
    CHECK(f.status().sample_rate == 48000);
}

TEST_CASE("SPEC-067 SPEC-096 the model and clock in effect are the header's, and 6581 and PAL when the header does not say") {
    PlayerFixture f;
    f.put("/MODELS/Old.sid", fixture("tunes/model_6581.sid"));
    f.put("/MODELS/New.sid", fixture("tunes/model_8580.sid"));
    f.put("/MODELS/Either.sid", fixture("tunes/model_either.sid"));
    f.put("/MODELS/Sixty.sid", fixture("tunes/clock_ntsc.sid"));
    f.put("/MODELS/Fifty.sid", fixture("tunes/clock_pal.sid"));

    f.play({"/MODELS/Old.sid"}, 0);
    CHECK(f.status().model == spz::SidModel::Mos6581);
    CHECK(f.status().clock == spz::C64Clock::Pal);
    CHECK_FALSE(f.status().model_forced);
    CHECK_FALSE(f.status().clock_forced);
    f.play({"/MODELS/New.sid"}, 0);
    CHECK(f.status().model == spz::SidModel::Mos8580);
    f.play({"/MODELS/Either.sid"}, 0);                      // "any": the library's default
    CHECK(f.status().model == spz::SidModel::Mos6581);
    CHECK_FALSE(f.status().model_forced);
    CHECK(f.backend.loaded_settings.model == spz::SidModel::FromTune);
    f.play({"/MODELS/Sixty.sid"}, 0);
    CHECK(f.status().clock == spz::C64Clock::Ntsc);
    f.play({"/MODELS/Fifty.sid"}, 0);
    CHECK(f.status().clock == spz::C64Clock::Pal);
}

#if SPZ_PART_FORCE
TEST_CASE("SPEC-077 SPEC-067 `sid_model` and `clock` set to a value force it for every tune, and the screen is told it is forced [parts: force]") {
    PlayerFixture f;
    f.put("/MODELS/Old.sid", fixture("tunes/model_6581.sid"));
    f.settings.sid_model = spz::SidModel::Mos8580;
    f.play({"/MODELS/Old.sid"}, 0);                         // marked 6581 and PAL
    CHECK(f.backend.loaded_settings.model == spz::SidModel::Mos8580);
    CHECK(f.backend.loaded_settings.clock == spz::C64Clock::FromTune);
    CHECK(f.status().model == spz::SidModel::Mos8580);
    CHECK(f.status().model_forced);
    CHECK(f.status().clock == spz::C64Clock::Pal);
    CHECK_FALSE(f.status().clock_forced);

    f.settings.sid_model = spz::SidModel::FromTune;
    f.settings.clock = spz::C64Clock::Ntsc;
    f.play({"/MODELS/Old.sid"}, 0);
    CHECK(f.backend.loaded_settings.model == spz::SidModel::FromTune);
    CHECK(f.backend.loaded_settings.clock == spz::C64Clock::Ntsc);
    CHECK(f.status().clock == spz::C64Clock::Ntsc);
    CHECK(f.status().clock_forced);
    CHECK(f.status().model == spz::SidModel::Mos6581);
    CHECK_FALSE(f.status().model_forced);

    f.settings.sid_model = spz::SidModel::Mos6581;          // forcing what the header says is forcing all the same
    f.settings.clock = spz::C64Clock::Pal;
    f.play({"/MODELS/Old.sid"}, 0);
    CHECK(f.backend.loaded_settings.model == spz::SidModel::Mos6581);
    CHECK(f.backend.loaded_settings.clock == spz::C64Clock::Pal);
    CHECK(f.status().model_forced);
    CHECK(f.status().clock_forced);
}
#endif

// ---- SPEC-079: when it cannot keep up ---------------------------------------------------------------------------------

#if SPZ_PART_TOO_SLOW
namespace {

struct Slow : PlayerFixture {
    explicit Slow(EngineChoice engine, double accurate_factor = 8.0) {
        settings.engine = engine;
        player->set_speed_factors(measured(accurate_factor));
    }
    // The fake backend's underrun counter rises, and a second of the fake clock passes.
    void rise() {
        backend.underrun_frames += 100;
        clock.advance(1000);
        settle();
    }
    void quiet(spz::Millis ms) {
        clock.advance(ms);
        settle();
    }
    int slow_notices() const {
        return notices.count("MSG_SWITCHED_LIGHT") + notices.count("MSG_TOO_HEAVY") + notices.count("MSG_CANNOT_KEEP_UP");
    }
    // Three seconds with underruns in each; then up to 10 s more of the fake clock, a second at a time,
    // until `id` has been raised. True if it was.
    bool burst_brings(const char* id) {
        const int before = notices.count(id);
        rise();
        rise();
        rise();
        for (int second = 0; second < 10 && notices.count(id) == before; ++second) quiet(1000);
        return notices.count(id) == before + 1;
    }
};

}  // namespace

TEST_CASE("SPEC-079 `auto` playing with reSIDfp: after underruns in three separate seconds the subtune is loaded again from its beginning with sidlite, with the notice MSG_SWITCHED_LIGHT [parts: too-slow]") {
    Slow f(EngineChoice::Auto);
    f.play({kOne, kThree}, 1);
    f.player->next_subtune();
    f.settle();
    REQUIRE(f.status().subtune == 3);
    REQUIRE(f.backend.loaded_settings.kind == A);
    const std::size_t mark = f.mark();
    f.rise();
    f.rise();
    CHECK(f.slow_notices() == 0);                           // two seconds are not three
    CHECK(f.calls_since(mark).empty());
    f.rise();
    f.quiet(spz::kSlowWindowMs);                            // within 10 s of the fake clock
    CHECK(f.notices.ids == Calls{"MSG_SWITCHED_LIGHT"});
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 3);                         // the same subtune
    CHECK(f.backend.loaded_settings.kind == L);
    CHECK(f.status().engine == L);
    CHECK(tail(f.calls_since(mark, false), 2) == Calls{"load 3", "start"});
    CHECK(f.count_since(mark, "start") == 1);
}

TEST_CASE("SPEC-079 after that switch, `auto` uses sidlite for the rest of the run for tunes with that many chips or more, and for no others [parts: too-slow]") {
    Slow f(EngineChoice::Auto);
    f.play({kChips2}, 0);                                   // two chips: 8.0 / 2 is enough for reSIDfp
    REQUIRE(f.backend.loaded_settings.kind == A);
    REQUIRE(f.burst_brings("MSG_SWITCHED_LIGHT"));
    f.play({kChips1}, 0);
    CHECK(f.backend.loaded_settings.kind == A);             // fewer chips: still reSIDfp
    f.play({kChips2}, 0);
    CHECK(f.backend.loaded_settings.kind == L);
    f.play({kChips3}, 0);
    CHECK(f.backend.loaded_settings.kind == L);             // more chips: sidlite too, though 8.0 / 3 would do
    f.play({kTwoChips}, 0);
    CHECK(f.backend.loaded_settings.kind == L);             // another tune with two
    f.settings.engine = EngineChoice::Accurate;             // the setting itself is not changed by it
    f.play({kChips2}, 0);
    CHECK(f.backend.loaded_settings.kind == A);
}

TEST_CASE("SPEC-079 `accurate`: the notice MSG_TOO_HEAVY offers the light engine, and L while it shows loads the subtune again with sidlite, for this tune only [parts: too-slow]") {
    Slow f(EngineChoice::Accurate);
    f.play({kThree, kOne}, 0);
    REQUIRE(f.backend.loaded_settings.kind == A);
    std::size_t mark = f.mark();
    REQUIRE(f.burst_brings("MSG_TOO_HEAVY"));
    CHECK(f.slow_notices() == 1);
    CHECK(f.backend.loaded_settings.kind == A);             // offered: nothing is loaded yet
    CHECK(f.calls_since(mark).empty());
    f.player->use_light_engine();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.backend.loaded_settings.kind == L);
    CHECK(f.status().subtune == 2);
    CHECK(f.status().elapsed_ms == 0);                      // from its beginning
    CHECK(tail(f.calls_since(mark, false), 2) == Calls{"load 2", "start"});

    f.player->next_subtune();                               // still this tune
    f.settle();
    CHECK(f.backend.loaded_settings.kind == L);
    f.player->next_tune();                                  // another tune: the setting holds again
    f.settle();
    REQUIRE(f.current() == kOne);
    CHECK(f.backend.loaded_settings.kind == A);
    CHECK(f.settings.engine == EngineChoice::Accurate);
}

TEST_CASE("SPEC-079 once MSG_TOO_HEAVY has gone, 10 s after it appeared, L does nothing [parts: too-slow]") {
    Slow f(EngineChoice::Accurate);
    f.play({kLong}, 0);
    REQUIRE(f.burst_brings("MSG_TOO_HEAVY"));
    f.quiet(spz::kLightOfferMs);
    const std::size_t mark = f.mark();
    f.player->use_light_engine();
    f.settle();
    CHECK(f.calls_since(mark).empty());
    CHECK(f.backend.loaded_settings.kind == A);
    CHECK(f.state() == PlayerState::Playing);
}

TEST_CASE("SPEC-079 L does nothing when nothing was offered [parts: too-slow]") {
    for (const EngineChoice engine : {EngineChoice::Auto, EngineChoice::Accurate, EngineChoice::Light}) {
        Slow f(engine);
        f.play({kLong}, 0);
        const std::size_t mark = f.mark();
        const EngineKind kind = f.backend.loaded_settings.kind;
        f.player->use_light_engine();
        f.settle();
        CHECK(f.calls_since(mark).empty());
        CHECK(f.backend.loaded_settings.kind == kind);
        CHECK(f.notices.ids.empty());
    }
}

TEST_CASE("SPEC-079 playing with sidlite already: the notice MSG_CANNOT_KEEP_UP, and nothing is loaded again [parts: too-slow]") {
    for (const EngineChoice engine : {EngineChoice::Light, EngineChoice::Auto}) {
        Slow f(engine, 2.0);                                // with this factor `auto` is sidlite too
        f.play({kLong}, 0);
        REQUIRE(f.backend.loaded_settings.kind == L);
        const std::size_t mark = f.mark();
        REQUIRE(f.burst_brings("MSG_CANNOT_KEEP_UP"));
        CHECK(f.slow_notices() == 1);
        CHECK(f.calls_since(mark).empty());
        CHECK(f.state() == PlayerState::Playing);
        f.player->use_light_engine();                       // and there is nothing for L to do
        f.settle();
        CHECK(f.calls_since(mark).empty());
    }
}

TEST_CASE("SPEC-079 the player acts once for each tune loaded: a second burst on the same tune does nothing, and one on the next tune counts anew [parts: too-slow]") {
    Slow f(EngineChoice::Accurate);
    f.play({kLong, kVoices}, 0);
    REQUIRE(f.burst_brings("MSG_TOO_HEAVY"));
    f.quiet(spz::kLightOfferMs);
    const std::size_t mark = f.mark();
    CHECK_FALSE(f.burst_brings("MSG_TOO_HEAVY"));           // the same tune: nothing
    CHECK(f.slow_notices() == 1);
    CHECK(f.calls_since(mark).empty());
    f.player->next_tune();
    f.settle();
    REQUIRE(f.current() == kVoices);
    CHECK(f.burst_brings("MSG_TOO_HEAVY"));                 // another tune loaded: once more
    CHECK(f.slow_notices() == 2);

    Slow g(EngineChoice::Auto);
    g.play({kLong}, 0);
    REQUIRE(g.burst_brings("MSG_SWITCHED_LIGHT"));
    const std::size_t after = g.mark();
    g.rise();
    g.rise();
    g.rise();
    g.quiet(spz::kSlowWindowMs);
    CHECK(g.slow_notices() == 1);                           // not MSG_CANNOT_KEEP_UP either: once for the tune
    CHECK(g.calls_since(after).empty());
}

TEST_CASE("SPEC-079 underruns in fewer than three separate seconds, or spread over more than ten seconds, are not acted on [parts: too-slow]") {
    {
        Slow f(EngineChoice::Auto);                         // two seconds only
        f.play({kLong}, 0);
        f.rise();
        f.rise();
        f.quiet(15000);
        CHECK(f.slow_notices() == 0);
        CHECK(f.backend.loaded_settings.kind == A);
    }
    {
        Slow f(EngineChoice::Auto);                         // three rises, all within the song's first second
        f.play({kLong}, 0);
        for (int i = 0; i < 3; ++i) {
            f.backend.underrun_frames += 100;
            f.quiet(200);
        }
        f.quiet(15000);
        CHECK(f.slow_notices() == 0);
        CHECK(f.backend.loaded_settings.kind == A);
    }
    {
        Slow f(EngineChoice::Auto);                         // three seconds, six seconds apart: 12 s from first to last
        f.play({kLong}, 0);
        f.quiet(500);
        for (int i = 0; i < 3; ++i) {
            f.backend.underrun_frames += 100;
            f.quiet(6000);
        }
        f.quiet(5000);
        CHECK(f.slow_notices() == 0);
        CHECK(f.backend.loaded_settings.kind == A);
    }
    {
        Slow f(EngineChoice::Auto);                         // three seconds, four seconds apart: 8 s from first to last
        f.play({kLong}, 0);
        f.quiet(500);
        for (int i = 0; i < 3; ++i) {
            f.backend.underrun_frames += 100;
            f.quiet(4000);
        }
        f.quiet(5000);
        CHECK(f.notices.ids == Calls{"MSG_SWITCHED_LIGHT"});
    }
}
#endif
