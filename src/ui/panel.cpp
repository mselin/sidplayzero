// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "ui/draw.h"
#include "ui/screen.h"

namespace spz::ui {

namespace {

const int kPanelX = 6;
const int kPanelW = kWidth - 2 * kPanelX;
const int kTitleH = 20;
const int kLinesShown = (kContentHeight - kTitleH) / kRowHeight;   // five lines under the title

Json fields_of(const TextField& field) {
    Json one = Json::object();
    one.set("text", field.text());
    one.set("cursor", static_cast<std::int64_t>(field.cursor()));
    Json fields = Json::object();
    fields.set("field", one);
    return fields;
}

Json blank_panel(const std::string& id) {
    Json panel = Json::object();
    for (const char* key : {"id", "title", "text", "rows", "fields"}) panel.set(key, nullptr);
    panel.set("id", id);
    return panel;
}

// Text broken into lines no wider than `width`, at spaces where it can be, keeping the text's own breaks.
std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string word, line;
        bool any = false;
        while (words >> word) {
            any = true;
            const std::string longer = line.empty() ? word : line + " " + word;
            if (!line.empty() && text_width(longer, text_font()) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = longer;
            }
        }
        if (any) lines.push_back(line);
        else lines.push_back("");
    }
    return lines;
}

}  // namespace

Json Panel::describe() const {
    Json panel = blank_panel(id());
    const std::string t = title();
    if (!t.empty()) panel.set("title", t);
    return panel;
}

int Panel::draw_frame(lv_obj_t* box, const std::string& title) const {
    make_fill(box, kPanelX - 2, 0, kPanelW + 4, kContentHeight, colour_line());
    lv_obj_t* inside = make_fill(box, kPanelX - 1, 1, kPanelW + 2, kContentHeight - 2, colour_panel());
    (void)inside;
    if (title.empty()) return 0;
    make_label(box, "panel.title", title, kPanelX + 4, 0, kPanelW - 8, kTitleH, LabelStyle{Align::Left, nullptr, true});
    return kTitleH;
}

// ---- text ------------------------------------------------------------------------------------------------

TextPanel::TextPanel(Shell* shell, std::string id, std::string title, std::string text, std::function<void()> on_enter,
                     std::function<void()> on_esc)
    : shell_(shell), id_(std::move(id)), title_(std::move(title)), text_(std::move(text)),
      on_enter_(std::move(on_enter)), on_esc_(std::move(on_esc)) {}

std::vector<std::string> TextPanel::wrapped() const {
    if (lines_.empty()) lines_ = wrap(text_, kPanelW - 8);
    return lines_;
}

void TextPanel::draw(lv_obj_t* box) const {
    const int top = draw_frame(box, title_);
    const std::vector<std::string> lines = wrapped();
    const int shown = (kContentHeight - top) / kRowHeight;
    for (int i = 0; i < shown && first_ + static_cast<std::size_t>(i) < lines.size(); ++i) {
        make_label(box, "panel.line" + std::to_string(i), lines[first_ + static_cast<std::size_t>(i)], kPanelX + 4,
                   top + i * kRowHeight, kPanelW - 8, kRowHeight);
    }
}

void TextPanel::on_action(const KeyEvent& event) {
    const std::size_t count = wrapped().size();
    const std::size_t shown = static_cast<std::size_t>(title_.empty() ? kContentHeight / kRowHeight : kLinesShown);
    const std::size_t most = count > shown ? count - shown : 0;
    const std::string& a = event.action;
    if (a == "up") first_ = first_ > 0 ? first_ - 1 : 0;
    else if (a == "down") first_ = std::min(first_ + 1, most);
    else if (a == "page_up") first_ = first_ > shown ? first_ - shown : 0;
    else if (a == "page_down") first_ = std::min(first_ + shown, most);
    else if (a == "first") first_ = 0;
    else if (a == "last") first_ = most;
    else if (a == "close" || a == "accept" || a == "back") {
        std::function<void()> then = a == "back" ? on_esc_ : on_enter_;
        shell_->close_panel();                       // this panel is gone after this line
        if (then) then();
        return;
    } else {
        return;
    }
    shell_->redraw();
}

Json TextPanel::describe() const {
    Json panel = Panel::describe();
    panel.set("text", text_);
    return panel;
}

// ---- confirm ---------------------------------------------------------------------------------------------

ConfirmPanel::ConfirmPanel(Shell* shell, std::string question, std::function<void()> on_yes, std::function<void()> on_no)
    : shell_(shell), question_(std::move(question)), on_yes_(std::move(on_yes)), on_no_(std::move(on_no)) {}

void ConfirmPanel::draw(lv_obj_t* box) const {
    draw_frame(box, "");
    const std::vector<std::string> lines = wrap(question_, kPanelW - 8);
    for (std::size_t i = 0; i < lines.size() && i < 3; ++i) {
        make_label(box, "panel.line" + std::to_string(i), lines[i], kPanelX + 4, 10 + static_cast<int>(i) * kRowHeight,
                   kPanelW - 8, kRowHeight);
    }
    make_label(box, "panel.keys", "Enter: yes    ESC: no", kPanelX + 4, kContentHeight - 30, kPanelW - 8, kRowHeight,
               LabelStyle{Align::Centre, nullptr, true});
}

void ConfirmPanel::on_action(const KeyEvent& event) {
    if (event.action != "yes" && event.action != "back") return;
    std::function<void()> then = event.action == "yes" ? on_yes_ : on_no_;
    shell_->close_panel();
    if (then) then();
}

Json ConfirmPanel::describe() const {
    Json panel = Panel::describe();
    panel.set("text", question_);
    return panel;
}

// ---- prompt ----------------------------------------------------------------------------------------------

PromptPanel::PromptPanel(Shell* shell, std::string title, const std::string& text, std::size_t limit,
                         std::function<bool(const std::string&)> on_accept, std::function<void()> on_cancel)
    : shell_(shell), title_(std::move(title)), field_(limit, text), on_accept_(std::move(on_accept)),
      on_cancel_(std::move(on_cancel)) {}

void PromptPanel::draw(lv_obj_t* box) const {
    const int top = draw_frame(box, title_);
    const int field_x = kPanelX + 4, field_w = kPanelW - 8, field_y = top + 10;
    make_fill(box, field_x - 2, field_y - 1, field_w + 4, kRowHeight + 2, colour_line());
    make_fill(box, field_x - 1, field_y, field_w + 2, kRowHeight, colour_background());
    // The part of the text that shows: as much as fits, with the cursor in it.
    const std::string text = field_.text();
    // Work in characters: the text before the cursor, cut from its start until it fits.
    std::vector<std::string> chars;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n = std::min(n, text.size() - i);
        chars.push_back(text.substr(i, n));
        i += n;
    }
    std::size_t start = 0;
    auto joined = [&](std::size_t from, std::size_t to) {
        std::string s;
        for (std::size_t i = from; i < to && i < chars.size(); ++i) s += chars[i];
        return s;
    };
    const std::size_t cursor = std::min(field_.cursor(), chars.size());
    while (start < cursor && text_width(joined(start, cursor), text_font()) > field_w - 4) ++start;
    std::size_t end = cursor;
    while (end < chars.size() && text_width(joined(start, end + 1), text_font()) <= field_w - 4) ++end;
    make_label(box, "panel.field", joined(start, end), field_x, field_y, field_w, kRowHeight);
    const int cursor_x = field_x + text_width(joined(start, cursor), text_font());
    make_fill(box, cursor_x, field_y + 3, 1, kRowHeight - 6, colour_text());
}

void PromptPanel::on_action(const KeyEvent& event) {
    if (event.action == "accept") {
        std::function<bool(const std::string&)> accept = on_accept_;
        const std::string text = field_.text();
        if (!accept || accept(text)) shell_->close_panel();
        return;
    }
    if (event.action == "back") {
        std::function<void()> then = on_cancel_;
        shell_->close_panel();
        if (then) then();
        return;
    }
    if (field_.apply(event.action, event.character)) shell_->redraw();
}

Json PromptPanel::describe() const {
    Json panel = Panel::describe();
    panel.set("fields", fields_of(field_));
    return panel;
}

// ---- list ------------------------------------------------------------------------------------------------

ListPanel::ListPanel(Shell* shell, std::string id, std::string title, std::vector<Row> rows,
                     std::function<void(std::size_t)> on_choose)
    : shell_(shell), id_(std::move(id)), title_(std::move(title)), on_choose_(std::move(on_choose)) {
    list_.set_rows(std::move(rows));
    list_.set_shown(title_.empty() ? kContentHeight / kRowHeight : kLinesShown);
}

void ListPanel::draw(lv_obj_t* box) const {
    const int top = draw_frame(box, title_);
    lv_obj_t* area = make_area(box, kPanelX, top, kPanelW, kContentHeight - top);
    list_.draw(area, 0, kPanelW);
}

void ListPanel::on_action(const KeyEvent& event) {
    if (list_.on_action(event.action)) {
        shell_->redraw();
        return;
    }
    if (event.action.empty()) return;
    if (event.action == "back") {
        shell_->close_panel();
        return;
    }
    std::function<void(std::size_t)> choose = on_choose_;
    const std::size_t chosen = list_.highlighted();
    const bool any = list_.size() > 0;
    shell_->close_panel();
    if (choose && any) choose(chosen);
}

Json ListPanel::describe() const {
    Json panel = Panel::describe();
    panel.set("rows", list_.describe_rows());
    return panel;
}

}  // namespace spz::ui
