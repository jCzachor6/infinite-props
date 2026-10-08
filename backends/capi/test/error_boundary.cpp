#include "../ErrorBoundary.hpp"
#include "iprop/VM.hpp"
#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
bool armed = false;
std::size_t allocationCount = 0;
std::size_t failAt = 0;
std::size_t injected = 0;

struct Unexpected : std::exception {
    const char* what() const noexcept override { return "invalid argument: format trap out of memory"; }
};
}

// The replacements below pair malloc/free; GCC sees free() on memory from
// the replaced operator new after inlining and warns about a mismatch.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new(std::size_t size) {
    if (armed && ++allocationCount == failAt) {
        ++injected;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main(int argc, char** argv) {
    using namespace iprop;
    using namespace iprop::capi;
    CHECK(argc == 3);
    std::ifstream uniformFile(argv[1], std::ios::binary);
    std::ifstream trapFile(argv[2], std::ios::binary);
    CHECK(uniformFile.good() && trapFile.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(uniformFile)), {});
    const std::vector<uint8_t> trapBytes((std::istreambuf_iterator<char>(trapFile)), {});
    const auto file = loadFile(bytes);
    const auto executionFile = loadFile(trapBytes);
    validate(file);
    validate(executionFile);
    iprop_error_ex error{};
    char message[128];
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    ErrorContext context{ErrorStage::Load};

    const uint8_t junk[] = {1, 2, 3};
    CHECK(errorBoundary(&error, context, [&]() {
        (void)loadFile(junk);
        return CallOutcome{};
    }) == IPROP_EX_E_FORMAT);
    CHECK(error.status == IPROP_EX_E_FORMAT && error.reason == IPROP_REASON_TRUNCATED_INPUT);
    CHECK(error.stage == IPROP_STAGE_LOAD && error.offset_space == IPROP_OFFSET_CONTAINER);
    CHECK(error.location_flags == IPROP_LOCATION_BYTE_OFFSET && message[0] != '\0');

    auto malformed = bytes;
    const auto instructionAt = file.code.entry().fileOffset;
    malformed[instructionAt] = 0xff; // decoding succeeds, static validation rejects this opcode
    const auto invalid = loadFile(malformed);
    context = {ErrorStage::Validate, &invalid};
    CHECK(errorBoundary(&error, context, [&]() {
        validate(invalid);
        return CallOutcome{};
    }) == IPROP_EX_E_FORMAT);
    CHECK(error.status == IPROP_EX_E_FORMAT && error.reason == IPROP_REASON_INVALID_INSTRUCTION);
    CHECK(error.stage == IPROP_STAGE_VALIDATE && error.function_index == 0 && error.pc == 0);
    CHECK(error.byte_offset == instructionAt && error.offset_space == IPROP_OFFSET_EXPANDED_FILE);
    CHECK(error.location_flags == (IPROP_LOCATION_BYTE_OFFSET | IPROP_LOCATION_FUNCTION | IPROP_LOCATION_PC));

    // Each constructor check still throws the legacy Trap type, but the new
    // policy identifies its actual Arguments stage, with no fabricated PC.
    const ErrorReason setupReasons[] = {ErrorReason::VariantRange, ErrorReason::ArchetypeRange,
        ErrorReason::CanvasSize, ErrorReason::UniformType};
    for (const auto reason : setupReasons) {
        GenerationInputs inputs;
        if (reason == ErrorReason::VariantRange) inputs.variant = file.meta.variants;
        if (reason == ErrorReason::ArchetypeRange) inputs.archetype = file.meta.archetypes;
        if (reason == ErrorReason::CanvasSize) inputs.sizeX = kMaxCanvasSide + 1;
        if (reason == ErrorReason::UniformType) {
            UniformValue value;
            value.type = InputType::F32; // base is declared color
            inputs.uniforms.emplace("base", value);
        }
        context = {ErrorStage::Execute, &file}; // typed originating stage takes precedence
        CHECK(errorBoundary(&error, context, [&]() {
            VM vm(file, inputs);
            return CallOutcome{};
        }) == IPROP_EX_E_ARGUMENT);
        CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == static_cast<uint32_t>(reason));
        CHECK(error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
        CHECK(error.byte_offset == 0 && error.function_index == 0 && error.pc == 0);
    }
    context = {ErrorStage::Execute, &executionFile};
    CHECK(errorBoundary(&error, context, [&]() {
        VM vm(executionFile, {});
        vm.run();
        return CallOutcome{};
    }) == IPROP_EX_E_TRAP);
    CHECK(error.status == IPROP_EX_E_TRAP && error.reason == IPROP_REASON_DIVIDE_BY_ZERO);
    CHECK(error.stage == IPROP_STAGE_EXECUTE && error.pc == 2 && error.function_index == 0);
    CHECK(error.byte_offset == executionFile.code.entry().fileOffset + 8);
    CHECK(error.location_flags == (IPROP_LOCATION_BYTE_OFFSET | IPROP_LOCATION_FUNCTION | IPROP_LOCATION_PC));

    // Exception wording never selects status. A base format exception tagged
    // Execute is an internal contract failure, rather than an actual VM Trap.
    const FormatError arguments("anything", ErrorReason::UniformType, ErrorStage::Arguments);
    const FormatError inconsistent("division by zero", ErrorReason::DivideByZero, ErrorStage::Execute,
        ErrorLocation::instruction(999, 7, 8));
    const FormatError missingStage("bad magic", ErrorReason::BadMagic, ErrorStage::None);
    const Trap unknownStage("fuel exhausted", ErrorReason::FuelExhausted, static_cast<ErrorStage>(99));
    static_assert(std::is_nothrow_copy_constructible_v<FormatError>);
    static_assert(std::is_nothrow_copy_constructible_v<Trap>);
    context = {ErrorStage::Validate};
    armed = true;
    allocationCount = 0; injected = 0; failAt = 1;
    CHECK(errorBoundary(&error, context, [&]() -> CallOutcome { throw arguments; }) == IPROP_EX_E_ARGUMENT);
    CHECK(error.reason == IPROP_REASON_UNIFORM_TYPE && error.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(errorBoundary(&error, context, [&]() -> CallOutcome { throw inconsistent; }) == IPROP_EX_E_INTERNAL);
    CHECK(error.reason == IPROP_REASON_INTERNAL && error.stage == IPROP_STAGE_VALIDATE && error.location_flags == 0);
    CHECK(errorBoundary(&error, context, [&]() -> CallOutcome { throw missingStage; }) == IPROP_EX_E_INTERNAL);
    CHECK(errorBoundary(&error, context, [&]() -> CallOutcome { throw unknownStage; }) == IPROP_EX_E_INTERNAL);
    CHECK(errorBoundary(&error, context, []() -> CallOutcome { throw Unexpected{}; }) == IPROP_EX_E_INTERNAL);
    CHECK(error.reason == IPROP_REASON_INTERNAL && error.stage == IPROP_STAGE_VALIDATE);
    CHECK(std::strcmp(message, "invalid argument: format trap out of memory") == 0);
    CHECK(errorBoundary(&error, context, []() -> CallOutcome { throw 17; }) == IPROP_EX_E_INTERNAL);
    CHECK(error.reason == IPROP_REASON_INTERNAL && std::strcmp(message, "unexpected internal exception") == 0);
    context.stage = ErrorStage::Load;
    CHECK(errorBoundary(&error, context, [&]() -> CallOutcome {
        context.stage = ErrorStage::Validate;
        throw std::bad_alloc{};
    }) == IPROP_EX_E_OOM);
    CHECK(error.reason == IPROP_REASON_OUT_OF_MEMORY && error.stage == IPROP_STAGE_VALIDATE);
    CHECK(std::strcmp(message, "out of memory") == 0);
    CHECK(errorBoundary(&error, context, [] { return CallOutcome::argument("required output pointer is null"); })
        == IPROP_EX_E_ARGUMENT);
    CHECK(error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(errorBoundary(&error, context, [] { return CallOutcome{}; }) == IPROP_EX_OK);
    armed = false;
    CHECK(allocationCount == 0 && injected == 0);
    CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && error.stage == IPROP_STAGE_NONE);
    CHECK(error.location_flags == 0 && error.byte_offset == 0 && error.function_index == 0 && error.pc == 0);
    CHECK(error.message_required_bytes == 0 && error.message_truncated == 0 && message[0] == '\0');
    CHECK(error.message_buffer == message && error.message_capacity == sizeof(message));

    // An actual allocation failure from load runs through the same boundary.
    context = {ErrorStage::Load};
    armed = true;
    allocationCount = 0; injected = 0; failAt = 1;
    const auto oomStatus = errorBoundary(&error, context, [&] {
        (void)loadFile(bytes);
        return CallOutcome{};
    });
    armed = false;
    CHECK(oomStatus == IPROP_EX_E_OOM && allocationCount == 1 && injected == 1);
    CHECK(error.reason == IPROP_REASON_OUT_OF_MEMORY && error.stage == IPROP_STAGE_LOAD);
    CHECK(error.location_flags == 0 && std::strcmp(message, "out of memory") == 0);

    bool invoked = false;
    error.message_buffer = nullptr;
    error.message_capacity = 12;
    CHECK(errorBoundary(&error, context, [&] { invoked = true; return CallOutcome{}; }) == IPROP_EX_E_ARGUMENT);
    CHECK(!invoked && error.status == IPROP_EX_E_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(error.message_buffer == nullptr && error.message_capacity == 12 && error.message_truncated == 1);
    CHECK(errorBoundary(nullptr, context, [&] { invoked = true; return CallOutcome{}; }) == IPROP_EX_OK);
    CHECK(invoked);
    CHECK(errorBoundary(nullptr, context, []() -> CallOutcome { throw 17; }) == IPROP_EX_E_INTERNAL);

    // Every exact descriptor prefix: malformed capacities stop the operation;
    // complete output fields work without touching inputs, padding or the tail.
    for (uint32_t capacity = 0; capacity <= sizeof(error) + 16; ++capacity) {
        const auto allocated = capacity < 4 ? 4 : capacity;
        auto* storage = static_cast<unsigned char*>(std::malloc(allocated));
        CHECK(storage != nullptr);
        std::memset(storage, 0xa5, allocated);
        std::memcpy(storage, &capacity, 4);
        auto* prefix = reinterpret_cast<iprop_error_ex*>(storage);
        (void)iprop_error_ex_init(prefix, capacity);
        unsigned char before[sizeof(error) + 16];
        std::memcpy(before, storage, allocated);
        invoked = false;
        CHECK(errorBoundary(prefix, context, [&] {
            invoked = true;
            return CallOutcome::argument("bad input", ErrorReason::UniformType);
        }) == IPROP_EX_E_ARGUMENT);
        CHECK(invoked == (capacity >= 4));
        CHECK(std::memcmp(storage, &capacity, 4) == 0);
        const std::pair<std::size_t, std::size_t> outputFields[] = {
            {offsetof(iprop_error_ex, status), 4}, {offsetof(iprop_error_ex, reason), 4},
            {offsetof(iprop_error_ex, stage), 4}, {offsetof(iprop_error_ex, byte_offset), 8},
            {offsetof(iprop_error_ex, location_flags), 4}, {offsetof(iprop_error_ex, offset_space), 4},
            {offsetof(iprop_error_ex, function_index), 4}, {offsetof(iprop_error_ex, pc), 4},
            {offsetof(iprop_error_ex, debug_line), 4}, {offsetof(iprop_error_ex, message_required_bytes), 8},
            {offsetof(iprop_error_ex, message_truncated), 4}};
        for (std::size_t byte = 4; byte < allocated; ++byte) {
            bool written = false;
            for (const auto& [at, width] : outputFields)
                if (at + width <= capacity && byte >= at && byte < at + width) written = true;
            if (!written) CHECK(storage[byte] == before[byte]);
        }
        if (capacity >= offsetof(iprop_error_ex, reason) + 4)
            CHECK(readField<uint32_t>(prefix, capacity, offsetof(iprop_error_ex, reason)) == IPROP_REASON_UNIFORM_TYPE);
        if (capacity >= offsetof(iprop_error_ex, message_required_bytes) + 8)
            CHECK(readField<uint64_t>(prefix, capacity, offsetof(iprop_error_ex, message_required_bytes)) == 9);
        std::free(storage);
    }

    // Public legacy policy still treats a setup failure as TRAP, while its
    // success/argument/OOM message policy belongs to the previous foundation.
    iprop_program* legacyProgram = nullptr;
    CHECK(iprop_program_load(bytes.data(), bytes.size(), &legacyProgram, message, sizeof(message)) == IPROP_OK);
    iprop_generate_options legacyOptions{};
    legacyOptions.variant = file.meta.variants;
    iprop_result* legacyResult = nullptr;
    CHECK(iprop_generate(legacyProgram, &legacyOptions, &legacyResult, message, sizeof(message)) == IPROP_E_TRAP);
    CHECK(legacyResult == nullptr);
    iprop_program_free(legacyProgram);
    puts("typed load/validate/arguments/execute policy, OOM/internal boundary, no-allocation reporting and exact prefixes: PASS");
    return 0;
}
