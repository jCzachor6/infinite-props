#pragma once

#include "iprop/Collections.hpp"
#include <array>

namespace iprop {
inline constexpr std::array<TypeDescriptor, 13> kListElementTypes{
    ValueType::I32, ValueType::F32, ValueType::Color, ValueType::Vec2, ValueType::Distribution,
    ValueType::sampler(ValueType::I32), ValueType::sampler(ValueType::F32),
    ValueType::sampler(ValueType::Color), ValueType::sampler(ValueType::Vec2),
    ValueType::stepper(ValueType::I32), ValueType::stepper(ValueType::F32),
    ValueType::stepper(ValueType::Color), ValueType::stepper(ValueType::Vec2)};
constexpr bool isSortableList(TypeDescriptor type) {
    return type.isList() && type.isStorable() && type.item().kind <= TypeDescriptor::Kind::Vec2;
}
struct ListSelection {
    std::array<u16, kMaxListElements> indices{};
    std::size_t count = 0;
    bool unchanged(const ListValue& source) const;
};
// A list whose handle, descriptor and every element passed the distinct/sort
// checks, with its fixed logical work: 2*N plus a worst-case comparison count
// times the widest element description, independent of early exits and
// native/WASM. Unassigned or malformed input throws std::invalid_argument.
struct CheckedListSelection {
    ListValue source;
    bool sort = false;
    u64 work = 0;
};
CheckedListSelection checkListSelection(const ListValue& source, bool sort);
// First occurrences (distinct) or a stable merge sort of checked input.
ListSelection selectList(const CheckedListSelection& list);
// A new list of a changed selection; an unchanged one shares its source.
ListValue materializeListSelection(const ListValue& source, const ListSelection& selection);
// The complete distinct()/sort() transformation.
ListValue transformList(const ListValue& source, bool sort);
} // namespace iprop
