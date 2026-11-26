// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/about_screen.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <sstream>

#include "spz/notice_gate.h"
#include "spz/version.h"
#include "ui/draw.h"
#include "ui/panel.h"

namespace spz::ui {

namespace {

const std::size_t kHvscTxtLimit = 1024 * 1024;       // HVSC's HVSC.txt is some 60 KiB

bool all_dashes(const std::string& line) {
    return !line.empty() && line.find_first_not_of('-') == std::string::npos;
}

std::string trimmed(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    return line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
}

}  // namespace

std::vector<std::string> about_lines(const std::vector<std::string>& libraries) {
    std::string named;
    for (const std::string& library : libraries) named += (named.empty() ? "" : ", ") + library;
    return {
        version_line(),
        named,
        "Licence: GPL-3.0-or-later",
        "/usr/share/doc/sidplayzero/LICENSE",
        "Source: github.com/mselin/sidplayzero",
    };
}

std::string hvsc_copyright_section(const std::string& file) {
    std::vector<std::string> lines;
    std::istringstream in(file);
    std::string line;
    while (std::getline(in, line)) lines.push_back(trimmed(line));
    // A heading is a line of words between two lines of dashes.
    std::size_t start = lines.size();
    for (std::size_t i = 1; i + 1 < lines.size(); ++i) {
        if (all_dashes(lines[i - 1]) && all_dashes(lines[i + 1]) && lines[i].find("COPYRIGHT") != std::string::npos) {
            start = i + 2;
            break;
        }
    }
    std::vector<std::string> section;
    for (std::size_t i = start; i < lines.size(); ++i) {
        if (all_dashes(lines[i])) break;          // the line over the next heading
        section.push_back(lines[i]);
    }
    while (!section.empty() && section.front().empty()) section.erase(section.begin());
    while (!section.empty() && section.back().empty()) section.pop_back();
    std::string text;
    for (const std::string& kept : section) text += (text.empty() ? "" : "\n") + kept;
    return text;
}

AboutScreen::AboutScreen(Shell* shell) : Screen(shell), lines_(about_lines(shell->services().libraries)) {}

bool AboutScreen::ready() {
    if (shell()->services().libraries.empty()) return false;     // the engine of `platform` is not built yet
    try {
        (void)hvsc_notice_text();
        return true;
    } catch (const std::exception&) {
        return false;                                            // SPEC-152's notice is not built yet
    }
}

void AboutScreen::draw(lv_obj_t* content) {
    for (std::size_t i = first_; i < lines_.size() && i < first_ + static_cast<std::size_t>(kRowsShown); ++i) {
        make_label(content, "line" + std::to_string(i - first_), lines_[i], 4,
                   static_cast<int>(i - first_) * kRowHeight, kWidth - 8, kRowHeight);
    }
}

void AboutScreen::show_notice() {
    Shell* frame = shell();
    const std::string path = frame->services().collection + "/DOCUMENTS/HVSC.txt";
    Files* files = frame->services().files;
    const std::string notice = hvsc_notice_text();
    // The file is read on the quick worker (SPEC-022); the panel opens when it has been.
    frame->quick()->post([frame, files, path, notice] {
        std::string section;
        std::vector<std::uint8_t> bytes;
        if (files != nullptr && files->read(path, kHvscTxtLimit, &bytes) == FileStatus::Ok) {
            section = hvsc_copyright_section(std::string(bytes.begin(), bytes.end()));
        }
        frame->ui()->post([frame, notice, section] {
            const std::string text = section.empty() ? notice : notice + "\n\n" + section;
            frame->open_panel(std::make_unique<TextPanel>(frame, "text", "HVSC's notice", text));
        });
    });
}

bool AboutScreen::on_action(const KeyEvent& event) {
    const std::string& a = event.action;
    const std::size_t most = lines_.size() > static_cast<std::size_t>(kRowsShown) ? lines_.size() - kRowsShown : 0;
    if (a == "up" || a == "page_up" || a == "first") first_ = a == "up" && first_ > 0 ? first_ - 1 : 0;
    else if (a == "down") first_ = std::min(first_ + 1, most);
    else if (a == "page_down" || a == "last") first_ = most;
    else if (a == "back") shell()->close_top();
    else if (a == "show_notice") show_notice();
    else return false;
    shell()->redraw();
    return true;
}

void AboutScreen::describe(Json* description) const {
    Json lines = Json::array();
    for (std::size_t i = first_; i < lines_.size() && i < first_ + static_cast<std::size_t>(kRowsShown); ++i) {
        lines.push(lines_[i]);
    }
    description->set("lines", lines);
}

}  // namespace spz::ui
