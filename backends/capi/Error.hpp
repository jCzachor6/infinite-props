#pragma once

#include "iprop.h"
#include "Sized.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Validator.hpp"

namespace iprop::capi {

// Compile-time guard for the numeric cause registry shared with C99 clients.
static_assert(static_cast<uint32_t>(ErrorReason::None) == IPROP_REASON_NONE);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidFormat) == IPROP_REASON_INVALID_FORMAT);
static_assert(static_cast<uint32_t>(ErrorReason::TruncatedInput) == IPROP_REASON_TRUNCATED_INPUT);
static_assert(static_cast<uint32_t>(ErrorReason::BadMagic) == IPROP_REASON_BAD_MAGIC);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidHeader) == IPROP_REASON_INVALID_HEADER);
static_assert(static_cast<uint32_t>(ErrorReason::Compression) == IPROP_REASON_COMPRESSION);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidLayout) == IPROP_REASON_INVALID_LAYOUT);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidConstants) == IPROP_REASON_INVALID_CONSTANTS);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidMetadata) == IPROP_REASON_INVALID_METADATA);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidCode) == IPROP_REASON_INVALID_CODE);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidDebug) == IPROP_REASON_INVALID_DEBUG);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidInstruction) == IPROP_REASON_INVALID_INSTRUCTION);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidSignature) == IPROP_REASON_INVALID_SIGNATURE);
static_assert(static_cast<uint32_t>(ErrorReason::PartCycle) == IPROP_REASON_PART_CYCLE);
static_assert(static_cast<uint32_t>(ErrorReason::RuntimeViolation) == IPROP_REASON_RUNTIME_VIOLATION);
static_assert(static_cast<uint32_t>(ErrorReason::FuelExhausted) == IPROP_REASON_FUEL_EXHAUSTED);
static_assert(static_cast<uint32_t>(ErrorReason::StackUnderflow) == IPROP_REASON_STACK_UNDERFLOW);
static_assert(static_cast<uint32_t>(ErrorReason::StackType) == IPROP_REASON_STACK_TYPE);
static_assert(static_cast<uint32_t>(ErrorReason::StackLimit) == IPROP_REASON_STACK_LIMIT);
static_assert(static_cast<uint32_t>(ErrorReason::CallDepth) == IPROP_REASON_CALL_DEPTH);
static_assert(static_cast<uint32_t>(ErrorReason::IndexRange) == IPROP_REASON_INDEX_RANGE);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidControl) == IPROP_REASON_INVALID_CONTROL);
static_assert(static_cast<uint32_t>(ErrorReason::DivideByZero) == IPROP_REASON_DIVIDE_BY_ZERO);
static_assert(static_cast<uint32_t>(ErrorReason::IntegerOverflow) == IPROP_REASON_INTEGER_OVERFLOW);
static_assert(static_cast<uint32_t>(ErrorReason::NumericRange) == IPROP_REASON_NUMERIC_RANGE);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidOutput) == IPROP_REASON_INVALID_OUTPUT);
static_assert(static_cast<uint32_t>(ErrorReason::PartOutputMissing) == IPROP_REASON_PART_OUTPUT_MISSING);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidPart) == IPROP_REASON_INVALID_PART);
static_assert(static_cast<uint32_t>(ErrorReason::StreamUnderflow) == IPROP_REASON_STREAM_UNDERFLOW);
static_assert(static_cast<uint32_t>(ErrorReason::InvalidArgument) == IPROP_REASON_INVALID_ARGUMENT);
static_assert(static_cast<uint32_t>(ErrorReason::CanvasSize) == IPROP_REASON_CANVAS_SIZE);
static_assert(static_cast<uint32_t>(ErrorReason::UniformType) == IPROP_REASON_UNIFORM_TYPE);
static_assert(static_cast<uint32_t>(ErrorReason::UnknownParameter) == IPROP_REASON_UNKNOWN_PARAMETER);
static_assert(static_cast<uint32_t>(ErrorReason::DuplicateParameter) == IPROP_REASON_DUPLICATE_PARAMETER);
static_assert(static_cast<uint32_t>(ErrorReason::ForeignParameters) == IPROP_REASON_FOREIGN_PARAMETERS);
static_assert(static_cast<uint32_t>(ErrorReason::OutOfMemory) == IPROP_REASON_OUT_OF_MEMORY);
static_assert(static_cast<uint32_t>(ErrorReason::Internal) == IPROP_REASON_INTERNAL);

// Status selection belongs to the calling API's policy. In particular, an
// Arguments-stage legacy Trap still has IPROP_E_TRAP. This adapter copies
// typed metadata and never parses/allocates a message or repeats validation.
inline iprop_error_ex describeError(iprop_status_ex status, const FormatError& error,
                                   const LoadedFile* file = nullptr) noexcept {
    iprop_error_ex out{};
    out.status = status;
    out.reason = static_cast<uint32_t>(error.reason);
    out.stage = static_cast<uint32_t>(error.stage);
    auto location = error.location;
    if (file != nullptr && (location.flags & (ErrorLocation::Function | ErrorLocation::PC)) ==
            (ErrorLocation::Function | ErrorLocation::PC) &&
        location.functionIndex < file->code.functions.size()) {
        if (const auto* section = file->layout.find(SectionId::Debug)) {
            location.debugLine = debugLineAt(file->bytes(), *section, location.functionIndex,
                location.pc, static_cast<u32>(file->code.functions.size()),
                static_cast<u32>(file->code.functions[location.functionIndex].code.size() / 4));
            if (location.debugLine != 0) location.flags |= ErrorLocation::DebugLine;
        }
    }
    out.byte_offset = location.byteOffset;
    out.location_flags = location.flags;
    out.offset_space = static_cast<uint32_t>(location.offsetSpace);
    out.function_index = location.functionIndex;
    out.pc = location.pc;
    out.debug_line = location.debugLine;
    return out;
}

inline bool writeError(iprop_error_ex* out, const iprop_error_ex& error) noexcept {
    uint32_t size = 0;
    if (!descriptorSize(out, size)) return false;
    auto flags = error.location_flags;
    const auto fits = [size](std::size_t offset, std::size_t count) {
        return offset <= size && count <= size - offset;
    };
    if (!fits(offsetof(iprop_error_ex, byte_offset), sizeof(error.byte_offset)) ||
        !fits(offsetof(iprop_error_ex, offset_space), sizeof(error.offset_space)))
        flags &= ~IPROP_LOCATION_BYTE_OFFSET;
    if (!fits(offsetof(iprop_error_ex, function_index), sizeof(error.function_index)))
        flags &= ~IPROP_LOCATION_FUNCTION;
    if (!fits(offsetof(iprop_error_ex, pc), sizeof(error.pc))) flags &= ~IPROP_LOCATION_PC;
    if (!fits(offsetof(iprop_error_ex, debug_line), sizeof(error.debug_line)))
        flags &= ~IPROP_LOCATION_DEBUG_LINE;
#define WRITE(field) writeField(out, size, offsetof(iprop_error_ex, field), error.field)
    WRITE(status); WRITE(reason); WRITE(stage); WRITE(byte_offset); WRITE(offset_space);
    WRITE(function_index); WRITE(pc); WRITE(debug_line);
    WRITE(message_required_bytes); WRITE(message_truncated);
#undef WRITE
    writeField(out, size, offsetof(iprop_error_ex, location_flags), flags);
    return true;
}

} // namespace iprop::capi
