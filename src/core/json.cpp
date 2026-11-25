// SPDX-License-Identifier: GPL-3.0-or-later
#include "spz/json.h"

#include <cstdio>

namespace spz {

Json& Json::push(Json value) {
    kind_ = Kind::Array;
    items_.push_back(std::move(value));
    return items_.back();
}

Json& Json::set(const std::string& key, Json value) {
    kind_ = Kind::Object;
    for (auto& member : members_) {
        if (member.first == key) {
            member.second = std::move(value);
            return member.second;
        }
    }
    members_.emplace_back(key, std::move(value));
    return members_.back().second;
}

const Json* Json::at(std::size_t index) const {
    return kind_ == Kind::Array && index < items_.size() ? &items_[index] : nullptr;
}

const Json* Json::get(const std::string& key) const {
    if (kind_ != Kind::Object) return nullptr;
    for (const auto& member : members_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

const Json* Json::find(const std::string& path) const {
    const Json* here = this;
    std::size_t i = 0;
    while (here && i < path.size()) {
        if (path[i] == '.') {
            ++i;
        } else if (path[i] == '[') {
            const std::size_t close = path.find(']', i);
            if (close == std::string::npos || close == i + 1) return nullptr;
            std::size_t index = 0;
            for (std::size_t k = i + 1; k < close; ++k) {
                if (path[k] < '0' || path[k] > '9') return nullptr;
                index = index * 10 + static_cast<std::size_t>(path[k] - '0');
            }
            here = here->at(index);
            i = close + 1;
        } else {
            std::size_t end = i;
            while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
            here = here->get(path.substr(i, end - i));
            i = end;
        }
    }
    return here;
}

namespace {

void write_string(const std::string& text, std::string* out) {
    out->push_back('"');
    for (const unsigned char c : text) {
        switch (c) {
            case '"': *out += "\\\""; break;
            case '\\': *out += "\\\\"; break;
            case '\n': *out += "\\n"; break;
            case '\r': *out += "\\r"; break;
            case '\t': *out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                    *out += escaped;
                } else {
                    out->push_back(static_cast<char>(c));
                }
        }
    }
    out->push_back('"');
}

void new_line(std::string* out, int indent, int depth) {
    if (indent < 0) return;
    out->push_back('\n');
    out->append(static_cast<std::size_t>(indent * depth), ' ');
}

}  // namespace

void Json::write(std::string* out, int indent, int depth) const {
    switch (kind_) {
        case Kind::Null: *out += "null"; break;
        case Kind::Bool: *out += bool_ ? "true" : "false"; break;
        case Kind::Number: *out += std::to_string(number_); break;
        case Kind::String: write_string(string_, out); break;
        case Kind::Array:
            out->push_back('[');
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i) out->push_back(',');
                new_line(out, indent, depth + 1);
                items_[i].write(out, indent, depth + 1);
            }
            if (!items_.empty()) new_line(out, indent, depth);
            out->push_back(']');
            break;
        case Kind::Object:
            out->push_back('{');
            for (std::size_t i = 0; i < members_.size(); ++i) {
                if (i) out->push_back(',');
                new_line(out, indent, depth + 1);
                write_string(members_[i].first, out);
                *out += indent < 0 ? ":" : ": ";
                members_[i].second.write(out, indent, depth + 1);
            }
            if (!members_.empty()) new_line(out, indent, depth);
            out->push_back('}');
            break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    write(&out, indent, 0);
    if (indent >= 0) out.push_back('\n');
    return out;
}

std::string Json::scalar_text() const {
    switch (kind_) {
        case Kind::String: return string_;
        case Kind::Number: return std::to_string(number_);
        case Kind::Bool: return bool_ ? "true" : "false";
        case Kind::Null: return "null";
        default: return dump(-1);
    }
}

}  // namespace spz
