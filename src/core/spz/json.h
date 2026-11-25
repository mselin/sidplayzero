// SPDX-License-Identifier: GPL-3.0-or-later
// A small JSON value: enough to build the screen description (SPEC-254), write it out, and look a value
// up in it by a path such as "rows[2].text". An object keeps its keys in the order they were added, so
// that the same screen always gives the same bytes.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace spz {

class Json {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : kind_(Kind::Bool), bool_(value) {}
    Json(int value) : kind_(Kind::Number), number_(value) {}
    Json(std::int64_t value) : kind_(Kind::Number), number_(value) {}
    Json(std::uint64_t value) : kind_(Kind::Number), number_(static_cast<std::int64_t>(value)) {}
    Json(const char* value) : kind_(Kind::String), string_(value) {}
    Json(std::string value) : kind_(Kind::String), string_(std::move(value)) {}

    static Json array() { Json j; j.kind_ = Kind::Array; return j; }
    static Json object() { Json j; j.kind_ = Kind::Object; return j; }

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }

    // Array: adds an element. Object: sets a key, keeping its place if it was there.
    Json& push(Json value);
    Json& set(const std::string& key, Json value);

    std::size_t size() const { return kind_ == Kind::Array ? items_.size() : members_.size(); }
    const Json* at(std::size_t index) const;
    const Json* get(const std::string& key) const;

    // The value at a path: names joined by dots, with [n] for an array's element. nullptr if there is none.
    const Json* find(const std::string& path) const;

    // The JSON text. `indent` < 0 gives one line; otherwise that many spaces per level.
    std::string dump(int indent = -1) const;
    // What `expect` compares: a string's own characters, a number's digits, "true", "false" or "null";
    // for an array or an object, its one-line JSON text.
    std::string scalar_text() const;

private:
    void write(std::string* out, int indent, int depth) const;

    Kind kind_ = Kind::Null;
    bool bool_ = false;
    std::int64_t number_ = 0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};

}  // namespace spz
