// SPDX-License-Identifier: GPL-3.0-or-later
// What the app reads from a tune's file itself: the check of SPEC-203, the header's fields (SPEC-096) and
// the conversion of its text from Windows-1252 (SPEC-098).
#include "spz/tune.h"

#include <cstdint>

namespace spz {

namespace {

const std::size_t kHeaderV1 = 0x76;   // the data offset of a version 1 header
const std::size_t kHeaderV2 = 0x7C;   // and of versions 2 to 4

int word_at(const Bytes& file, std::size_t at) { return (file[at] << 8) | file[at + 1]; }

// The Unicode code points of Windows-1252's 0x80 to 0x9F; 0 where it leaves a byte undefined.
const std::uint16_t kHigh[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

void put_utf8(std::uint32_t code, std::string* out) {
    if (code < 0x80) {
        *out += static_cast<char>(code);
    } else if (code < 0x800) {
        *out += static_cast<char>(0xC0 | (code >> 6));
        *out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        *out += static_cast<char>(0xE0 | (code >> 12));
        *out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        *out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// A 32-byte text field of the header, which need not end in a zero byte.
std::string text_field(const Bytes& file, std::size_t at) {
    return cp1252_to_utf8(std::string(reinterpret_cast<const char*>(file.data()) + at, 32));
}

// The first run of exactly four digits in the text, else 0 (SPEC-160).
int first_year(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] < '0' || text[i] > '9') {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
        if (end - i == 4) return std::stoi(text.substr(i, 4));
        i = end;
    }
    return 0;
}

// A chip address byte of a version 3 or 4 header that names a chip: even, $D420 to $D7E0 or $DE00 to $DFE0.
bool names_chip(int address) {
    return address != 0 && (address & 1) == 0 && ((address >= 0x42 && address <= 0x7E) || (address >= 0xE0 && address <= 0xFE));
}

}  // namespace

std::string check_tune(const Bytes& file) {
    const std::string invalid = "MSG_INVALID_SID";
    // Every header is at least as long as a version 1 header, and the file is longer than its header.
    if (file.size() > kTuneSizeLimit || file.size() <= kHeaderV1) return invalid;
    const bool psid = file[0] == 'P' && file[1] == 'S' && file[2] == 'I' && file[3] == 'D';
    const bool rsid = file[0] == 'R' && file[1] == 'S' && file[2] == 'I' && file[3] == 'D';
    if (!psid && !rsid) return invalid;
    const int version = word_at(file, 4);
    if (version < (rsid ? 2 : 1) || version > 4) return invalid;
    const std::size_t offset = static_cast<std::size_t>(word_at(file, 6));
    if (offset != (version == 1 ? kHeaderV1 : kHeaderV2)) return invalid;
    if (file.size() <= offset) return invalid;
    const int songs = word_at(file, 0x0E);
    if (songs < 1 || songs > 256) return invalid;
    const int start = word_at(file, 0x10);
    if (start > songs) return invalid;
    return "";
}

bool read_tune_info(const Bytes& file, TuneInfo* out) {
    if (!check_tune(file).empty()) return false;
    TuneInfo info;
    const int version = word_at(file, 4);
    info.title = text_field(file, 0x16);
    info.author = text_field(file, 0x36);
    info.released = text_field(file, 0x56);
    info.year = first_year(info.released);
    info.songs = word_at(file, 0x0E);
    info.default_song = word_at(file, 0x10);
    if (info.default_song == 0) info.default_song = 1;
    if (version >= 2) {
        const int flags = word_at(file, 0x76);
        const bool rsid = file[0] == 'R';
        info.needs_basic = rsid && (flags & 0x02) != 0;
        switch ((flags >> 2) & 3) {
            case 1: info.clock = C64Clock::Pal; break;
            case 2: info.clock = C64Clock::Ntsc; break;
            default: info.clock = C64Clock::FromTune; break;   // unknown, or either
        }
        switch ((flags >> 4) & 3) {
            case 1: info.model = SidModel::Mos6581; break;
            case 2: info.model = SidModel::Mos8580; break;
            default: info.model = SidModel::FromTune; break;
        }
        if (version >= 3 && names_chip(file[0x7A])) {
            info.chips = 2;
            if (version >= 4 && names_chip(file[0x7B]) && file[0x7B] != file[0x7A]) info.chips = 3;
        }
    }
    *out = info;
    return true;
}

std::string cp1252_to_utf8(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte == 0) break;
        if (byte < 0x80) {
            out += c;
        } else if (byte < 0xA0) {
            const std::uint16_t code = kHigh[byte - 0x80];
            if (code == 0) out += '?';
            else put_utf8(code, &out);
        } else {
            put_utf8(byte, &out);   // Latin-1 and Unicode agree from 0xA0
        }
    }
    return out;
}

}  // namespace spz
