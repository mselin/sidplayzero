// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw.h"

#include <cstdint>
#include <vector>

#include "ui/app_fonts.h"

namespace spz::ui {

namespace {

struct Record {
    std::string id;
    std::string full_text;   // the whole text, before it was cut to fit
    int full_width = 0;
    bool cut = false;
    bool scrolls = false;
};

// The marker of a label made here: its user data points at a Record, freed when the label goes.
const std::uint32_t kMagic = 0x5350'5A4Cu;
struct Owned {
    std::uint32_t magic = kMagic;
    Record record;
};

void forget(lv_event_t* event) {
    lv_obj_t* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
    Owned* owned = static_cast<Owned*>(lv_obj_get_user_data(object));
    lv_obj_set_user_data(object, nullptr);
    delete owned;
}

// The next character of UTF-8 text starting at `i`: how many bytes it has.
std::size_t char_bytes(const std::string& text, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    return i + n <= text.size() ? n : text.size() - i;
}

bool has_glyph(const lv_font_t* font, std::uint32_t letter) {
    lv_font_glyph_dsc_t glyph;
    return lv_font_get_glyph_dsc(font, &glyph, letter, 0) && glyph.adv_w > 0;
}

}  // namespace

lv_color_t colour_background() { return lv_color_hex(0x101014); }
lv_color_t colour_text() { return lv_color_hex(0xE8E8E8); }
lv_color_t colour_dim() { return lv_color_hex(0x9A9AA4); }
lv_color_t colour_highlight() { return lv_color_hex(0x2E5AAC); }
lv_color_t colour_panel() { return lv_color_hex(0x24242C); }
lv_color_t colour_line() { return lv_color_hex(0x6A6A78); }

const lv_font_t* text_font() { return spz::ui::font_12(); }

const lv_font_t* title_font() { return spz::ui::font_14(); }

int text_width(const std::string& text, const lv_font_t* font) {
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

lv_obj_t* make_label(lv_obj_t* parent, const std::string& id, const std::string& text, int x, int y, int w, int h,
                     const LabelStyle& style) {
    const lv_font_t* font = style.font ? style.font : text_font();
    Owned* owned = new Owned();
    owned->record.id = id;
    owned->record.full_text = text;
    owned->record.full_width = text_width(text, font);
    std::string shown = text;
    if (style.scroll >= 0 && owned->record.full_width > w) {
        // SPEC-098: the whole text, moved within a box of its own that clips it.
        owned->record.cut = true;
        owned->record.scrolls = true;
        lv_obj_t* box = make_area(parent, x, y, w, h);
        lv_obj_t* label = lv_label_create(box);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_pos(label, -style.scroll, 0);
        lv_obj_set_size(label, owned->record.full_width + 2, h);
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, style.dim ? colour_dim() : colour_text(), LV_PART_MAIN);
        const int pad = (h - static_cast<int>(lv_font_get_line_height(font))) / 2;
        if (pad > 0) lv_obj_set_style_pad_top(label, pad, LV_PART_MAIN);
        lv_label_set_text(label, text.c_str());
        lv_obj_set_user_data(label, owned);
        lv_obj_add_event_cb(label, forget, LV_EVENT_DELETE, nullptr);
        return label;
    }
    if (owned->record.full_width > w) {
        owned->record.cut = true;
        const std::string ellipsis = has_glyph(font, 0x2026) ? "\xE2\x80\xA6" : "...";
        std::vector<std::string> chars;
        for (std::size_t i = 0; i < text.size();) {
            const std::size_t n = char_bytes(text, i);
            chars.push_back(text.substr(i, n));
            i += n;
        }
        std::string kept;
        if (style.cut_start) {
            for (std::size_t i = chars.size(); i > 0; --i) {
                const std::string more = chars[i - 1] + kept;
                if (text_width(ellipsis + more, font) > w) break;
                kept = more;
            }
            shown = ellipsis + kept;
        } else {
            for (const std::string& c : chars) {
                const std::string more = kept + c;
                if (text_width(more + ellipsis, font) > w) break;
                kept = more;
            }
            shown = kept + ellipsis;
        }
    }
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, style.dim ? colour_dim() : colour_text(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, style.align == Align::Centre  ? LV_TEXT_ALIGN_CENTER
                                       : style.align == Align::Right ? LV_TEXT_ALIGN_RIGHT
                                                                     : LV_TEXT_ALIGN_LEFT,
                                LV_PART_MAIN);
    // Centre the line of text in the box's height.
    const int pad = (h - static_cast<int>(lv_font_get_line_height(font))) / 2;
    if (pad > 0) lv_obj_set_style_pad_top(label, pad, LV_PART_MAIN);
    lv_label_set_text(label, shown.c_str());
    lv_obj_set_user_data(label, owned);
    lv_obj_add_event_cb(label, forget, LV_EVENT_DELETE, nullptr);
    return label;
}

lv_obj_t* make_fill(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t colour) {
    lv_obj_t* fill = lv_obj_create(parent);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, x, y);
    lv_obj_set_size(fill, w, h);
    lv_obj_set_style_bg_color(fill, colour, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    return fill;
}

lv_obj_t* make_area(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* area = lv_obj_create(parent);
    lv_obj_remove_style_all(area);
    lv_obj_set_pos(area, x, y);
    lv_obj_set_size(area, w, h);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    return area;
}

bool label_record(lv_obj_t* label, std::string* id, int* full_width, bool* cut, std::string* full_text) {
    const Owned* owned = static_cast<const Owned*>(lv_obj_get_user_data(label));
    if (owned == nullptr || owned->magic != kMagic) return false;
    *id = owned->record.id;
    *full_width = owned->record.full_width;
    *cut = owned->record.cut;
    if (full_text) *full_text = owned->record.full_text;
    return true;
}

bool label_scrolls(lv_obj_t* label) {
    const Owned* owned = static_cast<const Owned*>(lv_obj_get_user_data(label));
    return owned != nullptr && owned->magic == kMagic && owned->record.scrolls;
}

int scroll_offset(int overflow, std::int64_t now_ms) {
    if (overflow <= 0) return 0;
    const std::int64_t rest = 1000;
    const std::int64_t move = static_cast<std::int64_t>(overflow) * 1000 / 25;   // 25 pixels a second
    const std::int64_t cycle = rest + move + rest;
    const std::int64_t t = now_ms % cycle;
    if (t < rest) return 0;
    if (t < rest + move) return static_cast<int>((t - rest) * 25 / 1000);
    return overflow;
}

}  // namespace spz::ui
