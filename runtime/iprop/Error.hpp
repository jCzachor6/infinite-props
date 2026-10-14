#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace iprop {

// Stable cause categories, assigned where a failure originates. Messages may
// change independently. Numeric values match the additive C error contract.
enum class ErrorReason : std::uint32_t {
    None = 0,
    InvalidFormat = 1000, TruncatedInput = 1001, BadMagic = 1002,
    UnsupportedVersion = 1003, InvalidHeader = 1004, Compression = 1005,
    InvalidLayout = 1006, InvalidConstants = 1007, InvalidMetadata = 1008,
    InvalidCode = 1009, InvalidDebug = 1010,
    InvalidInstruction = 2000, InvalidSignature = 2001, PartCycle = 2002,
    RuntimeViolation = 3000, FuelExhausted = 3001, StackUnderflow = 3002,
    StackType = 3003, StackLimit = 3004, CallDepth = 3005, IndexRange = 3006,
    InvalidControl = 3007, DivideByZero = 3008, IntegerOverflow = 3009,
    NumericRange = 3010, InvalidOutput = 3011, PartOutputMissing = 3012,
    InvalidPart = 3013, StreamUnderflow = 3014,
    InvalidArgument = 4000,
    CanvasSize = 4003, UniformType = 4004, UnknownParameter = 4005,
    DuplicateParameter = 4006, ForeignParameters = 4007,
    OutOfMemory = 5000, Internal = 5001,
};

enum class ErrorStage : std::uint32_t {
    None = 0, Load = 1, Validate = 2, Arguments = 3, Execute = 4,
};

enum class OffsetSpace : std::uint32_t { None = 0, ExpandedFile = 1, Container = 2 };

struct ErrorLocation {
    static constexpr std::uint32_t ByteOffset = 1u << 0;
    static constexpr std::uint32_t Function = 1u << 1;
    static constexpr std::uint32_t PC = 1u << 2;
    static constexpr std::uint32_t DebugLine = 1u << 3;

    std::uint32_t flags = 0;
    OffsetSpace offsetSpace = OffsetSpace::None;
    std::uint64_t byteOffset = 0;
    std::uint32_t functionIndex = 0;
    std::uint32_t pc = 0; // zero-based instruction index, not a byte offset
    std::uint32_t debugLine = 0; // one-based; zero means unavailable

    static constexpr ErrorLocation at(std::size_t offset, OffsetSpace space = OffsetSpace::ExpandedFile) {
        ErrorLocation location;
        location.flags = ByteOffset;
        location.offsetSpace = space;
        location.byteOffset = offset;
        return location;
    }
    static constexpr ErrorLocation instruction(std::size_t offset, std::uint32_t function,
                                               std::uint32_t instructionIndex) {
        auto location = at(offset);
        location.flags |= Function | PC;
        location.functionIndex = function;
        location.pc = instructionIndex;
        return location;
    }
};

struct FormatError : std::runtime_error {
    // Retained for existing C++ tooling/legacy located text. Zero by itself is
    // ambiguous; structured consumers use location.flags instead.
    std::size_t offset = 0;
    ErrorReason reason = ErrorReason::InvalidFormat;
    ErrorStage stage = ErrorStage::Load;
    ErrorLocation location;

    explicit FormatError(std::string message)
        : FormatError(std::move(message), ErrorReason::InvalidFormat, ErrorStage::Load) {}
    FormatError(std::string message, std::size_t at,
                ErrorReason cause = ErrorReason::InvalidFormat,
                ErrorStage phase = ErrorStage::Load,
                OffsetSpace space = OffsetSpace::ExpandedFile)
        : FormatError(std::move(message), cause, phase, ErrorLocation::at(at, space)) {}
    FormatError(std::string message, ErrorReason cause, ErrorStage phase, ErrorLocation where = {})
        : std::runtime_error(std::move(message)), offset(static_cast<std::size_t>(where.byteOffset)),
          reason(cause), stage(phase), location(where) {}
};

// Keep inheritance and located text compatibility. Setup-time checks remain
// legacy traps, while their typed stage identifies Arguments, with no fake PC.
struct Trap : FormatError {
    explicit Trap(std::string message)
        : FormatError(std::move(message), ErrorReason::RuntimeViolation, ErrorStage::Execute) {}
    Trap(std::string message, std::size_t at)
        : FormatError(std::move(message), at, ErrorReason::RuntimeViolation, ErrorStage::Execute) {}
    Trap(std::string message, ErrorReason cause, ErrorStage phase, ErrorLocation where = {})
        : FormatError(std::move(message), cause, phase, where) {}
};

} // namespace iprop
