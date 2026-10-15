#include "iprop/TypeDescriptor.hpp"

#include <map>
#include <string>

namespace iprop {
namespace {
using Kind = TypeDescriptor::Kind;

const char* kindName(Kind kind) {
    switch (kind) {
    case Kind::I32: return "i32";
    case Kind::F32: return "f32";
    case Kind::Color: return "color";
    case Kind::Vec2: return "vec2";
    case Kind::Distribution: return "distribution";
    case Kind::List: return "list";
    case Kind::Rect: return "rect";
    case Kind::Circle: return "circle";
    case Kind::Void: return "void";
    case Kind::Builder: return "list builder";
    case Kind::Sampler: return "sampler";
    case Kind::PointBuilder: return "distribution builder";
    case Kind::Layer: return "layer";
    case Kind::Mask: return "mask";
    case Kind::Stepper: return "stepper";
    }
    return "?";
}

std::string spelling(TypeDescriptor type) {
    if (!type.isParameterized()) return kindName(type.kind);
    return std::string(kindName(type.kind)) + "<" + spelling(type.item()) + ">";
}
} // namespace

const char* valueTypeName(TypeDescriptor type) {
    if (!type.isParameterized()) return kindName(type.kind);
    // Parameterized spellings compose from their kinds (list<sampler<i32>>),
    // built once for every storable descriptor.
    static const auto spellings = [] {
        std::map<TypeDescriptor, std::string> names;
        const auto add = [&](TypeDescriptor storable) { names.emplace(storable, spelling(storable)); };
        for (const Kind scalar : {Kind::I32, Kind::F32, Kind::Color, Kind::Vec2}) {
            for (const auto description : {TypeDescriptor::sampler(scalar), TypeDescriptor::stepper(scalar)}) {
                add(description);
                add(TypeDescriptor::list(description));
            }
        }
        for (const Kind element : {Kind::I32, Kind::F32, Kind::Color, Kind::Vec2, Kind::Distribution})
            add(TypeDescriptor::list(element));
        return names;
    }();
    if (const auto found = spellings.find(type); found != spellings.end()) return found->second.c_str();
    return type.isList() ? "list<?>" : type.isStepper() ? "stepper<?>" : "sampler<?>";
}
} // namespace iprop
