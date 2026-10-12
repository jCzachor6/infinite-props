#include "ips/Types.hpp"

namespace iprop::ips {

const char* typeName(ValueType type) { return iprop::valueTypeName(type); }

std::optional<unsigned> colorChannel(std::string_view member) {
    constexpr std::string_view names[] = {"r", "g", "b", "a"};
    for (unsigned i = 0; i < 4; ++i)
        if (member == names[i]) return i;
    return std::nullopt;
}

bool isNumeric(ValueType type) {
    return type == ValueType::I32 || type == ValueType::F32;
}

bool isStorable(ValueType type) { return type.isStorable(); }

ValueType promote(ValueType a, ValueType b) {
    return a == b ? a : ValueType::F32;
}

bool argFits(ValueType param, ValueType arg) {
    if (param == arg) {
        return true;
    }
    // i32 widens to f32; the compiler inserts i32.to.f32.
    return param == ValueType::F32 && arg == ValueType::I32;
}

ValueType valueTypeOf(ParamType type) { return type; }

ValueType valueTypeOf(InputType type) {
    switch (type) {
    case InputType::Color: return ValueType::Color;
    case InputType::F32: return ValueType::F32;
    case InputType::U32:
    case InputType::I32: return ValueType::I32;
    }
    return ValueType::I32;
}

} // namespace iprop::ips
