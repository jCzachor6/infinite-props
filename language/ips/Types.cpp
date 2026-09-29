#include "ips/Types.hpp"

namespace iprop::ips {

const char* typeName(ValueType type) {
    switch (type) {
    case ValueType::I32: return "i32";
    case ValueType::F32: return "f32";
    case ValueType::Color: return "color";
    case ValueType::Point: return "point";
    case ValueType::Rect: return "rect";
    case ValueType::Circle: return "circle";
    case ValueType::Void: return "void";
    }
    return "?";
}

bool isNumeric(ValueType type) {
    return type == ValueType::I32 || type == ValueType::F32;
}

bool isStorable(ValueType type) {
    return type == ValueType::I32 || type == ValueType::F32 ||
           type == ValueType::Color;
}

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

ValueType valueTypeOf(ParamType type) {
    switch (type) {
    case ParamType::I32: return ValueType::I32;
    case ParamType::F32: return ValueType::F32;
    case ParamType::Color: return ValueType::Color;
    }
    return ValueType::I32;
}

} // namespace iprop::ips
