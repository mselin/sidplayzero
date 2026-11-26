// SPDX-License-Identifier: GPL-3.0-or-later
// Writing a picture of the screen as a PNG file (SPEC-108, and the tests' `shot`, SPEC-255).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spz::ui {

// What LVGL has drawn on the active screen, as rows of red, green and blue bytes. False if LVGL could not.
bool snapshot_rgb(std::vector<std::uint8_t>* rgb, int* width, int* height);

// 8-bit RGB as the bytes of a PNG file; empty if the sizes do not agree.
std::vector<std::uint8_t> encode_png(const std::vector<std::uint8_t>& rgb, int width, int height);
// Writes 8-bit RGB as a PNG. The file is not compressed: the pictures are small and it keeps this short.
bool write_png(const std::string& path, const std::vector<std::uint8_t>& rgb, int width, int height);

}  // namespace spz::ui
