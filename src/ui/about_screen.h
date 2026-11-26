// SPDX-License-Identifier: GPL-3.0-or-later
// The About screen (SPEC-103): the app and its version, the libraries, the licence and where it is
// installed, and where the source is, as lines that Up and Down scroll. "Notice" opens a text panel with
// SPEC-152's notice and, once HVSC is set up, the copyright section of its DOCUMENTS/HVSC.txt.
#pragma once

#include <string>
#include <vector>

#include "ui/screen.h"

namespace spz::ui {

// SPEC-103: the lines, in order. The second names the libraries, as `sidplayzero --version` prints it.
std::vector<std::string> about_lines(const std::vector<std::string>& libraries);

// The copyright section of HVSC's DOCUMENTS/HVSC.txt: the lines under the heading "COPYRIGHT ...", up to
// the next heading, without their line endings and the empty lines at either end. "" if there is none.
std::string hvsc_copyright_section(const std::string& file);

class AboutScreen : public Screen {
public:
    explicit AboutScreen(Shell* shell);
    std::string name() const override { return "about"; }
    // It needs the libraries' versions, read from the libraries by the engine of `platform`, and the
    // notice of SPEC-152.
    bool ready() override;
    std::string title() const override { return "About"; }
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;

private:
    void show_notice();

    std::vector<std::string> lines_;
    std::size_t first_ = 0;
};

}  // namespace spz::ui
