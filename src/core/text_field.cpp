// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/text_field.h"

#include "spz/not_built.h"

namespace spz {

TextField::TextField(std::size_t limit, const std::string& text) : limit_(limit), text_(text) {}

bool TextField::apply(const std::string&, char32_t) { SPZ_NOT_BUILT("SPEC-099"); }
std::string TextField::text() const { SPZ_NOT_BUILT("SPEC-099"); }
std::size_t TextField::length() const { SPZ_NOT_BUILT("SPEC-099"); }
std::size_t TextField::cursor() const { SPZ_NOT_BUILT("SPEC-099"); }
std::size_t TextField::limit() const {
    (void)limit_;
    SPZ_NOT_BUILT("SPEC-099");
}

}  // namespace spz
