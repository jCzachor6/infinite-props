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
// Fixed logical work: 2*N plus a worst-case comparison count times the
// widest element description. Independent of early exits and native/WASM.
u64 listSelectionWork(const ListValue& source, bool sort);
ListSelection selectList(const ListValue& source, bool sort);
ListValue materializeListSelection(const ListValue& source, const ListSelection& selection);
} // namespace iprop
