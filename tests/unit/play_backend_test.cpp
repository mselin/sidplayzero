// SPDX-License-Identifier: GPL-3.0-or-later
// The software backend against the fake Engine and AudioSink (SPEC-025): the ring buffer and the render
// loop (SPEC-070), the end of a song (SPEC-064), starting again and skipping (SPEC-066), the sample rate
// (SPEC-073), mutes and filters (SPEC-074), and a sound device that cannot be opened or goes away
// (SPEC-078). The render loop is a function these tests call; the sink pulls when they say.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_sound_support.h"
#include "spz/software_backend.h"
#include "support.h"

using spz::SoftwareBackend;
using namespace spz::test;

namespace {

const std::size_t kTick = Sound::kTick;

// Whether frames [from, from + count) of what the sink received are all silence.
bool silent(const spz::fake::AudioSink& sink, std::size_t from, std::size_t count) {
    for (std::size_t i = 2 * from; i < 2 * (from + count); ++i) {
        if (i >= sink.received.size() || sink.received[i] != 0) return false;
    }
    return true;
}

}  // namespace

// ---- SPEC-025: what it is ---------------------------------------------------------------------------------------------

#if SPZ_PART_VOICES
TEST_CASE("SPEC-025 SPEC-074 the software backend says it can mute voices and switch filters [parts: voices]") {
    Sound s;
    CHECK(s.backend.can().mute);
    CHECK(s.backend.can().filter);
}
#endif

#if SPZ_PART_SKIP
TEST_CASE("SPEC-025 SPEC-066 the software backend says it can skip [parts: skip]") {
    Sound s;
    CHECK(s.backend.can().skip);
}
#endif

TEST_CASE("SPEC-025 SPEC-073 load gives the engine the tune, the subtune and the settings, and opens the sink at the settings' rate") {
    Sound s;
    s.settings.kind = spz::EngineKind::Accurate;
    s.settings.model = spz::SidModel::Mos8580;
    s.engine.chip_count = 3;
    REQUIRE(s.load(2));
    CHECK(s.engine.loads == 1);                             // through the Engine seam
    CHECK(s.engine.loaded_subtune == 2);
    CHECK(s.engine.loaded_settings.kind == spz::EngineKind::Accurate);
    CHECK(s.engine.loaded_settings.model == spz::SidModel::Mos8580);
    CHECK(s.engine.loaded_settings.sample_rate == 48000);
    CHECK(s.sink.opens == 1);                               // through the AudioSink seam
    CHECK(s.sink.is_open);
    CHECK(s.sink.sample_rate == 48000);
    CHECK(s.backend.position() == 0);
    CHECK(s.backend.chips() == 3);
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-025 SPEC-069 an engine that refuses the tune fails the load with its reason") {
    Sound s;
    s.engine.refuse = true;
    CHECK_FALSE(s.load(1));
    CHECK(s.why == "MSG_ENGINE_REFUSED");
    s.engine.refuse = false;
    CHECK(s.load(1));
}

#if SPZ_PART_BARS
TEST_CASE("SPEC-025 SPEC-080 the software backend can give a chip's last-written registers, as the engine has them [parts: bars]") {
    Sound s;
    CHECK(s.backend.can().registers);
    s.engine.chip_count = 2;
    REQUIRE(s.load(1));
    s.engine.register_values[0][1] = 80;                    // the first chip, voice 1's frequency, high byte
    s.engine.register_values[0][4] = 0x21;
    s.engine.register_values[1][8] = 160;                   // the second chip, voice 2's
    std::uint8_t out[spz::Engine::kRegisters] = {};
    REQUIRE(s.backend.registers(0, out));
    CHECK(out[1] == 80);
    CHECK(out[4] == 0x21);
    CHECK(out[8] == 0);
    REQUIRE(s.backend.registers(1, out));
    CHECK(out[8] == 160);
    CHECK(out[1] == 0);
}
#endif

TEST_CASE("SPEC-025 events reach the listener on the UI thread: nothing is told before the UI executor runs") {
    Sound s;
    s.ready(1, 4800);
    s.sink.pull(5000);                                      // the whole song, and then some
    s.backend.render_step();                                // the render loop sees that the last sample was played
    CHECK(s.events.log.empty());
    CHECK(s.ui.waiting() >= 1);
    s.ui.run_all();
    CHECK(s.events.log == std::vector<std::string>{"song_ended"});
}

// ---- SPEC-070: the buffer and the render loop -------------------------------------------------------------------------

TEST_CASE("SPEC-070 the render loop asks the engine for about 5 ms of samples at a time and writes them into the ring buffer") {
    Sound s;
    REQUIRE(s.load(1));
    s.backend.start();
    REQUIRE(s.backend.render_step());
    CHECK(s.engine.renders == 1);
    CHECK(s.engine.rendered >= 120);                        // about 5 ms: 240 frames at 48,000 Hz
    CHECK(s.engine.rendered <= 480);
    CHECK(s.backend.buffered() == s.engine.rendered);
    s.fill();
    const std::uint64_t each = s.engine.rendered / static_cast<std::uint64_t>(s.engine.renders);
    CHECK(each >= 120);
    CHECK(each <= 480);
}

TEST_CASE("SPEC-070 the buffer holds 8,192 frames: the render loop keeps it at least half full and waits when it is full") {
    Sound s;
    REQUIRE(s.load(1));
    s.backend.start();
    s.fill();
    CHECK(s.backend.buffered() >= SoftwareBackend::kRingFrames / 2);
    CHECK(s.backend.buffered() <= SoftwareBackend::kRingFrames);
    CHECK(SoftwareBackend::kRingFrames == 8192);
    CHECK_FALSE(s.backend.render_step());                   // full: it waits
    const std::uint64_t rendered = s.engine.rendered;
    CHECK_FALSE(s.backend.render_step());
    CHECK(s.engine.rendered == rendered);                   // and renders nothing meanwhile

    for (int tick = 0; tick < 400; ++tick) {                // two seconds of playing, 5 ms at a time
        s.fill();
        CHECK(s.backend.buffered() >= SoftwareBackend::kRingFrames / 2);
        s.sink.pull(kTick);
    }
    s.sink.pull(SoftwareBackend::kRingFrames / 2 + kTick);  // the device takes more than half at once
    REQUIRE(s.backend.buffered() < SoftwareBackend::kRingFrames / 2);
    CHECK(s.backend.render_step());                         // below half: it does not wait
}

TEST_CASE("SPEC-070 the sink is given 16-bit stereo frames as the engine made them, in order") {
    Sound s;
    s.ready(1);
    for (int tick = 0; tick < 100; ++tick) s.tick();
    CHECK(frames(s.sink) == 100 * kTick);
    CHECK(difference(received(s.sink), ramp(1, 0, 100 * kTick)) == "");
    CHECK(stereo_whole(s.sink));
    CHECK(s.backend.position() == 100 * kTick);
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-070 output for a subtune starts when 2,048 frames are in the buffer: until then the sink gets silence, and then the subtune's first sample") {
    Sound s;
    s.engine.frames_per_call = 100;                         // every turn of the render loop adds 100 frames
    REQUIRE(s.load(1));
    s.backend.start();
    for (int turn = 0; turn < 20; ++turn) {
        REQUIRE(s.backend.render_step());
        s.sink.pull(kTick);
    }
    CHECK(s.backend.buffered() == 2000);                    // nothing was taken: 48 frames short of the mark
    CHECK(frames(s.sink) == 20 * kTick);
    CHECK(silent(s.sink, 0, 20 * kTick));
    CHECK(s.backend.position() == 0);
    CHECK(SoftwareBackend::kStartFrames == 2048);

    REQUIRE(s.backend.render_step());                       // 2,100 frames: over the mark
    s.sink.pull(kTick);
    CHECK(difference(received(s.sink, 20 * kTick), ramp(1, 0, kTick)) == "");
    CHECK(s.backend.position() == kTick);
    CHECK(s.backend.underruns() == 0);                      // the silence before the start is no underrun
}

TEST_CASE("SPEC-070 SPEC-060 the frames received equal the frames produced, in order, across a pause and resume") {
    Sound s;
    s.ready(1);
    for (int tick = 0; tick < 20; ++tick) s.tick();         // 4,800 frames
    s.backend.pause();
    const std::size_t kept = s.backend.buffered();
    CHECK(s.backend.position() == 4800);                    // stopped at a known sample
    s.sink.pull(960);                                       // the device goes on asking while paused
    s.sink.pull(960);
    CHECK(s.backend.buffered() == kept);                    // "on pause() the buffer keeps its contents"
    CHECK(s.backend.position() == 4800);
    s.backend.resume();
    for (int tick = 0; tick < 20; ++tick) s.tick();

    CHECK(frames(s.sink) == 4800 + 1920 + 4800);
    CHECK(difference(received(s.sink, 0, 4800), ramp(1, 0, 4800)) == "");
    CHECK(silent(s.sink, 4800, 1920));
    CHECK(difference(received(s.sink, 6720), ramp(1, 4800, 4800)) == "");     // none lost, none repeated
    CHECK(stereo_whole(s.sink));
    CHECK(s.backend.position() == 9600);
    CHECK(s.backend.underruns() == 0);
    CHECK(s.events.log.empty());
}

TEST_CASE("SPEC-070 with the engine slower than real time, the sink plays what there is and silence for the rest, and the underrun counter rises by exactly the frames not delivered") {
    Sound s;
    s.ready(1);
    s.sink.pull(kTick);                                     // output has started
    REQUIRE(s.backend.underruns() == 0);
    s.engine.frames_per_call = 100;                         // 100 frames for every 240 the device takes
    for (int tick = 0; tick < 200; ++tick) {
        s.backend.render_step();
        s.sink.pull(kTick);
    }
    const Samples all = received(s.sink);
    const Samples real = without_zeros(all);                // no ramp frame is zero this early in the subtune
    const std::size_t silence = all.size() - real.size();
    CHECK(silence > 0);
    CHECK(s.backend.underruns() == silence);
    CHECK(difference(real, ramp(1, 0, real.size())) == ""); // what came through is in order, nothing lost
    CHECK(real.size() == s.engine.rendered - s.backend.buffered());    // delivered: produced, less what waits
    CHECK(s.backend.position() == real.size());
    // In each callback the real frames come first and the silence fills the rest.
    bool real_after_silence = false;
    for (std::size_t block = 0; block < all.size(); block += kTick) {
        bool seen_silence = false;
        for (std::size_t i = block; i < block + kTick && i < all.size(); ++i) {
            if (all[i] == 0) seen_silence = true;
            else if (seen_silence) real_after_silence = true;
        }
    }
    CHECK_FALSE(real_after_silence);
}

TEST_CASE("SPEC-070 the underrun counter does not rise during the start of a subtune, a pause, a stop or the end of a song") {
    Sound s;
    SUBCASE("the start of a subtune") {
        s.engine.frames_per_call = 50;
        REQUIRE(s.load(1));
        s.backend.start();
        for (int turn = 0; turn < 40; ++turn) {             // 2,000 frames buffered at most: not started yet
            s.backend.render_step();
            s.sink.pull(kTick);
        }
        CHECK(silent(s.sink, 0, 40 * kTick));
    }
    SUBCASE("a pause") {
        s.ready(1);
        s.sink.pull(kTick);
        s.backend.pause();
        for (int i = 0; i < 100; ++i) s.sink.pull(kTick);   // far more than the buffer holds
        CHECK(silent(s.sink, kTick, 100 * kTick));
    }
    SUBCASE("a stop") {
        s.ready(1);
        s.sink.pull(kTick);
        s.backend.stop();
        for (int i = 0; i < 100; ++i) s.sink.pull(kTick);   // if the stream is still open, it is silent
        CHECK(silent(s.sink, kTick, frames(s.sink) - kTick));
    }
    SUBCASE("the end of a song") {
        s.ready(1, 4800);
        for (int i = 0; i < 100; ++i) {
            s.fill();
            s.sink.pull(kTick);
        }
        CHECK(difference(received(s.sink, 0, 4800), ramp(1, 0, 4800)) == "");
        CHECK(silent(s.sink, 4800, 100 * kTick - 4800));
    }
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-070 on stop() what is in the buffer is thrown away and the sink is silent from its next callback") {
    Sound s;
    s.ready(1);
    s.sink.pull(kTick);
    REQUIRE(s.backend.buffered() > 0);
    s.backend.stop();
    CHECK(s.backend.buffered() == 0);
    const std::size_t before = frames(s.sink);
    s.sink.pull(kTick);                                     // the very next callback
    s.sink.pull(kTick);
    CHECK(silent(s.sink, before, frames(s.sink) - before));
    CHECK_FALSE(s.backend.render_step());                   // nothing is loaded: nothing to render
    CHECK(s.events.log.empty());
}

TEST_CASE("SPEC-070 a new load() throws away what is in the buffer: the sink goes silent, and then gets the new subtune from its first sample") {
    Sound s;
    s.ready(1);
    s.sink.pull(kTick);
    REQUIRE(s.load(2));                                     // no stop() in between
    CHECK(s.backend.buffered() == 0);
    CHECK(s.backend.position() == 0);
    s.sink.pull(kTick);                                     // the callback after the load: silence, not subtune 1's rest
    s.backend.start();
    s.fill();
    s.sink.pull(kTick);
    CHECK(difference(received(s.sink, 0, kTick), ramp(1, 0, kTick)) == "");
    CHECK(silent(s.sink, kTick, kTick));
    CHECK(difference(received(s.sink, 2 * kTick), ramp(2, 0, kTick)) == "");
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-066 SPEC-070 starting again: the same subtune loaded again sets position() to 0, and the sink then receives the tune's first samples") {
    Sound s;
    s.ready(1);
    for (int tick = 0; tick < 10; ++tick) s.tick();
    REQUIRE(s.backend.position() == 10 * kTick);
    REQUIRE(s.load(1));
    CHECK(s.backend.position() == 0);
    s.backend.start();
    s.fill();
    const std::size_t before = frames(s.sink);
    s.sink.pull(kTick);
    CHECK(difference(received(s.sink, before), ramp(1, 0, kTick)) == "");   // not frame 2,400 and onwards
    CHECK(s.backend.position() == kTick);
}

// ---- SPEC-064: the end of a song --------------------------------------------------------------------------------------

TEST_CASE("SPEC-064 with a 2.000-second length at 48,000 Hz the sink receives exactly 96,000 frames of the subtune, and song_ended comes after the last of them") {
    Sound s;
    s.ready(1, 96000);
    bool told_early = false;
    while (frames(s.sink) < 96000) {
        s.fill();
        s.ui.run_all();
        if (!s.events.log.empty()) told_early = true;       // the sink has not had the last frame yet
        s.sink.pull(kTick);
    }
    REQUIRE(frames(s.sink) == 96000);                       // 400 callbacks of 240 frames
    CHECK_FALSE(told_early);
    s.backend.render_step();                                // the render loop sees that the last sample was played
    s.ui.run_all();
    CHECK(s.events.log == std::vector<std::string>{"song_ended"});

    for (int i = 0; i < 20; ++i) {                          // the device goes on asking
        s.fill();
        s.sink.pull(kTick);
        s.ui.run_all();
    }
    CHECK(difference(received(s.sink, 0, 96000), ramp(1, 0, 96000)) == "");
    CHECK(silent(s.sink, 96000, 20 * kTick));               // exactly 96,000: nothing of the subtune after them
    CHECK(stereo_whole(s.sink));
    CHECK(s.engine.rendered == 96000);                      // "renders exactly that many samples"
    CHECK(s.backend.position() == 96000);
    CHECK(s.events.log == std::vector<std::string>{"song_ended"});     // told once
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-064 at a song's end every sample in the buffer is played, however the device asks for them") {
    Sound s;
    s.ready(1, 5000);                                       // all of it fits in the buffer at once
    CHECK(s.engine.rendered == 5000);
    CHECK_FALSE(s.backend.render_step());                   // rendered to its end: the loop waits
    s.sink.pull(4999);
    s.backend.render_step();
    s.ui.run_all();
    CHECK(s.events.log.empty());                            // one sample is still to be played
    s.sink.pull(7);
    s.backend.render_step();
    s.ui.run_all();
    CHECK(s.events.log == std::vector<std::string>{"song_ended"});
    CHECK(difference(received(s.sink, 0, 5000), ramp(1, 0, 5000)) == "");
    CHECK(silent(s.sink, 5000, 6));
}

TEST_CASE("SPEC-064 SPEC-025 a length of 0 means never: the subtune goes on and song_ended is not reported") {
    Sound s;
    s.ready(1, 0);
    s.play_for(3000);
    CHECK(frames(s.sink) == 144000);
    CHECK(difference(received(s.sink), ramp(1, 0, 144000)) == "");
    CHECK(s.events.log.empty());
    CHECK(s.backend.position() == 144000);
}

// ---- SPEC-066: skipping ahead -----------------------------------------------------------------------------------------

#if SPZ_PART_SKIP
TEST_CASE("SPEC-066 holding skip for one second of the fake clock advances position() by at least four seconds of samples, and adds nothing to underruns() [parts: skip]") {
    Sound s;
    s.ready(1);
    s.play_for(500);
    const std::uint64_t before = s.backend.position();
    const std::size_t heard_before = frames(s.sink);
    s.backend.set_skip(true);
    s.play_for(1000);
    s.backend.set_skip(false);
    const std::uint64_t advanced = s.backend.position() - before;
    CHECK(advanced >= 4 * 48000u);
    CHECK(advanced <= 8 * 48000u + SoftwareBackend::kRingFrames);   // "up to eight times real time"
    CHECK(frames(s.sink) - heard_before == 48000);          // the device took its second, no more
    CHECK(s.backend.underruns() == 0);
    CHECK(s.events.log.empty());
    s.play_for(1000);                                       // what was buffered while skipping has been played by now
    const std::uint64_t later = s.backend.position();
    s.play_for(500);                                        // and it plays on at its own speed
    CHECK(s.backend.position() - later == 24000);
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-066 SPEC-070 underruns are not counted while skipping, nor for one second after [parts: skip]") {
    Sound s;
    s.ready(1);
    s.sink.pull(kTick);
    s.backend.set_skip(true);
    s.engine.frames_per_call = 5;                           // an engine far too slow to skip with
    auto starve = [&s](int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            s.clock.advance(5);
            s.backend.render_step();
            s.sink.pull(kTick);
        }
    };
    starve(200);                                            // one second: the buffer runs dry long before its end
    REQUIRE(without_zeros(received(s.sink)).size() < frames(s.sink) - 20000);   // silence was played
    CHECK(s.backend.underruns() == 0);
    s.backend.set_skip(false);
    starve(190);                                            // 950 ms after
    CHECK(s.backend.underruns() == 0);
    starve(210);                                            // to two seconds after
    CHECK(s.backend.underruns() > 199u * 230u);             // the second second counts, as ever
    CHECK(s.backend.underruns() <= 210u * kTick);
}
#endif

// ---- SPEC-073: the sample rate ----------------------------------------------------------------------------------------

#if SPZ_PART_RATE
TEST_CASE("SPEC-073 with each value of the sample rate the sink is opened at that rate and receives that many frames per second of song [parts: rate]") {
    for (const unsigned rate : {44100u, 48000u}) {
        CAPTURE(rate);
        Sound s;
        s.settings.sample_rate = rate;
        REQUIRE(s.load(1, rate));                           // a song of one second
        CHECK(s.sink.sample_rate == rate);
        CHECK(s.engine.loaded_settings.sample_rate == rate);
        s.backend.start();
        const std::size_t callback = rate / 200;            // what the device takes in 5 ms
        for (int i = 0; i < 400; ++i) {                     // two seconds of asking
            s.fill();
            s.sink.pull(callback);
            s.backend.render_step();
            s.ui.run_all();
        }
        CHECK(without_zeros(received(s.sink)).size() == without_zeros(ramp(1, 0, rate)).size());
        CHECK(difference(received(s.sink, 0, rate), ramp(1, 0, rate)) == "");
        REQUIRE(frames(s.sink) == 400 * callback);
        CHECK(silent(s.sink, rate, frames(s.sink) - rate));
        CHECK(s.events.log == std::vector<std::string>{"song_ended"});
    }
}

TEST_CASE("SPEC-073 a change of the rate takes effect when the next tune is loaded: the stream is opened again at the new rate [parts: rate]") {
    Sound s;
    s.ready(1);
    s.sink.pull(kTick);
    REQUIRE(s.sink.sample_rate == 48000);
    s.settings.sample_rate = 44100;
    CHECK(s.sink.sample_rate == 48000);                     // nothing happens until a load
    REQUIRE(s.load(1));
    CHECK(s.sink.sample_rate == 44100);
    CHECK(s.sink.is_open);
    CHECK(s.sink.opens == 2);
    CHECK(s.sink.closes == 1);
    REQUIRE(s.load(2));                                     // the same rate again: the stream stays as it is
    CHECK(s.sink.opens == 2);
}
#endif

// ---- SPEC-074: mutes and filters --------------------------------------------------------------------------------------

#if SPZ_PART_VOICES
TEST_CASE("SPEC-074 a voice muted and a filter switched off while a tune plays reach the engine within one render chunk [parts: voices]") {
    Sound s;
    s.engine.chip_count = 3;
    s.ready(1);
    s.backend.set_muted(0, 1, true);                        // the first chip's second voice
    s.backend.set_muted(2, 2, true);                        // the third chip's third
    s.backend.set_filter(1, false);                         // the second chip's filter
    s.sink.pull(kTick);                                     // room for one chunk
    s.backend.render_step();
    for (int chip = 0; chip < 3; ++chip) {
        for (int voice = 0; voice < 3; ++voice) {
            CAPTURE(chip);
            CAPTURE(voice);
            CHECK(s.engine.muted[chip][voice] == ((chip == 0 && voice == 1) || (chip == 2 && voice == 2)));
        }
        CHECK(s.engine.filter_on[chip] == (chip != 1));
    }
    s.backend.set_muted(0, 1, false);                       // and on again
    s.backend.set_filter(1, true);
    s.sink.pull(kTick);
    s.backend.render_step();
    CHECK_FALSE(s.engine.muted[0][1]);
    CHECK(s.engine.muted[2][2]);
    CHECK(s.engine.filter_on[1]);
    CHECK(s.backend.underruns() == 0);
}
#endif

// ---- SPEC-078: the sound device ---------------------------------------------------------------------------------------

TEST_CASE("SPEC-078 if the sink cannot be opened the load fails with MSG_NO_SOUND, and the next load tries again") {
    Sound s;
    s.sink.refuse = true;
    CHECK_FALSE(s.load(1));
    CHECK(s.why == "MSG_NO_SOUND");
    CHECK(s.sink.opens == 1);
    CHECK_FALSE(s.sink.is_open);
    CHECK_FALSE(s.load(1));
    CHECK(s.sink.opens == 2);                               // it asked again
    s.sink.refuse = false;
    REQUIRE(s.load(1));
    CHECK(s.sink.opens == 3);
    s.backend.start();
    s.fill();
    s.sink.pull(kTick);
    CHECK(difference(received(s.sink), ramp(1, 0, kTick)) == "");
    CHECK(s.events.log.empty());
}

TEST_CASE("SPEC-078 the device dropped for 300 ms mid-tune: device_lost, the stream closed and opened again, device_back, and the frames received complete and in order") {
    Sound s;
    s.ready(1);
    s.play_for(1000);
    REQUIRE(frames(s.sink) == 48000);
    const int opens = s.sink.opens;
    s.sink.refuse = true;                                   // the device is away
    s.sink.drop_device();
    s.ui.run_all();
    CHECK(s.events.log == std::vector<std::string>{"device_lost"});
    CHECK(s.sink.closes >= 1);                              // it closes the stream
    s.play_for(300);
    CHECK(s.events.log == std::vector<std::string>{"device_lost"});     // still trying
    CHECK(s.sink.opens > opens);
    CHECK(frames(s.sink) == 48000);                         // nothing can be played meanwhile
    s.sink.refuse = false;                                  // and it is there again
    s.play_for(1700);
    CHECK(s.events.log == std::vector<std::string>{"device_lost", "device_back"});
    CHECK(s.sink.is_open);
    CHECK(s.sink.opens - opens <= SoftwareBackend::kReopenTries);

    const Samples heard = without_zeros(received(s.sink));
    CHECK(heard.size() >= 48000 + 72000 - 6);               // a second before, and a second and a half after at the least
    // No part of the tune was skipped or repeated: what was heard is the subtune from its first frame on.
    CHECK(difference_from_start(heard, without_zeros(ramp(1, 0, frames(s.sink) + 16))) == "");
    CHECK(stereo_whole(s.sink));
    CHECK(s.backend.underruns() == 0);                      // "no underruns are counted"
}

TEST_CASE("SPEC-078 while the device is lost the render loop waits, and position() stands still") {
    Sound s;
    s.ready(1);
    s.play_for(100);
    s.sink.refuse = true;
    s.sink.drop_device();
    s.ui.run_all();
    s.fill();                                               // it may fill the buffer, and then it waits
    const std::uint64_t rendered = s.engine.rendered;
    const std::uint64_t position = s.backend.position();
    s.clock.advance(100);
    s.ui.run_all();
    CHECK_FALSE(s.backend.render_step());
    CHECK(s.engine.rendered == rendered);
    CHECK(s.backend.position() == position);
    CHECK(s.backend.underruns() == 0);
}

TEST_CASE("SPEC-078 a device that does not come back: five attempts 200 ms apart, then device_gone; resume() opens the sink again and carries on from the same sample") {
    Sound s;
    s.ready(1);
    s.play_for(1000);
    const int opens = s.sink.opens;
    s.sink.refuse = true;
    s.sink.drop_device();
    s.ui.run_all();
    s.clock.advance(SoftwareBackend::kReopenEveryMs * SoftwareBackend::kReopenTries - 201);
    s.ui.run_all();
    CHECK(s.events.count("device_gone") == 0);              // the fifth attempt is still to come
    CHECK(s.sink.opens - opens < SoftwareBackend::kReopenTries);
    s.clock.advance(201);
    s.ui.run_all();
    CHECK(s.events.log == std::vector<std::string>{"device_lost", "device_gone"});
    CHECK(s.sink.opens - opens == SoftwareBackend::kReopenTries);       // five, and it gives up
    CHECK(SoftwareBackend::kReopenTries == 5);
    CHECK(SoftwareBackend::kReopenEveryMs == 200);

    const std::uint64_t position = s.backend.position();
    s.backend.pause();                                      // what the player does on device_gone
    s.clock.advance(5000);
    s.ui.run_all();
    CHECK(s.sink.opens - opens == SoftwareBackend::kReopenTries);       // it does not go on trying by itself
    CHECK(s.backend.position() == position);

    s.sink.refuse = false;
    s.backend.resume();                                     // Play/Pause: "tries to open the sink again"
    s.ui.run_all();
    CHECK(s.sink.is_open);
    CHECK(s.sink.opens - opens == SoftwareBackend::kReopenTries + 1);
    s.play_for(500);
    const Samples heard = without_zeros(received(s.sink));
    CHECK(heard.size() >= 48000 + 24000 - 4);
    // It carried on from the same sample: what was heard is the subtune from its first frame on.
    CHECK(difference_from_start(heard, without_zeros(ramp(1, 0, frames(s.sink) + 16))) == "");
    CHECK(s.events.count("failed MSG_NO_SOUND") == 0);
    CHECK(s.backend.underruns() == 0);
}
