// SPDX-License-Identifier: GPL-3.0-or-later
// The whole sound path in `core`: the player driving the software backend, which runs the fake Engine
// and feeds the fake AudioSink. For the oracles that speak of both ends at once: a tune's length and what
// follows it (SPEC-064, SPEC-065), pause and resume (SPEC-060), starting again (SPEC-066), mutes that
// hold for a tune (SPEC-074), and a sound device that will not open or goes away (SPEC-078).
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_sound_support.h"
#include "play_support.h"
#include "spz/player.h"
#include "spz/software_backend.h"
#include "support.h"

using spz::PlayerState;
using namespace spz::test;

namespace {

// A player whose backend is the software backend. Time passes 5 ms at a time: the render loop runs until
// it waits, the clock moves, the device takes its 240 frames, and the worker and the UI thread do what
// was posted to them.
struct Path : PlayerFixture {
    spz::fake::Engine engine;
    spz::fake::AudioSink sink;
    spz::SoftwareBackend software{&engine, &sink, &clock, &ui};

    Path() {
        sound = &software;
        make();
    }
    void fill() {
        int turns = 0;
        while (software.render_step()) REQUIRE(++turns < 1000000);
    }
    void play_for(int ms) {
        for (int t = 0; t < ms; t += 5) {
            fill();
            clock.advance(5);
            sink.pull(240);
            software.render_step();
            settle();
        }
    }
    // What the sink has been given, without silence.
    Samples heard() const { return without_zeros(received(sink)); }
    // Whether any voice of any chip is muted at the engine, or any filter off.
    bool anything_off() const {
        for (int chip = 0; chip < 3; ++chip) {
            if (!engine.filter_on[chip]) return true;
            for (int voice = 0; voice < 3; ++voice) {
                if (engine.muted[chip][voice]) return true;
            }
        }
        return false;
    }
};

}  // namespace

TEST_CASE("SPEC-064 SPEC-065 a two-second subtune is 96,000 frames at the sink, and the next subtune's first frame follows the last of them") {
    Path p;
    p.play({kThree, kOne}, 0);                              // subtunes 2 and 3 of the first, then the second tune
    REQUIRE(p.state() == PlayerState::Playing);
    p.play_for(7000);
    CHECK(p.state() == PlayerState::Idle);
    // Each subtune whole and in order, and nothing of any of them twice. (Silence between them, while the
    // next one loads, is left out of the comparison.)
    const Samples expected = without_zeros(ramp(2, 0, 96000) + ramp(3, 0, 96000) + ramp(1, 0, 96000));
    CHECK(difference(p.heard(), expected) == "");
    CHECK(stereo_whole(p.sink));
    CHECK(p.engine.loads == 3);
    CHECK(p.software.underruns() == 0);
    CHECK(p.notices.ids.empty());
}

TEST_CASE("SPEC-060 SPEC-070 after pause and resume through the player the samples are complete and in order") {
    Path p;
    p.play({kLong}, 0);
    p.play_for(1000);
    p.player->play_pause();
    p.settle();
    REQUIRE(p.state() == PlayerState::Paused);
    const std::size_t at_pause = p.heard().size();
    const spz::Millis elapsed = p.status().elapsed_ms;
    p.play_for(2000);                                       // the device asks on; nothing of the tune is given
    CHECK(p.heard().size() == at_pause);
    CHECK(p.status().elapsed_ms == elapsed);
    p.player->play_pause();
    p.settle();
    p.play_for(1000);
    CHECK(p.state() == PlayerState::Playing);
    const Samples heard = p.heard();
    CHECK(heard.size() >= 2 * 48000 - 4 - 480);
    CHECK(difference_from_start(heard, without_zeros(ramp(1, 0, 3 * 48000))) == "");
    CHECK(p.status().elapsed_ms >= 1990);
    CHECK(p.status().elapsed_ms <= 2000);
    CHECK(p.software.underruns() == 0);
}

#if SPZ_PART_RESTART
TEST_CASE("SPEC-066 restart sets position() to 0 and the sink then receives the tune's first samples [parts: restart]") {
    Path p;
    p.play({kLong}, 0);
    p.play_for(1000);
    REQUIRE(p.software.position() > 40000);
    const std::size_t before = frames(p.sink);
    p.player->restart();
    p.settle();
    CHECK(p.software.position() == 0);
    CHECK(p.status().elapsed_ms == 0);
    p.play_for(500);
    CHECK(p.state() == PlayerState::Playing);
    const Samples after = without_zeros(received(p.sink, before));
    CHECK(after.size() >= 24000 - 480);
    CHECK(difference_from_start(after, ramp(1, 0, 24000)) == "");   // from frame 0 again, not from where it was
}
#endif

// ---- SPEC-074 --------------------------------------------------------------------------------------------------------

#if SPZ_PART_VOICES
TEST_CASE("SPEC-074 a voice muted while a tune plays is still muted after next subtune, and after the next tune nothing is muted [parts: voices]") {
    Path p;
    p.play({kThree, kOne}, 0);
    p.play_for(100);
    p.player->toggle_voice(1);
    p.play_for(10);                                         // within one render chunk
    CHECK(p.engine.muted[0][0]);
    CHECK(p.status().muted[0][0]);
    CHECK_FALSE(p.engine.muted[0][1]);
    CHECK_FALSE(p.engine.muted[0][2]);

    p.player->next_subtune();
    p.settle();
    p.play_for(10);
    REQUIRE(p.status().subtune == 3);
    CHECK(p.engine.muted[0][0]);                            // "the choices hold for the tune: across its subtunes"
    CHECK(p.status().muted[0][0]);

    p.player->next_tune();
    p.settle();
    p.play_for(10);
    REQUIRE(p.current() == kOne);
    CHECK_FALSE(p.anything_off());                          // "loading another tune puts everything on again"
    CHECK_FALSE(p.status().muted[0][0]);
}

TEST_CASE("SPEC-074 each voice and the filter can be switched off and on again while a tune plays, and the player's status says which are off [parts: voices]") {
    Path p;
    p.play({kVoices}, 0);
    p.play_for(100);
    for (int voice = 1; voice <= 3; ++voice) {
        CAPTURE(voice);
        p.player->toggle_voice(voice);
        p.play_for(10);
        CHECK(p.engine.muted[0][voice - 1]);
        CHECK(p.status().muted[0][voice - 1]);
    }
    p.player->toggle_filter();
    p.play_for(10);
    CHECK_FALSE(p.engine.filter_on[0]);
    CHECK_FALSE(p.status().filter_on[0]);
    p.player->toggle_voice(2);                              // and on again
    p.player->toggle_filter();
    p.play_for(10);
    CHECK_FALSE(p.engine.muted[0][1]);
    CHECK_FALSE(p.status().muted[0][1]);
    CHECK(p.engine.muted[0][0]);
    CHECK(p.engine.muted[0][2]);
    CHECK(p.engine.filter_on[0]);
    CHECK(p.status().filter_on[0]);
    CHECK(p.state() == PlayerState::Playing);               // none of it stops or reloads the tune
    CHECK(p.engine.loads == 1);
}

TEST_CASE("SPEC-074 SPEC-096 with a tune for three chips, the voice keys act on the chosen chip, and the chip key goes round the chips [parts: voices]") {
    Path p;
    p.engine.chip_count = 3;                                // as the real engine would say of this tune
    p.play({kChips3}, 0);
    p.play_for(100);
    REQUIRE(p.status().chips == 3);
    CHECK(p.status().chip == 1);
    p.player->next_chip();
    CHECK(p.status().chip == 2);
    p.player->toggle_voice(3);
    p.player->toggle_filter();
    p.play_for(10);
    CHECK(p.engine.muted[1][2]);
    CHECK_FALSE(p.engine.filter_on[1]);
    CHECK_FALSE(p.engine.muted[0][2]);
    CHECK(p.engine.filter_on[0]);
    CHECK(p.status().muted[1][2]);
    p.player->next_chip();
    CHECK(p.status().chip == 3);
    p.player->next_chip();
    CHECK(p.status().chip == 1);                            // round again
}

TEST_CASE("SPEC-074 with a one-chip tune the chip key has nowhere to go [parts: voices]") {
    Path p;
    p.play({kOne}, 0);
    p.player->next_chip();
    CHECK(p.status().chip == 1);
    CHECK(p.status().chips == 1);
}
#endif

#if SPZ_PART_VOICES && SPZ_PART_RESTART
TEST_CASE("SPEC-074 SPEC-066 a mute holds across a restart of the tune [parts: voices restart]") {
    Path p;
    p.play({kThree, kOne}, 0);
    p.play_for(100);
    p.player->toggle_voice(2);
    p.player->toggle_filter();
    p.play_for(10);
    p.player->restart();
    p.settle();
    p.play_for(10);
    CHECK(p.engine.muted[0][1]);
    CHECK_FALSE(p.engine.filter_on[0]);
    CHECK(p.status().muted[0][1]);
    CHECK_FALSE(p.status().filter_on[0]);
}
#endif

// ---- SPEC-078 --------------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-078 with the sink refusing to open, play ends in Failed with MSG_NO_SOUND; when it accepts, the next attempt plays") {
    Path p;
    p.sink.refuse = true;
    p.play({kOne, kTwoChips}, 0);
    CHECK(p.state() == PlayerState::Failed);
    CHECK(p.status().message == "MSG_NO_SOUND");
    CHECK(p.current() == kOne);
    CHECK(p.sink.opens >= 1);
    p.play_for(100);
    CHECK(frames(p.sink) == 0);

    p.sink.refuse = false;
    p.player->play_pause();                                 // "the next attempt to play tries again"
    p.settle();
    CHECK(p.state() == PlayerState::Playing);
    CHECK(p.status().message == "");
    p.play_for(100);
    CHECK(difference_from_start(p.heard(), ramp(1, 0, 9600)) == "");
    CHECK(p.heard().size() >= 4800 - 480);
}

TEST_CASE("SPEC-078 with the sink dropping the device for 300 ms mid-tune, the frames received are complete and in order and the player is Playing") {
    Path p;
    p.play({kLong}, 0);
    p.play_for(1000);
    p.sink.refuse = true;                                   // the device is away
    p.sink.drop_device();
    p.play_for(300);
    CHECK(p.state() == PlayerState::Playing);               // "stays Playing"
    p.sink.refuse = false;                                  // and back
    p.play_for(1700);
    CHECK(p.state() == PlayerState::Playing);
    CHECK(p.notices.ids.empty());
    CHECK(p.sink.is_open);
    const Samples heard = p.heard();
    CHECK(heard.size() >= 48000 + 72000 - 6 - 480);
    CHECK(difference_from_start(heard, without_zeros(ramp(1, 0, 4 * 48000))) == "");
    CHECK(stereo_whole(p.sink));
    CHECK(p.software.underruns() == 0);
}

TEST_CASE("SPEC-078 dropping the device for good leaves the player Paused with the notice MSG_NO_SOUND, and Play/Pause after the sink accepts again carries on from the same sample") {
    Path p;
    p.play({kLong}, 0);
    p.play_for(1000);
    p.sink.refuse = true;
    p.sink.drop_device();
    p.play_for(1500);                                       // five attempts, 200 ms apart, have failed by now
    CHECK(p.state() == PlayerState::Paused);
    CHECK(p.notices.ids == Calls{"MSG_NO_SOUND"});
    CHECK(p.current() == kLong);
    const std::size_t before = p.heard().size();
    const std::uint64_t position = p.software.position();
    p.play_for(3000);                                       // nothing more happens by itself
    CHECK(p.state() == PlayerState::Paused);
    CHECK(p.notices.ids == Calls{"MSG_NO_SOUND"});
    CHECK(p.software.position() == position);

    p.sink.refuse = false;
    p.player->play_pause();
    p.settle();
    CHECK(p.state() == PlayerState::Playing);
    CHECK(p.sink.is_open);
    p.play_for(1000);
    const Samples heard = p.heard();
    CHECK(heard.size() >= before + 48000 - 4 - 480);
    CHECK(difference_from_start(heard, without_zeros(ramp(1, 0, 4 * 48000))) == "");   // none lost, none repeated
    CHECK(p.software.underruns() == 0);
}
