// SPDX-License-Identifier: GPL-3.0-or-later
// TF-046: the simulator level's C++ canaries. Tests of the test framework, not of the app.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "doctest.h"
#include "lvgl.h"
#include "ui/describe.h"
#include "ui/png.h"
#include "ui/stub_screen.h"

TEST_CASE("TF-046 canary: a C++ test can ask LVGL what a script cannot: whether a font has a glyph") {
    lv_font_glyph_dsc_t glyph;
    CHECK(lv_font_get_glyph_dsc(&lv_font_montserrat_12, &glyph, 'A', 0));
    CHECK_FALSE(lv_font_get_glyph_dsc(&lv_font_montserrat_12, &glyph, 0x4E2D, 0));   // a Chinese character
}

TEST_CASE("TF-046 canary: a C++ test of the simulator level built to fail [canary-fails]") {
    lv_font_glyph_dsc_t glyph;
    CHECK(lv_font_get_glyph_dsc(&lv_font_montserrat_12, &glyph, 0x4E2D, 0));
}

TEST_CASE("TF-043 the stub's screen gives the description of SPEC-254's empty screen") {
    spz::ui::StubScreen screen("/music/C64Music");
    const spz::Json d = screen.describe();
    CHECK(d.find("screen")->scalar_text() == "empty");
    CHECK(d.find("rows[0].text")->scalar_text() == "Get HVSC");
    CHECK(d.find("rows[0].kind")->scalar_text() == "action");
    CHECK(d.find("actions")->size() == 5);
    CHECK(d.find("text")->scalar_text().find("/music/C64Music") != std::string::npos);
    const spz::Json* boxes = d.find("boxes");
    REQUIRE(boxes != nullptr);
    bool found_title = false;
    for (std::size_t i = 0; i < boxes->size(); ++i) {
        if (boxes->at(i)->find("id")->scalar_text() == "title") {
            found_title = true;
            CHECK(boxes->at(i)->find("band")->scalar_text() == "top");
        }
    }
    CHECK(found_title);
}

TEST_CASE("TF-044 a picture of the screen is written as a PNG of 320 by 170") {
    spz::ui::StubScreen screen("/music/C64Music");
    std::vector<std::uint8_t> rgb;
    int width = 0, height = 0;
    REQUIRE(spz::ui::snapshot_rgb(&rgb, &width, &height));
    CHECK(width == 320);
    CHECK(height == 170);
    char name[] = "/tmp/spz-picture-XXXXXX";
    const int descriptor = mkstemp(name);
    REQUIRE(descriptor >= 0);
    close(descriptor);
    const std::string path = name;
    REQUIRE(spz::ui::write_png(path, rgb, width, height));
    std::FILE* file = std::fopen(path.c_str(), "rb");
    REQUIRE(file != nullptr);
    unsigned char head[8] = {};
    CHECK(std::fread(head, 1, 8, file) == 8);
    std::fclose(file);
    std::remove(path.c_str());
    CHECK(head[1] == 'P');
    CHECK(head[2] == 'N');
    CHECK(head[3] == 'G');
}
