// SPDX-License-Identifier: GPL-3.0-or-later
// The program's start: options, the display, the keyboard, and the UI loop (SPEC-022). The order of
// SPEC-200 and the exit of SPEC-201 belong to the area that builds them; this is the least of both.
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/app.h"
#include "app/start.h"
#include "lvgl.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "platform/real_clock.h"
#include "platform/real_files.h"
#include "spz/version.h"

#if SPZ_TEST_HOOKS
#include "app/hooks/script.h"
#endif

namespace {

volatile std::sig_atomic_t g_signals = 0;
spz::platform::RealClock* g_clock = nullptr;

void on_signal(int) { g_signals = g_signals + 1; }

std::uint32_t tick() { return static_cast<std::uint32_t>(g_clock->now_ms()); }

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
            // SPEC-103: the About screen's first two lines. The second, the libraries, comes with the engine.
            std::printf("%s\n", spz::version_line());
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

    spz::app::Lock lock;
    if (!lock.take()) {
        std::fprintf(stderr, "SidPlayZero is already running\n");
        return 1;
    }

    spz::platform::RealClock clock;
    g_clock = &clock;
    lv_init();
    lv_tick_set_cb(tick);
    std::string why;
    if (!spz::platform::open_display(spz::platform::default_display(), &why)) {
        std::fprintf(stderr, "sidplayzero: %s\n", why.c_str());
        return 1;
    }
    spz::platform::Keyboard keyboard;
    if (!keyboard.open(&why)) std::fprintf(stderr, "sidplayzero: %s\n", why.c_str());

    struct sigaction action;
    std::memset(&action, 0, sizeof action);
    action.sa_handler = on_signal;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    spz::platform::RealFiles files;
    spz::app::Seams seams;
    seams.clock = &clock;
    seams.files = &files;
    spz::app::App app(seams);
    app.pump();
    lv_timer_handler();
    lv_refr_now(nullptr);
    spz::app::record_for_selftest("start_ms", std::to_string(spz::app::ms_since_process_start()));
    spz::app::record_for_selftest("app_version", spz::app_version());

    while (!app.leaving()) {
        if (g_signals > 0) app.leave();
        keyboard.poll([&](int code, bool pressed) { app.key(code, pressed); });
        clock.run_due();
        app.pump();
        lv_timer_handler();
        usleep(5000);
    }
    const std::int64_t asked = clock.now_ms();
    // SPEC-201, in small: nothing to stop or save yet. A second signal ends the process at once.
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    spz::app::record_for_selftest("exit_ms", std::to_string(clock.now_ms() - asked));
    lock.release();
    return 0;
}
