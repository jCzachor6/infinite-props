#pragma once

#include "ErrorBoundary.hpp"
#include "Utf8View.hpp"
#include "iprop/VM.hpp"

#include <cstring>
#include <span>

namespace iprop::capi {

// Shared exact declaration lookup for root/part reflection and parameters.
inline CallOutcome findInput(const LoadedFile& file, std::span<const InputDecl> inputs,
                             iprop_utf8_view name, uint32_t* outIndex) noexcept {
    if (outIndex == nullptr) return CallOutcome::argument("out_index is null");
    std::string_view sought;
    if (!readUtf8View(name, sought))
        return CallOutcome::argument("input name is not a valid UTF-8 view");
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (file.pool.strings[inputs[index].nameIndex] == sought) {
            *outIndex = static_cast<uint32_t>(index);
            return {};
        }
    }
    return CallOutcome::argument("input name was not found");
}

inline void inputDefault(const InputDecl& input, UniformValue& value) noexcept {
    value.type = input.type;
    switch (input.type) {
    case InputType::Color: value.color = input.colorDefault; break;
    case InputType::F32: std::memcpy(&value.f, &input.f32Default, sizeof(value.f)); break;
    case InputType::U32: value.u = input.u32Default; break;
    }
}

} // namespace iprop::capi
