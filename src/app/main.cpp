// SPDX-License-Identifier: GPL-3.0-or-later
// The program: its options, the real world's seams (SPEC-021), the display and the keyboard, the UI loop
// (SPEC-022), and the order of SPEC-200 and SPEC-201 around the App. The simulator's hooks (SPEC-253) are in
// app/hooks and come in only with SIDPLAYZERO_TEST_HOOKS.
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "app/app.h"
#include "app/start.h"
#include "lvgl.h"
#include "platform/child_process.h"
#include "platform/curl_http.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "platform/miniaudio_sink.h"
#include "platform/real_clock.h"
#include "platform/real_engine.h"
#include "platform/real_files.h"
#include "platform/system_seams.h"
#include "spz/version.h"
#include "ui/about_screen.h"

#if SPZ_TEST_HOOKS
#include "app/hooks/script.h"
#endif

namespace {

int usage() {
    std::fprintf(stderr, "usage: sidplayzero [--version]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
#if SPZ_TEST_HOOKS
    spz::hooks::Options hooks;
#endif
    for (int i = 1; i < argc; ++i) {
        const std::string word = argv[i];
        if (word == "--version") {
            // SPEC-103: the About screen's first two lines: the app with its version, and the libraries.
            const std::vector<std::string> lines = spz::ui::about_lines(spz::app::engine_libraries());
            std::printf("%s\n%s\n", lines[0].c_str(), lines[1].c_str());
            return 0;
        }
#if SPZ_TEST_HOOKS
        auto value = [&](std::string* into) {
            if (i + 1 >= argc) return false;
            *into = argv[++i];
            return true;
        };
        std::string number;
        if (word == "--headless") { hooks.headless = true; continue; }
        if (word == "--script" && value(&hooks.script)) continue;
        if (word == "--out" && value(&hooks.out)) continue;
        if (word == "--fakes" && value(&hooks.fakes)) continue;
        if (word == "--backend" && value(&hooks.backend)) continue;
        if (word == "--engine" && value(&hooks.engine)) continue;
        if (word == "--clock" && value(&hooks.clock)) continue;
        if (word == "--metrics" && value(&hooks.metrics)) continue;
        if (word == "--seed" && value(&number)) { hooks.seed = std::stoull(number); hooks.has_seed = true; continue; }
#endif
        return usage();
    }
#if SPZ_TEST_HOOKS
    if (!hooks.script.empty()) return spz::hooks::run_script(hooks);
    if (hooks.headless) return usage();
#endif

    // SPEC-200, step 1: the lock. Another SidPlayZero holds it: say so, and touch nothing.
    spz::app::Lock lock;
    if (!lock.take()) {
        std::fprintf(stderr, "SidPlayZero is already running\n");
        return 1;
    }

    // The real seams (SPEC-021).
    spz::platform::RealClock clock;
    spz::platform::RealFiles files;
    spz::platform::CurlHttpClient http;
    spz::platform::SevenZipUnpacker unpacker;
    spz::platform::PactlVolume volume;
    spz::platform::SysfsBacklight backlight;
    spz::platform::ProcMemory memory;
    spz::platform::RealEngine engine;
    spz::platform::MiniaudioSink sink;

    // Step 3: the display and the keyboard.
    std::string why;
    if (!spz::app::start_program(&clock, spz::platform::default_display(), &why)) {
        std::fprintf(stderr, "sidplayzero: %s\n", why.c_str());
        return 1;
    }
    spz::platform::Keyboard keyboard;
    const bool keyboard_open = keyboard.open(&why);
    if (!keyboard_open) std::fprintf(stderr, "sidplayzero: %s\n", why.c_str());

    spz::app::count_exit_signals();

    spz::app::Seams seams;
    seams.clock = &clock;
    seams.files = &files;
    seams.http = &http;
    seams.unpacker = &unpacker;
    seams.volume = &volume;
    seams.backlight = &backlight;
    seams.memory = &memory;
    seams.engine = &engine;
    seams.sink = &sink;
    seams.render_thread = true;
    seams.make_engine = [] { return std::unique_ptr<spz::Engine>(new spz::platform::RealEngine()); };
    seams.libraries = spz::app::engine_libraries();
    seams.at_exit = [&http] {
        http.stop_all();
        spz::platform::stop_all_children();
    };
    seams.start_ms = spz::app::ms_since_process_start();
    if (spz::platform::default_display() == spz::platform::DisplayKind::Framebuffer) {
        // SPEC-241: what the app was allowed to open, and what miniaudio opened.
        seams.device_facts = [&backlight, &sink, keyboard_open] {
            std::map<std::string, std::string> facts = {
                {"framebuffer", "yes"},
                {"keyboard", keyboard_open ? "yes" : "no"},
                {"backlight_writable", backlight.can_change() ? "yes" : "no"},
            };
            const std::string backend = sink.backend_name();
            if (!backend.empty()) facts["audio_backend"] = backend;
            return facts;
        };
    }

    // Steps 2, 4 and 5.
    std::unique_ptr<spz::app::App> app(new spz::app::App(seams));
    app->pump();
    lv_timer_handler();
    lv_refr_now(nullptr);

    while (!app->leaving()) {
        if (spz::app::exit_signals() > 0) app->leave();
        keyboard.poll([&](int code, bool pressed) { app->key(code, pressed); });
        clock.run_due();
        app->pump();
        lv_timer_handler();
        usleep(5000);
    }
    // SPEC-201. A second signal ends the process at once.
    spz::app::obey_exit_signals();
    app->shut_down();
    app.reset();
    lock.release();
    return 0;
}
