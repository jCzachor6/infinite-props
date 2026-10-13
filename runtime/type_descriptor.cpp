#include "iprop/TypeDescriptor.hpp"

namespace iprop {
const char* valueTypeName(TypeDescriptor type) {
    if (type.isSampler()) {
        constexpr const char* names[] = {"sampler<i32>", "sampler<f32>", "sampler<color>", "sampler<vec2>"};
        return type.isStorable() ? names[static_cast<u8>(type.element)] : "sampler<?>";
    }
    if (type.isList()) {
        if (type.element == TypeDescriptor::Sampler) {
            constexpr const char* names[] = {"list<sampler<i32>>", "list<sampler<f32>>", "list<sampler<color>>", "list<sampler<vec2>>"};
            return type.isStorable() ? names[static_cast<u8>(type.parameter)] : "list<?>";
        }
        constexpr const char* names[] = {"list<i32>", "list<f32>", "list<color>",
            "list<vec2>", "list<distribution>"};
        return type.element <= TypeDescriptor::Kind::Distribution ? names[static_cast<u8>(type.element)] : "list<?>";
    }
    switch (type.kind) {
    case TypeDescriptor::I32: return "i32";
    case TypeDescriptor::F32: return "f32";
    case TypeDescriptor::Color: return "color";
    case TypeDescriptor::Vec2: return "vec2";
    case TypeDescriptor::Distribution: return "distribution";
    case TypeDescriptor::Rect: return "rect";
    case TypeDescriptor::Circle: return "circle";
    case TypeDescriptor::Void: return "void";
    case TypeDescriptor::Builder: return "list builder";
    case TypeDescriptor::List: return "list<?>";
    case TypeDescriptor::Sampler: return "sampler<?>";
    case TypeDescriptor::PointBuilder: return "distribution builder";
    case TypeDescriptor::Layer: return "layer";
    case TypeDescriptor::Mask: return "mask";
    }
    return "?";
}
} // namespace iprop
