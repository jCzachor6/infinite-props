#pragma once

#include "iprop/Types.hpp"
#include <compare>

namespace iprop {

// Scalar codes 0-4 are frozen. A parameterized type carries its element
// descriptor; its wire reference is resolved through the format's type table.
struct TypeDescriptor {
    enum class Kind : u8 {
        I32 = 0, F32 = 1, Color = 2, Vec2 = 3, Distribution = 4,
        List = 5, Rect = 6, Circle = 7, Void = 8, Builder = 9, Sampler = 10,
    };
    static const TypeDescriptor I32, F32, Color, Vec2, Distribution;
    static const TypeDescriptor List, Rect, Circle, Void, Builder, Sampler;
    Kind kind = Kind::I32;
    Kind element = Kind::Void;
    // The current language permits one parameterized element: sampler<T>.
    // Lists of lists and samplers of resources remain invalid.
    Kind parameter = Kind::Void;

    constexpr TypeDescriptor() = default;
    constexpr TypeDescriptor(Kind scalar) : kind(scalar) {}
    constexpr TypeDescriptor(Kind container, Kind item) : kind(container), element(item) {}
    constexpr TypeDescriptor(Kind container, Kind item, Kind leaf)
        : kind(container), element(item), parameter(leaf) {}
    constexpr operator Kind() const { return kind; }
    constexpr explicit operator u8() const { return static_cast<u8>(kind); }
    constexpr auto operator<=>(const TypeDescriptor&) const = default;
    constexpr bool operator==(const TypeDescriptor&) const = default;
    constexpr bool operator==(Kind other) const { return *this == TypeDescriptor(other); }
    friend constexpr bool operator==(Kind a, TypeDescriptor b) { return b == a; }
    constexpr bool isList() const { return kind == Kind::List; }
    constexpr bool isSampler() const { return kind == Kind::Sampler; }
    constexpr bool isParameterized() const { return isList() || isSampler(); }
    constexpr bool isScalar() const { return kind <= Kind::Distribution && element == Kind::Void && parameter == Kind::Void; }
    constexpr bool isStorable() const {
        return isScalar() || (isSampler() && element <= Kind::Vec2 && parameter == Kind::Void) ||
            (isList() && ((element <= Kind::Distribution && parameter == Kind::Void) ||
                         (element == Kind::Sampler && parameter <= Kind::Vec2)));
    }
    constexpr TypeDescriptor item() const { return {element, parameter}; }
    static constexpr TypeDescriptor list(TypeDescriptor item) { return {Kind::List, item.kind, item.element}; }
    static constexpr TypeDescriptor sampler(TypeDescriptor item) { return {Kind::Sampler, item.kind, item.element}; }
    static constexpr TypeDescriptor builder(TypeDescriptor list) { return {Kind::Builder, list.element, list.parameter}; }
};

inline constexpr TypeDescriptor TypeDescriptor::I32{Kind::I32};
inline constexpr TypeDescriptor TypeDescriptor::F32{Kind::F32};
inline constexpr TypeDescriptor TypeDescriptor::Color{Kind::Color};
inline constexpr TypeDescriptor TypeDescriptor::Vec2{Kind::Vec2};
inline constexpr TypeDescriptor TypeDescriptor::Distribution{Kind::Distribution};
inline constexpr TypeDescriptor TypeDescriptor::List{Kind::List};
inline constexpr TypeDescriptor TypeDescriptor::Rect{Kind::Rect};
inline constexpr TypeDescriptor TypeDescriptor::Circle{Kind::Circle};
inline constexpr TypeDescriptor TypeDescriptor::Void{Kind::Void};
inline constexpr TypeDescriptor TypeDescriptor::Builder{Kind::Builder};
inline constexpr TypeDescriptor TypeDescriptor::Sampler{Kind::Sampler};

using ValueType = TypeDescriptor;
const char* valueTypeName(TypeDescriptor type);

} // namespace iprop
