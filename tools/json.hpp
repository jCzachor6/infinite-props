#pragma once

// Minimal JSON helpers shared by the CLI tools (ipgen now, ipval/ipdis
// later). Hand-rolled: no external dependencies, deterministic output.

#include "iprop/Outputs.hpp"
#include "iprop/VM.hpp"

#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace iprop {
namespace tools {

// Formats an f32 with round-trip precision; non-finite values are null
// (JSON has no infinities).
inline std::string jsonNumber(f32 value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

inline std::string jsonEscape(const std::string& text) {
    std::ostringstream stream;
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

// Writes one named output as a JSON object.
inline void writeOutputJson(std::ostream& out, const NamedOutput& output) {
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
        out << "{\"x\":" << jsonNumber(point.x) << ",\"y\":" << jsonNumber(point.y) << "}";
    }
    for (const auto& rect : output.rects) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(rect.x) << ",\"y\":" << jsonNumber(rect.y)
            << ",\"w\":" << jsonNumber(rect.w) << ",\"h\":" << jsonNumber(rect.h) << "}";
    }
    for (const auto& circle : output.circles) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(circle.x) << ",\"y\":" << jsonNumber(circle.y)
            << ",\"r\":" << jsonNumber(circle.r) << "}";
    }
    out << "]}";
}

} // namespace tools
} // namespace iprop
