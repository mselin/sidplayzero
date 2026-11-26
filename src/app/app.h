// SPDX-License-Identifier: GPL-3.0-or-later
// The app's frame: its threads (SPEC-022), the stack of screens (SPEC-090), the three bands of every
// screen (SPEC-091), keys routed through the key map by place (SPEC-092 to SPEC-094, SPEC-106, SPEC-111),
// notices and panels (SPEC-113, SPEC-114), help (SPEC-107), and the screen description (SPEC-254). The
// screens themselves are ui::Screen classes, listed in screens.cpp.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/workers.h"
#include "spz/clock.h"
#include "spz/files.h"
#include "spz/json.h"
#include "spz/key_input.h"
#include "spz/notices.h"
#include "spz/screen_stack.h"
#include "spz/system.h"
#include "ui/screen.h"

namespace spz {
class SettingsFile;
}

namespace spz::app {

// What the program gives the app: the real world, or the simulator's fakes (SPEC-021, SPEC-253).
struct Seams {
    Clock* clock = nullptr;
    Files* files = nullptr;
    Backlight* backlight = nullptr;          // null where the brightness cannot be changed
    // SPEC-103: "libsidplayfp 3.1.2", "libresidfp 1.3.0", as the engine of `platform` reads them from the
    // libraries. Empty while there is no engine: the About screen waits for it.
    std::vector<std::string> libraries;
    // Part of "settled" (SPEC-253): whether a fake is holding back what a worker waits for.
    std::function<bool()> held;
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
    explicit App(const Seams& seams);
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
    void leave() { leaving_ = true; }
    bool leaving() const { return leaving_; }
    // SPEC-254.
    Json describe();

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
    void redraw() override { dirty_ = true; }
    void on_action(const std::string& action, std::function<void(const KeyEvent&)> handler) override;

private:
    // KeyListener
    void on_key(const KeyEvent& event) override;
    void on_hold_notice(bool shown) override;

    ui::Screen* top();
    std::string place();
    std::string help_under_place();
    bool typing();
    void draw();
    void draw_top_line(lv_obj_t* root);
    void draw_help(lv_obj_t* content);
    void draw_action_bar(lv_obj_t* root);
    void draw_notice(lv_obj_t* root);
    void help_action(const std::string& action);
    void brightness(bool up);
    void screenshot();
    void start_settings();

    Seams seams_;
    ui::Services services_;
    std::unique_ptr<SettingsFile> settings_;
    ScreenList list_;
    ScreenStack stack_;
    std::map<std::string, std::unique_ptr<ui::Screen>> screens_;   // the open ones, by name
    std::unique_ptr<ui::Panel> panel_;
    std::unique_ptr<ui::Panel> closed_panel_;                       // kept until the key that closed it is done
    bool help_open_ = false;
    std::size_t help_first_ = 0;
    std::map<std::string, std::function<void(const KeyEvent&)>> handlers_;
    NoticeQueue notices_;
    KeyInput keys_;
    bool dirty_ = true;
    bool leaving_ = false;

    // The threads, last: they go first, so that no job of theirs outlives what it uses.
    UiQueue ui_;
    Worker quick_{"spz-quick", true};
    Worker fetch_{"spz-fetch"};
    Worker long_{"spz-long"};
    Worker render_{"spz-render"};
};

}  // namespace spz::app
