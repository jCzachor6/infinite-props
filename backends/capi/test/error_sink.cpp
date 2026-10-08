#include "../ErrorSink.hpp"
#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace {
bool armed = false;
std::size_t allocationCount = 0;
std::size_t failAt = 0;
std::size_t injected = 0;
}

// One controlled bad_alloc, rather than exhausting memory. Static linkage
// makes the allocation hook apply equally on Windows and Unix platforms.
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

int main(int argc, char** argv) {
    using namespace iprop;
    using namespace iprop::capi;
    CHECK(argc == 3);
    iprop_error_ex descriptor{};
    CHECK(iprop_error_ex_init(&descriptor, sizeof(descriptor)) == IPROP_EX_OK);
    char buffer[64];
    descriptor.message_buffer = buffer;
    descriptor.message_capacity = sizeof(buffer);
    ErrorSink sink;
    CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
    FormatError runtime("arbitrary borrowed runtime message", ErrorReason::FuelExhausted, ErrorStage::Execute,
        ErrorLocation::instruction(1234, 2, 7));
    armed = true;
    allocationCount = 0; failAt = 1; injected = 0;
    CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
    sink.runtime(IPROP_EX_E_TRAP, runtime);
    CHECK(descriptor.status == IPROP_EX_E_TRAP && descriptor.reason == IPROP_REASON_FUEL_EXHAUSTED);
    CHECK(descriptor.byte_offset == 1234 && descriptor.function_index == 2 && descriptor.pc == 7);
    sink.argument("required output pointer is null");
    CHECK(std::strcmp(buffer, "required output pointer is null") == 0);
    CHECK(descriptor.status == IPROP_EX_E_ARGUMENT && descriptor.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(descriptor.location_flags == 0 && descriptor.byte_offset == 0);
    sink.outOfMemory(ErrorStage::Load);
    CHECK(std::strcmp(buffer, "out of memory") == 0);
    CHECK(descriptor.status == IPROP_EX_E_OOM && descriptor.reason == IPROP_REASON_OUT_OF_MEMORY);
    CHECK(descriptor.stage == IPROP_STAGE_LOAD && descriptor.message_required_bytes == 13);
    sink.success();
    armed = false;
    CHECK(allocationCount == 0 && injected == 0);
    CHECK(descriptor.status == IPROP_EX_OK && descriptor.reason == IPROP_REASON_NONE);
    CHECK(descriptor.stage == IPROP_STAGE_NONE && descriptor.location_flags == 0);
    CHECK(descriptor.message_required_bytes == 0 && descriptor.message_truncated == 0 && buffer[0] == '\0');
    CHECK(descriptor.message_buffer == buffer && descriptor.message_capacity == sizeof(buffer));

    // Borrowed message storage may also be the destination. Neither shift
    // direction nor identical addresses may invoke overlapping memcpy.
    char alias[32];
    for (const int shift : {-1, 0, 1}) {
        std::memset(alias, 0xa5, sizeof(alias));
        char* source = alias + 1;
        std::memcpy(source, "abcdef", 6);
        char* target = source + shift;
        armed = true;
        CHECK(copyMessage(std::string_view(source, 6), {target, 7}, true) == 6);
        armed = false;
        CHECK(std::strcmp(target, "abcdef") == 0);
        CHECK(static_cast<unsigned char>(alias[9]) == 0xa5);
        CHECK(allocationCount == 0 && injected == 0);
    }

    // Three/four-byte characters exercise every truncation boundary. Count
    // and NUL capacity are bytes; complete Unicode scalars remain intact.
    constexpr std::string_view wideText = "A\xe2\x82\xac\xf0\x9f\x92\xa1Z";
    for (uint32_t capacity = 0; capacity <= wideText.size() + 2; ++capacity) {
        std::memset(buffer, 0xa5, sizeof(buffer));
        descriptor.message_capacity = capacity;
        CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
        armed = true;
        sink.report(IPROP_EX_E_ARGUMENT, ErrorReason::InvalidArgument, ErrorStage::Arguments, wideText);
        armed = false;
        const std::size_t expected = capacity >= 10 ? 9 : capacity >= 9 ? 8 : capacity >= 5 ? 4 : capacity >= 2 ? 1 : 0;
        CHECK(descriptor.message_required_bytes == 9);
        CHECK(descriptor.message_truncated == (expected < 9 ? 1u : 0u));
        CHECK(capacity == 0 || std::strlen(buffer) == expected);
        CHECK(capacity == 0 || std::memcmp(buffer, wideText.data(), expected) == 0);
        for (std::size_t byte = capacity; byte < sizeof(buffer); ++byte)
            CHECK(static_cast<unsigned char>(buffer[byte]) == 0xa5);
        CHECK(allocationCount == 0 && injected == 0);
    }
    for (const int shift : {-1, 0, 1}) {
        std::memset(alias, 0xa5, sizeof(alias));
        char* source = alias + 1;
        std::memcpy(source, wideText.data(), wideText.size());
        descriptor.message_buffer = source + shift;
        descriptor.message_capacity = 5;
        CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
        armed = true;
        sink.report(IPROP_EX_E_ARGUMENT, ErrorReason::InvalidArgument, ErrorStage::Arguments,
                    std::string_view(source, wideText.size()));
        armed = false;
        CHECK(descriptor.message_required_bytes == 9 && descriptor.message_truncated == 1);
        CHECK(std::strlen(source + shift) == 4);
        CHECK(std::memcmp(source + shift, wideText.data(), 4) == 0);
        CHECK(static_cast<unsigned char>(alias[11]) == 0xa5);
        CHECK(allocationCount == 0 && injected == 0);
    }
    descriptor.message_buffer = buffer;
    descriptor.message_capacity = sizeof(buffer);
    CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);

    // Independent sinks cannot overwrite another call's metadata or message.
    iprop_error_ex other{};
    char otherBuffer[32];
    armed = true;
    CHECK(iprop_error_ex_init(&other, sizeof(other)) == IPROP_EX_OK);
    other.message_buffer = otherBuffer;
    other.message_capacity = sizeof(otherBuffer);
    ErrorSink otherSink;
    CHECK(otherSink.bind(&other) == ErrorSinkIssue::None);
    sink.argument();
    otherSink.outOfMemory(ErrorStage::Execute);
    sink.success();
    armed = false;
    CHECK(allocationCount == 0 && injected == 0);
    CHECK(descriptor.status == IPROP_EX_OK && buffer[0] == '\0');
    CHECK(other.status == IPROP_EX_E_OOM && other.stage == IPROP_STAGE_EXECUTE);
    CHECK(std::strcmp(otherBuffer, "out of memory") == 0);

    // All capacities, including NULL/zero length-only output. UTF-8 byte
    // counts are exact; a truncated new message retains complete characters.
    constexpr std::string_view text = "a\xc5\x82" "b";
    for (uint32_t capacity = 0; capacity <= text.size() + 2; ++capacity) {
        std::memset(buffer, 0xa5, sizeof(buffer));
        descriptor.message_capacity = capacity;
        CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
        sink.report(IPROP_EX_E_ARGUMENT, ErrorReason::InvalidArgument, ErrorStage::Arguments, text);
        CHECK(descriptor.message_required_bytes == text.size());
        const std::size_t copied = capacity == 0 ? 0 : std::strlen(buffer);
        CHECK(copied == (capacity >= 5 ? 4 : capacity == 4 ? 3 : capacity >= 2 ? 1 : 0));
        CHECK(descriptor.message_truncated == (copied < text.size() ? 1u : 0u));
        CHECK(capacity == 0 || buffer[copied] == '\0');
        for (std::size_t byte = capacity; byte < sizeof(buffer); ++byte)
            CHECK(static_cast<unsigned char>(buffer[byte]) == 0xa5);
        sink.success();
        CHECK(descriptor.message_required_bytes == 0 && descriptor.message_truncated == 0);
        CHECK(capacity == 0 || buffer[0] == '\0');
    }
    descriptor.message_buffer = nullptr;
    descriptor.message_capacity = 0;
    CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
    sink.argument();
    CHECK(descriptor.message_required_bytes == std::strlen("invalid argument"));
    CHECK(descriptor.message_truncated == 1);
    descriptor.message_capacity = 10;
    CHECK(sink.bind(&descriptor) == ErrorSinkIssue::InvalidMessageBuffer);
    sink.argument("message_buffer is null with nonzero capacity");
    CHECK(descriptor.status == IPROP_EX_E_ARGUMENT && descriptor.message_truncated == 1);
    CHECK(descriptor.message_buffer == nullptr && descriptor.message_capacity == 10);
    CHECK(sink.bind(nullptr) == ErrorSinkIssue::None);
    sink.outOfMemory(ErrorStage::Execute); // no output is also valid

    // Unlike canaries alone, these are exact allocations: ASan sees any read
    // or write beyond the caller's capacity, including each UTF-8 boundary.
    for (uint32_t capacity = 1; capacity <= text.size() + 2; ++capacity) {
        auto* exact = static_cast<char*>(std::malloc(capacity));
        CHECK(exact != nullptr);
        descriptor.message_buffer = exact;
        descriptor.message_capacity = capacity;
        CHECK(sink.bind(&descriptor) == ErrorSinkIssue::None);
        sink.report(IPROP_EX_E_ARGUMENT, ErrorReason::InvalidArgument, ErrorStage::Arguments, text);
        CHECK(descriptor.message_required_bytes == text.size());
        const auto copied = std::strlen(exact);
        CHECK(copied == (capacity >= 5 ? 4 : capacity == 4 ? 3 : capacity >= 2 ? 1 : 0));
        CHECK(std::memcmp(exact, text.data(), copied) == 0);
        CHECK(descriptor.message_truncated == (copied < text.size() ? 1u : 0u));
        sink.success();
        CHECK(exact[0] == '\0');
        std::free(exact);
    }

    // True prefix-only allocations exercise bounded reads, then verify that
    // publication preserves input buffer configuration and unknown tail bytes.
    descriptor.message_buffer = buffer;
    descriptor.message_capacity = sizeof(buffer);
    for (uint32_t capacity = 0; capacity <= sizeof(descriptor) + 16; ++capacity) {
        const auto allocated = capacity < 4 ? 4 : capacity;
        auto* bytes = static_cast<unsigned char*>(std::malloc(allocated));
        CHECK(bytes != nullptr);
        std::memset(bytes, 0xa5, allocated);
        std::memcpy(bytes, &descriptor, capacity < sizeof(descriptor) ? capacity : sizeof(descriptor));
        std::memcpy(bytes, &capacity, 4);
        unsigned char before[sizeof(descriptor) + 16];
        std::memcpy(before, bytes, allocated);
        auto* prefix = reinterpret_cast<iprop_error_ex*>(bytes);
        CHECK(sink.bind(prefix) == (capacity < 4 ? ErrorSinkIssue::DescriptorTooSmall : ErrorSinkIssue::None));
        sink.outOfMemory(ErrorStage::Load);
        CHECK(std::memcmp(bytes, &capacity, 4) == 0);
        if (capacity >= offsetof(iprop_error_ex, message_buffer) + sizeof(char*)) {
            char* preserved = nullptr;
            std::memcpy(&preserved, bytes + offsetof(iprop_error_ex, message_buffer), sizeof(preserved));
            CHECK(preserved == buffer);
        }
        if (capacity >= offsetof(iprop_error_ex, message_capacity) + 4) {
            uint32_t preserved = 0;
            std::memcpy(&preserved, bytes + offsetof(iprop_error_ex, message_capacity), 4);
            CHECK(preserved == sizeof(buffer));
        }
        for (std::size_t byte = sizeof(descriptor); byte < capacity; ++byte) CHECK(bytes[byte] == 0xa5);
        const std::pair<std::size_t, std::size_t> outputFields[] = {
            {offsetof(iprop_error_ex, status), 4}, {offsetof(iprop_error_ex, reason), 4},
            {offsetof(iprop_error_ex, stage), 4}, {offsetof(iprop_error_ex, byte_offset), 8},
            {offsetof(iprop_error_ex, location_flags), 4}, {offsetof(iprop_error_ex, offset_space), 4},
            {offsetof(iprop_error_ex, function_index), 4}, {offsetof(iprop_error_ex, pc), 4},
            {offsetof(iprop_error_ex, debug_line), 4},
            {offsetof(iprop_error_ex, message_required_bytes), 8},
            {offsetof(iprop_error_ex, message_truncated), 4}};
        for (std::size_t byte = 4; byte < allocated; ++byte) {
            bool completeOutput = false;
            for (const auto& [at, width] : outputFields)
                if (at + width <= capacity && byte >= at && byte < at + width) completeOutput = true;
            if (!completeOutput) CHECK(bytes[byte] == before[byte]);
        }
        if (capacity >= offsetof(iprop_error_ex, status) + 4)
            CHECK(readField<uint32_t>(prefix, capacity, offsetof(iprop_error_ex, status)) == IPROP_EX_E_OOM);
        if (capacity >= offsetof(iprop_error_ex, message_required_bytes) + 8)
            CHECK(readField<uint64_t>(prefix, capacity, offsetof(iprop_error_ex, message_required_bytes)) == 13);
        std::free(bytes);
    }

    // Legacy messages retain raw byte-prefix truncation, including a cut UTF-8
    // character. They do not inherit the new success/argument/OOM policy.
    std::memset(buffer, 0xa5, sizeof(buffer));
    CHECK(copyMessage(text, {buffer, 3}) == 2);
    CHECK(buffer[0] == 'a' && static_cast<unsigned char>(buffer[1]) == 0xc5 && buffer[2] == '\0');

    std::ifstream file(argv[1], std::ios::binary);
    CHECK(file.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    std::ifstream ownerFile(argv[2], std::ios::binary);
    CHECK(ownerFile.good());
    const std::vector<uint8_t> ownerBytes((std::istreambuf_iterator<char>(ownerFile)), {});
    iprop_program* program = nullptr;
    CHECK(iprop_program_load(bytes.data(), bytes.size(), &program, buffer, sizeof(buffer)) == IPROP_OK);
    iprop_program* ownerProgram = nullptr;
    CHECK(iprop_program_load(ownerBytes.data(), ownerBytes.size(), &ownerProgram, buffer, sizeof(buffer)) == IPROP_OK);
    iprop_generate_options options{};
    iprop_uniform uniform{};
    uniform.name = "base";
    uniform.type = IPROP_UNIFORM_F32; // fixture declares color
    iprop_uniform overrides[3]{};
    overrides[0].name = "base";
    overrides[0].type = IPROP_UNIFORM_COLOR;
    overrides[0].r = 20; overrides[0].g = 40; overrides[0].b = 60; overrides[0].a = 255;
    overrides[1].name = "fx";
    overrides[1].type = IPROP_UNIFORM_F32;
    overrides[1].f = 0.5f;
    overrides[2].name = "ux";
    overrides[2].type = IPROP_UNIFORM_U32;
    overrides[2].u = 4;
    const uint8_t junk[] = {1, 2, 3};
    std::size_t faultCases = 0;
    std::size_t injectedCases = 0;
    const char* operationNames[] = {"load uniforms", "load malformed input", "generate defaults",
        "generate type trap", "generate overrides", "generate part outputs", "load part outputs"};
    for (unsigned operation = 0; operation < 7; ++operation) {
        std::size_t measured = 0;
        for (std::size_t ordinal = 0; ordinal <= measured + 2; ++ordinal) {
            iprop_program* outProgram = nullptr;
            iprop_result* outResult = nullptr;
            std::strcpy(buffer, "SENTINEL");
            options.uniforms = operation == 3 ? &uniform : operation == 4 ? overrides : nullptr;
            options.uniform_count = operation == 3 ? 1 : operation == 4 ? 3 : 0;
            allocationCount = 0; injected = 0; failAt = ordinal;
            int status = -1;
            bool crossed = false;
            armed = true;
            try {
                if (operation < 2 || operation == 6) {
                    const auto* input = operation == 1 ? junk : operation == 6 ? ownerBytes.data() : bytes.data();
                    const auto length = operation == 1 ? sizeof(junk) : operation == 6 ? ownerBytes.size() : bytes.size();
                    status = static_cast<int>(iprop_program_load(input, length, &outProgram, buffer, sizeof(buffer)));
                } else {
                    status = static_cast<int>(iprop_generate(operation == 5 ? ownerProgram : program,
                        &options, &outResult, buffer, sizeof(buffer)));
                }
            } catch (...) {
                crossed = true;
            }
            armed = false;
            ++faultCases;
            CHECK(!crossed);
            if (ordinal == 0) measured = allocationCount;
            if (ordinal != 0 && ordinal <= measured) {
                CHECK(injected == 1 && status == IPROP_E_OOM);
                ++injectedCases;
                CHECK(outProgram == nullptr && outResult == nullptr);
                CHECK(std::strcmp(buffer, "SENTINEL") == 0);
            } else {
                CHECK(injected == 0);
                CHECK(status == (operation == 1 ? IPROP_E_FORMAT : operation == 3 ? IPROP_E_TRAP : IPROP_OK));
                if (operation != 1 && operation != 3) CHECK(std::strcmp(buffer, "SENTINEL") == 0);
                else CHECK(buffer[0] != '\0' && std::strcmp(buffer, "SENTINEL") != 0);
            }
            iprop_program_free(outProgram);
            iprop_result_free(outResult);
        }
        std::printf("legacy %s: %zu allocations, all fault points contained\n", operationNames[operation], measured);
    }
    std::strcpy(buffer, "SENTINEL");
    CHECK(iprop_program_load(nullptr, 4, &program, buffer, sizeof(buffer)) == IPROP_E_ARGUMENT);
    CHECK(std::strcmp(buffer, "SENTINEL") == 0);
    CHECK(iprop_generate(nullptr, nullptr, nullptr, buffer, sizeof(buffer)) == IPROP_E_ARGUMENT);
    CHECK(std::strcmp(buffer, "SENTINEL") == 0);
    iprop_program_free(program);
    iprop_program_free(ownerProgram);
    std::printf("allocation-free error sink, bounded messages, success reset: %zu injected failures in %zu legacy calls PASS\n",
        injectedCases, faultCases);
    return 0;
}
