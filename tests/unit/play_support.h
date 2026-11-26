// SPDX-License-Identifier: GPL-3.0-or-later
// What the tests of playing share (tests/unit/play_*_test.cpp): a player set up against the fakes, on a
// copy of the fixture collection tests/fixtures/play/C64Music (tools/fixture_makers/play.py), and a
// stand-in for the fetching of absent tunes, which belongs to another part of `core` (SPEC-141).
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/listing.h"
#include "spz/player.h"
#include "support.h"

namespace spz::test {

// ---- The fixture collection's tunes, by their collection paths ------------------------------------------
const char* const kOne = "/QUEUE/A_One.sid";                // one subtune, 0:02
const char* const kThree = "/QUEUE/B_Three.sid";            // three subtunes, default 2, 0:02 each
const char* const kTwoChips = "/QUEUE/C_Two_Chips.sid";     // one subtune, two SID chips, 0:02
const char* const kChips1 = "/CHIPS/One_Chip.sid";
const char* const kChips2 = "/CHIPS/Two_Chips_2SID.sid";
const char* const kChips3 = "/CHIPS/Three_Chips_3SID.sid";
const char* const kLong = "/LENGTHS/Long.sid";              // 0:30
const char* const kOdd = "/LENGTHS/Odd.sid";                // 0:02.001
const char* const kThreeLengths = "/LENGTHS/Three_Lengths.sid";  // 0:01.001, 0:02.345 and 1:01.288; default 1
const char* const kUnlisted = "/LENGTHS/Unlisted.sid";      // on the device, not in the database
const char* const kGoodA = "/BAD/A_Good.sid";
const char* const kGoodG = "/BAD/G_Good.sid";
const char* const kBad[5] = {"/BAD/B_Bad_1.sid", "/BAD/C_Bad_2.sid", "/BAD/D_Bad_3.sid", "/BAD/E_Bad_4.sid",
                             "/BAD/F_Bad_5.sid"};          // five tunes cut short
const char* const kInvalid = "/FAIL/A_Invalid.sid";         // cut short
const char* const kRefused = "/FAIL/B_Refused.sid";         // passes SPEC-203's check; the real engine refuses it
const char* const kNeedsBasic = "/FAIL/C_Needs_BASIC.sid";
const char* const kVoices = "/VOICES/Voices.sid";           // three subtunes, 0:30 each
const char* const kPresentA = "/FETCH/A_Present.sid";
const char* const kPresentE = "/FETCH/E_Present.sid";
const char* const kAbsentB = "/FETCH/B_Absent.sid";         // absent; three subtunes, default 2: 0:05, 0:06, 0:07
const char* const kAbsentC = "/FETCH/C_Absent.sid";         // absent; one subtune, 0:02
const char* const kAbsentD = "/FETCH/D_Absent.sid";         // absent; one subtune, 0:02
const char* const kNowhere = "/NOWHERE/Gone.sid";           // neither on the device nor in the database: missing

inline Entry entry(const std::string& path, int subtune = 0) {
    Entry e;
    e.path = path;
    e.subtune = subtune;
    return e;
}

inline const char* name(PlayerState state) {
    switch (state) {
        case PlayerState::Idle: return "Idle";
        case PlayerState::Waiting: return "Waiting";
        case PlayerState::Fetching: return "Fetching";
        case PlayerState::Loading: return "Loading";
        case PlayerState::Playing: return "Playing";
        case PlayerState::Paused: return "Paused";
        case PlayerState::Failed: return "Failed";
    }
    return "?";
}

// For CAPTURE, which shows text only if it is given a string.
inline std::string named(PlayerState state) { return name(state); }

const PlayerState kEveryState[7] = {PlayerState::Idle, PlayerState::Waiting, PlayerState::Fetching,
                                    PlayerState::Loading, PlayerState::Playing, PlayerState::Paused,
                                    PlayerState::Failed};

// ---- A listener that keeps what it is told ------------------------------------------------------------------
struct Notices : PlayerListener {
    std::vector<std::string> ids;
    int changes = 0;

    void notice(const std::string& id, const std::map<std::string, std::string>&) override { ids.push_back(id); }
    void changed() override { ++changes; }
    int count(const std::string& id) const { return static_cast<int>(std::count(ids.begin(), ids.end(), id)); }
};

// ---- What stands in for the fetching of absent tunes ------------------------------------------------------
// It keeps every request until the test answers it, as HVSC's site would answer after a while. A request
// "goes out" when fetch() or fetch_ahead() names a tune that no request is in flight for; `requests` is
// the log of those, and stands for the log of the fake HttpClient behind the real fetcher.
struct FakeFetcher : TuneFetcher {
    struct Request {
        std::string path;
        bool attended;
        bool ahead;
    };

    std::string root;                               // the collection root: deliver() puts the file there
    UnattendedFetch leave = UnattendedFetch::Allowed;    // what unattended() answers
    Millis wait_ms = 0;
    std::vector<Request> requests;
    int cancels = 0;
    int asked = 0;                                  // how often unattended() was asked
    int calls = 0;

    UnattendedFetch unattended(Millis* wait) override {
        ++calls;
        ++asked;
        if (wait) *wait = wait_ms;
        return leave;
    }
    void fetch(const std::string& path, bool attended, Done done) override {
        ++calls;
        for (auto it = flying_.begin(); it != flying_.end();) {     // a request for another tune is cancelled
            it = it->first == path ? std::next(it) : flying_.erase(it);
        }
        if (flying_.count(path) == 0) requests.push_back({path, attended, false});
        flying_[path] = std::move(done);                              // one for this tune is taken over
    }
    void fetch_ahead(const std::string& path, Done done) override {
        ++calls;
        if (flying_.count(path) == 0) requests.push_back({path, false, true});
        flying_[path] = std::move(done);
    }
    void cancel() override {
        ++calls;
        ++cancels;
        flying_.clear();
    }

    bool in_flight(const std::string& path) const { return flying_.count(path) != 0; }
    std::size_t flying() const { return flying_.size(); }

    // The request for `path` is answered. False, and nobody is told, if it is no longer in flight: a
    // request that was cancelled never calls back.
    bool answer(const std::string& path, const FetchResult& result) {
        const auto found = flying_.find(path);
        if (found == flying_.end()) return false;
        Done done = std::move(found->second);
        flying_.erase(found);
        done(result);
        return true;
    }
    // The fetch succeeds: the file is put in its place, as the real fetcher does before it says so.
    bool deliver(const std::string& path) {
        const std::filesystem::path target = root + path;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(fixture_path("play/absent" + path), target,
                                   std::filesystem::copy_options::overwrite_existing);
        FetchResult result;
        result.fetched = true;
        return answer(path, result);
    }
    // The fetch fails with one of SPEC-069's messages.
    bool fail(const std::string& path, const std::string& message) {
        FetchResult result;
        result.message = message;
        return answer(path, result);
    }

private:
    std::map<std::string, Done> flying_;
};

// ---- A player against the fakes ---------------------------------------------------------------------------
struct PlayerFixture {
    TempFolder home;
    std::string root;                   // the collection root: a copy of tests/fixtures/play/C64Music
    fake::Clock clock;
    fake::Files files;
    fake::Executor worker, ui;
    fake::SoundBackend backend{&clock};
    SoundBackend* sound = &backend;     // what the player is given; a test of the whole sound path points it elsewhere
    FakeFetcher fetcher;
    Notices notices;
    Settings settings;
    Listing listing;
    PlayerOptions options;
    std::unique_ptr<Player> player;

    // With `with_listing`, the listing is built from the collection's own song length database.
    explicit PlayerFixture(bool with_listing = true) {
        root = (home.path / "C64Music").string();
        copy_fixture_tree("play/C64Music", root);
        fetcher.root = root;
        options.root = root;
        options.has_seed = true;
        options.seed = 1;
        if (with_listing) {
            std::filesystem::create_directories(home.path / "cache");
            const std::string database = root + "/DOCUMENTS/Songlengths.md5";
            const ListingBuild built = Listing::build(&files, database, home.file("cache/listing.bin"));
            REQUIRE(built.ok);
            REQUIRE(listing.open(&files, home.file("cache/listing.bin"), database));
        }
        make(with_listing);
    }

    // Makes the player anew, as at a start of the app.
    void make(bool with_listing = true) {
        PlayerSeams seams;
        seams.backend = sound;
        seams.clock = &clock;
        seams.files = &files;
        seams.worker = &worker;
        seams.ui = &ui;
        seams.fetcher = &fetcher;
        seams.listener = &notices;
        player.reset(new Player(seams, options, &settings));
        if (with_listing) player->set_listing(&listing);
    }

    // Runs the jobs of the worker and of the UI thread until neither has one left.
    void settle() {
        while (worker.run_one() || ui.run_one()) {
        }
    }

    PlayerStatus status() const { return player->status(); }
    PlayerState state() const { return player->status().state; }
    std::string current() const { return player->status().entry.path; }

    // Plays the entry at `chosen` of a queue made of these paths, and lets the load finish.
    void play(const std::vector<std::string>& paths, std::size_t chosen = 0) {
        player->play(entries(paths), chosen);
        settle();
    }
    static std::vector<Entry> entries(const std::vector<std::string>& paths) {
        std::vector<Entry> queue;
        for (const std::string& path : paths) queue.push_back(entry(path));
        return queue;
    }

    // Where the backend's log stands now, for calls_since().
    std::size_t mark() const { return backend.log.size(); }
    // The operations of SPEC-060's table that the backend was asked for since a mark: "load 2", "start",
    // "pause", "resume" and, unless `with_stop` is false, "stop". The other operations are left out.
    std::vector<std::string> calls_since(std::size_t from, bool with_stop = true) const {
        std::vector<std::string> found;
        for (std::size_t i = from; i < backend.log.size(); ++i) {
            const std::string& call = backend.log[i];
            if (call.compare(0, 5, "load ") == 0 || call == "start" || call == "pause" || call == "resume" ||
                (with_stop && call == "stop")) {
                found.push_back(call);
            }
        }
        return found;
    }
    int count_since(std::size_t from, const std::string& what) const {
        const std::vector<std::string> calls = calls_since(from);
        return static_cast<int>(std::count(calls.begin(), calls.end(), what));
    }

    // The playing subtune runs to its end: the clock moves on by what is left of the length the player
    // gave the backend, and the fake backend reports `song_ended`, which it does only when told.
    void run_to_end() {
        REQUIRE(state() == PlayerState::Playing);
        REQUIRE(backend.length > 0);
        const std::uint64_t at = backend.position();
        const std::uint64_t left = backend.length > at ? backend.length - at : 0;
        clock.advance(static_cast<std::int64_t>(left * 1000 / backend.loaded_settings.sample_rate));
        settle();
        backend.emit_song_ended();
        settle();
    }

    // Writes a file into the collection, at a collection path.
    void put(const std::string& path, const Bytes& data) { home.write("C64Music" + path, data); }
    Bytes tune(const std::string& path) const {
        std::vector<std::uint8_t> data;
        std::ifstream in(root + path, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return data;
    }
};

using Calls = std::vector<std::string>;

// The last `n` of a list of calls.
inline Calls tail(const Calls& calls, std::size_t n) {
    return calls.size() <= n ? calls : Calls(calls.end() - static_cast<std::ptrdiff_t>(n), calls.end());
}

}  // namespace spz::test

// So that a failed check shows what was found.
namespace doctest {
template <>
struct StringMaker<std::vector<std::string>> {
    static String convert(const std::vector<std::string>& value) {
        std::string text = "[";
        for (std::size_t i = 0; i < value.size(); ++i) text += (i ? ", " : "") + value[i];
        return (text + "]").c_str();
    }
};
template <>
struct StringMaker<std::vector<int>> {
    static String convert(const std::vector<int>& value) {
        std::string text = "[";
        for (std::size_t i = 0; i < value.size(); ++i) text += (i ? ", " : "") + std::to_string(value[i]);
        return (text + "]").c_str();
    }
};
template <>
struct StringMaker<std::vector<std::size_t>> {
    static String convert(const std::vector<std::size_t>& value) {
        std::string text = "[";
        for (std::size_t i = 0; i < value.size(); ++i) text += (i ? ", " : "") + std::to_string(value[i]);
        return (text + "]").c_str();
    }
};
template <>
struct StringMaker<std::vector<std::pair<std::string, int>>> {
    static String convert(const std::vector<std::pair<std::string, int>>& value) {
        std::string text = "[";
        for (std::size_t i = 0; i < value.size(); ++i) {
            text += (i ? ", " : "") + value[i].first + " #" + std::to_string(value[i].second);
        }
        return (text + "]").c_str();
    }
};
template <>
struct StringMaker<spz::PlayerState> {
    static String convert(spz::PlayerState value) { return spz::test::name(value); }
};
}  // namespace doctest
