// SPDX-License-Identifier: GPL-3.0-or-later
// Panels (SPEC-114): a box over the screen that takes the keys until it is closed. One is open at a time;
// help can open over it. Only a key press opens one. The kinds that are always built are here; a part
// adds its own (`more-playing`, `pick-playlist` ...) as a ListPanel, or as a class derived from Panel.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"
#include "spz/json.h"
#include "spz/key_input.h"
#include "spz/text_field.h"
#include "ui/rows.h"

namespace spz::ui {

class Shell;

class Panel {
public:
    virtual ~Panel() = default;
    // The panel's name in SPEC-114 and in the screen description's `panel.id`.
    virtual std::string id() const = 0;
    // The place of the key map whose keys apply while it is open: its id, unless text is typed in it.
    virtual std::string place() const { return id(); }
    virtual bool typing() const { return false; }
    // Draws the panel in `box`, the content band (320×120).
    virtual void draw(lv_obj_t* box) const = 0;
    // A key's action in the panel's place, "back" (ESC) included. The panel closes itself through the
    // shell it was given.
    virtual void on_action(const KeyEvent& event) = 0;
    // SPEC-254's `panel`: id, title, text, rows, fields.
    virtual Json describe() const;

protected:
    // Draws the frame and the title that every panel has, and returns the top of what is under the title.
    int draw_frame(lv_obj_t* box, const std::string& title) const;
    virtual std::string title() const { return ""; }
};

// `text` and `notice-panel`: lines of text that Up and Down scroll. Enter closes a text panel; in the
// notice panel Enter accepts and ESC cancels.
class TextPanel : public Panel {
public:
    // `id` is "text" or "notice-panel". `on_enter` and `on_esc` run after the panel has closed.
    TextPanel(Shell* shell, std::string id, std::string title, std::string text,
              std::function<void()> on_enter = nullptr, std::function<void()> on_esc = nullptr);
    std::string id() const override { return id_; }
    void draw(lv_obj_t* box) const override;
    void on_action(const KeyEvent& event) override;
    Json describe() const override;

private:
    std::string title() const override { return title_; }
    std::vector<std::string> wrapped() const;

    Shell* shell_;
    std::string id_, title_, text_;
    std::function<void()> on_enter_, on_esc_;
    std::size_t first_ = 0;
    mutable std::vector<std::string> lines_;     // the text, wrapped to the panel's width
};

// `confirm`: a question. Enter is yes, ESC is no.
class ConfirmPanel : public Panel {
public:
    ConfirmPanel(Shell* shell, std::string question, std::function<void()> on_yes, std::function<void()> on_no = nullptr);
    std::string id() const override { return "confirm"; }
    void draw(lv_obj_t* box) const override;
    void on_action(const KeyEvent& event) override;
    Json describe() const override;

private:
    Shell* shell_;
    std::string question_;
    std::function<void()> on_yes_, on_no_;
};

// `prompt`: a text field (SPEC-099). Enter offers the text to `on_accept`, which returns true to close the
// panel and false to leave it open (a value that is not allowed, SPEC-101). ESC cancels.
class PromptPanel : public Panel {
public:
    PromptPanel(Shell* shell, std::string title, const std::string& text, std::size_t limit,
                std::function<bool(const std::string&)> on_accept, std::function<void()> on_cancel = nullptr);
    std::string id() const override { return "prompt"; }
    bool typing() const override { return true; }
    void draw(lv_obj_t* box) const override;
    void on_action(const KeyEvent& event) override;
    Json describe() const override;
    const TextField& field() const { return field_; }

private:
    std::string title() const override { return title_; }

    Shell* shell_;
    std::string title_;
    TextField field_;
    std::function<bool(const std::string&)> on_accept_;
    std::function<void()> on_cancel_;
};

// A list to choose from (`more-playing`, `pick-playlist`, `more-playlist`, `import` ...): Up and Down move,
// Enter (whatever the key map calls it there) chooses, ESC closes. `on_choose` runs after the panel has
// closed, with the row chosen.
class ListPanel : public Panel {
public:
    ListPanel(Shell* shell, std::string id, std::string title, std::vector<Row> rows,
              std::function<void(std::size_t)> on_choose);
    std::string id() const override { return id_; }
    void draw(lv_obj_t* box) const override;
    void on_action(const KeyEvent& event) override;
    Json describe() const override;

private:
    std::string title() const override { return title_; }

    Shell* shell_;
    std::string id_, title_;
    RowList list_;
    std::function<void(std::size_t)> on_choose_;
};

}  // namespace spz::ui
