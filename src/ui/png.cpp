// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/png.h"

#include <algorithm>
#include <cstdio>

#include "lvgl.h"

namespace spz::ui {

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        made = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out->push_back(static_cast<std::uint8_t>(value >> shift));
}

void chunk(std::vector<std::uint8_t>* out, const char type[4], const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out->insert(out->end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

}  // namespace

bool snapshot_rgb(std::vector<std::uint8_t>* rgb, int* width, int* height) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_update_layout(screen);
    lv_draw_buf_t* picture = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
    if (picture == nullptr) return false;
    *width = static_cast<int>(picture->header.w);
    *height = static_cast<int>(picture->header.h);
    rgb->clear();
    rgb->reserve(static_cast<std::size_t>(*width) * static_cast<std::size_t>(*height) * 3);
    for (int y = 0; y < *height; ++y) {
        const std::uint8_t* row = picture->data + static_cast<std::size_t>(y) * picture->header.stride;
        for (int x = 0; x < *width; ++x) {   // stored as blue, green, red, unused
            rgb->push_back(row[x * 4 + 2]);
            rgb->push_back(row[x * 4 + 1]);
            rgb->push_back(row[x * 4 + 0]);
        }
    }
    lv_draw_buf_destroy(picture);
    return true;
}

bool write_png(const std::string& path, const std::vector<std::uint8_t>& rgb, int width, int height) {
    if (width <= 0 || height <= 0 || rgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3) return false;
    // Each row gets a filter byte of 0 in front.
    std::vector<std::uint8_t> raw;
    raw.reserve(rgb.size() + static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto begin = rgb.begin() + static_cast<std::ptrdiff_t>(y) * width * 3;
        raw.insert(raw.end(), begin, begin + static_cast<std::ptrdiff_t>(width) * 3);
    }
    // A zlib stream of "stored" blocks, each at most 65,535 bytes, with its Adler-32 at the end.
    std::vector<std::uint8_t> zlib = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::size_t at = 0; at < raw.size();) {
        const std::size_t size = std::min<std::size_t>(65535, raw.size() - at);
        zlib.push_back(at + size == raw.size() ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(size & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>(size >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~size & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>((~size >> 8) & 0xFF));
        for (std::size_t i = 0; i < size; ++i) {
            a = (a + raw[at + i]) % 65521;
            b = (b + a) % 65521;
        }
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size;
    }
    put32(&zlib, (b << 16) | a);

    std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    put32(&header, static_cast<std::uint32_t>(width));
    put32(&header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});   // 8 bits, RGB, no interlace
    chunk(&file, "IHDR", header);
    chunk(&file, "IDAT", zlib);
    chunk(&file, "IEND", {});

    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) return false;
    const bool ok = std::fwrite(file.data(), 1, file.size(), out) == file.size();
    return std::fclose(out) == 0 && ok;
}

}  // namespace spz::ui
