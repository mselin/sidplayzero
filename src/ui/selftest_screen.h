// SPDX-License-Identifier: GPL-3.0-or-later
// The self-test screen (SPEC-260): opened over the browser by the device test, never by a key. It shows the
// step that runs and its progress as lines of text, and "Done. Hold ESC to leave." when the step is done.
// It has no keys of its own: the groups of the key map apply, except during the key step, whose place is
// `key-step`, where every key is only recorded. What it shows is the self-test's, which the app's
// composition runs (src/app/compose_selftest.cpp) and gives the screen through Services::selftest.
#pragma once

#include <string>
#include <vector>

#include "ui/screen.h"

namespace spz::ui {

// What the self-test screen shows: the composition's self-test, seen from the screen.
class SelfTestView {
public:
    virtual ~SelfTestView() = default;
    // The lines of text, top to bottom; at most kRowsShown - 1, so that a notice covers none of them.
    virtual std::vector<std::string> lines() const = 0;
    // "key-step" while the key step asks for keys; else "selftest".
    virtual std::string place() const = 0;
};

// The words the screen ends a step with.
extern const char* const kSelfTestDone;     // "Done. Hold ESC to leave."

class SelfTestScreen : public Screen {
public:
    explicit SelfTestScreen(Shell* shell);
    std::string name() const override { return "selftest"; }
    std::string place() const override;
    // Only while a device test asks for a step.
    bool ready() override { return shell()->services().selftest != nullptr; }
    std::string title() const override { return "Self-test"; }
    void draw(lv_obj_t* content) override;
    bool on_action(const KeyEvent& event) override;
    void describe(Json* description) const override;
    void tick() override;

private:
    std::vector<std::string> now() const;
    std::vector<std::string> drawn_;
};

}  // namespace spz::ui
