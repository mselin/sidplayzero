// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-010: the fonts built into the program have a glyph for every printable character of Windows-1252,
// which is what the text of a tune's header is made of (SPEC-098). A script cannot ask LVGL for a glyph,
// so this is one of the simulator level's C++ tests. That the characters reach the screen is
// tests/simulator/structure_fonts.script.
#include <cstdint>
#include <vector>

#include "doctest.h"
#include "lvgl.h"
#include "ui/app_fonts.h"

namespace {

// The 218 printable characters of Windows-1252, as Unicode: 0x20 to 0x7E, the 27 that it has at 0x80 to
// 0x9F (five bytes there are undefined), and 0xA0 to 0xFF.
std::vector<std::uint32_t> printable_windows_1252() {
    static const std::uint32_t at_80[32] = {
        0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,   // € ‚ ƒ „ … † ‡
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,        // ˆ ‰ Š ‹ Œ Ž
        0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,   // ‘ ’ “ ” • – —
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};  // ˜ ™ š › œ ž Ÿ
    std::vector<std::uint32_t> all;
    for (std::uint32_t c = 0x20; c <= 0x7E; ++c) all.push_back(c);
    for (const std::uint32_t c : at_80) {
        if (c != 0) all.push_back(c);
    }
    for (std::uint32_t c = 0xA0; c <= 0xFF; ++c) all.push_back(c);
    return all;
}

// How many of the characters the font has no glyph of its own for.
int missing_glyphs(const lv_font_t* font, const std::vector<std::uint32_t>& characters) {
    int missing = 0;
    for (const std::uint32_t character : characters) {
        lv_font_glyph_dsc_t glyph;
        const bool found = lv_font_get_glyph_dsc(font, &glyph, character, 0);
        CAPTURE(character);
        CHECK(found);
        if (found) CHECK_FALSE(glyph.is_placeholder);          // a box drawn in place of a glyph is not one
        if (!found || glyph.is_placeholder) ++missing;
    }
    return missing;
}

}  // namespace

TEST_CASE("SPEC-010 the 12-pixel font has a glyph for each of the 218 printable Windows-1252 characters") {
    const std::vector<std::uint32_t> characters = printable_windows_1252();
    REQUIRE(characters.size() == 218);
    const lv_font_t* font = spz::ui::font_12();
    REQUIRE(font != nullptr);
    CHECK(missing_glyphs(font, characters) == 0);
}

TEST_CASE("SPEC-010 the 14-pixel font has a glyph for each of the 218 printable Windows-1252 characters") {
    const std::vector<std::uint32_t> characters = printable_windows_1252();
    REQUIRE(characters.size() == 218);
    const lv_font_t* font = spz::ui::font_14();
    REQUIRE(font != nullptr);
    CHECK(missing_glyphs(font, characters) == 0);
    CHECK(font != spz::ui::font_12());                         // two fonts, not one under two names
}
