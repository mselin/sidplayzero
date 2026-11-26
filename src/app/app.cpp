// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <utility>

#include "app/composition.h"
#include "spz/brightness.h"
#include "spz/help.h"
#include "spz/keymap.h"
#include "spz/messages.h"
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

// Hides the labels of the content band that the notice covers (SPEC-091: it covers the last row).
void hide_under_notice(lv_obj_t* object) {
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t i = 0; i < children; ++i) {
        lv_obj_t* child = lv_obj_get_child(object, static_cast<std::int32_t>(i));
        if (lv_obj_check_type(child, &lv_label_class)) {
            lv_area_t area;
            lv_obj_get_coords(child, &area);
            // SPEC-113: the notice covers the last row of the content band and nothing else; the action bar
            // below it stays.
            if (area.y2 >= kNoticeTop && area.y1 < kNoticeTop + ui::kRowHeight) lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
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
    add_screens(&list_);
    notices_.on_change([this] { redraw(); });
    // SPEC-200, step 2: the settings and the state, and everything made from them.
    parts_.reset(new Composition(this, seams_));

    // Step 4: the browser at the bottom of the stack. If the collection turns out to hold nothing, the empty
    // screen takes its place (SPEC-048, compose_browse.cpp).
    stack_.set_bottom("browser");
    screens_["browser"] = (*list_.find("browser"))(this);
    top()->shown();
    lv_obj_t* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, ui::colour_background(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    draw();
    // The screen's own clock: what changes by itself on the screen on top (SPEC-080, SPEC-098).
    every(33, [this] {
        if (!panel_ && !help_open_) top()->tick();
    });
    // Step 5, on the workers.
    parts_->start();
}

App::~App() { shut_down(); }

void App::leave() {
    if (!leaving_) asked_to_leave_ms_ = seams_.clock->now_ms();
    leaving_ = true;
}

void App::shut_down() {
    if (shut_down_) return;
    shut_down_ = true;
    if (asked_to_leave_ms_ < 0) asked_to_leave_ms_ = seams_.clock->now_ms();
    // SPEC-201: stop taking keys (nothing more is read once this has begun); then the sound, the screen's
    // brightness, state.ini and the outside world's waits.
    for (auto& each : every_) {
        if (each.second.timer != 0) seams_.clock->cancel(each.second.timer);
        each.second.timer = 0;
    }
    parts_->stop();
    // The workers stop: each ends the job that runs, and drops those that wait; the quick worker still runs
    // the saves that wait (App::saves()).
    quick_.stop();
    fetch_.stop();
    long_.stop();
    // The measurements, once everything has stopped.
    parts_->write_metrics(seams_.clock->now_ms() - asked_to_leave_ms_);
    // The screens go, and their LVGL objects with them, then what they used.
    panel_.reset();
    closed_panel_.reset();
    closed_screens_.clear();
    screens_.clear();
    lv_obj_clean(lv_screen_active());
    parts_.reset();
}

void App::render_turn() {
    if (parts_) parts_->render();
}

App::Hook App::every(Millis ms, std::function<void()> fn) {
    const Hook hook = ++last_hook_;
    every_[hook] = Every{ms, std::move(fn), 0};
    tick(hook);
    return hook;
}

void App::tick(Hook hook) {
    const auto found = every_.find(hook);
    if (found == every_.end()) return;
    found->second.timer = seams_.clock->call_after(found->second.ms, [this, hook] {
        auto it = every_.find(hook);
        if (it == every_.end()) return;
        it->second.timer = 0;
        if (shut_down_) return;
        // A copy: `fn` may remove its own hook.
        const std::function<void()> fn = it->second.fn;
        fn();
        if (!shut_down_) tick(hook);
    });
}

App::Hook App::add_describer(std::function<void(Json*)> fn) {
    const Hook hook = ++last_hook_;
    describers_[hook] = std::move(fn);
    return hook;
}

App::Hook App::add_top_right(std::function<std::string()> text) {
    const Hook hook = ++last_hook_;
    top_right_[hook] = std::move(text);
    return hook;
}

void App::remove(Hook hook) {
    if (hook == 0) return;
    handlers_.erase(hook);
    describers_.erase(hook);
    top_right_.erase(hook);
    const auto found = every_.find(hook);
    if (found != every_.end()) {
        if (found->second.timer != 0) seams_.clock->cancel(found->second.timer);
        every_.erase(found);
    }
}

std::string App::bottom() const { return stack_.screens().front(); }

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
        if (std::find(open.begin(), open.end(), it->first) == open.end()) {
            closed_screens_.push_back(std::move(it->second));
            it = screens_.erase(it);
        }
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
    closed_screens_.push_back(std::move(screens_[now]));
    screens_.erase(now);
    if (stack_.top() == name) top()->shown();
    redraw();
}

void App::close_top() {
    const std::string closing = stack_.top();
    if (!stack_.close_top()) return;
    closed_screens_.push_back(std::move(screens_[closing]));
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

void App::when_no_overlay(std::function<void()> fn) {
    if (!panel_ && !help_open_) {
        fn();
        return;
    }
    after_overlay_.push_back(std::move(fn));
}

void App::notice(const std::string& id, const std::map<std::string, std::string>& about) {
    notices_.raise(id, message_words(id, about));
}

App::Hook App::on_action(const std::string& action, std::function<void(const KeyEvent&)> handler) {
    for (const auto& each : handlers_) {
        if (each.second.action == action) {
            // One handler for an action: a second would take the first one's keys without a word.
            std::fprintf(stderr, "sidplayzero: the action %s has a handler already; a second is refused\n",
                         action.c_str());
            return 0;
        }
    }
    const Hook hook = ++last_hook_;
    handlers_[hook] = Handler{action, std::move(handler)};
    return hook;
}

// ---- keys --------------------------------------------------------------------------------------------------

std::string App::place() {
    if (help_open_) return "help";
    if (panel_) return panel_->place();
    return top()->place();
}

bool App::typing() { return place_kind(place()) == PlaceKind::Typing; }

void App::key(int linux_code, bool pressed) {
    if (shut_down_) return;
    // SPEC-207: a key that turns the screen back on does nothing else, and nor does its release.
    if (pressed && key_filter_ && key_filter_()) {
        swallowed_.push_back(linux_code);
        return;
    }
    if (!pressed) {
        const auto found = std::find(swallowed_.begin(), swallowed_.end(), linux_code);
        if (found != swallowed_.end()) {
            swallowed_.erase(found);
            return;
        }
    }
    if (pressed) keys_read_.push_back(seams_.clock->now_ms());   // SPEC-112
    keys_.key(linux_code, pressed);
    closed_panel_.reset();
    closed_screens_.clear();
}

std::string App::top_right_text() {
    std::string text;
    const std::map<Hook, std::function<std::string()>> pieces = top_right_;   // a piece may remove a hook
    for (const auto& piece : pieces) {
        const std::string part = piece.second();
        if (part.empty()) continue;
        text += (text.empty() ? "" : "  ") + part;
    }
    return text;
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
        for (const auto& each : handlers_) {
            if (each.second.action != a) continue;
            const std::function<void(const KeyEvent&)> handler = each.second.fn;   // it may remove its hook
            handler(event);
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
        // A screen. SPEC-093: a key 4 to 8 with no label over it does nothing.
        if (event.key.size() == 1 && event.key[0] >= '4' && event.key[0] <= '8' &&
            top()->actions()[static_cast<std::size_t>(event.key[0] - '4')].empty()) {
            return;
        }
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
    // The seconds and the milliseconds of one reading of the wall clock (A12).
    const std::int64_t wall_ms = seams_.clock->wall_time_ms();
    const std::time_t seconds = static_cast<std::time_t>(wall_ms / 1000);
    std::tm when;
    localtime_r(&seconds, &when);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &when);
    char millis[8];
    std::snprintf(millis, sizeof millis, "%03d", static_cast<int>(wall_ms % 1000));
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
    closed_screens_.clear();
    if (!panel_ && !help_open_ && !after_overlay_.empty()) {
        std::vector<std::function<void()>> waiting;
        waiting.swap(after_overlay_);
        for (auto& fn : waiting) fn();
    }
    if (dirty_) draw();
    if (!keys_read_.empty() && key_timer_) {
        // SPEC-112: the first screen update after each key has been made.
        const Millis now = seams_.clock->now_ms();
        for (const Millis read : keys_read_) key_timer_(now - read);
    }
    keys_read_.clear();
}

bool App::settled() const {
    if (!ui_.empty() || dirty_) return false;
    if (!panel_ && !help_open_ && !after_overlay_.empty()) return false;
    for (const Worker* worker : {&quick_, &fetch_, &long_}) {
        if (worker->idle()) continue;
        // A job that waits for the fake clock, or for an answer a fake holds back, counts as settled.
        if (!(seams_.held && worker->running() && seams_.held(worker->id()))) return false;
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
    // SPEC-109: the mini player on the right, at most a third of the line.
    const std::string right = screen->name() != "playing" ? top_right_text() : "";
    const int right_w = right.empty() ? 0 : std::min(ui::text_width(right, ui::text_font()) + 2, ui::kWidth / 3);
    if (right_w > 0) {
        ui::make_label(root, "mini", right, ui::kWidth - 2 - right_w, 0, right_w, ui::kTopHeight,
                       ui::LabelStyle{ui::Align::Right, nullptr, false, false});
    }
    const int title_w = ui::kWidth - 4 - (position_w > 0 ? position_w + 6 : 0) - (right_w > 0 ? right_w + 6 : 0);
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
    const std::map<Hook, std::function<void(Json*)>> describers = describers_;   // one may remove a hook
    for (const auto& describer : describers) describer.second(&d);
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
