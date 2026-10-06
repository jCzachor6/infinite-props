#include "../Handles.hpp"
#include "../LegacyBoundary.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <vector>

namespace {
enum class Failure { Oom, Standard, Unknown };
bool armed = false;
std::size_t count = 0, failAt = 0, injected = 0;
Failure failure = Failure::Oom;
struct Unexpected : std::exception {
    const char* what() const noexcept override { return "bad magic, argument and trap wording is irrelevant"; }
};
}

void* operator new(std::size_t size) {
    if (armed && ++count == failAt) {
        ++injected;
        if (failure == Failure::Standard) throw Unexpected{};
        if (failure == Failure::Unknown) throw 17;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "checked errors: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::ifstream fixture(argv[1], std::ios::binary);
    CHECK(fixture.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fixture)), {});
    iprop_program* original = nullptr;
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &original, nullptr) == IPROP_EX_OK);
    const auto instructionAt = original->file.code.entry().fileOffset;
    auto invalid = bytes;
    invalid[instructionAt] = 0xff;
    const uint8_t junk[] = {1, 2, 3};
    iprop_error_ex error{};
    char message[128];
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    std::size_t faultCases = 0;
    bool validationOom = false;
    for (unsigned operation = 0; operation < 3; ++operation) {
        const auto* input = operation == 0 ? bytes.data() : operation == 1 ? junk : invalid.data();
        const auto length = operation == 1 ? sizeof(junk) : bytes.size();
        std::size_t measured = 0;
        for (std::size_t ordinal = 0; ordinal <= measured + 1; ++ordinal) {
            auto* candidate = original;
            count = 0; injected = 0; failAt = ordinal; failure = Failure::Oom;
            std::strcpy(message, "SENTINEL");
            armed = true;
            const auto status = iprop_program_load_ex(input, length, &candidate, &error);
            armed = false;
            if (ordinal == 0) measured = count;
            if (ordinal != 0 && ordinal <= measured) {
                ++faultCases;
                CHECK(injected == 1 && status == IPROP_EX_E_OOM && candidate == original);
                CHECK(error.status == IPROP_EX_E_OOM && error.reason == IPROP_REASON_OUT_OF_MEMORY);
                CHECK(error.stage == IPROP_STAGE_LOAD || error.stage == IPROP_STAGE_VALIDATE);
                validationOom |= error.stage == IPROP_STAGE_VALIDATE;
                CHECK(error.location_flags == 0 && std::strcmp(message, "out of memory") == 0);
            } else {
                CHECK(injected == 0 && status == (operation == 0 ? IPROP_EX_OK : IPROP_EX_E_FORMAT));
                if (operation == 0) {
                    CHECK(candidate != nullptr && candidate != original && error.status == IPROP_EX_OK);
                    CHECK(message[0] == '\0' && error.message_required_bytes == 0 && error.location_flags == 0);
                    iprop_program_free(candidate);
                } else {
                    CHECK(candidate == original && error.stage == (operation == 1 ? IPROP_STAGE_LOAD : IPROP_STAGE_VALIDATE));
                    CHECK(error.reason == (operation == 1 ? IPROP_REASON_TRUNCATED_INPUT : IPROP_REASON_INVALID_INSTRUCTION));
                    if (operation == 2) {
                        CHECK(error.pc == 0 && error.function_index == 0 && error.byte_offset == instructionAt);
                        CHECK(error.location_flags == (IPROP_LOCATION_BYTE_OFFSET | IPROP_LOCATION_FUNCTION | IPROP_LOCATION_PC));
                    }
                }
            }
        }
        std::printf("checked load operation %u: every one of %zu C++ allocations injected\n", operation, measured);
    }
    CHECK(validationOom);

    for (const auto kind : {Failure::Standard, Failure::Unknown}) {
        auto* candidate = original;
        count = 0; injected = 0; failAt = 1; failure = kind;
        armed = true;
        const auto status = iprop_program_load_ex(bytes.data(), bytes.size(), &candidate, &error);
        armed = false;
        CHECK(status == IPROP_EX_E_INTERNAL && count == 1 && injected == 1 && candidate == original);
        CHECK(error.reason == IPROP_REASON_INTERNAL && error.stage == IPROP_STAGE_LOAD && error.location_flags == 0);
        CHECK(std::strcmp(message, kind == Failure::Standard ? Unexpected{}.what() : "unexpected internal exception") == 0);
        count = 0; injected = 0;
        armed = true;
        const auto oldLoad = iprop_program_load(bytes.data(), bytes.size(), &candidate, message, sizeof(message));
        armed = false;
        CHECK(oldLoad == IPROP_E_FORMAT && candidate == original && count == 1 && injected == 1);
        if (kind == Failure::Unknown) CHECK(std::strcmp(message, "unexpected exception while loading program") == 0);
        iprop_generate_options options{};
        iprop_result* result = nullptr;
        count = 0; injected = 0;
        armed = true;
        const auto oldGenerate = iprop_generate(original, &options, &result, message, sizeof(message));
        armed = false;
        CHECK(oldGenerate == IPROP_E_TRAP && result == nullptr && count == 1 && injected == 1);
        if (kind == Failure::Unknown) CHECK(std::strcmp(message, "unexpected exception during generation") == 0);
    }

    // Checked argument/query/reporting paths perform no C++ allocation.
    iprop_program_info_ex info{};
    iprop_library_info_ex library{};
    CHECK(iprop_program_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    count = 0; injected = 0; failAt = 1; failure = Failure::Oom;
    armed = true;
    CHECK(iprop_program_load_ex(nullptr, 0, &original, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_query(nullptr, &info, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_query(original, &info, &error) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(nullptr, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_library_query_ex(&library, &error) == IPROP_EX_OK);
    armed = false;
    CHECK(count == 0 && injected == 0 && message[0] == '\0');
    CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE);
    iprop_program_free(original);
    std::printf("%zu checked OOM points, real format/validate locations, internal ABI containment and frozen legacy policy: PASS\n", faultCases);
    return 0;
}
