#pragma once

#include "iprop/Types.hpp"
#include <compare>

namespace iprop {

// Scalar codes 0-4 are frozen. A parameterized type carries its element
// descriptor; its wire reference is resolved through the format's type table.
struct TypeDescriptor {
    enum class Kind : u8 {
        I32 = 0, F32 = 1, Color = 2, Vec2 = 3, Distribution = 4,
        List = 5, Rect = 6, Circle = 7, Void = 8, Builder = 9,
    };
    static const TypeDescriptor I32, F32, Color, Vec2, Distribution;
    static const TypeDescriptor List, Rect, Circle, Void, Builder;
    Kind kind = Kind::I32;
    Kind element = Kind::Void;

    constexpr TypeDescriptor() = default;
    constexpr TypeDescriptor(Kind scalar) : kind(scalar) {}
    constexpr TypeDescriptor(Kind container, Kind item) : kind(container), element(item) {}
    constexpr operator Kind() const { return kind; }
    constexpr explicit operator u8() const { return static_cast<u8>(kind); }
    constexpr auto operator<=>(const TypeDescriptor&) const = default;
    constexpr bool operator==(const TypeDescriptor&) const = default;
    constexpr bool operator==(Kind other) const { return *this == TypeDescriptor(other); }
    friend constexpr bool operator==(Kind a, TypeDescriptor b) { return b == a; }
    constexpr bool isList() const { return kind == Kind::List; }
    constexpr bool isScalar() const { return kind <= Kind::Distribution && element == Kind::Void; }
    constexpr bool isStorable() const { return isScalar() || (isList() && element <= Kind::Distribution); }
    constexpr TypeDescriptor item() const { return TypeDescriptor(element); }
    static constexpr TypeDescriptor list(TypeDescriptor item) { return {Kind::List, item.kind}; }
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

using ValueType = TypeDescriptor;
const char* valueTypeName(TypeDescriptor type);

} // namespace iprop
