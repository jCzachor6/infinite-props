#pragma once

// Minimal JSON helpers shared by the CLI tools: ipgen's output writer and
// the Json documents of ipc --describe-language and ipval --json.
// Hand-rolled: no external dependencies, deterministic output.

#include "iprop/Outputs.hpp"
#include "iprop/VM.hpp"

#include <cmath>
#include <concepts>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace iprop {
namespace tools {

// Formats an f32 with round-trip precision; non-finite values are null
// (JSON has no infinities).
inline std::string jsonNumber(f32 value, bool bits = false) {
    if (bits) {
        u32 encoded = 0;
        std::memcpy(&encoded, &value, sizeof(encoded));
        return std::to_string(encoded);
    }
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

inline std::string jsonEscape(std::string_view text) {
    std::ostringstream stream;
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    for (const char c : text) {
        switch (c) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\n': stream << "\\n"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(static_cast<unsigned char>(c)) << std::dec;
            } else {
                stream << c;
            }
        }
    }
    return stream.str();
}

// Length of the well-formed UTF-8 sequence at `at`, or, when `valid` is
// false, of its maximal ill-formed subpart (Unicode 3.9, at least one byte).
inline std::pair<std::size_t, bool> utf8Sequence(std::string_view text, std::size_t at) {
    const auto byte = [&](std::size_t i) -> unsigned {
        return at + i < text.size() ? static_cast<unsigned char>(text[at + i]) : 0u;
    };
    const unsigned lead = byte(0);
    std::size_t length = 0;
    unsigned low = 0x80, high = 0xbf; // range of the second byte
    if (lead >= 0xc2 && lead <= 0xdf) {
        length = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3;
        if (lead == 0xe0) low = 0xa0;
        if (lead == 0xed) high = 0x9f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4;
        if (lead == 0xf0) low = 0x90;
        if (lead == 0xf4) high = 0x8f;
    } else {
        return {1, false};
    }
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned next = byte(i);
        if (next < low || next > high) return {i, false};
        low = 0x80;
        high = 0xbf;
    }
    return {length, true};
}

// Appends a quoted JSON string. Ill-formed UTF-8 becomes U+FFFD, one per
// maximal subpart, so any byte string (a file name) yields valid JSON.
inline void appendJsonString(std::string& out, std::string_view text) {
    out += '"';
    for (std::size_t at = 0; at < text.size();) {
        const auto c = static_cast<unsigned char>(text[at]);
        if (c >= 0x80) {
            const auto [length, valid] = utf8Sequence(text, at);
            if (valid) out += text.substr(at, length);
            else out += "\xEF\xBF\xBD";
            at += length;
            continue;
        }
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out += static_cast<char>(c);
            }
        }
        ++at;
    }
    out += '"';
}

// An insertion-ordered JSON document. Scalars are serialized on
// construction; dump() is compact, or pretty-printed with `indent` spaces.
class Json {
public:
    Json() = default; // null
    Json(bool value) : text_(value ? "true" : "false") {}
    template<std::integral T>
    Json(T value) : text_(std::to_string(value)) {}
    Json(const char* text) : Json(std::string_view(text)) {}
    Json(const std::string& text) : Json(std::string_view(text)) {}
    Json(std::string_view text) : text_() { appendJsonString(text_, text); }

    static Json array(std::initializer_list<Json> items = {}) {
        Json json(Kind::Array);
        json.values_.assign(items.begin(), items.end());
        return json;
    }
    static Json object(std::initializer_list<std::pair<std::string, Json>> members = {});

    // An object member, appended as null when absent.
    Json& operator[](std::string_view key) {
        require(Kind::Object);
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == key) return values_[i];
        keys_.emplace_back(key);
        return values_.emplace_back();
    }

    void push_back(Json value) {
        require(Kind::Array);
        values_.push_back(std::move(value));
    }

    std::string dump(int indent = -1) const {
        std::string out;
        write(out, indent, 0);
        return out;
    }

private:
    enum class Kind { Scalar, Array, Object };
    explicit Json(Kind kind) : kind_(kind) {}

    Kind kind_ = Kind::Scalar;
    std::string text_ = "null";     // a serialized scalar
    std::vector<std::string> keys_; // object member names
    std::vector<Json> values_;      // array elements or object member values

    void require(Kind kind) const {
        if (kind_ != kind) throw std::logic_error("JSON value has another type");
    }

    void write(std::string& out, int indent, int depth) const {
        if (kind_ == Kind::Scalar) {
            out += text_;
            return;
        }
        const bool object = kind_ == Kind::Object;
        const auto newline = [&](int level) {
            if (indent < 0) return;
            out += '\n';
            out.append(static_cast<std::size_t>(indent * level), ' ');
        };
        out += object ? '{' : '[';
        for (std::size_t i = 0; i < values_.size(); ++i) {
            if (i != 0) out += ',';
            newline(depth + 1);
            if (object) {
                appendJsonString(out, keys_[i]);
                out += indent < 0 ? ":" : ": ";
            }
            values_[i].write(out, indent, depth + 1);
        }
        if (!values_.empty()) newline(depth);
        out += object ? '}' : ']';
    }
};

inline Json Json::object(std::initializer_list<std::pair<std::string, Json>> members) {
    Json json(Kind::Object);
    for (const auto& [key, value] : members) json[key] = value;
    return json;
}

// Writes one named output as a JSON object.
inline void writeOutputJson(std::ostream& out, const NamedOutput& output, bool bits = false) {
    out << "{\"name\":\"" << jsonEscape(output.name) << "\",\"kind\":\"";
    switch (output.kind) {
    case OutputKind::Point: out << "point"; break;
    case OutputKind::Rect: out << "rect"; break;
    case OutputKind::Circle: out << "circle"; break;
    }
    out << "\",\"list\":" << (output.isList ? "true" : "false") << ",\"values\":[";
    bool first = true;
    for (const auto& point : output.points) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(point.x, bits) << ",\"y\":" << jsonNumber(point.y, bits) << "}";
    }
    for (const auto& rect : output.rects) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(rect.x, bits) << ",\"y\":" << jsonNumber(rect.y, bits)
            << ",\"w\":" << jsonNumber(rect.w, bits) << ",\"h\":" << jsonNumber(rect.h, bits) << "}";
    }
    for (const auto& circle : output.circles) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(circle.x, bits) << ",\"y\":" << jsonNumber(circle.y, bits)
            << ",\"r\":" << jsonNumber(circle.r, bits) << "}";
    }
    out << "]}";
}

} // namespace tools
} // namespace iprop
