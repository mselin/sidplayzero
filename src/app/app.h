// SPDX-License-Identifier: GPL-3.0-or-later
// The app's frame: its threads (SPEC-022), the stack of screens (SPEC-090), the three bands of every
// screen (SPEC-091), keys routed through the key map by place (SPEC-092 to SPEC-094, SPEC-106, SPEC-111),
// notices and panels (SPEC-113, SPEC-114), help (SPEC-107), and the screen description (SPEC-254). The
// screens themselves are ui::Screen classes, listed in screens.cpp; the objects of `core` they work with
// are made and joined by the composition (composition.h).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/workers.h"
#include "spz/audio_sink.h"
#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/json.h"
#include "spz/key_input.h"
#include "spz/notices.h"
#include "spz/screen_stack.h"
#include "spz/sound_backend.h"
#include "spz/system.h"
#include "ui/screen.h"

namespace spz::app {

class Composition;

// What the program gives the app: the real world, or the simulator's fakes (SPEC-021, SPEC-253). Every
// pointer is the program's, and outlives the app.
struct Seams {
    Clock* clock = nullptr;
    Files* files = nullptr;
    HttpClient* http = nullptr;
    Unpacker* unpacker = nullptr;
    SystemVolume* volume = nullptr;
    Backlight* backlight = nullptr;
    Memory* memory = nullptr;
    // Sound (SPEC-025): the software backend is made of an engine and a sink, in the one place that makes
    // it (platform::SoftwareSound, which the composition makes). With `backend` set, the player drives that
    // backend instead (the simulator's --backend fake), and no engine runs.
    Engine* engine = nullptr;
    AudioSink* sink = nullptr;
    SoundBackend* backend = nullptr;
    // True: the render loop runs on a thread of its own (SPEC-022). False: the program turns it itself,
    // with App::render_turn(), as the simulator does on its fake clock.
    bool render_thread = true;
    // SPEC-072: a new engine for the speed measurement, beside the one that plays. Null: no measurement.
    std::function<std::unique_ptr<Engine>()> make_engine;
    // SPEC-103: "libsidplayfp 3.1.2", "libresidfp 1.3.0", as the engine of `platform` reads them.
    std::vector<std::string> libraries;
    // Part of "settled" (SPEC-253): whether a fake is holding back what this thread waits for.
    std::function<bool(std::thread::id)> held;
    // SPEC-201: ends the outside world's waits at exit: requests in flight, `7z` and its children.
    std::function<void()> at_exit;
    // The test options of SPEC-253.
    bool has_seed = false;
    std::uint64_t seed = 0;
    std::string metrics;                     // --metrics FILE: the measurements, written at exit (SPEC-241)
    long long start_ms = -1;                 // the process's age when the app was made (SPEC-200's start time)
};

// A screen this build has, and how to make it.
using ScreenMaker = std::function<std::unique_ptr<ui::Screen>(ui::Shell*)>;
class ScreenList {
public:
    void add(const std::string& name, ScreenMaker make) { makers_[name] = std::move(make); }
    const ScreenMaker* find(const std::string& name) const;

private:
    std::map<std::string, ScreenMaker> makers_;
};
// screens.cpp: every screen of the app.
void add_screens(ScreenList* list);

class App : public ui::Shell, private KeyListener {
public:
    // SPEC-200, steps 2 and 4: reads the settings and the state, shows the browser (or the empty screen), and
    // hands step 5 to the workers.
    explicit App(const Seams& seams);
    // Calls shut_down() if it has not been called.
    ~App() override;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // A key went down or came up, as a Linux key code (SPEC-111).
    void key(int linux_code, bool pressed);
    // The UI thread's turn: runs the messages waiting for it, and draws what has changed.
    void pump();
    // SPEC-253: no message waits for the UI thread, every worker is idle or held by a fake, and everything
    // is drawn.
    bool settled() const;
    // Asked to leave: a held ESC (SPEC-106) or a signal (SPEC-201).
    void leave();
    bool leaving() const { return leaving_; }
    // SPEC-201, from "stop taking keys" to the measurements: stops the sound, puts the brightness back,
    // writes state.ini, stops the workers (cutting downloads short), and writes the measurements. The lock
    // is the program's to release after this.
    void shut_down();
    // SPEC-070: one turn of the render loop, as far as the ring buffer has room, for a program that turns
    // it itself (Seams::render_thread false). Never with a render thread.
    void render_turn();
    // SPEC-254.
    Json describe();

    // What the composition adds to the frame.
    // SPEC-207: asked first at every key press; true means the key only turned the screen back on.
    void set_key_filter(std::function<bool()> filter) { key_filter_ = std::move(filter); }
    // SPEC-112: called with how long after it was read each key's first screen update came.
    void set_key_timer(std::function<void(Millis)> fn) { key_timer_ = std::move(fn); }
    const Seams& seams() const { return seams_; }

    // ui::Shell
    Clock* clock() override { return seams_.clock; }
    Executor* ui() override { return &ui_; }
    Executor* quick() override { return &quick_; }
    Executor* fetch_worker() override { return &fetch_; }
    Executor* long_worker() override { return &long_; }
    Executor* render() override { return &render_; }
    ui::Services& services() override { return services_; }
    void notice(const std::string& id, const std::map<std::string, std::string>& about = {}) override;
    bool open(const std::string& screen) override;
    void set_bottom(const std::string& screen) override;
    void close_top() override;
    void open_panel(std::unique_ptr<ui::Panel> panel) override;
    void close_panel() override;
    ui::Panel* panel() override { return panel_.get(); }
    bool help_open() const override { return help_open_; }
    void when_no_overlay(std::function<void()> fn) override;
    void redraw() override { dirty_ = true; }
    void on_action(const std::string& action, std::function<void(const KeyEvent&)> handler) override;
    void add_describer(std::function<void(Json*)> fn) override { describers_.push_back(std::move(fn)); }
    void add_top_right(std::function<std::string()> text) override { top_right_.push_back(std::move(text)); }
    void every(Millis ms, std::function<void()> fn) override;

    // The stack's bottom and top, by name.
    std::string bottom() const;
    std::string top_name() const { return stack_.top(); }

private:
    // KeyListener
    void on_key(const KeyEvent& event) override;
    void on_hold_notice(bool shown) override;

    ui::Screen* top();
    std::string place();
    std::string help_under_place();
    bool typing();
    std::string top_right_text();
    void draw();
    void draw_top_line(lv_obj_t* root);
    void draw_help(lv_obj_t* content);
    void draw_action_bar(lv_obj_t* root);
    void draw_notice(lv_obj_t* root);
    void help_action(const std::string& action);
    void brightness(bool up);
    void screenshot();
    void tick(std::size_t index);

    Seams seams_;
    ui::Services services_;
    ScreenList list_;
    ScreenStack stack_;
    std::map<std::string, std::unique_ptr<ui::Screen>> screens_;   // the open ones, by name
    std::vector<std::unique_ptr<ui::Screen>> closed_screens_;      // kept until the key that closed them is done
    std::unique_ptr<ui::Panel> panel_;
    std::unique_ptr<ui::Panel> closed_panel_;                       // kept until the key that closed it is done
    bool help_open_ = false;
    std::size_t help_first_ = 0;
    std::map<std::string, std::function<void(const KeyEvent&)>> handlers_;
    std::vector<std::function<void()>> after_overlay_;              // waiting for no panel and no help
    std::vector<std::function<void(Json*)>> describers_;
    std::vector<std::function<std::string()>> top_right_;
    struct Every {
        Millis ms;
        std::function<void()> fn;
        Clock::TimerId timer;
    };
    std::vector<Every> every_;
    std::function<bool()> key_filter_;
    std::function<void(Millis)> key_timer_;
    std::vector<Millis> keys_read_;                                 // when each key not yet answered was read
    std::vector<int> swallowed_;                                    // codes whose press turned the screen on
    NoticeQueue notices_;
    KeyInput keys_;
    bool dirty_ = true;
    bool leaving_ = false;
    bool shut_down_ = false;
    Millis asked_to_leave_ms_ = -1;

    // The threads, last: they go first, so that no job of theirs outlives what it uses.
    UiQueue ui_;
    Worker quick_{"spz-quick", true};
    Worker fetch_{"spz-fetch"};
    Worker long_{"spz-long"};
    Worker render_{"spz-render"};
    // The composition is made in the constructor's body, once the threads run, and goes before them.
    std::unique_ptr<Composition> parts_;
};

}  // namespace spz::app
