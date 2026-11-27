// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-131: the state file: what is remembered, when it is written, and that it comes back. SPEC-133: a
// state file that cannot be read. SPEC-154: a path that comes in from the state file is checked.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/state_file.h"
#include "support.h"

using spz::test::TempFolder;
using spz::test::bytes;
using spz::test::has_line;
using spz::test::is_there;
using spz::test::names_in;
using spz::test::on_disk;
using spz::test::text_on_disk;

namespace {

// A state file in a folder of the test's own, with the clock and the quick worker it is given.
struct Kept {
    TempFolder folder;
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::fake::Executor quick;
    std::string path = folder.file("state.ini");

    // Time moves on, and then the quick worker does whatever it was handed.
    void pass(std::int64_t ms) {
        clock.advance(ms);
        quick.run_all();
    }
    // How many times the state file was put in place.
    int saves() const {
        int count = 0;
        for (const auto& operation : files.log) {
            if (operation.what == "rename" && operation.to == path) ++count;
        }
        return count;
    }
    // What a new start would read from the card.
    spz::State read_back() const {
        spz::fake::Files files_then;
        spz::fake::Clock clock_then;
        spz::fake::Executor quick_then;
        spz::StateFile then(&files_then, &clock_then, &quick_then, path);
        then.load();
        return then.state();
    }
};

// Three levels down in the fixture tree, with a tune playing from that folder.
spz::State three_levels_down() {
    spz::State state;
    state.browser_folder = "/MUSICIANS/T/Tester_Tom";
    state.browser_entry = "New_Chip.sid";
    state.current_path = "/MUSICIANS/T/Tester_Tom/Three_Songs.sid";
    state.current_subtune = 3;
    state.queue_kind = "folder";
    state.queue_source = "/MUSICIANS/T/Tester_Tom";
    state.queue_place = 6;
    state.notice_accepted = true;
    state.backlight_saved = 70;
    return state;
}

void remember(spz::StateFile* file, const spz::State& state) {
    file->set_browser(state.browser_folder, state.browser_entry);
    file->set_current(state.current_path, state.current_subtune);
    file->set_queue(state.queue_kind, state.queue_source, state.queue_place);
    file->set_notice_accepted(state.notice_accepted);
    file->set_backlight_saved(state.backlight_saved);
}

}  // namespace

TEST_CASE("SPEC-131 with no state file nothing is remembered, and reading it makes nothing") {
    Kept kept;
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    const spz::State state = file.state();
    CHECK(state.browser_folder == "/");
    CHECK(state.browser_entry == "");
    CHECK(state.current_path == "");
    CHECK(state.current_subtune == 0);
    CHECK(state.queue_kind == "");
    CHECK(state.queue_source == "");
    CHECK(state.queue_place == 0);
    CHECK_FALSE(state.notice_accepted);
    CHECK(state.backlight_saved == -1);
    CHECK_FALSE(file.had_fault());
    CHECK_FALSE(file.unsaved());
    kept.pass(10000);
    CHECK(names_in(kept.folder.path).empty());
    CHECK(kept.files.count("write") == 0);
}

TEST_CASE("SPEC-131 stopping three levels down and starting again gives the same folder, highlight, current entry and queue") {
    Kept kept;
    const spz::State wanted = three_levels_down();
    {
        spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
        file.load();
        remember(&file, wanted);
        CHECK(file.state() == wanted);
        CHECK(file.unsaved());
        CHECK(file.save_now() == spz::FileStatus::Ok);     // the exit (SPEC-201)
        CHECK_FALSE(file.unsaved());
    }
    // The file holds each thing under its key of the table.
    const std::string text = text_on_disk(kept.path);
    CHECK(has_line(text, "browser_folder = /MUSICIANS/T/Tester_Tom"));
    CHECK(has_line(text, "browser_entry = New_Chip.sid"));
    CHECK(has_line(text, "current_path = /MUSICIANS/T/Tester_Tom/Three_Songs.sid"));
    CHECK(has_line(text, "current_subtune = 3"));
    CHECK(has_line(text, "queue_kind = folder"));
    CHECK(has_line(text, "queue_source = /MUSICIANS/T/Tester_Tom"));
    CHECK(has_line(text, "queue_place = 6"));
    CHECK(has_line(text, "notice_accepted = on"));
    CHECK(has_line(text, "backlight_saved = 70"));
    CHECK(spz::test::is_utf8(on_disk(kept.path)));

    const spz::State restored = kept.read_back();
    CHECK(restored == wanted);
    CHECK(restored.browser_folder == "/MUSICIANS/T/Tester_Tom");
    CHECK(restored.browser_entry == "New_Chip.sid");
    CHECK(restored.current_path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(restored.current_subtune == 3);
}

TEST_CASE("SPEC-131 the other kinds of queue, a query and a name with spaces, and a tune outside the collection come back as they were") {
    for (const char* kind : {"search", "playlist"}) {
        CAPTURE(kind);
        Kept kept;
        spz::State wanted;
        wanted.browser_folder = "/";
        wanted.browser_entry = "MUSICIANS";
        wanted.current_path = "file:///media/card/Other tunes/S\xC3\xB6ng.sid";
        wanted.current_subtune = 0;
        wanted.queue_kind = kind;
        wanted.queue_source = std::string(kind) == "search" ? "hub 1985" : "Loading t\xC3\xBCnes";
        wanted.queue_place = 2;
        spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
        file.load();
        remember(&file, wanted);
        REQUIRE(file.save_now() == spz::FileStatus::Ok);
        CHECK(kept.read_back() == wanted);
        CHECK(spz::test::is_utf8(on_disk(kept.path)));
    }
}

TEST_CASE("SPEC-131 a highlight moved once every second for 20 s is saved at least every two seconds") {
    Kept kept;
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    std::vector<std::string> names;
    for (int second = 0; second <= 20; ++second) names.push_back("Tune_" + std::to_string(second) + ".sid");

    for (int second = 0; second <= 20; ++second) {
        CAPTURE(second);
        if (second > 0) kept.pass(1000);
        file.set_browser("/MUSICIANS/T/Tester_Tom", names[static_cast<std::size_t>(second)]);
        kept.quick.run_all();
        if (second < 2) continue;
        // What the card holds now was the highlight at some moment of the last two seconds.
        const std::string on_card = kept.read_back().browser_entry;
        const bool recent = on_card == names[static_cast<std::size_t>(second)] ||
                            on_card == names[static_cast<std::size_t>(second - 1)] ||
                            on_card == names[static_cast<std::size_t>(second - 2)];
        CHECK_MESSAGE(recent, "the card holds '" << on_card << "'");
        CHECK(kept.read_back().browser_folder == "/MUSICIANS/T/Tester_Tom");
    }
    CHECK(kept.saves() >= 10);
    CHECK(kept.saves() <= 21);      // and never more than once for a move
}

TEST_CASE("SPEC-131 the file is written two seconds after the first change not yet saved, and not before") {
    Kept kept;
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    CHECK(spz::kStateSaveAfterMs == 2000);

    file.set_browser("/MUSICIANS", "H");                  // the first change, at 0 ms
    kept.pass(1500);
    file.set_browser("/MUSICIANS", "T");                  // a later change does not put the save off
    kept.pass(499);
    CHECK(kept.saves() == 0);
    CHECK_FALSE(is_there(kept.path));
    CHECK(file.unsaved());
    kept.pass(1);                                         // 2,000 ms after the first change
    CHECK(kept.saves() == 1);
    CHECK(kept.read_back().browser_entry == "T");         // what is remembered then, not what was at first
    CHECK_FALSE(file.unsaved());

    kept.pass(5000);                                      // nothing has changed: nothing is written
    CHECK(kept.saves() == 1);

    file.set_browser("/MUSICIANS", "T");                  // a call that changes nothing counts for nothing
    CHECK_FALSE(file.unsaved());
    kept.pass(5000);
    CHECK(kept.saves() == 1);

    file.set_current("/MUSICIANS/H/Hubbard_Rob/Commando.sid", 1);    // any of the keys starts the two seconds
    kept.pass(1999);
    CHECK(kept.saves() == 1);
    kept.pass(1);
    CHECK(kept.saves() == 2);
    CHECK(kept.read_back().current_path == "/MUSICIANS/H/Hubbard_Rob/Commando.sid");

    file.set_notice_accepted(true);
    kept.pass(2000);
    CHECK(kept.saves() == 3);
    CHECK(kept.read_back().notice_accepted);

    file.set_queue("folder", "/MUSICIANS/H/Hubbard_Rob", 1);
    kept.pass(2000);
    CHECK(kept.saves() == 4);
    CHECK(kept.read_back().queue_kind == "folder");
}

TEST_CASE("SPEC-131 SPEC-022 the save after two seconds is handed to the quick worker, not done where the timer fires") {
    Kept kept;
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    file.set_browser("/MUSICIANS", "H");
    const int calls = kept.files.calls;
    kept.clock.advance(2000);                  // the timer fires on the UI thread
    CHECK(kept.files.calls == calls);          // which touches no file
    CHECK(kept.quick.waiting() >= 1);
    CHECK_FALSE(is_there(kept.path));
    kept.quick.run_all();
    CHECK(kept.read_back().browser_entry == "H");
}

TEST_CASE("SPEC-131 SPEC-201 at exit the file is written at once with what is remembered then") {
    Kept kept;
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    file.set_browser("/MUSICIANS", "H");
    kept.pass(2000);
    file.set_browser("/MUSICIANS", "T");
    file.set_current("/MUSICIANS/H/Hubbard_Rob/Commando.sid", 0);
    kept.pass(500);                            // half a second later the app is told to leave
    REQUIRE(kept.read_back().browser_entry == "H");
    CHECK(file.save_now() == spz::FileStatus::Ok);
    CHECK(kept.read_back().browser_entry == "T");
    CHECK(kept.read_back().current_path == "/MUSICIANS/H/Hubbard_Rob/Commando.sid");
    CHECK(file.last_save() == spz::FileStatus::Ok);
}

TEST_CASE("SPEC-131 SPEC-132 the state file is written through the safe write") {
    Kept kept;
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    file.set_browser("/MUSICIANS", "H");
    kept.pass(2000);
    file.set_browser("/MUSICIANS", "T");
    REQUIRE(file.save_now() == spz::FileStatus::Ok);

    const std::string temp = kept.folder.file(".state.ini.tmp-sidplayzero");
    int renames = 0;
    for (const auto& operation : kept.files.log) {
        if (operation.what == "rename" && operation.path == temp && operation.to == kept.path) ++renames;
        CHECK_FALSE((operation.what == "write" && operation.path == kept.path));   // never in place
    }
    CHECK(renames == 2);
    CHECK(kept.files.count("sync") >= 4);      // the new file and the folder, each time
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"state.ini"});
}

TEST_CASE("SPEC-132 SPEC-131 with a failure at every operation of a state save in turn, the file is the old or the new one, whole") {
    const spz::State old_state = three_levels_down();
    spz::State new_state = old_state;
    new_state.browser_folder = "/DEMOS/A-F";
    new_state.browser_entry = "Clock_Demo.sid";

    const auto prepare = [&](Kept* kept) {
        spz::StateFile first(&kept->files, &kept->clock, &kept->quick, kept->path);
        first.load();
        remember(&first, old_state);
        REQUIRE(first.save_now() == spz::FileStatus::Ok);
    };
    int operations = 0;
    {
        Kept kept;
        prepare(&kept);
        spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
        file.load();
        const int calls = kept.files.calls;
        file.set_browser(new_state.browser_folder, new_state.browser_entry);
        REQUIRE(file.save_now() == spz::FileStatus::Ok);
        operations = kept.files.calls - calls;
    }
    REQUIRE(operations >= 4);
    for (int n = 1; n <= operations; ++n) {
        CAPTURE(n);
        Kept kept;
        prepare(&kept);
        const spz::Bytes old_bytes = on_disk(kept.path);
        spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
        file.load();
        file.set_browser(new_state.browser_folder, new_state.browser_entry);
        kept.files.fail_at(n);
        const spz::FileStatus result = file.save_now();

        const spz::State now = kept.read_back();
        CHECK((now == old_state || now == new_state));
        if (result == spz::FileStatus::Ok) CHECK(now == new_state);
        if (now == old_state) CHECK(on_disk(kept.path) == old_bytes);
    }
}

// ---- SPEC-133: a state file that cannot be read -------------------------------------------------------

TEST_CASE("SPEC-133 a state file of random bytes is not used, and there is no copy of it and no notice") {
    Kept kept;
    const spz::Bytes noise = spz::test::fixture("keep/settings_noise.ini");
    kept.folder.write("state.ini", noise);
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    CHECK(file.had_fault());
    CHECK(file.state() == spz::State());
    kept.pass(5000);
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"state.ini"});   // no state.ini.bad
    CHECK(on_disk(kept.path) == noise);                                           // and nothing was written

    const spz::StateRead read = spz::read_state(noise);
    CHECK(read.fault);
    CHECK_FALSE(read.used);
    CHECK(read.state == spz::State());
}

TEST_CASE("SPEC-133 in a state file a faulty line or value costs only its own key, and an unknown key is kept") {
    const std::string text =
        "# written by a newer version\n"
        "browser_folder = /MUSICIANS/H\n"
        "this line is not a key and a value\n"
        "browser_entry = Hubbard_Rob\n"
        "current_subtune = many\n"
        "queue_kind = shelf\n"
        "notice_accepted = perhaps\n"
        "backlight_saved = bright\n"
        "future_key = kept for the version that wrote it\n";
    const spz::StateRead read = spz::read_state(bytes(text));
    CHECK(read.fault);
    CHECK(read.used);
    CHECK(read.state.browser_folder == "/MUSICIANS/H");
    CHECK(read.state.browser_entry == "Hubbard_Rob");
    CHECK(read.state.current_subtune == 0);
    CHECK(read.state.queue_kind == "");
    CHECK_FALSE(read.state.notice_accepted);
    CHECK(read.state.backlight_saved == -1);

    const spz::StateRead unknown_only = spz::read_state(bytes("# a comment\nfuture_key = 1\n\nnotice_accepted = on\n"));
    CHECK_FALSE(unknown_only.fault);
    CHECK(unknown_only.state.notice_accepted);

    const spz::StateRead twice = spz::read_state(bytes("browser_entry = H\nbrowser_entry = T\n"));
    CHECK(twice.fault);
    CHECK(twice.state.browser_entry == "T");          // the last wins

    const spz::StateRead empty = spz::read_state(spz::Bytes());
    CHECK_FALSE(empty.fault);
    CHECK(empty.used);
    CHECK(empty.state == spz::State());

    // Saved again, the unknown key and the comment are still there.
    Kept kept;
    kept.folder.write("state.ini", text);
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    CHECK(file.had_fault());
    file.set_browser("/MUSICIANS/H", "Huelsbeck_Chris");
    REQUIRE(file.save_now() == spz::FileStatus::Ok);
    const std::string saved = text_on_disk(kept.path);
    CHECK(has_line(saved, "future_key = kept for the version that wrote it"));
    CHECK(has_line(saved, "# written by a newer version"));
    CHECK(has_line(saved, "browser_entry = Huelsbeck_Chris"));
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"state.ini"});
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::fake::Executor quick;
    spz::StateFile again(&files, &clock, &quick, kept.path);
    again.load();
    CHECK_FALSE(again.had_fault());                    // what the app writes has no fault
    CHECK(again.state().browser_entry == "Huelsbeck_Chris");
}

TEST_CASE("SPEC-133 SPEC-204 a state file larger than 64 KiB, or not valid UTF-8, is not used at all") {
    std::string large = "browser_folder = /MUSICIANS/H\nbrowser_entry = Hubbard_Rob\n";
    while (large.size() <= 64 * 1024) large += "# " + std::string(98, '.') + "\n";
    const spz::StateRead too_large = spz::read_state(bytes(large));
    CHECK(too_large.fault);
    CHECK_FALSE(too_large.used);
    CHECK(too_large.state == spz::State());

    const spz::StateRead not_text = spz::read_state(bytes("browser_folder = /MUSICIANS/H\nbrowser_entry = \xFF\xFE\n"));
    CHECK(not_text.fault);
    CHECK_FALSE(not_text.used);
    CHECK(not_text.state == spz::State());

    Kept kept;
    kept.folder.write("state.ini", large);
    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    CHECK(file.had_fault());
    CHECK(file.state() == spz::State());
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"state.ini"});
}

// ---- SPEC-154: the path that comes in from the state file ------------------------------------------------

TEST_CASE("SPEC-154 SPEC-131 a saved path that is not a collection path is dropped by the state reader; one with a space or an accent is kept") {
    const std::vector<std::string> refused = {
        "/MUSICIANS/../../etc/Commando.sid",          // a ".." part
        "/MUSICIANS//Commando.sid",                   // a doubled slash
        "/MUSICIANS\\Commando.sid",                   // a backslash
        std::string("/MUSICIANS/Com\x01mando.sid"),   // a control character
        "/MUSICIANS/H/Hubbard_Rob/Commando.txt",      // no .sid
        "/MUSICIANS/./Commando.sid",                  // a "." part
        "MUSICIANS/H/Commando.sid",                   // does not start with "/"
    };
    for (const std::string& path : refused) {
        CAPTURE(path);
        const spz::StateRead read = spz::read_state(bytes("current_path = " + path + "\ncurrent_subtune = 2\n"));
        CHECK(read.state.current_path == "");
    }
    for (const char* path : {"/My Rips/Cool S\xC3\xB6ng.sid", "/MUSICIANS/H/Hubbard_Rob/Commando.sid",
                             "/DEMOS/A-F/Devils_ReSIDence_3SID.SID"}) {
        CAPTURE(path);
        const spz::StateRead read = spz::read_state(bytes(std::string("current_path = ") + path + "\ncurrent_subtune = 2\n"));
        CHECK(read.state.current_path == path);
        CHECK(read.state.current_subtune == 2);
    }
}

TEST_CASE("SPEC-131 hvsc_complete_release is written once a release is recorded as complete, comes back, and is gone again when it is removed (FIND-031)") {
    Kept kept;
    {
        spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
        file.load();
        CHECK(file.state().hvsc_complete_release == 0);       // absent: nothing is held complete
        file.set_notice_accepted(true);
        REQUIRE(file.save_now() == spz::FileStatus::Ok);
        CHECK(text_on_disk(kept.path).find("hvsc_complete_release") == std::string::npos);

        file.set_hvsc_complete_release(84);
        CHECK(file.unsaved());
        kept.pass(2000);                                      // saved as every other key is
        CHECK_FALSE(file.unsaved());
        CHECK(has_line(text_on_disk(kept.path), "hvsc_complete_release = 84"));
    }
    CHECK(kept.read_back().hvsc_complete_release == 84);

    spz::StateFile file(&kept.files, &kept.clock, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    CHECK(file.state().hvsc_complete_release == 84);
    file.set_hvsc_complete_release(84);                       // no change counts for nothing
    CHECK_FALSE(file.unsaved());
    file.set_hvsc_complete_release(0);                        // removed
    REQUIRE(file.save_now() == spz::FileStatus::Ok);
    const std::string text = text_on_disk(kept.path);
    CHECK(text.find("hvsc_complete_release") == std::string::npos);
    CHECK(has_line(text, "notice_accepted = on"));
    CHECK(kept.read_back().hvsc_complete_release == 0);
}

TEST_CASE("SPEC-133 SPEC-131 an hvsc_complete_release that is not a release number is a fault that costs that key alone") {
    for (const char* value : {"many", "0", "-84", "84.5", "1234567"}) {
        CAPTURE(value);
        const spz::StateRead read =
            spz::read_state(bytes(std::string("notice_accepted = on\nhvsc_complete_release = ") + value + "\n"));
        CHECK(read.fault);
        CHECK(read.used);
        CHECK(read.state.hvsc_complete_release == 0);
        CHECK(read.state.notice_accepted);
    }
    const spz::StateRead good = spz::read_state(bytes("hvsc_complete_release = 85\n"));
    CHECK_FALSE(good.fault);
    CHECK(good.state.hvsc_complete_release == 85);
}
