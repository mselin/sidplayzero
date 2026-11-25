// SPDX-License-Identifier: GPL-3.0-or-later
// The script language of the simulator's tests (SPEC-253, TF-042). One Session runs one script against
// the app: it presses keys, moves the clock, waits for the app to settle, and compares the screen
// description (SPEC-254) with what the script expects.
#include "app/hooks/script.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include "app/hooks/sim_fakes.h"
#include "app/stub_app.h"
#include "lvgl.h"
#include "platform/display.h"
#include "platform/real_clock.h"
#include "spz/keys.h"
#include "spz/version.h"
#include "ui/png.h"

namespace spz::hooks {

namespace {

namespace fs = std::filesystem;

const int kStatusExpectFailed = 1;
const int kStatusScriptError = 2;
const std::int64_t kStepMs = 10;                         // `advance` moves the fake clock in steps of this
const std::chrono::seconds kSettleLimit(10);

std::vector<std::function<bool()>>& idle_checks() {
    static std::vector<std::function<bool()>> checks;
    return checks;
}

Clock* g_clock = nullptr;
std::uint32_t tick() { return static_cast<std::uint32_t>(g_clock->now_ms()); }

// SIGTERM and SIGINT leave, as they do without a script (SPEC-201): the script ends there, with status 0.
volatile std::sig_atomic_t g_signals = 0;
void on_signal(int) { g_signals = g_signals + 1; }

// How a script stops early: with the status the program ends with, and what to say.
struct Stop {
    int status;
    std::string message;
    std::string expected, found;
};

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// Takes the first word off `rest` and returns it.
std::string word(std::string* rest) {
    *rest = trim(*rest);
    const std::size_t end = rest->find_first_of(" \t");
    std::string first = rest->substr(0, end);
    *rest = end == std::string::npos ? "" : trim(rest->substr(end));
    return first;
}

std::string unquote(const std::string& text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') return text.substr(1, text.size() - 2);
    return text;
}

long long number(const std::string& text, const std::string& what) {
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used);
        if (used == text.size()) return value;
    } catch (const std::exception&) {
    }
    throw Stop{kStatusScriptError, what + " must be a number, not '" + text + "'", "", ""};
}

bool compare(const std::string& found, const std::string& is, const std::string& value) {
    if (is == "=") return found == value;
    if (is == "!=") return found != value;
    if (is == "contains") return found.find(value) != std::string::npos;
    if (is == "matches") {
        try {
            return std::regex_search(found, std::regex(value));
        } catch (const std::regex_error&) {
            throw Stop{kStatusScriptError, "'" + value + "' is not a regular expression", "", ""};
        }
    }
    if (is == "<" || is == ">") {
        const long long a = number(found, "the value found"), b = number(value, "the value expected");
        return is == "<" ? a < b : a > b;
    }
    throw Stop{kStatusScriptError, "'" + is + "' is not one of =, !=, contains, matches, <, >", "", ""};
}

std::vector<std::uint8_t> read_file(const fs::path& path, bool* ok) {
    std::ifstream in(path, std::ios::binary);
    *ok = static_cast<bool>(in);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class Session {
public:
    explicit Session(const Options& options) : options_(options), fakes_() {
        const char* home = std::getenv("HOME");
        home_ = home && *home ? home : ".";
        if (options_.clock == "real") clock_ = &real_clock_;
        else clock_ = &fake_clock_;
        fakes_.backend = fake::SoundBackend(clock_);
    }

    int run() {
        std::ifstream in(options_.script);
        if (!in) return fail_plain(kStatusScriptError, "the script cannot be read: " + options_.script);
        if (options_.out.empty()) return fail_plain(kStatusScriptError, "--out DIR is needed with --script");
        if (options_.clock != "fake" && options_.clock != "real") return fail_plain(kStatusScriptError, "--clock is fake or real");
        if (options_.backend != "software" && options_.backend != "fake") return fail_plain(kStatusScriptError, "--backend is software or fake");
        if (options_.engine != "real" && options_.engine != "fake") return fail_plain(kStatusScriptError, "--engine is real or fake");
        std::error_code ec;
        fs::create_directories(options_.out, ec);

        std::string why;
        if (!fakes_.load(options_.fakes, home_, &why)) return fail_plain(kStatusScriptError, why);

        g_clock = clock_;
        lv_init();
        lv_tick_set_cb(tick);
        const platform::DisplayKind kind = options_.headless ? platform::DisplayKind::Memory : platform::default_display();
        if (!platform::open_display(kind, &why)) return fail_plain(kStatusScriptError, why);

        struct sigaction action;
        std::memset(&action, 0, sizeof action);
        action.sa_handler = on_signal;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);

        int status = 0;
        int line_number = 0;
        std::string line;
        try {
            start_app();
            while (std::getline(in, line)) {
                ++line_number;
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;
                command(line);
            }
            if (fakes_.http.forbidden_requests > 0) {
                throw Stop{kStatusExpectFailed, "the app used the network, and this test gave it none (no http.tsv)",
                           "no request", std::to_string(fakes_.http.forbidden_requests) + " request(s)"};
            }
        } catch (const Stop& stop) {
            status = stop.status;
            if (status != 0) report(line_number, line, stop);
        }
        stop_app();
        if (!options_.metrics.empty()) {
            std::ofstream metrics(options_.metrics);
            metrics << "app_version = " << app_version() << "\n";
        }
        return status;
    }

private:
    // ---- the app's life ----------------------------------------------------------------------------
    void start_app() {
        if (!lock_.take()) throw Stop{kStatusScriptError, "SidPlayZero is already running in this home folder", "", ""};
        app_.reset(new app::StubApp(clock_));
        settle();
    }

    void stop_app() {
        app_.reset();
        lock_.release();
    }

    void need_app(const std::string& what) {
        if (!app_) throw Stop{kStatusScriptError, "'" + what + "' needs the app, and it has left", "", ""};
    }

    // The app left by itself (a held ESC): like `quit`, it is simply gone.
    void notice_leaving() {
        if (app_ && app_->leaving()) stop_app();
    }

    // ---- time ----------------------------------------------------------------------------------------
    void run_ui_once() {
        if (g_signals > 0) throw Stop{0, "told to leave by a signal", "", ""};
        if (clock_ == &real_clock_) real_clock_.run_due();
        lv_timer_handler();
    }

    // SPEC-253: no work waiting anywhere, and everything drawn.
    void settle() {
        const auto limit = std::chrono::steady_clock::now() + kSettleLimit;
        for (;;) {
            run_ui_once();
            lv_refr_now(nullptr);
            bool idle = true;
            for (const auto& check : idle_checks()) idle = idle && check();
            if (idle) return;
            if (std::chrono::steady_clock::now() > limit) {
                throw Stop{kStatusExpectFailed, "the app did not settle within 10 s", "settled", "still busy"};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void advance(std::int64_t ms) {
        if (clock_ == &real_clock_) {
            // The real clock: wait that long, with the UI running. Nothing waits for "settled" (SPEC-253).
            const std::int64_t end = real_clock_.now_ms() + ms;
            while (real_clock_.now_ms() < end && !(app_ && app_->leaving())) {
                run_ui_once();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return;
        }
        for (std::int64_t done = 0; done < ms; done += kStepMs) {
            const std::int64_t step = std::min(kStepMs, ms - done);
            fake_clock_.advance(step);
            if (fakes_.sink.is_open) fakes_.sink.pull(static_cast<std::size_t>(fakes_.sink.sample_rate * step / 1000));
            run_ui_once();
            if (app_ && app_->leaving()) break;
        }
        settle();
    }

    // ---- keys ----------------------------------------------------------------------------------------
    int code_of(const std::string& name) {
        const Key key = key_from_name(name);
        if (key == Key::None) throw Stop{kStatusScriptError, "'" + name + "' is not a key name of SPEC-111", "", ""};
        return key_code(key);
    }

    void send(int code, bool pressed) {
        need_app("a key");
        app_->key(code, pressed);
        if (clock_ == &fake_clock_) settle();
        else run_ui_once();
    }

    void type(const std::string& text) {
        for (const unsigned char c : text) {
            bool shift = false;
            const int code = c < 0x80 ? code_for_character(static_cast<char32_t>(c), &shift) : 0;
            if (code == 0) throw Stop{kStatusScriptError, "no key types the character in '" + text + "'", "", ""};
            if (shift) send(key_code(Key::Shift), true);
            send(code, true);
            send(code, false);
            if (shift) send(key_code(Key::Shift), false);
        }
    }

    // ---- the screen ----------------------------------------------------------------------------------
    Json describe() {
        need_app("the screen description");
        return app_->describe();
    }

    bool shot(const fs::path& path) {
        std::vector<std::uint8_t> rgb;
        int width = 0, height = 0;
        return ui::snapshot_rgb(&rgb, &width, &height) && ui::write_png(path.string(), rgb, width, height);
    }

    void expect(std::string rest) {
        const std::string what = word(&rest);
        const std::string is = word(&rest);
        const std::string value = unquote(rest);
        if (what.empty() || is.empty()) throw Stop{kStatusScriptError, "expect needs: WHAT IS VALUE", "", ""};
        const Json description = describe();
        const Json* at = description.find(what);
        const std::string found = at ? at->scalar_text() : "(no such field)";
        if (!at || !compare(found, is, value)) {
            throw Stop{kStatusExpectFailed, "expect " + what + " " + is + " " + value, is + " " + value, found};
        }
    }

    // ---- files ---------------------------------------------------------------------------------------
    fs::path in_home(const std::string& path) { return fs::path(home_) / path; }
    fs::path in_out(const std::string& name) { return fs::path(options_.out) / name; }

    void expect_file(std::string rest) {
        const std::string path = word(&rest);
        const std::string how = word(&rest);
        const fs::path full = in_home(path);
        const bool exists = fs::exists(fs::symlink_status(full));
        bool ok = false;
        std::string found = exists ? "it exists" : "it does not exist";
        if (how == "exists") ok = exists;
        else if (how == "absent") ok = !exists;
        else if (how == "equals" || how == "contains") {
            bool read_ok = false, other_ok = true;
            const std::vector<std::uint8_t> bytes = read_file(full, &read_ok);
            if (how == "equals") {
                const std::vector<std::uint8_t> other = read_file(in_home(trim(rest)), &other_ok);
                ok = read_ok && other_ok && bytes == other;
                found = !read_ok ? "it cannot be read" : !other_ok ? "the other file cannot be read" : "they differ";
            } else {
                const std::string text(bytes.begin(), bytes.end());
                ok = read_ok && text.find(unquote(rest)) != std::string::npos;
                found = read_ok ? "it does not hold that text" : "it cannot be read";
            }
        } else {
            throw Stop{kStatusScriptError, "expect-file PATH is followed by exists, absent, equals OTHER or contains TEXT", "", ""};
        }
        if (!ok) throw Stop{kStatusExpectFailed, "expect-file " + path + " " + how + " " + rest, how + " " + rest, found};
    }

    void put(std::string rest) {
        const std::string from = word(&rest);
        const std::string to = trim(rest);
        if (fakes_.folder.empty() || from.empty() || to.empty()) throw Stop{kStatusScriptError, "put needs FROM and PATH, and a --fakes folder", "", ""};
        std::error_code ec;
        const fs::path source = fs::path(fakes_.folder) / from, target = in_home(to);
        if (!fs::exists(source)) throw Stop{kStatusScriptError, "the fakes folder has no " + from, "", ""};
        fs::create_directories(target.parent_path(), ec);
        fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) throw Stop{kStatusScriptError, "could not copy " + from + " to " + to + ": " + ec.message(), "", ""};
    }

    void set_writable(const std::string& path, bool writable) {
        const fs::path full = in_home(path);
        std::error_code ec;
        fs::permissions(full, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                        writable ? fs::perm_options::add : fs::perm_options::remove, ec);
        // The tests may run as a user whom permissions do not stop, so the fake files are told as well.
        if (writable) fakes_.files.make_writable(full.string());
        else fakes_.files.make_read_only(full.string());
    }

    // ---- sound, requests, logs -----------------------------------------------------------------------
    void save_samples(const std::string& name) {
        std::ofstream out(in_out(name + ".raw"), std::ios::binary);
        const auto& all = fakes_.sink.received;
        out.write(reinterpret_cast<const char*>(all.data() + fakes_.samples_saved),
                  static_cast<std::streamsize>((all.size() - fakes_.samples_saved) * sizeof(std::int16_t)));
        fakes_.samples_saved = all.size();
    }

    void expect_same(std::string rest) {
        const std::string a = word(&rest), b = word(&rest);
        bool ok_a = false, ok_b = false;
        const auto bytes_a = read_file(in_out(a + ".raw"), &ok_a), bytes_b = read_file(in_out(b + ".raw"), &ok_b);
        if (!ok_a || !ok_b) throw Stop{kStatusScriptError, "expect-same needs two names that save-samples wrote", "", ""};
        if (bytes_a != bytes_b) throw Stop{kStatusExpectFailed, "expect-same " + a + " " + b, "the same samples", "they differ"};
    }

    void expect_samples(std::string rest) {
        const std::string how = word(&rest);
        const auto& all = fakes_.sink.received;
        if (how == "frames") {
            const std::string is = word(&rest);
            const std::string found = std::to_string(all.size() / 2);
            if (!compare(found, is, trim(rest))) throw Stop{kStatusExpectFailed, "expect-samples frames " + is + " " + rest, is + " " + rest, found};
        } else if (how == "equals") {
            bool ok = false;
            const auto bytes = read_file(fs::path(fakes_.folder) / trim(rest), &ok);
            if (!ok) throw Stop{kStatusScriptError, "the fakes folder has no " + trim(rest), "", ""};
            const auto* mine = reinterpret_cast<const std::uint8_t*>(all.data());
            if (bytes.size() != all.size() * 2 || !std::equal(bytes.begin(), bytes.end(), mine)) {
                throw Stop{kStatusExpectFailed, "expect-samples equals " + rest, "the samples of " + trim(rest), "other samples"};
            }
        } else {
            throw Stop{kStatusScriptError, "expect-samples is followed by equals FILE or frames IS N", "", ""};
        }
    }

    void fake(std::string rest) {
        const std::string name = word(&rest);
        std::string why;
        if (name == "http") {
            const std::string what = word(&rest);
            if (what == "down") fakes_.http.down = true;
            else if (what == "up") fakes_.http.down = false;
            else if (what == "release") fakes_.http.release(trim(rest));
            else throw Stop{kStatusScriptError, "fake http is followed by down, up or release URL", "", ""};
        } else if (name == "sink") {
            if (word(&rest) != "drop") throw Stop{kStatusScriptError, "fake sink is followed by drop MS", "", ""};
            fakes_.sink.drop_device();
        } else if (name == "backend") {
            const std::string event = word(&rest);
            if (event == "song_ended") fakes_.backend.emit_song_ended();
            else if (event == "device_lost") fakes_.backend.emit_device_lost();
            else if (event == "device_back") fakes_.backend.emit_device_back();
            else if (event == "device_gone") fakes_.backend.emit_device_gone();
            else if (event == "failed") fakes_.backend.emit_failed(trim(rest));
            else if (event == "underrun") fakes_.backend.underrun_frames += static_cast<std::uint64_t>(number(trim(rest), "SECONDS"));
            else if (event == "registers") {
                const long long chip = number(word(&rest), "CHIP"), reg = number(word(&rest), "REGISTER");
                const long long value = number(trim(rest), "VALUE");
                if (chip < 1 || chip > 3 || reg < 0 || reg >= Engine::kRegisters) throw Stop{kStatusScriptError, "no such chip or register", "", ""};
                fakes_.backend.register_values[chip - 1][reg] = static_cast<std::uint8_t>(value);
            } else {
                throw Stop{kStatusScriptError, "'" + event + "' is not an event of the fake backend", "", ""};
            }
        } else if (!fakes_.set(name, trim(rest), &why)) {
            throw Stop{kStatusScriptError, why, "", ""};
        }
    }

    // ---- one line ------------------------------------------------------------------------------------
    void command(const std::string& line) {
        std::string rest = line;
        const std::string name = word(&rest);
        if (name == "key") { const int code = code_of(trim(rest)); send(code, true); send(code, false); }
        else if (name == "press") send(code_of(trim(rest)), true);
        else if (name == "release") send(code_of(trim(rest)), false);
        else if (name == "hold") {
            const int code = code_of(word(&rest));
            const long long ms = number(trim(rest), "MS");
            send(code, true);
            advance(ms);
            if (app_ && !app_->leaving()) send(code, false);
        }
        else if (name == "type") { need_app("type"); type(rest); }
        else if (name == "advance") advance(number(trim(rest), "MS"));
        else if (name == "settle") settle();
        else if (name == "fake") { fake(rest); if (clock_ == &fake_clock_) settle(); }
        else if (name == "dump") {
            std::ofstream out(in_out(trim(rest) + ".json"));
            out << describe().dump(2);
        }
        else if (name == "shot") {
            need_app("shot");
            if (!shot(in_out(trim(rest) + ".png"))) throw Stop{kStatusScriptError, "the screen could not be written as a picture", "", ""};
        }
        else if (name == "expect") expect(rest);
        else if (name == "expect-file") expect_file(rest);
        else if (name == "expect-requests") {
            const std::string found = std::to_string(fakes_.http.log.size());
            if (found != trim(rest)) throw Stop{kStatusExpectFailed, line, trim(rest) + " request(s)", found};
        }
        else if (name == "expect-request") {
            const long long n = number(word(&rest), "N");
            const std::string found = n >= 1 && n <= static_cast<long long>(fakes_.http.log.size()) ? fakes_.http.log[n - 1].url : "(no such request)";
            if (found != trim(rest)) throw Stop{kStatusExpectFailed, line, trim(rest), found};
        }
        else if (name == "expect-samples") expect_samples(rest);
        else if (name == "save-samples") save_samples(trim(rest));
        else if (name == "expect-same") expect_same(rest);
        else if (name == "expect-log") {
            const std::string which = word(&rest);
            if (word(&rest) != "contains") throw Stop{kStatusScriptError, "expect-log NAME contains TEXT", "", ""};
            std::string text;
            if (!fakes_.log(which, &text)) throw Stop{kStatusScriptError, "there is no log named " + which, "", ""};
            if (text.find(unquote(rest)) == std::string::npos) throw Stop{kStatusExpectFailed, line, "contains " + rest, text};
        }
        else if (name == "put") { put(rest); if (clock_ == &fake_clock_) settle(); }
        else if (name == "remove") {
            std::error_code ec;
            fs::remove_all(in_home(trim(rest)), ec);
            if (clock_ == &fake_clock_) settle();
        }
        else if (name == "readonly") set_writable(trim(rest), false);
        else if (name == "writable") set_writable(trim(rest), true);
        else if (name == "restart") { stop_app(); start_app(); }
        else if (name == "quit") stop_app();
        else throw Stop{kStatusScriptError, "'" + name + "' is not a command of the script language (SPEC-253)", "", ""};
        notice_leaving();
    }

    // ---- when a line fails ---------------------------------------------------------------------------
    void report(int line_number, const std::string& line, const Stop& stop) {
        std::ostringstream text;
        text << options_.script << ":" << line_number << ": " << stop.message << "\n";
        text << "line: " << line << "\n";
        if (!stop.expected.empty() || !stop.found.empty()) {
            text << "expected: " << stop.expected << "\n" << "found: " << stop.found << "\n";
        }
        std::fputs(text.str().c_str(), stderr);
        std::ofstream(in_out("failure.txt")) << text.str();
        if (app_) {
            std::ofstream(in_out("failure.json")) << app_->describe().dump(2);
            shot(in_out("failure.png"));
        }
    }

    int fail_plain(int status, const std::string& message) {
        std::fprintf(stderr, "sidplayzero: %s\n", message.c_str());
        return status;
    }

    Options options_;
    SimFakes fakes_;
    fake::Clock fake_clock_;
    platform::RealClock real_clock_;
    Clock* clock_ = nullptr;
    std::unique_ptr<app::StubApp> app_;
    app::Lock lock_;
    std::string home_;
};

}  // namespace

void add_idle_check(std::function<bool()> idle) { idle_checks().push_back(std::move(idle)); }

int run_script(const Options& options) {
    Session session(options);
    return session.run();
}

}  // namespace spz::hooks
