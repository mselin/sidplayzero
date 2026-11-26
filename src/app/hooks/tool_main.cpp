// SPDX-License-Identifier: GPL-3.0-or-later
// spz_tool: the integration tests' way into the `platform` layer, one command for each real part
// (the real HttpClient, the real engine, the real unpacker, the real volume, the device's input set-up).
//
//   spz_tool COMMAND [ARGUMENT ...]
//
// Each test that uses a command says, where it uses it, what the command takes and what it prints. Every
// command prints `key = value` lines. No command, or one not listed here, is a usage error (status 2).
//
//   spz_tool listing DATABASE [--path HVSC_PATH]                       tests/integration/test_hvsc.py, SPEC-142
//   spz_tool fetch URL TO [--limit BYTES] [--resume-from BYTE] [--ca FILE] [--release-rules]  test_hvsc.py, SPEC-150
//   spz_tool setup --root DIR --cache DIR --url-files URL [--ca FILE]                      test_hvsc.py, SPEC-140
//   spz_tool unpacker list ARCHIVE | unpacker unpack ARCHIVE INTO              test_hvsc.py, SPEC-007, SPEC-149
//   spz_tool complete-download ...                                                         test_hvsc.py, SPEC-144
//   spz_tool volume get | set N | toggle-mute                                              test_play.py, SPEC-076
//   spz_tool input-setup DEVICES                                                           test_keys.py, SPEC-110
//   spz_tool audio-guard clean | allocate | lock                                      test_structure.py, SPEC-022
//   spz_tool load-tunes [--cut] [--unchecked] FILE...                                 test_bad_tunes.py, SPEC-203
//   spz_tool kept-files                                                                    test_keep.py, SPEC-133
//
// This file has the tool's frame and its small commands; setup and complete-download are in tool_hvsc.cpp,
// audio-guard and load-tunes in tool_sound.cpp, and what they share is declared in tool.h.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/hooks/tool.h"
#include "app/workers.h"
#include "lvgl.h"
#include "platform/audio_guard.h"
#include "platform/curl_http.h"
#include "platform/display.h"
#include "platform/input_setup.h"
#include "platform/miniaudio_sink.h"
#include "platform/real_clock.h"
#include "platform/real_engine.h"
#include "platform/real_files.h"
#include "platform/software_sound.h"
#include "platform/system_seams.h"
#include "spz/complete_download.h"
#include "spz/folders.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/listing.h"
#include "spz/notice_gate.h"
#include "spz/path_text.h"
#include "spz/safe_file.h"
#include "spz/settings.h"
#include "spz/settings_file.h"
#include "spz/state_file.h"
#include "spz/tune.h"

namespace spz::tool {

int usage(const char* text) {
    std::fprintf(stderr, "usage: spz_tool %s\n", text);
    return 2;
}

void say(const std::string& key, const std::string& value) {
    std::printf("%s = %s\n", key.c_str(), value.c_str());
    std::fflush(stdout);
}

bool Arguments::flag(const std::string& name) const {
    for (const std::string& f : flags) {
        if (f == name) return true;
    }
    return false;
}

std::string Arguments::value(const std::string& name, const std::string& otherwise) const {
    const auto found = values.find(name);
    return found == values.end() ? otherwise : found->second;
}

bool parse(const std::vector<std::string>& args, const std::vector<std::string>& flag_names, Arguments* out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& word = args[i];
        if (word.compare(0, 2, "--") != 0) {
            out->words.push_back(word);
            continue;
        }
        bool is_flag = false;
        for (const std::string& name : flag_names) is_flag = is_flag || name == word;
        if (is_flag) {
            out->flags.push_back(word);
        } else if (i + 1 < args.size()) {
            out->values[word] = args[++i];
        } else {
            std::fprintf(stderr, "spz_tool: %s wants a value\n", word.c_str());
            return false;
        }
    }
    return true;
}

// --- what the jobs of `core` need around them ---------------------------------------------------------------

void PrintedNotices::raise(const std::string& id, const std::map<std::string, std::string>& about) {
    std::string words = id;
    for (const auto& [key, value] : about) words += " " + key + "=" + value;
    say("notice", words);
}

bool run_ui(app::UiQueue* ui, platform::RealClock* clock, const std::function<bool()>& done,
            const std::function<void()>& each, long seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until) {
        ui->run_all();
        if (clock) clock->run_due();
        if (each) each();
        if (done()) return true;
        if (ui->empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// --- listing (SPEC-142) -----------------------------------------------------------------------------------

// Builds the listing of a database into a folder of its own, opens it, and says what it holds.
int listing(const std::vector<std::string>& args) {
    if (args.empty()) return usage("listing DATABASE [--path HVSC_PATH]");
    std::string path;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--path" && i + 1 < args.size()) {
            path = args[++i];
        } else {
            std::fprintf(stderr, "spz_tool listing: what is %s?\n", args[i].c_str());
            return 2;
        }
    }
    char folder[] = "/tmp/spz-listing-XXXXXX";
    if (::mkdtemp(folder) == nullptr) {
        std::perror("spz_tool listing");
        return 1;
    }
    RealFiles files;
    const std::string file = std::string(folder) + "/listing.bin";
    const ListingBuild built = Listing::build(&files, args[0], file);
    Listing listing;
    const bool ok = built.ok && listing.open(&files, file, args[0]);
    std::printf("ok = %s\n", ok ? "yes" : "no");
    std::printf("tunes = %d\nfolders = %d\nlengths = %d\nskipped = %d\n", built.tunes, built.folders, built.lengths,
                built.lines_skipped);
    ListedTune tune;
    if (ok && !path.empty() && listing.by_path(path, &tune)) {
        std::string lengths;
        for (const Millis ms : tune.lengths) lengths += (lengths.empty() ? "" : " ") + std::to_string(ms);
        std::printf("lengths_ms = %s\n", lengths.c_str());
    }
    files.remove(file);
    files.remove(folder);
    return 0;
}

// --- fetch (SPEC-150) -------------------------------------------------------------------------------------

namespace {

const char* failure_name(HttpFailure failure) {
    switch (failure) {
        case HttpFailure::None: return "none";
        case HttpFailure::NotFound: return "not_found";
        case HttpFailure::Network: return "network";
        case HttpFailure::Tls: return "tls";
        case HttpFailure::TooLarge: return "too_large";
        case HttpFailure::CannotWrite: return "cannot_write";
        case HttpFailure::CardFull: return "card_full";
        case HttpFailure::Cancelled: return "cancelled";
    }
    return "network";
}

}  // namespace

platform::HttpRules rules_from(const Arguments& a) {
    platform::HttpRules rules;
    if (!a.flag("--release-rules")) {
        rules.loopback_http = true;
        rules.extra_authority = a.value("--ca");
    }
    return rules;
}

int fetch(const std::vector<std::string>& args) {
    Arguments a;
    if (!parse(args, {"--release-rules"}, &a) || a.words.size() != 2) {
        return usage("fetch URL TO [--limit BYTES] [--resume-from BYTE] [--ca FILE] [--release-rules]");
    }
    platform::CurlHttpClient http(rules_from(a));
    HttpRequest request;
    request.url = a.words[0];
    request.to_path = a.words[1];
    request.limit_bytes = std::strtoull(a.value("--limit", "0").c_str(), nullptr, 10);
    request.resume_from = std::strtoull(a.value("--resume-from", "0").c_str(), nullptr, 10);
    const HttpResult result = http.fetch(request, nullptr);
    say("failure", failure_name(result.failure));
    say("status", std::to_string(result.status));
    say("bytes", std::to_string(result.bytes));
    say("resumed", result.resumed ? "yes" : "no");
    return 0;
}

// --- unpacker (SPEC-007, SPEC-149) ------------------------------------------------------------------------

namespace {

const char* fault_name(ArchiveFault fault) {
    switch (fault) {
        case ArchiveFault::None: return "none";
        case ArchiveFault::AbsolutePath: return "absolute_path";
        case ArchiveFault::EmptyPart: return "empty_part";
        case ArchiveFault::DotPart: return "dot_part";
        case ArchiveFault::Backslash: return "backslash";
        case ArchiveFault::Link: return "link";
        case ArchiveFault::Special: return "special";
        case ArchiveFault::OutsideTopFolder: return "outside_top_folder";
        case ArchiveFault::TooManyEntries: return "too_many_entries";
        case ArchiveFault::TooLarge: return "too_large";
    }
    return "none";
}

}  // namespace

int unpacker(const std::vector<std::string>& args) {
    platform::SevenZipUnpacker unpacker;
    if (args.size() == 2 && args[0] == "list") {
        say("available", unpacker.available() ? "yes" : "no");
        std::vector<ArchiveEntry> entries;
        if (!unpacker.list(args[1], &entries)) {
            say("listed", "no");
            return 0;
        }
        say("listed", "yes");
        say("entries", std::to_string(entries.size()));
        for (const ArchiveEntry& entry : entries) {
            const char* kind = entry.is_link ? "link" : entry.is_special ? "special" : entry.is_folder ? "folder" : "file";
            say("entry", std::string(kind) + " " + std::to_string(entry.size) + " " + entry.path);
        }
        say("fault", fault_name(check_archive(entries)));
        return 0;
    }
    if (args.size() == 3 && args[0] == "unpack") {
        say("ok", unpacker.unpack(args[1], args[2], nullptr) ? "yes" : "no");
        return 0;
    }
    return usage("unpacker list ARCHIVE | unpacker unpack ARCHIVE INTO");
}

// --- volume (SPEC-076) ------------------------------------------------------------------------------------

int volume(const std::vector<std::string>& args) {
    platform::PactlVolume volume;
    if (args.size() == 1 && args[0] == "get") {
        int level = 0;
        bool muted = false;
        if (!volume.get(&level, &muted)) {
            say("ok", "no");
            return 0;
        }
        say("ok", "yes");
        say("level", std::to_string(level));
        say("muted", muted ? "yes" : "no");
        return 0;
    }
    if (args.size() == 2 && args[0] == "set") {
        say("ok", volume.set(std::atoi(args[1].c_str())) ? "yes" : "no");
        return 0;
    }
    if (args.size() == 1 && args[0] == "toggle-mute") {
        say("ok", volume.toggle_mute() ? "yes" : "no");
        return 0;
    }
    return usage("volume get | volume set N | volume toggle-mute");
}

// --- input-setup (SPEC-110, SPEC-111) ---------------------------------------------------------------------

namespace {

void read_no_key(lv_indev_t*, lv_indev_data_t* data) { data->state = LV_INDEV_STATE_RELEASED; }

}  // namespace

int input_setup(const std::vector<std::string>& args) {
    if (args.size() != 1) return usage("input-setup DEVICES");
    std::FILE* file = std::fopen(args[0].c_str(), "r");
    if (file == nullptr) {
        std::perror(args[0].c_str());
        return 1;
    }
    // One device to a line: its path, a tab, and the words of what it reports.
    std::vector<platform::InputDevice> devices;
    char line[4096];
    while (std::fgets(line, sizeof line, file)) {
        std::string text = line;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (text.empty()) continue;
        const std::size_t tab = text.find('\t');
        platform::InputDevice device;
        device.path = text.substr(0, tab);
        const std::string words = tab == std::string::npos ? "" : " " + text.substr(tab + 1) + " ";
        device.letters = words.find(" letters ") != std::string::npos;
        device.buttons = words.find(" buttons ") != std::string::npos;
        device.relative = words.find(" relative ") != std::string::npos;
        device.absolute = words.find(" absolute ") != std::string::npos;
        devices.push_back(device);
    }
    std::fclose(file);

    lv_init();
    std::string why;
    if (!platform::open_display(platform::DisplayKind::Memory, &why)) {
        std::fprintf(stderr, "spz_tool input-setup: %s\n", why.c_str());
        return 1;
    }
    const char* named = std::getenv("SIDPLAYZERO_KEYBOARD");
    const std::string keyboard = platform::choose_keyboard(devices, named ? named : "");
    platform::make_keypad(read_no_key, nullptr);
    int keypads = 0, pointers = 0, encoders = 0, buttons = 0;
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        switch (lv_indev_get_type(indev)) {
            case LV_INDEV_TYPE_KEYPAD: ++keypads; break;
            case LV_INDEV_TYPE_POINTER: ++pointers; break;
            case LV_INDEV_TYPE_ENCODER: ++encoders; break;
            case LV_INDEV_TYPE_BUTTON: ++buttons; break;
            default: break;
        }
    }
    say("keyboard", keyboard);
    say("keypads", std::to_string(keypads));
    say("pointers", std::to_string(pointers));
    say("encoders", std::to_string(encoders));
    say("buttons", std::to_string(buttons));
    say("grabbed", "no");   // the keyboard is read beside the launcher, never taken from it (SPEC-111)
    return 0;
}

// --- kept-files (SPEC-132, SPEC-133) ----------------------------------------------------------------------

namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

int count_temp_files(RealFiles* files, const std::string& folder, int depth = 0) {
    std::vector<FileInfo> entries;
    if (depth > 8 || files->list(folder, &entries) != FileStatus::Ok) return 0;
    int count = 0;
    for (const FileInfo& entry : entries) {
        if (entry.is_folder && !entry.is_link) {
            count += count_temp_files(files, folder + "/" + entry.name, depth + 1);
        } else if (is_temp_name(entry.name)) {
            ++count;
        }
    }
    return count;
}

}  // namespace

int kept_files(const std::vector<std::string>& args) {
    if (!args.empty()) return usage("kept-files");
    Environment env;
    env.home = environment("HOME");
    env.xdg_config_home = environment("XDG_CONFIG_HOME");
    env.xdg_data_home = environment("XDG_DATA_HOME");
    env.xdg_state_home = environment("XDG_STATE_HOME");
    env.xdg_cache_home = environment("XDG_CACHE_HOME");
    const Folders folders = app_folders(env);
    RealFiles files;
    const std::size_t limit = 64 * 1024 + 1;   // anything longer is a fault of SPEC-133 all the same

    std::vector<std::uint8_t> bytes;
    FileStatus status = files.read(settings_path(folders), limit, &bytes);
    if (status == FileStatus::NotFound) {
        say("settings", "missing");
    } else if (status != FileStatus::Ok) {
        say("settings", "fault");
    } else {
        const SettingsRead read = read_settings(bytes);
        say("settings", read.fault || !read.used ? "fault" : "ok");
    }
    bytes.clear();
    status = files.read(state_path(folders), limit, &bytes);
    if (status == FileStatus::NotFound) {
        say("state", "missing");
    } else if (status != FileStatus::Ok) {
        say("state", "fault");
    } else {
        const StateRead read = read_state(bytes);
        say("state", read.fault || !read.used ? "fault" : "ok");
    }
    // The app's four folders (SPEC-134): the playlists' folder is in the data folder.
    int temp = 0;
    for (const std::string& folder : {folders.config, path_text::folder_of(folders.playlists), folders.state, folders.cache}) {
        temp += count_temp_files(&files, folder);
    }
    say("temp_files", std::to_string(temp));
    return 0;
}

}  // namespace spz::tool

namespace {

using namespace spz::tool;

// The command named, or -1 if there is no such command.
int dispatch(const std::string& command, const std::vector<std::string>& args) {
    if (command == "listing") return listing(args);
    if (command == "fetch") return fetch(args);
    if (command == "setup") return setup(args);
    if (command == "unpacker") return unpacker(args);
    if (command == "complete-download") return complete_download(args);
    if (command == "volume") return volume(args);
    if (command == "input-setup") return input_setup(args);
    if (command == "audio-guard") return audio_guard(args);
    if (command == "load-tunes") return load_tunes(args);
    if (command == "kept-files") return kept_files(args);
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage("COMMAND [ARGUMENT ...]");
    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    const int status = dispatch(command, args);
    std::fflush(stdout);
    return status < 0 ? usage(("COMMAND [ARGUMENT ...]: there is no command " + command).c_str()) : status;
}
