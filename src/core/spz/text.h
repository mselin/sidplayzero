// SPDX-License-Identifier: GPL-3.0-or-later
// Small helpers of text that the whole app shares: spaces at the ends, beginnings and endings, ASCII case,
// hexadecimal digits, and UTF-8. Every file of the app that works on text bytes uses these, so that two
// places never disagree on what a space, a tune's name or valid UTF-8 is.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace spz::text {

// What trim() takes away from a line of a file that may have been written with CR LF: spaces, tabs and CRs.
const char kLineSpaces[] = " \t\r";

// `text` without the bytes of `spaces` at its start and at its end. By default spaces and tabs.
std::string trim(const std::string& text, const char* spaces = " \t");
// The same, at the start only, or at the end only.
std::string trim_start(const std::string& text, const char* spaces = " \t");
std::string trim_end(const std::string& text, const char* spaces = " \t");

bool starts_with(const std::string& text, const std::string& start);
bool ends_with(const std::string& text, const std::string& ending);

// A-Z as a-z; every other byte as it is.
char ascii_lower(char c);
std::string ascii_lower(std::string text);
// Whether two texts are the same but for the case of A-Z.
bool same_ignoring_case(const std::string& a, const std::string& b);
// Whether a file's name makes it a tune: it ends in ".sid" in any case, with something before it or not
// (SPEC-041).
bool is_tune_name(const std::string& name);

// The value of a hexadecimal digit, in either case; -1 for any other byte.
int hex_value(char c);

// The valid UTF-8 character at text[i]: its length in bytes, 1 to 4, with its code point put in `code` if
// that is not null. 0 if the bytes there start no valid character: a stray or missing continuation byte,
// an overlong form, a surrogate, or a code point past U+10FFFF.
std::size_t utf8_char(const char* text, std::size_t size, std::size_t i, std::uint32_t* code = nullptr);
inline std::size_t utf8_char(const std::string& text, std::size_t i, std::uint32_t* code = nullptr) {
    return utf8_char(text.data(), text.size(), i, code);
}
// Whether bytes are valid UTF-8 throughout (utf8_char() finds a character at every step).
bool is_valid_utf8(const char* text, std::size_t size);
inline bool is_valid_utf8(const std::string& text) { return is_valid_utf8(text.data(), text.size()); }
// Adds the UTF-8 form of a code point of at most U+10FFFF to `out`.
void put_utf8(std::uint32_t code, std::string* out);
// Latin-1 to UTF-8: every byte is the code point of the same number.
std::string latin1_to_utf8(const std::string& text);
// How many characters UTF-8 text holds: its bytes that are not continuation bytes.
std::size_t utf8_length(const std::string& text);
// The first `characters` characters of UTF-8 text (all of it, if it has no more).
std::string utf8_prefix(const std::string& text, std::size_t characters);

}  // namespace spz::text
