#pragma once

#include "iprop/TypeDescriptor.hpp"
#include "iprop/Surface.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace iprop {

// A typed operand-stack value (spec §3: i32, f32, colour, vec2; v0.5
// distribution). u32 values travel in i32 slots; conversions follow the
// pinned spec rules when needed.
struct ListData;
struct ListBuilderData;
struct SamplerData;
struct SamplerValue {
    TypeDescriptor type;
    std::shared_ptr<const SamplerData> data;
};
struct ListValue {
    TypeDescriptor type;
    std::shared_ptr<const ListData> data;
};
struct ListBuilder {
    std::shared_ptr<ListBuilderData> data;
};
struct PointDistributionBuilderData {
    PointDistributionData distribution;
    std::size_t expected = 0;
    double mass = 0.0;
};
struct PointDistributionBuilder {
    std::shared_ptr<PointDistributionBuilderData> data;
};
using Value = std::variant<i32, f32, Color, Vec2, Distribution, ListValue, ListBuilder, SamplerValue,
                           PointDistributionBuilder, LayerValue, MaskValue>;

enum class SamplerMode : u8 { Constant, Uniform, With };
struct SamplerData {
    TypeDescriptor type;
    SamplerMode mode = SamplerMode::Constant;
    Value lower, upper;
    Distribution distribution;
};

struct ListData {
    TypeDescriptor type;
    std::vector<Value> elements;
};
struct ListBuilderData {
    ListData list;
    std::size_t expected = 0;
};
TypeDescriptor valueType(const Value& value);

} // namespace iprop
