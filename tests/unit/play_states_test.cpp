// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-060: the player's state machine, cell by cell of the item's table. Each test is one row of the
// table (one event) taken through all seven states, against the fake SoundBackend, Clock and Executor and
// a stand-in for the fetching of absent tunes. After each event it checks the state and the calls made.
//
// An event that only the player's own timers, its worker or the fetcher can make is sent into the other
// states in the one way it can arrive there: late. The state it belongs to is left by a key before the
// event comes, and the test then lets it come. Where the table says "—" nothing may change.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/player.h"
#include "support.h"

using spz::Entry;
using spz::PlayerState;
using spz::PlayerStatus;
using namespace spz::test;

namespace {

// A player brought to one of the seven states with the queue [Three, X, last] and X the current entry.
// X has one subtune, so "next subtune" and "previous subtune" are steps to its neighbours (SPEC-061). It
// is the 30-second tune where it has to be on the device, so that nothing is fetched ahead meanwhile, and
// an absent tune in Waiting and Fetching.
struct Table : PlayerFixture {
    std::string x;
    std::vector<std::string> paths;

    void bring(PlayerState to, const std::string& last = kOne) {
        const bool absent = to == PlayerState::Waiting || to == PlayerState::Fetching;
        x = absent ? kAbsentC : kLong;
        paths = {kThree, x, last};
        switch (to) {
            case PlayerState::Idle:
                play(paths, 1);
                player->stop();
                settle();
                break;
            case PlayerState::Waiting:          // reached by stepping, from the tune before
                play(paths, 0);
                player->next_tune();
                settle();
                break;
            case PlayerState::Fetching:         // chosen with Enter: requested at once
                play(paths, 1);
                break;
            case PlayerState::Loading:          // the worker has not run yet: the file is still to be read
                player->play(entries(paths), 1);
                break;
            case PlayerState::Playing:
                play(paths, 1);
                break;
            case PlayerState::Paused:
                play(paths, 1);
                player->play_pause();
                settle();
                break;
            case PlayerState::Failed:           // chosen with Enter and refused by the backend: it stays Failed
                backend.refuse = true;
                play(paths, 1);
                backend.refuse = false;
                break;
        }
        REQUIRE(state() == to);
        REQUIRE(current() == x);
    }

    // Leaves the present state for another by an event that a key makes, on tunes of its own.
    void go(PlayerState to) {
        switch (to) {
            case PlayerState::Idle:
                player->stop();
                settle();
                break;
            case PlayerState::Waiting:
                play({kLong, kAbsentD}, 0);
                player->next_tune();
                settle();
                break;
            case PlayerState::Fetching:
                play({kAbsentD}, 0);
                break;
            case PlayerState::Loading:
                player->play(entries({kPresentE}), 0);
                break;
            case PlayerState::Playing:
                play({kPresentE}, 0);
                break;
            case PlayerState::Paused:
                play({kPresentE}, 0);
                player->play_pause();
                settle();
                break;
            case PlayerState::Failed:
                play({kInvalid}, 0);
                break;
        }
        REQUIRE(state() == to);
    }

    // What a cell that ends in a new load must have asked of the backend since `mark`. In Playing and
    // Paused the table says "after stop()". Where nothing was loaded there is the load and the start and
    // nothing else. In Loading and Failed an earlier load may still arrive, or may have left something
    // behind that the player clears away: there the last two calls are the load and the start, and it
    // started once.
    void check_loaded(PlayerState from, std::size_t mark, int subtune) {
        const std::string load = "load " + std::to_string(subtune);
        if (from == PlayerState::Playing || from == PlayerState::Paused) {
            CHECK(calls_since(mark) == Calls{"stop", load, "start"});
        } else if (from == PlayerState::Loading || from == PlayerState::Failed) {
            CHECK(tail(calls_since(mark, false), 2) == Calls{load, "start"});
            CHECK(count_since(mark, "start") == 1);
        } else {
            CHECK(calls_since(mark) == Calls{load, "start"});
        }
        CHECK(count_since(mark, "pause") == 0);
        CHECK(count_since(mark, "resume") == 0);
        CHECK(state() == PlayerState::Playing);
        CHECK(status().subtune == subtune);
        CHECK(status().message == "");
    }

    // What a cell that loads nothing must have asked of the backend: stop() where the table says so, and
    // otherwise nothing that starts, pauses or resumes.
    void check_nothing_started(PlayerState from, std::size_t mark) {
        if (from == PlayerState::Playing || from == PlayerState::Paused) {
            CHECK(calls_since(mark) == Calls{"stop"});
        } else if (from == PlayerState::Loading || from == PlayerState::Failed) {
            CHECK(count_since(mark, "start") == 0);
            CHECK(count_since(mark, "pause") + count_since(mark, "resume") == 0);
        } else {
            CHECK(calls_since(mark).empty());
        }
    }

    // Cuts a tune's file short on the device, so that it no longer passes SPEC-203's check.
    void cut_short(const std::string& path) {
        const spz::Bytes whole = tune(path);
        put(path, spz::Bytes(whole.begin(), whole.begin() + 0x40));
    }

    // The slow-engine notices of SPEC-079.
    int slow_notices() const {
        return notices.count("MSG_SWITCHED_LIGHT") + notices.count("MSG_TOO_HEAVY") + notices.count("MSG_CANNOT_KEEP_UP");
    }
    // After an event that Waiting or Fetching ignores, the tune is still on its way: the wait ends when
    // it would have, 700 ms after the step, and the request in flight is still in flight.
    void still_on_its_way(PlayerState in) {
        if (in == PlayerState::Waiting) {
            CHECK(fetcher.requests.empty());
            clock.advance(spz::kStepWaitMs - 1);
            settle();
            CHECK(state() == PlayerState::Waiting);
            clock.advance(1);
            settle();
            CHECK(state() == PlayerState::Fetching);
        }
        CHECK(fetcher.in_flight(x));
        CHECK(fetcher.requests.size() == 1);
    }

    // The backend's underrun counter rises, and a second passes.
    void rise(bool then_settle = true) {
        backend.underrun_frames += 100;
        clock.advance(1000);
        if (then_settle) settle();
    }
};

bool is(PlayerState state, PlayerState a, PlayerState b) { return state == a || state == b; }

}  // namespace

// ---- Play an entry -------------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 SPEC-062 Enter on a present tune, in every state: the queue is set and the tune loads, after stop() where one was loaded") {
    for (const PlayerState from : kEveryState) {
        CAPTURE(named(from));
        Table t;
        t.bring(from);
        const std::size_t mark = t.mark();
        const std::vector<Entry> queue = {entry(kOne), entry(kThree)};
        t.player->play(queue, 1);
        CHECK(t.state() == PlayerState::Loading);
        CHECK(t.current() == kThree);
        CHECK(t.player->queue() == queue);
        CHECK(t.player->place() == 1);
        t.settle();
        t.check_loaded(from, mark, 2);                      // the three-subtune fixture's default
        CHECK(t.current() == kThree);
        if (from == PlayerState::Waiting) {                 // "replacing the wait": the tune stepped to is never requested
            t.clock.advance(spz::kStepWaitMs + 100);
            t.settle();
            CHECK(t.fetcher.requests.empty());
            CHECK(t.state() == PlayerState::Playing);
        }
        if (from == PlayerState::Fetching) CHECK_FALSE(t.fetcher.in_flight(t.x));   // "cancelling the fetch"
    }
}

TEST_CASE("SPEC-060 Enter on an absent tune, in every state: it is requested at once and the player is Fetching") {
    for (const PlayerState from : kEveryState) {
        CAPTURE(named(from));
        Table t;
        t.bring(from);
        const std::size_t mark = t.mark();
        t.player->play({entry(kOne), entry(kAbsentD)}, 1);
        t.settle();
        CHECK(t.state() == PlayerState::Fetching);
        CHECK(t.current() == kAbsentD);
        CHECK(t.player->place() == 1);
        CHECK(t.fetcher.in_flight(kAbsentD));
        REQUIRE_FALSE(t.fetcher.requests.empty());
        CHECK(t.fetcher.requests.back().path == kAbsentD);
        CHECK(t.fetcher.requests.back().attended);
        CHECK_FALSE(t.fetcher.requests.back().ahead);
        t.check_nothing_started(from, mark);
        if (from == PlayerState::Fetching) {                // the fetch in flight was for another tune: cancelled
            CHECK_FALSE(t.fetcher.in_flight(t.x));
            CHECK(t.fetcher.requests.size() == 2);
        } else {
            CHECK(t.fetcher.requests.size() == 1);
        }
        t.clock.advance(spz::kStepWaitMs + 100);            // and in Waiting the wait is replaced: nothing more goes out
        t.settle();
        CHECK(t.fetcher.requests.size() == (from == PlayerState::Fetching ? 2 : 1));
        CHECK(t.state() == PlayerState::Fetching);
    }
}

TEST_CASE("SPEC-060 SPEC-141 Enter on the tune that is being fetched keeps its request: none is cancelled and no second one goes out") {
    Table t;
    t.bring(PlayerState::Fetching);
    REQUIRE(t.fetcher.requests.size() == 1);
    const std::vector<Entry> other_queue = {entry(kOne), entry(t.x)};      // the same tune, in another list
    t.player->play(other_queue, 1);
    t.settle();
    CHECK(t.state() == PlayerState::Fetching);
    CHECK(t.current() == t.x);
    CHECK(t.fetcher.in_flight(t.x));
    CHECK(t.fetcher.requests.size() == 1);
    CHECK(t.player->queue() == other_queue);                // the queue is set all the same
    CHECK(t.player->place() == 1);
    REQUIRE(t.fetcher.deliver(t.x));
    t.settle();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.backend.count("start") == 1);
}

// ---- The Play/Pause key --------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 Play/Pause, in every state: plays the current entry from Idle and Failed, pauses, resumes, and is ignored while a tune is on its way") {
    for (const PlayerState from : kEveryState) {
        CAPTURE(named(from));
        Table t;
        t.bring(from);
        if (from == PlayerState::Playing) t.clock.advance(1000);
        const std::size_t mark = t.mark();
        const std::size_t requests = t.fetcher.requests.size();
        t.player->play_pause();
        switch (from) {
            case PlayerState::Idle:
            case PlayerState::Failed:                       // "play the current entry", "again"
                CHECK(t.state() == PlayerState::Loading);
                t.settle();
                t.check_loaded(from, mark, 1);
                CHECK(t.current() == t.x);
                CHECK(t.status().elapsed_ms == 0);          // from its start
                break;
            case PlayerState::Waiting:
            case PlayerState::Fetching:
                t.settle();
                CHECK(t.state() == from);
                CHECK(t.calls_since(mark).empty());
                CHECK(t.fetcher.requests.size() == requests);
                t.still_on_its_way(from);
                break;
            case PlayerState::Loading:
                CHECK(t.state() == PlayerState::Loading);
                t.settle();                                 // the load goes on as if nothing had been pressed
                CHECK(t.state() == PlayerState::Playing);
                CHECK(t.calls_since(mark) == Calls{"load 1", "start"});
                break;
            case PlayerState::Playing:
                t.settle();
                CHECK(t.state() == PlayerState::Paused);
                CHECK(t.calls_since(mark) == Calls{"pause"});
                t.clock.advance(1000);
                CHECK(t.status().elapsed_ms == 1000);       // stopped at a known sample
                break;
            case PlayerState::Paused:
                t.settle();
                CHECK(t.state() == PlayerState::Playing);
                CHECK(t.calls_since(mark) == Calls{"resume"});
                break;
        }
        CHECK(t.current() == t.x);
    }
}

TEST_CASE("SPEC-060 a player that has played nothing has no current entry and no queue: Play/Pause, Stop and the four steps do nothing") {
    PlayerFixture f;
    CHECK(f.state() == PlayerState::Idle);
    CHECK_FALSE(f.status().has_entry);
    f.player->play_pause();
    f.player->stop();
    f.player->next_tune();
    f.player->previous_tune();
    f.player->next_subtune();
    f.player->previous_subtune();
    f.settle();
    CHECK(f.state() == PlayerState::Idle);
    CHECK_FALSE(f.status().has_entry);
    CHECK(f.backend.log.empty());
    CHECK(f.fetcher.requests.empty());
    CHECK(f.notices.ids.empty());
}

TEST_CASE("SPEC-060 SPEC-131 a restored entry is the current entry of an Idle player, and Play/Pause plays it from its start") {
    PlayerFixture f;
    f.player->restore(f.entries({kOne, kThree, kTwoChips}), 1);
    f.settle();
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.status().has_entry);
    CHECK(f.current() == kThree);
    CHECK(f.backend.log.empty());                           // nothing is loaded for it
    f.player->play_pause();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.calls_since(0) == Calls{"load 2", "start"});
    f.player->next_tune();                                  // and its queue came back with it
    f.settle();
    CHECK(f.current() == kTwoChips);
}

// ---- Stop ------------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 Stop, in every state: the player is Idle, a load or a fetch on its way is given up, and the current entry stays") {
    for (const PlayerState from : kEveryState) {
        CAPTURE(named(from));
        Table t;
        t.bring(from);
        const std::size_t mark = t.mark();
        t.player->stop();
        CHECK(t.state() == PlayerState::Idle);
        t.settle();
        CHECK(t.state() == PlayerState::Idle);
        CHECK(t.status().has_entry);                        // "the current entry stays after playback stops"
        CHECK(t.current() == t.x);
        CHECK(t.status().message == "");
        if (is(from, PlayerState::Playing, PlayerState::Paused)) {
            CHECK(t.calls_since(mark) == Calls{"stop"});
        } else {
            CHECK(t.count_since(mark, "start") == 0);
            CHECK(t.count_since(mark, "pause") + t.count_since(mark, "resume") == 0);
        }
        if (is(from, PlayerState::Idle, PlayerState::Waiting)) CHECK(t.calls_since(mark).empty());
        CHECK_FALSE(t.backend.running);
        if (from == PlayerState::Fetching) CHECK_FALSE(t.fetcher.in_flight(t.x));   // "cancel the fetch"
        const std::size_t requests = t.fetcher.requests.size();
        t.clock.advance(5000);                              // no wait is left running
        t.settle();
        CHECK(t.state() == PlayerState::Idle);
        CHECK(t.fetcher.requests.size() == requests);
        CHECK(t.count_since(mark, "start") == 0);
    }
}

// ---- Next and previous tune, next and previous subtune -------------------------------------------------------

TEST_CASE("SPEC-060 SPEC-061 SPEC-062 a step to a present tune, in every state and by each of the four keys: it loads, after stop() where one was loaded") {
    struct Step {
        const char* key;
        void (spz::Player::*press)();
        const char* lands_on;
        int subtune;
        std::size_t place;
    };
    // X, the current entry, has one subtune: next from its last is "next tune", previous from its first
    // "previous tune". The queue is [Three, X, One], and Three plays its default subtune, 2.
    const Step steps[] = {
        {"next tune", &spz::Player::next_tune, kOne, 1, 2},
        {"previous tune", &spz::Player::previous_tune, kThree, 2, 0},
        {"next subtune", &spz::Player::next_subtune, kOne, 1, 2},
        {"previous subtune", &spz::Player::previous_subtune, kThree, 2, 0},
    };
    for (const PlayerState from : kEveryState) {
        for (const Step& step : steps) {
            CAPTURE(named(from));
            CAPTURE(std::string(step.key));
            Table t;
            t.bring(from);
            const std::size_t mark = t.mark();
            (t.player.get()->*step.press)();
            CHECK(t.state() == PlayerState::Loading);       // "a step to an entry that is present goes to Loading"
            CHECK(t.current() == step.lands_on);
            CHECK(t.player->place() == step.place);
            t.settle();
            t.check_loaded(from, mark, step.subtune);
            CHECK(t.current() == step.lands_on);
            CHECK(t.player->queue() == t.entries(t.paths)); // a step does not change the queue
            if (from == PlayerState::Fetching) CHECK_FALSE(t.fetcher.in_flight(t.x));   // "cancelling the fetch"
            const std::size_t requests = t.fetcher.requests.size();
            t.clock.advance(spz::kStepWaitMs + 100);        // the wait for X, if there was one, is gone
            t.settle();
            CHECK(t.fetcher.requests.size() == requests);
            CHECK(t.state() == PlayerState::Playing);
        }
    }
}

TEST_CASE("SPEC-060 SPEC-141 a step to an absent tune, in every state: the player is Waiting, and the tune is requested when it has been current for 700 ms") {
    for (const PlayerState from : kEveryState) {
        CAPTURE(named(from));
        Table t;
        t.bring(from, kAbsentD);                            // the queue is [Three, X, an absent tune]
        if (from == PlayerState::Waiting) t.clock.advance(500);     // X's own wait has 200 ms left
        const std::size_t mark = t.mark();
        const std::size_t requests = t.fetcher.requests.size();
        t.player->next_tune();
        t.settle();
        CHECK(t.state() == PlayerState::Waiting);
        CHECK(t.current() == kAbsentD);
        CHECK(t.fetcher.requests.size() == requests);       // nothing is requested yet
        t.check_nothing_started(from, mark);
        if (from == PlayerState::Fetching) CHECK_FALSE(t.fetcher.in_flight(t.x));   // "cancelling the fetch"
        t.clock.advance(spz::kStepWaitMs - 1);              // "restarting the wait": X's wait would have ended by now
        t.settle();
        CHECK(t.state() == PlayerState::Waiting);
        CHECK(t.fetcher.requests.size() == requests);
        t.clock.advance(1);
        t.settle();
        CHECK(t.state() == PlayerState::Fetching);
        REQUIRE(t.fetcher.requests.size() == requests + 1);
        CHECK(t.fetcher.requests.back().path == kAbsentD);
        CHECK(t.fetcher.requests.back().attended);          // a key reached it
        CHECK(t.fetcher.in_flight(kAbsentD));
    }
}

// ---- Restart -----------------------------------------------------------------------------------------------------

#if SPZ_PART_RESTART
TEST_CASE("SPEC-060 SPEC-066 Restart, in every state: the same subtune is loaded again from Idle, Playing, Paused and Failed, and it is ignored while a tune is on its way [parts: restart]") {
    for (const PlayerState from : kEveryState) {
        CAPTURE(named(from));
        Table t;
        t.bring(from);
        if (from == PlayerState::Playing) t.clock.advance(1000);
        const std::size_t mark = t.mark();
        const std::size_t requests = t.fetcher.requests.size();
        t.player->restart();
        t.settle();
        switch (from) {
            case PlayerState::Idle:
            case PlayerState::Failed:
            case PlayerState::Playing:
            case PlayerState::Paused:                       // "load the same subtune again; → Playing"
                CHECK(t.state() == PlayerState::Playing);
                CHECK(tail(t.calls_since(mark, false), 2) == Calls{"load 1", "start"});
                CHECK(t.count_since(mark, "start") == 1);
                CHECK(t.count_since(mark, "resume") == 0);
                CHECK(t.status().elapsed_ms == 0);
                break;
            case PlayerState::Waiting:
            case PlayerState::Fetching:
                CHECK(t.state() == from);
                CHECK(t.calls_since(mark).empty());
                CHECK(t.fetcher.requests.size() == requests);
                t.still_on_its_way(from);
                break;
            case PlayerState::Loading:                      // the load it was in the middle of simply ends
                CHECK(t.state() == PlayerState::Playing);
                CHECK(t.calls_since(mark) == Calls{"load 1", "start"});
                break;
        }
        CHECK(t.current() == t.x);
    }
}
#endif

// ---- Wait is over ------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 SPEC-141 the wait is over 700 ms after the step, and the absent tune is then requested: Waiting goes to Fetching") {
    Table t;
    t.bring(PlayerState::Waiting);
    CHECK(t.fetcher.requests.empty());
    t.clock.advance(spz::kStepWaitMs - 1);
    t.settle();
    CHECK(t.state() == PlayerState::Waiting);
    CHECK(t.fetcher.requests.empty());
    t.clock.advance(1);
    t.settle();
    CHECK(t.state() == PlayerState::Fetching);
    REQUIRE(t.fetcher.requests.size() == 1);
    CHECK(t.fetcher.requests[0].path == t.x);
    CHECK(t.calls_since(0) == Calls{"load 2", "start", "stop"});    // the tune before, and nothing since
}

TEST_CASE("SPEC-060 a wait that a key has ended does nothing when its time comes, whatever state the player is in by then") {
    for (const PlayerState to : kEveryState) {
        if (to == PlayerState::Waiting) continue;
        CAPTURE(named(to));
        Table t;
        t.bring(PlayerState::Waiting);
        t.go(to);
        const std::string current = t.current();
        const std::size_t mark = t.mark();
        const std::size_t requests = t.fetcher.requests.size();
        t.clock.advance(spz::kStepWaitMs + 100);
        if (to != PlayerState::Loading) t.settle();
        CHECK(t.state() == to);
        CHECK(t.current() == current);
        CHECK(t.fetcher.requests.size() == requests);       // the tune that was waited for is not requested
        CHECK_FALSE(t.fetcher.in_flight(t.x));
        CHECK(t.calls_since(mark).empty());
    }
}

// ---- Fetched, and could not fetch --------------------------------------------------------------------------------

TEST_CASE("SPEC-060 Fetched: the tune that was being fetched is loaded and played, with the one request") {
    Table t;
    t.bring(PlayerState::Fetching);
    const std::size_t mark = t.mark();
    REQUIRE(t.fetcher.deliver(t.x));
    CHECK(t.state() == PlayerState::Loading);
    t.settle();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.calls_since(mark) == Calls{"load 1", "start"});
    CHECK(t.current() == t.x);
    CHECK(t.fetcher.requests.size() == 1);
}

TEST_CASE("SPEC-060 SPEC-069 Could not fetch: the player is Failed with the fetch's message, and a tune chosen with Enter stays there") {
    Table t;
    t.bring(PlayerState::Fetching);
    const std::size_t mark = t.mark();
    REQUIRE(t.fetcher.fail(t.x, "MSG_FETCH_NOT_FOUND"));
    t.settle();
    CHECK(t.state() == PlayerState::Failed);
    CHECK(t.status().message == "MSG_FETCH_NOT_FOUND");
    CHECK(t.current() == t.x);
    CHECK(t.calls_since(mark).empty());
    t.clock.advance(10000);
    t.settle();
    CHECK(t.state() == PlayerState::Failed);
    CHECK(t.fetcher.requests.size() == 1);
}

TEST_CASE("SPEC-060 an answer to a fetch that the player has left behind changes nothing, whether the tune came or not, in every other state") {
    for (const PlayerState to : kEveryState) {
        if (to == PlayerState::Fetching) continue;
        for (const bool came : {true, false}) {
            CAPTURE(named(to));
            CAPTURE(came);
            Table t;
            t.bring(PlayerState::Fetching);
            t.go(to);
            const PlayerStatus before = t.status();
            const std::size_t mark = t.mark();
            const std::size_t notices = t.notices.ids.size();
            // A request that was cancelled never answers; one that was not may. Neither may move the player.
            if (came) t.fetcher.deliver(t.x);
            else t.fetcher.fail(t.x, "MSG_FETCH_NETWORK");
            CHECK(t.state() == to);
            if (to != PlayerState::Loading) t.settle();
            CHECK(t.state() == to);
            CHECK(t.current() == before.entry.path);
            CHECK(t.status().message == before.message);
            CHECK(t.calls_since(mark).empty());
            CHECK(t.notices.ids.size() == notices);
            if (to == PlayerState::Loading) {               // and the load that was on its way ends as it would have
                t.settle();
                CHECK(t.state() == PlayerState::Playing);
                CHECK(t.current() == kPresentE);
            }
        }
    }
}

// ---- Loaded, and could not load ----------------------------------------------------------------------------------

TEST_CASE("SPEC-060 Loaded: the backend is started and the player is Playing") {
    Table t;
    t.bring(PlayerState::Loading);
    CHECK(t.count_since(0, "start") == 0);
    t.settle();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.calls_since(0) == Calls{"load 1", "start"});
    CHECK(t.backend.running);
}

TEST_CASE("SPEC-060 SPEC-069 SPEC-203 Could not load: the player is Failed with the reason, and a file that fails the check never reaches the backend") {
    Table t;
    t.player->play(t.entries({kThree, kInvalid, kOne}), 1);
    CHECK(t.state() == PlayerState::Loading);
    t.settle();
    CHECK(t.state() == PlayerState::Failed);
    CHECK(t.status().message == "MSG_INVALID_SID");
    CHECK(t.current() == kInvalid);
    CHECK(t.backend.count("load") == 0);
    CHECK(t.backend.count("start") == 0);
}

TEST_CASE("SPEC-060 the end of a load that the player has left behind changes nothing, whether it loaded or not: in Idle, Waiting, Fetching and Failed") {
    for (const bool good : {true, false}) {
        for (const PlayerState to : {PlayerState::Idle, PlayerState::Waiting, PlayerState::Fetching, PlayerState::Failed}) {
            CAPTURE(named(to));
            CAPTURE(good);
            Table t;
            t.bring(PlayerState::Loading, kAbsentD);
            // The worker has not read the file yet. Cut short now, it will not pass SPEC-203's check.
            if (!good) t.cut_short(t.x);
            switch (to) {
                case PlayerState::Idle: t.player->stop(); break;
                case PlayerState::Waiting: t.player->next_tune(); break;                     // to the absent tune
                case PlayerState::Fetching: t.player->play(t.entries({kAbsentD}), 0); break;
                default: t.backend.emit_failed("MSG_NO_SOUND"); break;                       // Failed
            }
            t.settle();                                     // the load that was left ends now
            CHECK(t.state() == to);
            CHECK(t.backend.count("start") == 0);
            CHECK_FALSE(t.backend.running);
            if (to == PlayerState::Failed) CHECK(t.status().message == "MSG_NO_SOUND");
            else CHECK(t.status().message == "");
            if (to == PlayerState::Idle) CHECK(t.current() == t.x);
            else if (to != PlayerState::Failed) CHECK(t.current() == kAbsentD);
        }
    }
}

TEST_CASE("SPEC-060 a load that another has replaced starts nothing: Playing and Paused are reached only by the load that is current") {
    // In Playing and Paused no load is on its way, so "Loaded" and "Could not load" can only belong to a
    // load that was replaced before it ended. Such a one must not start the backend a second time, and
    // its failure must not fail the tune that plays.
    for (const bool good : {true, false}) {
        CAPTURE(good);
        Table t;
        t.bring(PlayerState::Loading);
        if (!good) t.cut_short(t.x);
        t.player->play(t.entries({kThree}), 0);
        t.settle();
        CHECK(t.state() == PlayerState::Playing);
        CHECK(t.current() == kThree);
        CHECK(t.status().subtune == 2);
        CHECK(t.backend.loaded_subtune == 2);
        CHECK(t.backend.count("start") == 1);
        CHECK(t.status().message == "");
        t.player->play_pause();
        t.settle();
        CHECK(t.state() == PlayerState::Paused);
        CHECK(t.backend.count("start") == 1);
    }
}

// ---- song_ended ----------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 SPEC-065 song_ended while Playing: the next entry is loaded and played") {
    Table t;
    t.bring(PlayerState::Playing);
    const std::size_t mark = t.mark();
    t.run_to_end();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.current() == kOne);
    CHECK(tail(t.calls_since(mark, false), 2) == Calls{"load 1", "start"});
    CHECK(t.count_since(mark, "start") == 1);
}

TEST_CASE("SPEC-060 song_ended in any other state is ignored") {
    for (const PlayerState in : kEveryState) {
        if (in == PlayerState::Playing) continue;
        CAPTURE(named(in));
        Table t;
        t.bring(in);
        const PlayerStatus before = t.status();
        const std::size_t mark = t.mark();
        const std::size_t requests = t.fetcher.requests.size();
        t.backend.emit_song_ended();
        CHECK(t.state() == in);
        CHECK(t.current() == t.x);
        CHECK(t.status().message == before.message);
        CHECK(t.fetcher.requests.size() == requests);
        if (in == PlayerState::Waiting || in == PlayerState::Fetching) t.still_on_its_way(in);
        if (in == PlayerState::Loading) {
            t.settle();                                     // the load ends, and it is X that plays, not the tune after it
            CHECK(t.state() == PlayerState::Playing);
            CHECK(t.current() == t.x);
            CHECK(t.calls_since(mark) == Calls{"load 1", "start"});
        } else if (in != PlayerState::Waiting) {
            t.settle();
            CHECK(t.state() == in);
            CHECK(t.calls_since(mark).empty());
        }
    }
}

// ---- The fetch-ahead -------------------------------------------------------------------------------------------------

namespace {

// A two-second tune playing, with an absent tune after it: the fetch-ahead goes out as soon as the song
// has 10 s or less to go, which is from its first moment.
struct Ahead : Table {
    Ahead() {
        play({kPresentA, kAbsentC, kPresentE}, 0);
        clock.advance(100);
        settle();
        REQUIRE(state() == PlayerState::Playing);
        REQUIRE(fetcher.requests.size() == 1);
        REQUIRE(fetcher.requests[0].path == kAbsentC);
        REQUIRE(fetcher.requests[0].ahead);
        REQUIRE(fetcher.in_flight(kAbsentC));
    }
};

}  // namespace

TEST_CASE("SPEC-060 SPEC-141 a fetch-ahead that is done while the song plays is remembered: at the song's end the next tune loads at once, with no second request") {
    for (const bool paused_meanwhile : {false, true}) {
        CAPTURE(paused_meanwhile);
        Ahead t;
        if (paused_meanwhile) {
            t.player->play_pause();
            t.settle();
            REQUIRE(t.state() == PlayerState::Paused);
        }
        const std::size_t mark = t.mark();
        REQUIRE(t.fetcher.deliver(kAbsentC));
        t.settle();
        CHECK(t.state() == (paused_meanwhile ? PlayerState::Paused : PlayerState::Playing));   // "It does not touch the player's state"
        CHECK(t.current() == kPresentA);
        CHECK(t.calls_since(mark).empty());
        if (paused_meanwhile) {
            t.player->play_pause();
            t.settle();
        }
        REQUIRE(t.state() == PlayerState::Playing);
        t.clock.advance(1900);
        t.settle();
        t.backend.emit_song_ended();
        CHECK(t.state() == PlayerState::Loading);           // Playing, Loading, Playing: never Fetching
        CHECK(t.current() == kAbsentC);
        t.settle();
        CHECK(t.state() == PlayerState::Playing);
        CHECK(t.fetcher.requests.size() == 1);
    }
}

TEST_CASE("SPEC-060 SPEC-141 a fetch-ahead that failed while the song plays is remembered: nothing happens until the song's end, and then the entry fails with that failure, with no second request") {
    for (const bool paused_meanwhile : {false, true}) {
        CAPTURE(paused_meanwhile);
        Ahead t;
        if (paused_meanwhile) {
            t.player->play_pause();
            t.settle();
        }
        const std::size_t mark = t.mark();
        REQUIRE(t.fetcher.fail(kAbsentC, "MSG_FETCH_NOT_FOUND"));
        t.settle();
        CHECK(t.state() == (paused_meanwhile ? PlayerState::Paused : PlayerState::Playing));
        CHECK(t.current() == kPresentA);
        CHECK(t.status().message == "");
        CHECK(t.notices.ids.empty());
        CHECK(t.calls_since(mark).empty());
        if (paused_meanwhile) {
            t.player->play_pause();
            t.settle();
        }
        t.run_to_end();
        CHECK(t.state() == PlayerState::Failed);
        CHECK(t.current() == kAbsentC);
        CHECK(t.status().message == "MSG_FETCH_NOT_FOUND");
        CHECK(t.fetcher.requests.size() == 1);
    }
}

TEST_CASE("SPEC-060 SPEC-141 song_ended while the next entry's fetch-ahead is still running: the player is Fetching and takes that request over") {
    Ahead t;
    t.run_to_end();
    CHECK(t.state() == PlayerState::Fetching);
    CHECK(t.current() == kAbsentC);
    CHECK(t.fetcher.requests.size() == 1);                  // still the one request
    CHECK(t.fetcher.in_flight(kAbsentC));
    REQUIRE(t.fetcher.deliver(kAbsentC));
    t.settle();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.current() == kAbsentC);
    CHECK(t.fetcher.requests.size() == 1);
}

TEST_CASE("SPEC-060 the end of a fetch-ahead that the player has left behind changes nothing, done or failed: in Idle, Waiting, Fetching, Loading and Failed") {
    for (const PlayerState to : {PlayerState::Idle, PlayerState::Waiting, PlayerState::Fetching, PlayerState::Loading,
                                 PlayerState::Failed}) {
        for (const bool came : {true, false}) {
            CAPTURE(named(to));
            CAPTURE(came);
            Ahead t;
            t.go(to);
            const PlayerStatus before = t.status();
            const std::size_t mark = t.mark();
            const std::size_t notices = t.notices.ids.size();
            if (came) t.fetcher.deliver(kAbsentC);
            else t.fetcher.fail(kAbsentC, "MSG_FETCH_NETWORK");
            CHECK(t.state() == to);
            if (to != PlayerState::Loading) t.settle();
            CHECK(t.state() == to);
            CHECK(t.current() == before.entry.path);
            CHECK(t.status().message == before.message);
            CHECK(t.calls_since(mark).empty());
            CHECK(t.notices.ids.size() == notices);
        }
    }
}

// ---- The device ------------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 SPEC-078 device_lost and device_back, in every state: the player stays as it is, and asks nothing of the backend") {
    for (const PlayerState in : kEveryState) {
        CAPTURE(named(in));
        Table t;
        t.bring(in);
        const std::size_t mark = t.mark();
        t.backend.emit_device_lost();
        CHECK(t.state() == in);
        t.backend.emit_device_back();
        CHECK(t.state() == in);
        CHECK(t.calls_since(mark).empty());
        CHECK(t.notices.ids.empty());
        CHECK(t.current() == t.x);
    }
}

TEST_CASE("SPEC-060 SPEC-078 device_gone, in every state: a playing tune is paused with the notice MSG_NO_SOUND, and elsewhere it is ignored") {
    for (const PlayerState in : kEveryState) {
        CAPTURE(named(in));
        Table t;
        t.bring(in);
        const std::size_t mark = t.mark();
        t.backend.emit_device_gone();
        if (in == PlayerState::Playing) {
            CHECK(t.state() == PlayerState::Paused);
            CHECK(t.calls_since(mark) == Calls{"pause"});
            CHECK(t.notices.ids == Calls{"MSG_NO_SOUND"});
            t.player->play_pause();                         // Play/Pause then tries again
            CHECK(t.state() == PlayerState::Playing);
            CHECK(t.calls_since(mark) == Calls{"pause", "resume"});
        } else {
            CHECK(t.state() == in);
            CHECK(t.calls_since(mark).empty());
            CHECK(t.notices.ids.empty());
        }
        CHECK(t.current() == t.x);
    }
}

TEST_CASE("SPEC-060 SPEC-069 failed from the backend, in every state: Loading, Playing and Paused go to Failed with its reason, and elsewhere it is ignored") {
    for (const PlayerState in : kEveryState) {
        CAPTURE(named(in));
        Table t;
        t.bring(in);
        const PlayerStatus before = t.status();
        const std::size_t mark = t.mark();
        t.backend.emit_failed("MSG_NO_SOUND");
        if (in == PlayerState::Loading || in == PlayerState::Playing || in == PlayerState::Paused) {
            CHECK(t.state() == PlayerState::Failed);
            CHECK(t.status().message == "MSG_NO_SOUND");
            t.settle();
            CHECK(t.state() == PlayerState::Failed);
            CHECK(t.status().message == "MSG_NO_SOUND");
            CHECK(t.count_since(mark, "start") == 0);
        } else {
            CHECK(t.state() == in);
            CHECK(t.status().message == before.message);    // in Failed: still the first failure's
            t.settle();
            CHECK(t.state() == in);
            CHECK(t.calls_since(mark).empty());
        }
        CHECK(t.current() == t.x);
    }
}

// ---- The unattended wait ---------------------------------------------------------------------------------------------

namespace {

// A good tune has played to its end and the entry after it is a file cut short: the player is Failed
// without a key having asked for that entry, and its 2 s wait is running.
struct Unattended : Table {
    Unattended() {
        play({kGoodA, kInvalid, kGoodG}, 0);
        run_to_end();
        REQUIRE(state() == PlayerState::Failed);
        REQUIRE(current() == kInvalid);
        REQUIRE(status().message == "MSG_INVALID_SID");
    }
};

}  // namespace

TEST_CASE("SPEC-060 SPEC-069 the unattended wait is over after 2 s: the player goes from Failed to the next entry") {
    Unattended t;
    const std::size_t mark = t.mark();
    t.clock.advance(spz::kFailedWaitMs - 1);
    t.settle();
    CHECK(t.state() == PlayerState::Failed);
    CHECK(t.current() == kInvalid);
    t.clock.advance(1);
    t.settle();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.current() == kGoodG);
    CHECK(tail(t.calls_since(mark, false), 2) == Calls{"load 1", "start"});
    CHECK(t.notices.ids.empty());
}

TEST_CASE("SPEC-060 a key that touches the player takes the place of the unattended wait: when its 2 s are over nothing happens, in every other state") {
    for (const PlayerState to : kEveryState) {
        if (to == PlayerState::Failed) continue;
        CAPTURE(named(to));
        Unattended t;
        t.go(to);
        const std::string current = t.current();
        const std::size_t mark = t.mark();
        t.clock.advance(spz::kFailedWaitMs + 100);
        if (to != PlayerState::Loading) t.settle();
        // A tune stepped to is requested after its own 700 ms, as ever; nothing else has moved.
        CHECK(t.state() == (to == PlayerState::Waiting ? PlayerState::Fetching : to));
        CHECK(t.current() == current);
        CHECK(t.calls_since(mark).empty());
        CHECK(t.notices.ids.empty());
    }
}

TEST_CASE("SPEC-060 SPEC-069 in Failed after a key there is no unattended wait: the player stays Failed") {
    Table t;
    t.bring(PlayerState::Failed);
    const std::size_t mark = t.mark();
    t.clock.advance(60000);
    t.settle();
    CHECK(t.state() == PlayerState::Failed);
    CHECK(t.current() == t.x);
    CHECK(t.status().message == "MSG_ENGINE_REFUSED");
    CHECK(t.count_since(mark, "start") == 0);
}

// ---- Too slow, and the L key -----------------------------------------------------------------------------------------

#if SPZ_PART_TOO_SLOW
namespace {

// The 30-second tune playing with reSIDfp: `engine` is the setting, and the stored measurement says
// reSIDfp is fast enough for `auto` to choose it.
struct Slow : Table {
    explicit Slow(spz::EngineChoice engine) {
        settings.engine = engine;
        spz::SpeedFactors factors;
        factors.measured = true;
        factors.accurate = 8.0;
        factors.light = 100.0;
        player->set_speed_factors(factors);
        bring(PlayerState::Playing);
        REQUIRE(backend.loaded_settings.kind ==
                (engine == spz::EngineChoice::Light ? spz::EngineKind::Light : spz::EngineKind::Accurate));
    }
    // Three seconds with underruns in each, and then up to 10 s more until the notice is there.
    void three_rises_and_notice(const char* id) {
        rise();
        rise();
        rise();
        for (int second = 0; second < 10 && notices.count(id) == 0; ++second) {
            clock.advance(1000);
            settle();
        }
        REQUIRE(notices.count(id) == 1);
    }
    // By a key where a key can do it; Failed by the backend failing, which is the way there from Playing.
    void leave_for(PlayerState to) {
        if (to == PlayerState::Failed) backend.emit_failed("MSG_NO_SOUND");
        else if (to == PlayerState::Paused) player->play_pause();
        else go(to);
        if (to != PlayerState::Loading) settle();
        REQUIRE(state() == to);
    }
};

}  // namespace

TEST_CASE("SPEC-060 SPEC-079 too slow while Playing: the player acts by SPEC-079 [parts: too-slow]") {
    Slow t(spz::EngineChoice::Auto);
    const std::size_t mark = t.mark();
    t.rise();
    t.rise();
    CHECK(t.slow_notices() == 0);
    t.rise();
    t.clock.advance(spz::kSlowWindowMs);
    t.settle();
    CHECK(t.notices.count("MSG_SWITCHED_LIGHT") == 1);
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.backend.loaded_settings.kind == spz::EngineKind::Light);
    CHECK(tail(t.calls_since(mark, false), 2) == Calls{"load 1", "start"});
}

TEST_CASE("SPEC-060 SPEC-079 underruns that go on in any other state are ignored: nothing is loaded again and no notice is raised [parts: too-slow]") {
    for (const PlayerState to : kEveryState) {
        if (to == PlayerState::Playing) continue;
        CAPTURE(named(to));
        Slow t(spz::EngineChoice::Auto);
        t.rise();
        t.rise();                                           // two of the three seconds, while Playing
        t.leave_for(to);
        const std::size_t mark = t.mark();
        t.rise(to != PlayerState::Loading);                 // the third comes in another state
        t.rise(to != PlayerState::Loading);
        CHECK(t.slow_notices() == 0);
        CHECK(t.count_since(mark, "start") == 0);
        if (to != PlayerState::Loading) CHECK(t.backend.loaded_settings.kind == spz::EngineKind::Accurate);
    }
}

TEST_CASE("SPEC-060 SPEC-079 the L key while the offer shows and the tune plays: the same subtune is loaded again with the light engine [parts: too-slow]") {
    Slow t(spz::EngineChoice::Accurate);
    t.three_rises_and_notice("MSG_TOO_HEAVY");
    CHECK(t.backend.loaded_settings.kind == spz::EngineKind::Accurate);     // offered, not done
    const std::size_t mark = t.mark();
    t.player->use_light_engine();
    t.settle();
    CHECK(t.state() == PlayerState::Playing);
    CHECK(t.current() == t.x);
    CHECK(t.backend.loaded_settings.kind == spz::EngineKind::Light);
    CHECK(tail(t.calls_since(mark, false), 2) == Calls{"load 1", "start"});
    CHECK(t.status().elapsed_ms == 0);                      // from its beginning
}

TEST_CASE("SPEC-060 SPEC-079 the L key in any other state does nothing, though the offer still shows [parts: too-slow]") {
    for (const PlayerState to : kEveryState) {
        if (to == PlayerState::Playing) continue;
        CAPTURE(named(to));
        Slow t(spz::EngineChoice::Accurate);
        t.three_rises_and_notice("MSG_TOO_HEAVY");          // it shows for 10 s from now
        t.leave_for(to);
        const std::string current = t.current();
        const std::size_t mark = t.mark();
        t.player->use_light_engine();
        CHECK(t.state() == to);
        if (to == PlayerState::Loading) {
            t.settle();                                     // the tune that was on its way plays, with the engine the setting names
            CHECK(t.state() == PlayerState::Playing);
            CHECK(t.backend.loaded_settings.kind == spz::EngineKind::Accurate);
            CHECK(t.count_since(mark, "start") == 1);
        } else {
            t.settle();
            CHECK(t.state() == to);
            CHECK(t.calls_since(mark).empty());
        }
        CHECK(t.current() == current);
    }
}
#endif

// ---- Playing switched off --------------------------------------------------------------------------------------------

TEST_CASE("SPEC-060 SPEC-144 while playing is switched off, every event that would load a tune raises MSG_UNPACKING and does nothing else") {
    struct Event {
        const char* what;
        void (*send)(Table&);
    };
    const Event events[] = {
        {"Enter on a tune", [](Table& t) { t.player->play(t.entries({kOne, kThree}), 1); }},
        {"Play/Pause", [](Table& t) { t.player->play_pause(); }},
        {"next tune", [](Table& t) { t.player->next_tune(); }},
        {"previous tune", [](Table& t) { t.player->previous_tune(); }},
        {"next subtune", [](Table& t) { t.player->next_subtune(); }},
        {"previous subtune", [](Table& t) { t.player->previous_subtune(); }},
    };
    for (const Event& event : events) {
        CAPTURE(std::string(event.what));
        Table t;
        t.bring(PlayerState::Idle);                         // the download stops the player first (SPEC-144)
        t.player->set_playing_off(true);
        const std::size_t mark = t.mark();
        event.send(t);
        t.settle();
        CHECK(t.notices.ids == Calls{"MSG_UNPACKING"});
        CHECK(t.state() == PlayerState::Idle);
        CHECK(t.current() == t.x);
        CHECK(t.player->queue() == t.entries(t.paths));
        CHECK(t.player->place() == 1);
        CHECK(t.calls_since(mark).empty());
        CHECK(t.fetcher.requests.empty());

        t.player->set_playing_off(false);                   // and when it is on again the same event plays
        event.send(t);
        t.settle();
        CHECK(t.state() == PlayerState::Playing);
        CHECK(t.notices.ids == Calls{"MSG_UNPACKING"});
    }
}

TEST_CASE("SPEC-060 SPEC-144 while playing is switched off, Stop still does nothing and raises no notice") {
    Table t;
    t.bring(PlayerState::Idle);
    t.player->set_playing_off(true);
    t.player->stop();
    t.settle();
    CHECK(t.state() == PlayerState::Idle);
    CHECK(t.notices.ids.empty());
}

#if SPZ_PART_RESTART
TEST_CASE("SPEC-060 SPEC-144 while playing is switched off, Restart raises MSG_UNPACKING and loads nothing [parts: restart]") {
    Table t;
    t.bring(PlayerState::Idle);
    t.player->set_playing_off(true);
    const std::size_t mark = t.mark();
    t.player->restart();
    t.settle();
    CHECK(t.notices.ids == Calls{"MSG_UNPACKING"});
    CHECK(t.state() == PlayerState::Idle);
    CHECK(t.calls_since(mark).empty());
}
#endif
