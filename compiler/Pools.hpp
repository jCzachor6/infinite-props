#pragma once

// The constant pools of a compiled file (spec §5): strings, i32 bit
// patterns, f32 bit patterns and colours, deduplicated in insertion order
// so identical inputs produce byte-identical outputs.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "iprop/Types.hpp"

namespace iprop::compiler {

class Pools {
public:
    // Registers a name/literal, returning its pool index. Repeated values
    // return the first index.
    u32 addString(const std::string& text) {
        const auto found = stringIndex.find(text);
        if (found != stringIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(strings.size());
        strings.push_back(text);
        stringIndex.emplace(text, index);
        return index;
    }

    u32 addInt(u32 bits) {
        const auto found = intIndex.find(bits);
        if (found != intIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(ints.size());
        ints.push_back(bits);
        intIndex.emplace(bits, index);
        return index;
    }

    u32 addFloat(u32 bits) {
        const auto found = floatIndex.find(bits);
        if (found != floatIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(floats.size());
        floats.push_back(bits);
        floatIndex.emplace(bits, index);
        return index;
    }

    u32 addColor(Color color) {
        const u32 key = static_cast<u32>(color.r) |
                        (static_cast<u32>(color.g) << 8) |
                        (static_cast<u32>(color.b) << 16) |
                        (static_cast<u32>(color.a) << 24);
        const auto found = colorIndex.find(key);
        if (found != colorIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(colors.size());
        colors.push_back(color);
        colorIndex.emplace(key, index);
        return index;
    }

    // The pool contents, in registration order.
    const std::vector<std::string>& stringsValue() const { return strings; }
    const std::vector<u32>& intsValue() const { return ints; }
    const std::vector<u32>& floatsValue() const { return floats; }
    const std::vector<Color>& colorsValue() const { return colors; }

    // The registered index of a previously added string (names are
    // registered during planning, before emission).
    u32 indexOf(const std::string& text) const { return stringIndex.at(text); }

private:
    std::vector<std::string> strings;
    std::map<std::string, u32> stringIndex;
    std::vector<u32> ints;
    std::map<u32, u32> intIndex;
    std::vector<u32> floats;
    std::map<u32, u32> floatIndex;
    std::vector<Color> colors;
    std::map<u32, u32> colorIndex;
};

} // namespace iprop::compiler
