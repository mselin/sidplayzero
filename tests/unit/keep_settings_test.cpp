// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-130: the settings file. SPEC-133: a settings file that cannot be read. SPEC-009: it is text that a
// person can write. A setting that belongs to a part of SPEC-115 is tested in a test of that part.
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/messages.h"
#include "spz/settings_file.h"
#include "support.h"

using spz::test::TempFolder;
using spz::test::bytes;
using spz::test::fixture;
using spz::test::has_line;
using spz::test::is_there;
using spz::test::names_in;
using spz::test::on_disk;
using spz::test::text_on_disk;

namespace {

// A settings file in a folder of the test's own, as the app has it in its configuration folder.
struct Kept {
    TempFolder folder;
    spz::fake::Files files;
    spz::fake::Executor quick;
    std::string path = folder.file("settings.ini");
};

// SPEC-130's table: every key with its default, as the file writes it.
const std::vector<std::pair<std::string, std::string>> kDefaults = {
    {"collection", "~/C64Music"},
    {"engine", "auto"},
    {"sample_rate", "48000"},
    {"when_song_ends", "subtunes"},
    {"repeat_all", "off"},
    {"shuffle", "off"},
    {"default_length", "3:30"},
    {"sid_model", "auto"},
    {"clock", "auto"},
    {"rom_kernal", ""},
    {"rom_basic", ""},
    {"rom_chargen", ""},
    {"resume_on_start", "off"},
    {"screen_off_after", "0"},
    {"url_files", "https://hvsc.c64.org/download/C64Music"},
    {"url_archive", "https://hvsc.brona.dk/HVSC/HVSC_{release}-all-of-them.7z"},
};

// Every setting but those named has its default.
void check_defaults(const spz::Settings& settings, const std::vector<std::string>& but = {}) {
    for (const auto& row : kDefaults) {
        if (std::find(but.begin(), but.end(), row.first) != but.end()) continue;
        CAPTURE(row.first);
        CHECK(spz::setting_text(settings, row.first) == row.second);
    }
}

// SPEC-130's oracle for one key: set to each value, saved, read by a new start, and found again.
void check_kept(const std::string& key, const std::vector<std::string>& values) {
    for (const std::string& value : values) {
        CAPTURE(key);
        CAPTURE(value);
        Kept kept;
        {
            spz::SettingsFile before(&kept.files, &kept.quick, kept.path);
            before.load();
            CHECK(before.set(key, value));
            CHECK(spz::setting_text(before.settings(), key) == value);
            kept.quick.run_all();
            CHECK(before.last_save() == spz::FileStatus::Ok);
        }
        spz::SettingsFile after(&kept.files, &kept.quick, kept.path);
        after.load();
        CHECK_FALSE(after.had_fault());
        CHECK(spz::setting_text(after.settings(), key) == value);
        check_defaults(after.settings(), {key});
    }
}

// A value the table does not allow is refused: by the setter, and, in a file, as a fault that leaves the
// setting at its default (SPEC-133).
void check_refused(const std::string& key, const std::vector<std::string>& values) {
    for (const std::string& value : values) {
        CAPTURE(key);
        CAPTURE(value);
        spz::Settings settings;
        CHECK_FALSE(spz::set_setting(&settings, key, value));
        check_defaults(settings);

        const spz::SettingsRead read = spz::read_settings(bytes(key + " = " + value + "\n"));
        CHECK(read.fault);
        CHECK(read.used);
        check_defaults(read.settings);
    }
}

// Comment lines, each a hundred bytes or fewer, until the text is exactly `size` bytes long.
std::string padded_to(std::string text, std::size_t size) {
    while (text.size() < size) {
        const std::size_t room = size - text.size();
        if (room == 1) {
            text += "\n";
            break;
        }
        const std::size_t line = room < 100 ? room : 100;
        text += "#" + std::string(line - 2, '.') + "\n";
    }
    return text;
}

// How many times something was put at a path: written there, or renamed there.
int saves_of(const spz::fake::Files& files, const std::string& path) {
    int saves = 0;
    for (const auto& operation : files.log) {
        if ((operation.what == "write" && operation.path == path) || (operation.what == "rename" && operation.to == path)) ++saves;
    }
    return saves;
}

}  // namespace

// ---- SPEC-130 ---------------------------------------------------------------------------------------

TEST_CASE("SPEC-130 the keys are those of the table, and with no file every setting has its default") {
    std::vector<std::string> keys;
    for (const auto& row : kDefaults) keys.push_back(row.first);
    CHECK(spz::setting_keys() == keys);

    check_defaults(spz::Settings());
    Kept kept;
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    check_defaults(file.settings());
    CHECK_FALSE(file.had_fault());
    CHECK(file.take_notice() == "");
    CHECK(names_in(kept.folder.path).empty());        // reading what is not there writes nothing
}

TEST_CASE("SPEC-130 each setting that is always built, set to each of its values and to the ends of its range, is read back after a reload") {
    check_kept("collection", {"/media/card/C64Music", "~/Music/SID tunes", "/media/card/M\xC3\xBCsik"});
    check_kept("engine", {"auto", "accurate", "light"});
    check_kept("when_song_ends", {"subtunes"});
    check_kept("default_length", {"0", "0:05", "3:30", "9:59", "10:00", "59:59"});
    check_kept("url_files", {"https://hvsc.c64.org/download/C64Music", "https://mirror.example.org/hvsc/C64Music"});

    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "engine", "accurate"));
    CHECK(settings.engine == spz::EngineChoice::Accurate);
    REQUIRE(spz::set_setting(&settings, "engine", "light"));
    CHECK(settings.engine == spz::EngineChoice::Light);
    REQUIRE(spz::set_setting(&settings, "default_length", "0:05"));
    CHECK(settings.default_length_s == 5);
    REQUIRE(spz::set_setting(&settings, "default_length", "59:59"));
    CHECK(settings.default_length_s == 3599);
    REQUIRE(spz::set_setting(&settings, "default_length", "0"));
    CHECK(settings.default_length_s == 0);
    REQUIRE(spz::set_setting(&settings, "collection", "/media/card/C64Music"));
    CHECK(settings.collection == "/media/card/C64Music");
    REQUIRE(spz::set_setting(&settings, "url_files", "https://mirror.example.org/hvsc/C64Music"));
    CHECK(settings.url_files == "https://mirror.example.org/hvsc/C64Music");
}

TEST_CASE("SPEC-130 SPEC-133 a value the table does not allow is refused for a setting that is always built") {
    check_refused("engine", {"fast", "reSIDfp", ""});
    check_refused("when_song_ends", {"loop", ""});
    check_refused("default_length", {"0:04", "60:00", "soon", "-1", ""});
    check_refused("url_files", {"http://hvsc.c64.org/download/C64Music", "hvsc.c64.org/download/C64Music", ""});

    spz::Settings settings;
    CHECK_FALSE(spz::set_setting(&settings, "no_such_setting", "1"));
    CHECK(spz::setting_text(settings, "no_such_setting") == "");
}

#if SPZ_PART_MODES
TEST_CASE("SPEC-130 when_song_ends and repeat_all take each of their values and keep them [parts: modes]") {
    check_kept("when_song_ends", {"subtunes", "next_tune", "repeat", "stop"});
    check_kept("repeat_all", {"on", "off"});
    check_refused("repeat_all", {"yes", "1", ""});

    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "when_song_ends", "next_tune"));
    CHECK(settings.when_song_ends == spz::WhenSongEnds::NextTune);
    REQUIRE(spz::set_setting(&settings, "when_song_ends", "repeat"));
    CHECK(settings.when_song_ends == spz::WhenSongEnds::Repeat);
    REQUIRE(spz::set_setting(&settings, "when_song_ends", "stop"));
    CHECK(settings.when_song_ends == spz::WhenSongEnds::Stop);
    REQUIRE(spz::set_setting(&settings, "repeat_all", "on"));
    CHECK(settings.repeat_all);
}
#endif

#if SPZ_PART_SHUFFLE
TEST_CASE("SPEC-130 shuffle takes each of its values and keeps it [parts: shuffle]") {
    check_kept("shuffle", {"on", "off"});
    check_refused("shuffle", {"random", ""});
    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "shuffle", "on"));
    CHECK(settings.shuffle);
}
#endif

#if SPZ_PART_RATE
TEST_CASE("SPEC-130 sample_rate takes each of its values and keeps it [parts: rate]") {
    check_kept("sample_rate", {"44100", "48000"});
    check_refused("sample_rate", {"22050", "96000", "fast", ""});
    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "sample_rate", "44100"));
    CHECK(settings.sample_rate == 44100);
}
#endif

#if SPZ_PART_FORCE
TEST_CASE("SPEC-130 sid_model and clock take each of their values and keep them [parts: force]") {
    check_kept("sid_model", {"auto", "6581", "8580"});
    check_kept("clock", {"auto", "pal", "ntsc"});
    check_refused("sid_model", {"6582", "new", ""});
    check_refused("clock", {"secam", "50", ""});

    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "sid_model", "6581"));
    CHECK(settings.sid_model == spz::SidModel::Mos6581);
    REQUIRE(spz::set_setting(&settings, "sid_model", "8580"));
    CHECK(settings.sid_model == spz::SidModel::Mos8580);
    REQUIRE(spz::set_setting(&settings, "sid_model", "auto"));
    CHECK(settings.sid_model == spz::SidModel::FromTune);
    REQUIRE(spz::set_setting(&settings, "clock", "pal"));
    CHECK(settings.clock == spz::C64Clock::Pal);
    REQUIRE(spz::set_setting(&settings, "clock", "ntsc"));
    CHECK(settings.clock == spz::C64Clock::Ntsc);
}
#endif

#if SPZ_PART_ROMS
TEST_CASE("SPEC-130 each ROM setting takes a path, or nothing, and keeps it [parts: roms]") {
    for (const char* key : {"rom_kernal", "rom_basic", "rom_chargen"}) {
        check_kept(key, {"/media/card/roms/a rom.bin", "~/roms/rom.bin", ""});
    }
    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "rom_kernal", "/roms/kernal.bin"));
    REQUIRE(spz::set_setting(&settings, "rom_basic", "/roms/basic.bin"));
    REQUIRE(spz::set_setting(&settings, "rom_chargen", "/roms/chargen.bin"));
    CHECK(settings.rom_kernal == "/roms/kernal.bin");
    CHECK(settings.rom_basic == "/roms/basic.bin");
    CHECK(settings.rom_chargen == "/roms/chargen.bin");
}
#endif

#if SPZ_PART_RESUME
TEST_CASE("SPEC-130 resume_on_start takes each of its values and keeps it [parts: resume]") {
    check_kept("resume_on_start", {"on", "off"});
    check_refused("resume_on_start", {"always", ""});
    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "resume_on_start", "on"));
    CHECK(settings.resume_on_start);
}
#endif

#if SPZ_PART_SCREEN_OFF
TEST_CASE("SPEC-130 screen_off_after takes 0 and 10 to 3600 seconds, and keeps them [parts: screen-off]") {
    check_kept("screen_off_after", {"0", "10", "11", "120", "3599", "3600"});
    check_refused("screen_off_after", {"9", "1", "3601", "-10", "never", ""});
    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "screen_off_after", "3600"));
    CHECK(settings.screen_off_after_s == 3600);
}
#endif

#if SPZ_PART_DOWNLOAD_ALL
TEST_CASE("SPEC-130 url_archive takes an https address that holds {release}, and keeps it [parts: download-all]") {
    check_kept("url_archive", {"https://hvsc.brona.dk/HVSC/HVSC_{release}-all-of-them.7z",
                               "https://mirror.example.org/{release}/hvsc.7z"});
    check_refused("url_archive", {"https://mirror.example.org/hvsc-85.7z",          // no {release}
                                  "http://mirror.example.org/HVSC_{release}.7z",    // not https
                                  ""});
    spz::Settings settings;
    REQUIRE(spz::set_setting(&settings, "url_archive", "https://mirror.example.org/{release}/hvsc.7z"));
    CHECK(settings.url_archive == "https://mirror.example.org/{release}/hvsc.7z");
}
#endif

TEST_CASE("SPEC-130 a file with only one key gives that setting and the defaults for the rest") {
    Kept kept;
    kept.folder.write("settings.ini", "default_length = 1:00\n");
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    CHECK(file.settings().default_length_s == 60);
    CHECK(spz::setting_text(file.settings(), "default_length") == "1:00");
    check_defaults(file.settings(), {"default_length"});
}

TEST_CASE("SPEC-130 a file with a comment and an unknown key keeps both when it is saved again") {
    Kept kept;
    kept.folder.write("settings.ini",
                      "# my own notes: the card is slow\n"
                      "future_key = something a newer version wrote\n"
                      "engine = accurate\n");
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    CHECK(file.settings().engine == spz::EngineChoice::Accurate);

    REQUIRE(file.set("default_length", "2:00"));
    kept.quick.run_all();
    std::string text = text_on_disk(kept.path);
    CHECK(has_line(text, "# my own notes: the card is slow"));
    CHECK(has_line(text, "future_key = something a newer version wrote"));
    CHECK(has_line(text, "engine = accurate"));
    CHECK(has_line(text, "default_length = 2:00"));

    REQUIRE(file.set("engine", "light"));     // and again, after a second save
    kept.quick.run_all();
    text = text_on_disk(kept.path);
    CHECK(has_line(text, "# my own notes: the card is slow"));
    CHECK(has_line(text, "future_key = something a newer version wrote"));
    CHECK(has_line(text, "engine = light"));
    CHECK_FALSE(has_line(text, "engine = accurate"));

    spz::SettingsFile again(&kept.files, &kept.quick, kept.path);
    again.load();
    CHECK_FALSE(again.had_fault());
    CHECK(again.settings().engine == spz::EngineChoice::Light);
    CHECK(again.settings().default_length_s == 120);
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"settings.ini"});   // and no copy was made
}

TEST_CASE("SPEC-130 SPEC-022 a change is in force at once, and its save is the quick worker's") {
    Kept kept;
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    const int calls = kept.files.calls;

    REQUIRE(file.set("engine", "light"));
    CHECK(file.settings().engine == spz::EngineChoice::Light);
    CHECK(kept.files.calls == calls);              // nothing touched the card on the caller's thread
    CHECK(kept.quick.waiting() >= 1);
    CHECK_FALSE(is_there(kept.path));

    kept.quick.run_all();
    CHECK(has_line(text_on_disk(kept.path), "engine = light"));
    CHECK(file.last_save() == spz::FileStatus::Ok);

    // A value that is not allowed changes nothing and saves nothing.
    const spz::Bytes before = on_disk(kept.path);
    const int saves = saves_of(kept.files, kept.path);
    CHECK_FALSE(file.set("engine", "fast"));
    kept.quick.run_all();
    CHECK(file.settings().engine == spz::EngineChoice::Light);
    CHECK(on_disk(kept.path) == before);
    CHECK(saves_of(kept.files, kept.path) == saves);
}

TEST_CASE("SPEC-130 SPEC-132 the settings file is written through the safe write, every time a setting changes") {
    Kept kept;
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    const std::string temp = kept.folder.file(".settings.ini.tmp-sidplayzero");
    int saves = 0;
    for (const char* value : {"light", "accurate", "auto"}) {
        REQUIRE(file.set("engine", value));
        kept.quick.run_all();
        ++saves;
        int renames = 0;
        for (const auto& operation : kept.files.log) {
            if (operation.what == "rename" && operation.path == temp && operation.to == kept.path) ++renames;
            CHECK_FALSE((operation.what == "write" && operation.path == kept.path));   // never in place
        }
        CHECK(renames == saves);
        CHECK(has_line(text_on_disk(kept.path), std::string("engine = ") + value));
    }
    CHECK(kept.files.count("sync") >= 2 * saves);      // the new file, and the folder, each time
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"settings.ini"});
}

TEST_CASE("SPEC-132 SPEC-130 with a failure at every operation of a settings save in turn, the file is the old or the new one and loads without a fault") {
    const std::string old_file = "# kept\nengine = accurate\ndefault_length = 1:00\n";
    int operations = 0;
    {
        Kept kept;
        kept.folder.write("settings.ini", old_file);
        spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
        file.load();
        const int calls = kept.files.calls;
        REQUIRE(file.set("engine", "light"));
        kept.quick.run_all();
        REQUIRE(file.last_save() == spz::FileStatus::Ok);
        operations = kept.files.calls - calls;
    }
    REQUIRE(operations >= 4);
    for (int n = 1; n <= operations; ++n) {
        CAPTURE(n);
        Kept kept;
        kept.folder.write("settings.ini", old_file);
        spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
        file.load();
        REQUIRE(file.set("engine", "light"));
        kept.files.fail_at(n);
        kept.quick.run_all();

        spz::fake::Files later;
        spz::fake::Executor later_quick;
        spz::SettingsFile after(&later, &later_quick, kept.path);
        after.load();
        CHECK_FALSE(after.had_fault());
        CHECK(after.settings().default_length_s == 60);
        const std::string engine = spz::setting_text(after.settings(), "engine");
        CHECK((engine == "accurate" || engine == "light"));
        if (file.last_save() == spz::FileStatus::Ok) CHECK(engine == "light");
        if (engine == "accurate") CHECK(text_on_disk(kept.path) == old_file);
        CHECK(has_line(text_on_disk(kept.path), "# kept"));
        CHECK_FALSE(is_there(kept.path + ".bad"));
    }
}

// ---- SPEC-009 ---------------------------------------------------------------------------------------

TEST_CASE("SPEC-130 SPEC-009 a settings file written by hand from the table loads, and what the app writes is UTF-8 text") {
    Kept kept;
    kept.folder.write("settings.ini", fixture("keep/settings_by_hand_must.ini"));
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    CHECK(file.settings().collection == "/media/card/My SID Music");
    CHECK(file.settings().engine == spz::EngineChoice::Light);
    CHECK(file.settings().default_length_s == 245);
    CHECK(file.settings().url_files == "https://mirror.example.org/hvsc/C64Music");
    check_defaults(file.settings(), {"collection", "engine", "default_length", "url_files"});

    REQUIRE(file.set("collection", "/media/card/M\xC3\xBCsik f\xC3\xBCr den C64"));
    kept.quick.run_all();
    const spz::Bytes written = on_disk(kept.path);
    REQUIRE_FALSE(written.empty());
    CHECK(spz::test::is_utf8(written));
    CHECK(std::find(written.begin(), written.end(), std::uint8_t{0}) == written.end());
    CHECK(has_line(text_on_disk(kept.path), "collection = /media/card/M\xC3\xBCsik f\xC3\xBCr den C64"));
    CHECK(has_line(text_on_disk(kept.path), "# SidPlayZero settings, typed in by hand"));
}

#if SPZ_PART_MODES && SPZ_PART_SHUFFLE && SPZ_PART_RATE && SPZ_PART_FORCE && SPZ_PART_ROMS && SPZ_PART_RESUME && SPZ_PART_SCREEN_OFF && SPZ_PART_DOWNLOAD_ALL
TEST_CASE("SPEC-130 SPEC-009 a hand-written file with every key of the table gives every setting its value [parts: modes shuffle rate force roms resume screen-off download-all]") {
    Kept kept;
    kept.folder.write("settings.ini", fixture("keep/settings_by_hand.ini"));
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    const std::map<std::string, std::string> expected = {
        {"collection", "/media/card/My SID Music"},
        {"engine", "light"},
        {"sample_rate", "44100"},
        {"when_song_ends", "next_tune"},
        {"repeat_all", "on"},
        {"shuffle", "on"},
        {"default_length", "4:05"},
        {"sid_model", "8580"},
        {"clock", "ntsc"},
        {"rom_kernal", "/media/card/roms/kernal.bin"},
        {"rom_basic", "/media/card/roms/basic.bin"},
        {"rom_chargen", "/media/card/roms/chargen.bin"},
        {"resume_on_start", "on"},
        {"screen_off_after", "120"},
        {"url_files", "https://mirror.example.org/hvsc/C64Music"},
        {"url_archive", "https://mirror.example.org/hvsc/HVSC_{release}-all-of-them.7z"},
    };
    REQUIRE(expected.size() == kDefaults.size());
    for (const auto& row : expected) {
        CAPTURE(row.first);
        CHECK(spz::setting_text(file.settings(), row.first) == row.second);
    }
    CHECK(file.settings().sample_rate == 44100);
    CHECK(file.settings().screen_off_after_s == 120);
    CHECK(file.settings().resume_on_start);
}
#endif

// ---- SPEC-133 ---------------------------------------------------------------------------------------

TEST_CASE("SPEC-133 a settings file of random bytes is not used at all: defaults, the notice once, and the bytes kept in settings.ini.bad") {
    Kept kept;
    const spz::Bytes noise = fixture("keep/settings_noise.ini");
    kept.folder.write("settings.ini", noise);
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();

    CHECK(file.had_fault());
    check_defaults(file.settings());
    CHECK(file.bad_copy() == kept.path + ".bad");
    CHECK(on_disk(kept.path + ".bad") == noise);
    CHECK(on_disk(kept.path) == noise);                 // the file itself is as it was: nothing is written yet
    CHECK(file.take_notice() == "MSG_SETTINGS_RESET");
    CHECK(file.take_notice() == "");                    // shown once
    CHECK(spz::is_message("MSG_SETTINGS_RESET"));

    const spz::SettingsRead read = spz::read_settings(noise);
    CHECK(read.fault);
    CHECK_FALSE(read.used);
}

TEST_CASE("SPEC-133 a file with only comments and an unknown key gives no notice and no copy") {
    Kept kept;
    kept.folder.write("settings.ini", fixture("keep/settings_comments.ini"));
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    CHECK(file.take_notice() == "");
    CHECK(file.bad_copy() == "");
    check_defaults(file.settings());
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"settings.ini"});
}

TEST_CASE("SPEC-133 an empty file gives the defaults and no notice") {
    Kept kept;
    kept.folder.write("settings.ini", fixture("keep/settings_empty.ini"));
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK_FALSE(file.had_fault());
    CHECK(file.take_notice() == "");
    check_defaults(file.settings());
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"settings.ini"});

    const spz::SettingsRead read = spz::read_settings(spz::Bytes());
    CHECK_FALSE(read.fault);
    CHECK(read.used);
    check_defaults(read.settings);
}

TEST_CASE("SPEC-133 started twice with a faulty file that is not saved in between, the second copy is settings.ini.bad.2") {
    Kept kept;
    const spz::Bytes noise = fixture("keep/settings_noise.ini");
    kept.folder.write("settings.ini", noise);
    {
        spz::SettingsFile first(&kept.files, &kept.quick, kept.path);
        first.load();
        CHECK(first.bad_copy() == kept.path + ".bad");
    }
    {
        spz::SettingsFile second(&kept.files, &kept.quick, kept.path);
        second.load();
        CHECK(second.had_fault());
        CHECK(second.bad_copy() == kept.path + ".bad.2");
        CHECK(second.take_notice() == "MSG_SETTINGS_RESET");
    }
    {
        spz::SettingsFile third(&kept.files, &kept.quick, kept.path);
        third.load();
        CHECK(third.bad_copy() == kept.path + ".bad.3");
    }
    CHECK(names_in(kept.folder.path) ==
          std::vector<std::string>{"settings.ini", "settings.ini.bad", "settings.ini.bad.2", "settings.ini.bad.3"});
    for (const char* name : {"settings.ini.bad", "settings.ini.bad.2", "settings.ini.bad.3"}) {
        CHECK(on_disk(kept.folder.file(name)) == noise);
    }
}

TEST_CASE("SPEC-133 the copy is made before anything is written, and the file the app then writes has no fault") {
    Kept kept;
    const std::string faulty = "engine = light\nthis line is not a setting\ndefault_length = 1:00\n";
    kept.folder.write("settings.ini", faulty);
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    REQUIRE(file.had_fault());
    CHECK(text_on_disk(kept.path + ".bad") == faulty);
    CHECK(saves_of(kept.files, kept.path) == 0);             // the settings file itself has not been saved
    CHECK(file.settings().engine == spz::EngineChoice::Light);   // what could be read is in force
    CHECK(file.settings().default_length_s == 60);

    REQUIRE(file.set("default_length", "2:00"));
    kept.quick.run_all();
    CHECK(text_on_disk(kept.path + ".bad") == faulty);       // the copy still holds the file as it was found
    CHECK(text_on_disk(kept.path) != faulty);

    spz::SettingsFile again(&kept.files, &kept.quick, kept.path);
    again.load();
    CHECK_FALSE(again.had_fault());                          // so no second notice, and no second copy
    CHECK(again.take_notice() == "");
    CHECK(again.settings().engine == spz::EngineChoice::Light);
    CHECK(again.settings().default_length_s == 120);
    CHECK(names_in(kept.folder.path) == std::vector<std::string>{"settings.ini", "settings.ini.bad"});
}

TEST_CASE("SPEC-133 a line that is neither empty, a comment nor key = value is a fault, and the rest of the file is used") {
    const spz::SettingsRead read = spz::read_settings(bytes("engine = light\n"
                                                            "\n"
                                                            "# a comment\n"
                                                            "this line is not a setting\n"
                                                            "default_length = 1:00\n"));
    CHECK(read.fault);
    CHECK(read.used);
    CHECK(read.settings.engine == spz::EngineChoice::Light);
    CHECK(read.settings.default_length_s == 60);
    check_defaults(read.settings, {"engine", "default_length"});

    const spz::SettingsRead clean = spz::read_settings(bytes("engine = light\n\n# a comment\ndefault_length = 1:00\n"));
    CHECK_FALSE(clean.fault);
    CHECK(clean.settings.engine == spz::EngineChoice::Light);
}

TEST_CASE("SPEC-133 a known key given twice is a fault, and the last one wins") {
    const spz::SettingsRead read = spz::read_settings(bytes("engine = accurate\ndefault_length = 1:00\nengine = light\n"));
    CHECK(read.fault);
    CHECK(read.used);
    CHECK(read.settings.engine == spz::EngineChoice::Light);
    CHECK(read.settings.default_length_s == 60);

    // An unknown key given twice is nobody's business: it is kept and otherwise ignored.
    const spz::SettingsRead unknown = spz::read_settings(bytes("future_key = 1\nfuture_key = 2\n"));
    CHECK_FALSE(unknown.fault);
}

TEST_CASE("SPEC-133 a value that is not allowed for its key is a fault: that setting keeps its default, the others are used") {
    const spz::SettingsRead read = spz::read_settings(bytes("engine = fast\ndefault_length = 1:00\n"));
    CHECK(read.fault);
    CHECK(read.used);
    CHECK(read.settings.engine == spz::EngineChoice::Auto);
    CHECK(read.settings.default_length_s == 60);
    check_defaults(read.settings, {"default_length"});

    Kept kept;
    kept.folder.write("settings.ini", "engine = fast\ndefault_length = 1:00\n");
    spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
    file.load();
    CHECK(file.had_fault());
    CHECK(file.take_notice() == "MSG_SETTINGS_RESET");
    CHECK(text_on_disk(kept.path + ".bad") == "engine = fast\ndefault_length = 1:00\n");
    CHECK(file.settings().engine == spz::EngineChoice::Auto);
    CHECK(file.settings().default_length_s == 60);
}

TEST_CASE("SPEC-133 a key the app does not know is kept and otherwise ignored") {
    const spz::SettingsRead read = spz::read_settings(bytes("future_key = 7\nengine = light\nnewer_setting = a value with spaces\n"));
    CHECK_FALSE(read.fault);
    CHECK(read.used);
    CHECK(read.settings.engine == spz::EngineChoice::Light);
    check_defaults(read.settings, {"engine"});
}

TEST_CASE("SPEC-133 a file that is not valid UTF-8 is not used at all, though most of its lines are good") {
    const spz::SettingsRead read = spz::read_settings(bytes("engine = light\ncollection = /media/\xFF\xFE\ndefault_length = 1:00\n"));
    CHECK(read.fault);
    CHECK_FALSE(read.used);
    check_defaults(read.settings);

    const spz::SettingsRead cut = spz::read_settings(bytes("engine = light\ncollection = /media/M\xC3"));   // cut inside a character
    CHECK(cut.fault);
    CHECK_FALSE(cut.used);
    check_defaults(cut.settings);

    const spz::SettingsRead good = spz::read_settings(bytes("engine = light\ncollection = /media/M\xC3\xBCsik\n"));
    CHECK_FALSE(good.fault);
    CHECK(good.settings.collection == "/media/M\xC3\xBCsik");
}

TEST_CASE("SPEC-133 SPEC-204 a file larger than 64 KiB is not used at all; one of exactly 64 KiB is") {
    const std::size_t limit = 64 * 1024;
    const std::string at_limit = padded_to("engine = light\ndefault_length = 1:00\n", limit);
    REQUIRE(at_limit.size() == limit);
    const std::string past_limit = at_limit + "\n";

    {
        Kept kept;
        kept.folder.write("settings.ini", at_limit);
        spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
        file.load();
        CHECK_FALSE(file.had_fault());
        CHECK(file.settings().engine == spz::EngineChoice::Light);
        CHECK(file.settings().default_length_s == 60);
    }
    {
        Kept kept;
        kept.folder.write("settings.ini", past_limit);
        spz::SettingsFile file(&kept.files, &kept.quick, kept.path);
        file.load();
        CHECK(file.had_fault());
        check_defaults(file.settings());
        CHECK(file.take_notice() == "MSG_SETTINGS_RESET");
        CHECK(text_on_disk(kept.path + ".bad") == past_limit);   // the whole of it, as it was found
    }
    const spz::SettingsRead read = spz::read_settings(bytes(past_limit));
    CHECK(read.fault);
    CHECK_FALSE(read.used);
    check_defaults(read.settings);
}
