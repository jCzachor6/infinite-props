#pragma once

#include "ips/Ast.hpp"
#include "ips/BuiltinSpec.hpp"

#include <string_view>

namespace iprop::ips {

struct ArgumentParameter {
    std::string name;
    bool required = true;
    bool positional = true;
};

// The generic binder also represents optional/defaulted and named-only slots.
// Omitted optional parameters remain null; consumers supply their typed
// default. Positional arguments retain the legacy ordinal mapping, so naming
// a slot and also reaching it positionally is a duplicate, never an overwrite.
std::vector<const Arg*> bindArguments(const Call& call,
    std::span<const ArgumentParameter> parameters, std::string_view function);

// Maps positional/named arguments into signature order. Rejects duplicate,
// missing and unknown names before typing or emission can observe a call.
std::vector<const Arg*> bindArguments(const Call& call, std::span<const std::string> names,
                                     std::string_view function);

// The same fixed/variadic names used by call binding and language description.
std::vector<std::string> builtinParameterNames(const BuiltinSpec& spec, std::size_t arity,
                                              const SourceLocation& at, std::string_view function);
std::span<const std::string> rampParameterNames();

// Part controls: the seed, then the named-only placement, transforms and
// uniform overrides; transforms and uniforms are optional. The slots of
// partParameters, shared by binding, typing and lowering:
inline constexpr std::size_t kPartSeedSlot = 0;
inline constexpr std::size_t kPartAtSlot = 1;
inline constexpr std::size_t kPartTransformBegin = 2; // rot, mirror_x, mirror_y
inline constexpr std::size_t kPartTransformEnd = 5;
inline constexpr std::size_t kPartFirstInput = kPartTransformEnd; // declared uniforms follow
std::vector<ArgumentParameter> partParameters(std::span<const std::string> uniforms);
// A skin call of a render asset (models v1.2): the seed, the named-only
// pixel size, then the named-only input overrides of the imported asset.
inline constexpr std::size_t kSkinSeedSlot = 0;
inline constexpr std::size_t kSkinSizeSlot = 1;
inline constexpr std::size_t kSkinFirstInput = 2;
std::vector<ArgumentParameter> skinParameters(std::span<const std::string> uniforms);

// The named arguments of `place(...) { }` (models v1.2), in the order their
// values are evaluated: the bit order of place.begin's mask, whatever order
// the source names them in. Checking, lowering and the language description
// read this one table.
enum class PlaceKind : u8 {
    Offset,  // a vec3
    Degrees, // a number
    Scale,   // a number, or a vec3 with one factor per axis
    Mirror,  // an i32, 0 or 1
};
struct PlaceArgument {
    std::string_view name;
    PlaceKind kind;
    u16 bit; // of the mask; a vec3 scale sets kPlaceScale3 instead
};
std::span<const PlaceArgument> placeArguments();
const PlaceArgument* findPlaceArgument(std::string_view name);
// "at, yaw, pitch, ...": the names for a diagnostic.
std::string placeArgumentNames();
std::vector<const Arg*> partArguments(const Call& call,
    std::span<const std::string> uniforms, std::string_view function);

} // namespace iprop::ips
