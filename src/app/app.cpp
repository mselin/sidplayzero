// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <utility>

#include "app/start.h"
#include "spz/brightness.h"
#include "spz/folders.h"
#include "spz/help.h"
#include "spz/keymap.h"
#include "spz/messages.h"
#include "spz/settings_file.h"
#include "ui/describe.h"
#include "ui/draw.h"
#include "ui/panel.h"
#include "ui/png.h"

namespace spz::app {

namespace {

// SPEC-091: the centres of the five action labels, for the keys 4 to 8.
const int kActionCentres[5] = {49, 105, 161, 217, 273};
const int kActionW = 52;
const int kNoticeTop = ui::kContentTop + ui::kContentHeight - ui::kRowHeight;   // the last row of the content band

// The screen an action of the key map opens, by SPEC-090's table, when the frame can open it by itself.
const std::map<std::string, std::string> kOpens = {
    {"open_playing", "playing"}, {"open_search", "search"},  {"open_playlists", "playlists"},
    {"open_settings", "settings"}, {"open_hvsc", "hvsc"},    {"open_about", "about"},
};

std::string getenv_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// Hides the labels of the content band that the notice covers (SPEC-091: it covers the last row).
void hide_under_notice(lv_obj_t* object) {
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t i = 0; i < children; ++i) {
        lv_obj_t* child = lv_obj_get_child(object, static_cast<std::int32_t>(i));
        if (lv_obj_check_type(child, &lv_label_class)) {
            lv_area_t area;
            lv_obj_get_coords(child, &area);
            if (area.y2 >= kNoticeTop) lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        } else {
            hide_under_notice(child);
        }
    }
}

}  // namespace

const ScreenMaker* ScreenList::find(const std::string& name) const {
    const auto found = makers_.find(name);
    return found == makers_.end() ? nullptr : &found->second;
}

App::App(const Seams& seams)
    : seams_(seams), notices_(seams.clock), keys_(seams.clock, [this] { return place(); }, this) {
    services_.files = seams.files;
    services_.backlight = seams.backlight;
    services_.home = home_folder();
    services_.collection = collection_root();
    services_.collection_from_variable = root_from_variable();
    add_screens(&list_);
    start_settings();
    notices_.on_change([this] { redraw(); });

    // The bottom of the stack: the browser, or in its place the empty screen (SPEC-048). Until the browser
    // is built, the empty screen stands there.
    const ScreenMaker* browser = list_.find("browser");
    std::unique_ptr<ui::Screen> bottom = browser ? (*browser)(this) : nullptr;
    if (bottom && bottom->ready()) {
        stack_.set_bottom("browser");
        screens_["browser"] = std::move(bottom);
    } else {
        stack_.set_bottom("empty");
        screens_["empty"] = (*list_.find("empty"))(this);
    }
    top()->shown();
    lv_obj_t* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, ui::colour_background(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    draw();
}

App::~App() {
    // The screens go, and their LVGL objects with them. The workers stop after this (they are declared
    // last, so they go first of the members): the quick worker finishes the saves that wait.
    panel_.reset();
    closed_panel_.reset();
    screens_.clear();
    lv_obj_clean(lv_screen_active());
}

void App::start_settings() {
    // SPEC-130: the settings, read once at start. SettingsFile and the folders belong to other areas; while
    // they are not built, there are no settings to show and the Settings screen waits.
    try {
        Environment environment;
        environment.home = getenv_or_empty("HOME");
        environment.xdg_config_home = getenv_or_empty("XDG_CONFIG_HOME");
        environment.xdg_data_home = getenv_or_empty("XDG_DATA_HOME");
        environment.xdg_state_home = getenv_or_empty("XDG_STATE_HOME");
        environment.xdg_cache_home = getenv_or_empty("XDG_CACHE_HOME");
        const Folders folders = app_folders(environment);
        std::unique_ptr<SettingsFile> file(new SettingsFile(seams_.files, &quick_, settings_path(folders)));
        file->load();
        const std::string reset = file->take_notice();
        if (!reset.empty()) notice(reset);
        settings_ = std::move(file);
        services_.settings = settings_.get();
    } catch (const std::exception&) {
        services_.settings = nullptr;
    }
}

// ---- the stack ---------------------------------------------------------------------------------------------

ui::Screen* App::top() { return screens_.at(stack_.top()).get(); }

bool App::open(const std::string& name) {
    if (name == "browser" || name == "empty") {
        stack_.open(stack_.screens().front());
    } else if (screens_.count(name) == 0) {
        const ScreenMaker* make = list_.find(name);
        if (make == nullptr) return false;
        std::unique_ptr<ui::Screen> screen = (*make)(this);
        if (!screen->ready()) return false;
        stack_.open(name);
        screens_[name] = std::move(screen);
    } else {
        stack_.open(name);
    }
    // The screens above the one returned to are closed.
    const std::vector<std::string> open = stack_.screens();
    for (auto it = screens_.begin(); it != screens_.end();) {
        if (std::find(open.begin(), open.end(), it->first) == open.end()) it = screens_.erase(it);
        else ++it;
    }
    top()->shown();
    redraw();
    return true;
}

void App::set_bottom(const std::string& name) {
    const std::string now = stack_.screens().front();
    if (now == name) return;
    const ScreenMaker* make = list_.find(name);
    if (make == nullptr) return;
    screens_[name] = (*make)(this);
    stack_.set_bottom(name);
    screens_.erase(now);
    if (stack_.top() == name) top()->shown();
    redraw();
}

void App::close_top() {
    const std::string closing = stack_.top();
    if (!stack_.close_top()) return;
    screens_.erase(closing);
    top()->shown();
    redraw();
}

void App::open_panel(std::unique_ptr<ui::Panel> panel) {
    closed_panel_ = std::move(panel_);
    panel_ = std::move(panel);
    redraw();
}

void App::close_panel() {
    // The panel may be the one whose key closed it: it lives until that key is done.
    closed_panel_ = std::move(panel_);
    redraw();
}

void App::notice(const std::string& id, const std::map<std::string, std::string>& about) {
    notices_.raise(id, message_words(id, about));
}

void App::on_action(const std::string& action, std::function<void(const KeyEvent&)> handler) {
    handlers_[action] = std::move(handler);
}

// ---- keys --------------------------------------------------------------------------------------------------

std::string App::place() {
    if (help_open_) return "help";
    if (panel_) return panel_->place();
    return top()->place();
}

bool App::typing() { return place_kind(place()) == PlaceKind::Typing; }

void App::key(int linux_code, bool pressed) {
    keys_.key(linux_code, pressed);
    closed_panel_.reset();
}

void App::on_hold_notice(bool shown) {
    // SPEC-106, FIND-021: the notice shows at once, in place of any other.
    if (shown) notices_.show_now("MSG_HOLD_TO_LEAVE", message_words("MSG_HOLD_TO_LEAVE"));
    else notices_.remove("MSG_HOLD_TO_LEAVE");
}

void App::on_key(const KeyEvent& event) {
    const std::string& a = event.action;
    if (a.empty()) return;
    try {
        // What is the same in every place.
        const auto handler = handlers_.find(a);
        if (handler != handlers_.end()) {
            handler->second(event);
            return;
        }
        if (a == "leave") {
            leave();
            return;
        }
        if (a == "toggle_help") {
            help_open_ = !help_open_;
            help_first_ = 0;
            redraw();
            return;
        }
        if (a == "brightness_up" || a == "brightness_down") {
            brightness(a == "brightness_up");
            return;
        }
        if (a == "screenshot") {
            screenshot();
            return;
        }
        if (help_open_) {
            help_action(a);
            return;
        }
        if (panel_) {
            panel_->on_action(event);
            return;
        }
        // A screen.
        if (a == "back" && stack_.screens().size() > 1) {
            close_top();
            return;
        }
        const auto opens = kOpens.find(a);
        if (opens != kOpens.end()) {
            open(opens->second);
            return;
        }
        top()->on_action(event);
    } catch (const std::exception& error) {
        // A part of `core` that is not built yet (Phase 5) or that failed: the key does nothing more.
        std::fprintf(stderr, "sidplayzero: the key %s (%s) failed: %s\n", event.key.c_str(), a.c_str(), error.what());
    }
}

void App::help_action(const std::string& action) {
    // SPEC-107: Up and Down scroll; Enter or ESC closes it (fn+H too, above).
    if (action == "close" || action == "back") {
        help_open_ = false;
        redraw();
        return;
    }
    const std::size_t count = help_lines(help_under_place()).size();
    const std::size_t shown = static_cast<std::size_t>((ui::kContentHeight - ui::kRowHeight) / ui::kRowHeight);
    const std::size_t most = count > shown ? count - shown : 0;
    if (action == "up") help_first_ = help_first_ > 0 ? help_first_ - 1 : 0;
    else if (action == "down") help_first_ = std::min(help_first_ + 1, most);
    else if (action == "page_up") help_first_ = help_first_ > shown ? help_first_ - shown : 0;
    else if (action == "page_down") help_first_ = std::min(help_first_ + shown, most);
    else if (action == "first") help_first_ = 0;
    else if (action == "last") help_first_ = most;
    else return;
    redraw();
}

std::string App::help_under_place() {
    if (panel_) return panel_->place();
    return top()->place();
}

void App::brightness(bool up) {
    // SPEC-104: the backlight is asked on the quick worker (SPEC-022), and the notice comes back.
    Backlight* backlight = services_.backlight;
    quick_.post([this, backlight, up] {
        const BrightnessStep step = step_brightness(backlight, up);
        ui_.post([this, step] {
            if (step.notice == "MSG_BRIGHTNESS") notice(step.notice, {{"n", std::to_string(step.percent)}});
            else notice(step.notice);
        });
    });
}

void App::screenshot() {
    // SPEC-108: what LVGL has drawn, now, written by the quick worker as a PNG in ~/Pictures/Screenshots.
    draw();
    std::vector<std::uint8_t> rgb;
    int width = 0, height = 0;
    if (!ui::snapshot_rgb(&rgb, &width, &height)) {
        notice("MSG_SCREENSHOT_FAILED");
        return;
    }
    const std::time_t seconds = static_cast<std::time_t>(seams_.clock->wall_time_s());
    std::tm when;
    localtime_r(&seconds, &when);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &when);
    char millis[8];
    std::snprintf(millis, sizeof millis, "%03d", static_cast<int>(seams_.clock->now_ms() % 1000));
    const std::string folder = services_.home + "/Pictures/Screenshots";
    const std::string path = folder + "/sidplayzero_" + stamp + "_" + millis + ".png";
    Files* files = seams_.files;
    quick_.post([this, files, folder, path, rgb, width, height] {
        const std::vector<std::uint8_t> bytes = ui::encode_png(rgb, width, height);
        const bool saved = files != nullptr && !bytes.empty() && files->make_folders(folder) == FileStatus::Ok &&
                           files->write(path, bytes) == FileStatus::Ok;
        ui_.post([this, saved, path] {
            if (saved) notice("MSG_SCREENSHOT_SAVED", {{"path", path}});
            else notice("MSG_SCREENSHOT_FAILED");
        });
    });
}

// ---- the UI thread's turn ----------------------------------------------------------------------------------

void App::pump() {
    ui_.run_all();
    closed_panel_.reset();
    if (dirty_) draw();
}

bool App::settled() const {
    if (!ui_.empty() || dirty_) return false;
    const bool held = seams_.held && seams_.held();
    for (const Worker* worker : {&quick_, &fetch_, &long_, &render_}) {
        if (!worker->idle() && !(held && worker->running())) return false;
    }
    return true;
}

// ---- drawing -----------------------------------------------------------------------------------------------

void App::draw() {
    dirty_ = false;
    lv_obj_t* root = lv_screen_active();
    lv_obj_clean(root);
    draw_top_line(root);
    lv_obj_t* content = ui::make_area(root, 0, ui::kContentTop, ui::kWidth, ui::kContentHeight);
    try {
        if (help_open_) draw_help(content);
        else if (panel_) panel_->draw(content);
        else top()->draw(content);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sidplayzero: drawing %s failed: %s\n", stack_.top().c_str(), error.what());
    }
    draw_action_bar(root);
    draw_notice(root);
}

void App::draw_top_line(lv_obj_t* root) {
    ui::Screen* screen = top();
    const std::string position = screen->position();
    const int position_w = position.empty() ? 0 : ui::text_width(position, ui::text_font()) + 2;
    const int title_w = ui::kWidth - 4 - (position_w > 0 ? position_w + 6 : 0);
    ui::LabelStyle style;
    style.cut_start = screen->name() == "browser";       // SPEC-091: a path loses its start
    ui::make_label(root, "title", screen->title(), 2, 0, title_w, ui::kTopHeight, style);
    if (position_w > 0) {
        ui::make_label(root, "position", position, 2 + title_w + 6, 0, position_w, ui::kTopHeight,
                       ui::LabelStyle{ui::Align::Left, nullptr, true, false});
    }
}

void App::draw_help(lv_obj_t* content) {
    ui::make_fill(content, 4, 0, ui::kWidth - 8, ui::kContentHeight, ui::colour_line());
    ui::make_fill(content, 5, 1, ui::kWidth - 10, ui::kContentHeight - 2, ui::colour_panel());
    ui::make_label(content, "help.title", "Keys", 10, 0, ui::kWidth - 20, ui::kRowHeight,
                   ui::LabelStyle{ui::Align::Left, nullptr, true, false});
    const std::vector<HelpLine> lines = help_lines(help_under_place());
    const std::size_t shown = static_cast<std::size_t>((ui::kContentHeight - ui::kRowHeight) / ui::kRowHeight);
    for (std::size_t i = help_first_; i < lines.size() && i < help_first_ + shown; ++i) {
        const int y = ui::kRowHeight * static_cast<int>(i - help_first_ + 1);
        const std::string id = "help.line" + std::to_string(i - help_first_);
        ui::make_label(content, id + ".key", lines[i].key, 10, y, 96, ui::kRowHeight,
                       ui::LabelStyle{ui::Align::Left, nullptr, true, false});
        ui::make_label(content, id, lines[i].words, 110, y, ui::kWidth - 120, ui::kRowHeight);
    }
}

void App::draw_action_bar(lv_obj_t* root) {
    lv_obj_t* bar = ui::make_area(root, 0, ui::kActionTop, ui::kWidth, ui::kHeight - ui::kActionTop);
    ui::make_fill(bar, 0, 0, ui::kWidth, 1, ui::colour_line());
    const ui::LabelStyle centred{ui::Align::Centre, nullptr, false, false};
    if (typing()) {
        // SPEC-091: while text is typed, Enter and ESC are what can be done.
        ui::make_label(bar, "typing.enter", "Enter: OK", 105 - 50, 4, 100, 16, centred);
        ui::make_label(bar, "typing.esc", "ESC: cancel", 217 - 50, 4, 100, 16, centred);
        return;
    }
    const std::array<std::string, 5> labels = help_open_ || panel_ ? action_labels(place()) : top()->actions();
    for (int i = 0; i < 5; ++i) {
        const std::string& text = labels[static_cast<std::size_t>(i)];
        if (text.empty()) continue;
        ui::make_label(bar, "action" + std::to_string(4 + i), text, kActionCentres[i] - kActionW / 2, 4, kActionW, 16,
                       centred);
        ui::make_fill(bar, kActionCentres[i], 22, 1, 8, ui::colour_dim());     // the tick, pointing at its key
    }
}

void App::draw_notice(lv_obj_t* root) {
    const Notice* showing = notices_.showing();
    if (showing == nullptr) return;
    lv_obj_update_layout(root);
    hide_under_notice(root);
    lv_obj_t* line = ui::make_fill(root, 0, kNoticeTop, ui::kWidth, ui::kRowHeight, ui::colour_panel());
    ui::make_label(line, "notice", showing->text, 4, 0, ui::kWidth - 8, ui::kRowHeight);
}

// ---- the screen description --------------------------------------------------------------------------------

Json App::describe() {
    if (dirty_) draw();
    Json d = ui::blank_description();
    ui::Screen* screen = top();
    d.set("screen", stack_.top());
    Json stack = Json::array();
    for (const std::string& name : stack_.screens()) stack.push(name);
    d.set("stack", stack);
    d.set("place", place());
    d.set("title", screen->title());
    const std::string position = screen->position();
    if (!position.empty()) d.set("position", position);
    screen->describe(&d);
    Json actions = Json::array();
    const bool now_typing = typing();
    const std::array<std::string, 5> labels =
        now_typing ? std::array<std::string, 5>{} : help_open_ || panel_ ? action_labels(place()) : screen->actions();
    for (const std::string& label : labels) actions.push(label);
    d.set("actions", actions);
    d.set("typing", now_typing);
    if (panel_) d.set("panel", panel_->describe());
    if (help_open_) {
        Json help = Json::array();
        for (const HelpLine& line : help_lines(help_under_place())) help.push(line.key + "  " + line.words);
        d.set("help", help);
    }
    if (const Notice* showing = notices_.showing()) {
        Json notice = Json::object();
        notice.set("id", showing->id);
        notice.set("text", showing->text);
        d.set("notice", notice);
    }
    ui::describe_drawn(&d);
    return d;
}

}  // namespace spz::app
