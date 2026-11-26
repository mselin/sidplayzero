// SPDX-License-Identifier: GPL-3.0-or-later
// The composition's self-test (app/selftest.h): the key script of SPEC-260, which presses keys through
// App::key() while the benchmark plays, as the keyboard does, a round every 20 s, and times what SPEC-240
// asks for: a folder's first screenful, the detail line, a search, the rows a held Down moves.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/selftest.h"
#include "spz/keymap.h"

namespace spz::app {

void Composition::SelfTest::press(int code) {
    app()->key(code, true);
    app()->key(code, false);
}

void Composition::SelfTest::then_press(Key key, Millis gap) {
    then([this, key, gap](Millis) {
        press(key);
        return Outcome{true, gap};
    });
}

// Waits until `ready` holds, then gives how long it took to `took`. Gives up, and drops the rest of the
// round, after kWaitMostMs.
void Composition::SelfTest::then_wait(std::function<bool()> ready, std::function<void(Millis)> took) {
    auto since = std::make_shared<Millis>(-1);
    then([this, ready, took, since](Millis now) {
        if (*since < 0) *since = now;
        if (ready()) {
            if (took) took(now - *since);
            return Outcome{true, kKeyGapMs};
        }
        if (now - *since > kWaitMostMs) {
            script.clear();
            return Outcome{true, 0};
        }
        return Outcome{false, 0};
    });
}

bool Composition::SelfTest::listed(const std::string& folder) const {
    const BrowserModel* model = browser();
    return model && model->folder() == folder && model->note() != ListNote::Reading &&
           (model->count() > 0 || model->note() == ListNote::Empty);
}

int Composition::SelfTest::row_named(const std::string& name) const {
    const BrowserModel* model = browser();
    if (!model) return -1;
    for (int i = 0; i < model->count(); ++i) {
        if (model->row(i).name == name) return i;
    }
    return -1;
}

// Moves the highlight to the row named `name`, a key at a time; the round ends if there is none.
void Composition::SelfTest::then_highlight(const std::string& name, bool rest) {
    auto presses = std::make_shared<int>(0);
    then([this, name, presses, rest](Millis now) {
        const BrowserModel* model = browser();
        const int target = row_named(name);
        if (!model || target < 0 || ++*presses > 2000 || app()->top_name() != "browser") {
            script.clear();
            return Outcome{true, 0};
        }
        const int at = model->highlight();
        if (at == target) return Outcome{true, rest ? 0 : kKeyGapMs};
        moved_at = now;
        const int distance = target - at;
        if (distance > 6) press(Key::PageDown);
        else if (distance < -6) press(Key::PageUp);
        else press(distance > 0 ? Key::Down : Key::Up);
        return Outcome{false, kMoveGapMs};
    });
}

// Opens the folder `path` from the root, as the keys would: each part highlighted, then Enter. The last
// one's first screenful is timed (SPEC-240).
void Composition::SelfTest::then_open(const std::string& path, bool timed) {
    std::string folder;
    std::size_t start = 1;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        folder += "/" + name;
        const bool last = slash == std::string::npos;
        then_highlight(name);
        then_press(Key::Enter, 0);
        if (last && timed) {
            const std::string opened = folder;
            then_wait([this, opened] { return listed(opened); },
                      [this, opened](Millis ms) {
                          Millis& most = listed_before.count(opened) ? folder_again_max : folder_first_max;
                          most = std::max(most, ms);
                          listed_before.insert(opened);
                      });
        } else {
            const std::string opened = folder;
            then_wait([this, opened] { return listed(opened); });
        }
        if (last) break;
        start = slash + 1;
    }
}

void Composition::SelfTest::then_parent(const std::string& to) {
    then_press(Key::Left, 0);
    then_wait([this, to] { return listed(to); });
}

// SPEC-240: Down held for 10 s; the highlight's moves give the rows a second and the longest pause.
void Composition::SelfTest::then_hold() {
    auto hold = std::make_shared<Hold>();
    then([this, hold](Millis now) {
        const BrowserModel* model = browser();
        if (!model || app()->top_name() != "browser") {
            release_held();
            script.clear();
            return Outcome{true, 0};
        }
        if (hold->pressed < 0) {
            hold->pressed = now;
            hold->last_row = model->highlight();
            held_code = key_code(Key::Down);
            app()->key(held_code, true);
            return Outcome{false, 0};
        }
        const int row = model->highlight();
        if (row != hold->last_row && !hold->at_end) {
            hold->moves.push_back(now);
            hold->last_row = row;
            if (row >= model->count() - 1) hold->at_end = true;
        }
        if (now - hold->pressed < kHoldMs) return Outcome{false, 0};
        release_held();
        hold_figures(*hold.get(), now);
        return Outcome{true, kKeyGapMs};
    });
    then_press(Key::Home);
}

void Composition::SelfTest::hold_figures(const Hold& hold, Millis now) {
    // The moves made by the app's own repeat, after its first delay (SPEC-045).
    std::vector<Millis> repeated;
    for (const Millis at : hold.moves) {
        if (at >= hold.pressed + kKeyRepeatDelayMs) repeated.push_back(at);
    }
    const std::vector<Millis>& used = repeated.size() >= 2 ? repeated : hold.moves;
    double rate = 0;
    Millis pause = 0;
    if (used.size() >= 2) {
        rate = 1000.0 * static_cast<double>(used.size() - 1) / static_cast<double>(std::max<Millis>(1, used.back() - used.front()));
        for (std::size_t i = 1; i < used.size(); ++i) pause = std::max(pause, used[i] - used[i - 1]);
        if (!hold.at_end) pause = std::max(pause, now - used.back());
    }
    rows_per_second_min = rows_per_second_min < 0 ? rate : std::min(rows_per_second_min, rate);
    row_pause_max = std::max(row_pause_max, pause);
}

void Composition::SelfTest::release_held() {
    if (held_code == 0) return;
    app()->key(held_code, false);
    held_code = 0;
}

// SPEC-240: the detail line, from when the highlight comes to rest on a tune until it shows.
void Composition::SelfTest::then_rest_on(const std::string& name) {
    then([this](Millis now) {
        moved_at = now;
        return Outcome{true, 0};
    });
    then_highlight(name, true);
    if (!part_built("detail")) {
        then([](Millis) { return Outcome{true, 1000}; });
        return;
    }
    then_wait(
        [this] {
            const Json description = app()->describe();
            const Json* detail = description.get("detail");
            return detail && !detail->is_null() && !detail->scalar_text().empty();
        },
        [this](Millis) { detail_max = std::max(detail_max, script_now - moved_at); });
    then([](Millis) { return Outcome{true, 500}; });     // and rests a little longer, as a reader would
}

void Composition::SelfTest::then_rest_on_tunes() {
    then([this](Millis) {
        // Three tunes of the folder, present ones first (SPEC-260); in the order they are shown.
        const BrowserModel* model = browser();
        std::vector<std::string> chosen;
        if (model) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int i = 0; i < model->count() && chosen.size() < 3; ++i) {
                    const FolderEntry& row = model->row(i);
                    if (row.is_folder || row.present != (pass == 0)) continue;
                    chosen.push_back(row.name);
                }
            }
        }
        // These steps go before the rest of the round.
        std::deque<Action> rest;
        rest.swap(script);
        for (const std::string& name : chosen) then_rest_on(name);
        for (Action& action : rest) script.push_back(std::move(action));
        return Outcome{true, 0};
    });
}

void Composition::SelfTest::then_type(const std::string& text) {
    for (const char c : text) {
        then([this, c](Millis) {
            bool shift = false;
            const int code = code_for_character(static_cast<char32_t>(static_cast<unsigned char>(c)), &shift);
            if (shift) app()->key(key_code(Key::Shift), true);
            if (code != 0) press(code);
            if (shift) app()->key(key_code(Key::Shift), false);
            return Outcome{true, kKeyGapMs};
        });
    }
}

// SPEC-240: a search has been answered when its results, or "No results", are shown.
bool Composition::SelfTest::search_answered() {
    if (app()->top_name() != "search") return true;
    const Json description = app()->describe();
    const Json* list = description.get("list");
    if (list && !list->is_null()) return true;
    const Json* title = description.get("title");
    return title && title->scalar_text().find("result") != std::string::npos;
}

void Composition::SelfTest::then_query(const std::string& query, bool first) {
    then([this, query, first](Millis) {
        // Only while Search is open: in another place the letters would do other things.
        if (app()->top_name() != "search") return Outcome{true, 0};
        std::deque<Action> rest;
        rest.swap(script);
        if (!first) {
            // Back to the field, and what it holds taken away.
            then([this](Millis) {
                const Json description = app()->describe();
                const Json* place = description.get("place");
                if (place && place->scalar_text() == "search-results") press(key_code(Key::D4));
                return Outcome{true, kKeyGapMs};
            });
            for (std::size_t i = 0; i < std::string(kQueries[0]).size() + 2; ++i) then_press(Key::Backspace, kMoveGapMs);
        }
        then_type(query);
        then_press(Key::Enter, 0);
        then_wait([this] { return search_answered(); }, [this](Millis ms) { search_max = std::max(search_max, ms); });
        for (Action& action : rest) script.push_back(std::move(action));
        return Outcome{true, 0};
    });
}

// One round of the key script (SPEC-260).
void Composition::SelfTest::plan_round() {
    const bool hold = rounds % kHoldEvery == 0;
    ++rounds;
    // From the browser at the root.
    auto tries = std::make_shared<int>(0);
    then([this, tries](Millis) {
        if (app()->top_name() == "browser") return Outcome{true, 0};
        if (++*tries > 10) {
            script.clear();
            return Outcome{true, 0};
        }
        press(Key::Esc);
        return Outcome{false, kKeyGapMs};
    });
    then([this, tries](Millis) {
        const BrowserModel* model = browser();
        if (!model || model->folder() == "/") return Outcome{true, kKeyGapMs};
        if (++*tries > 100 || app()->top_name() != "browser") {
            script.clear();
            return Outcome{true, 0};
        }
        if (model->note() != ListNote::Reading) press(Key::Left);
        return Outcome{false, kKeyGapMs};
    });
    // The first folder: open it, hold Down in it, rest on three tunes, and leave it.
    then_open(kFirstFolder, true);
    if (hold) then_hold();
    then_rest_on_tunes();
    then_parent("/MUSICIANS/B");
    then_parent("/MUSICIANS");
    // The second, from the root.
    then_parent("/");
    then_open(kSecondFolder, true);
    then_parent("/MUSICIANS");
    then_parent("/");
    // Search: two queries, and back.
    then_press(Key::D5);
    then_query(kQueries[0], true);
    then_query(kQueries[1], false);
    then([this](Millis) {
        if (app()->top_name() == "search") press(Key::Esc);
        return Outcome{true, kKeyGapMs};
    });
    // Settings, opened and closed.
    then_press(Key::Tab, 400);
    then([this](Millis) {
        if (app()->top_name() == "settings") press(Key::Esc);
        return Outcome{true, kKeyGapMs};
    });
}

void Composition::SelfTest::script_tick(Millis now) {
    if (script.empty() && (round_began < 0 || now - round_began >= kRoundEveryMs) && app()->top_name() != "selftest") {
        round_began = now;
        plan_round();
    }
    script_now = now;
    while (!script.empty() && now >= script_next) {
        // Taken off first: an action may put steps of its own in front of the rest, or end the round.
        Action action = std::move(script.front());
        script.pop_front();
        const Outcome outcome = action(now);
        script_next = now + outcome.then;
        if (!outcome.done) {
            script.push_front(std::move(action));
            return;
        }
        if (outcome.then > 0) return;
    }
}

}  // namespace spz::app
