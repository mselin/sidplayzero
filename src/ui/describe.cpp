// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/describe.h"

#include "ui/draw.h"

#include <algorithm>
#include <string>
#include <vector>

namespace spz::ui {

Json blank_description() {
    Json d = Json::object();
    for (const char* field : {"screen", "stack", "place", "title", "position", "mini", "rows", "list", "detail",
                              "actions", "typing", "fields", "panel", "help", "notice", "player", "lines", "voices",
                              "job", "text", "boxes"}) {
        d.set(field, nullptr);
    }
    return d;
}

namespace {

struct Box {
    std::string id, band, text;
    int x, y, w, h, text_x, text_w;
    bool clipped;
};

void collect(lv_obj_t* object, std::vector<Box>* boxes) {
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(object, &lv_label_class)) {
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        const char* text = lv_label_get_text(object);
        lv_point_t size;
        lv_text_get_size(&size, text, lv_obj_get_style_text_font(object, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(object, LV_PART_MAIN),
                         lv_obj_get_style_text_line_space(object, LV_PART_MAIN), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        Box box;
        int full_width = 0;
        bool cut = false;
        const bool recorded = label_record(object, &box.id, &full_width, &cut);
        box.text = text ? text : "";
        box.x = area.x1;
        box.y = area.y1;
        box.w = area.x2 - area.x1 + 1;
        box.h = area.y2 - area.y1 + 1;
        box.band = box.y <= kTopLineBottom ? "top" : box.y <= kContentBottom ? "content" : "actions";
        box.text_x = box.x;
        box.text_w = recorded ? std::max(full_width, static_cast<int>(size.x)) : static_cast<int>(size.x);
        box.clipped = cut || box.text_w > box.w;
        boxes->push_back(box);
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t i = 0; i < children; ++i) collect(lv_obj_get_child(object, static_cast<std::int32_t>(i)), boxes);
}

}  // namespace

void describe_drawn(Json* description) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_update_layout(screen);
    std::vector<Box> boxes;
    collect(screen, &boxes);
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    Json list = Json::array();
    std::string text;
    for (const Box& box : boxes) {
        Json item = Json::object();
        item.set("id", box.id);
        item.set("band", box.band);
        item.set("x", box.x);
        item.set("y", box.y);
        item.set("w", box.w);
        item.set("h", box.h);
        item.set("text", box.text);
        item.set("text_x", box.text_x);
        item.set("text_w", box.text_w);
        item.set("clipped", box.clipped);
        list.push(item);
        if (!box.text.empty()) {
            if (!text.empty()) text += "\n";
            text += box.text;
        }
    }
    description->set("boxes", list);
    description->set("text", text);
}

}  // namespace spz::ui
