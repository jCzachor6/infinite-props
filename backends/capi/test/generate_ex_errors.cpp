#include "../Handles.hpp"
#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
enum class Failure { Oom, Standard, Unknown };
bool armed = false;
std::size_t count = 0, failAt = 0, injected = 0;
Failure failure = Failure::Oom;
struct Unexpected : std::exception {
    const char* what() const noexcept override { return "generation allocation failure is internal"; }
};
struct AllocationGuard {
    explicit AllocationGuard(std::size_t ordinal, Failure kind = Failure::Oom) {
        count = 0; injected = 0; failAt = ordinal; failure = kind; armed = true;
    }
    ~AllocationGuard() { armed = false; }
};

uint32_t readU32(const std::vector<uint8_t>& bytes, std::size_t at) {
    return uint32_t{bytes[at]} | uint32_t{bytes[at + 1]} << 8 | uint32_t{bytes[at + 2]} << 16 |
        uint32_t{bytes[at + 3]} << 24;
}

// Replace one stored constant-pool string of the same length (plain files).
bool renameInput(std::vector<uint8_t>& bytes, const char* from, const char* to, std::size_t length) {
    if (readU32(bytes, 8) & 2u) return false;
    std::size_t pool = 0;
    for (uint32_t section = 0; section < readU32(bytes, 12); ++section)
        if (readU32(bytes, 20 + section * 12) == 2) pool = readU32(bytes, 24 + section * 12);
    if (pool == 0) return false;
    std::size_t at = pool + 4;
    for (uint32_t index = 0; index < readU32(bytes, pool); ++index) {
        const uint32_t size = readU32(bytes, at);
        at += 4;
        if (size == length && std::memcmp(bytes.data() + at, from, length) == 0) {
            std::memcpy(bytes.data() + at, to, length);
            return true;
        }
        at += size;
    }
    return false;
}
} // namespace

// The replacements below pair malloc/free; GCC sees free() on memory from
// the replaced operator new after inlining and warns about a mismatch.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
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
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main(int argc, char** argv) {
    CHECK(argc == 3);
    std::ifstream uniformFile(argv[1], std::ios::binary), portraitFile(argv[2], std::ios::binary);
    CHECK(uniformFile.good() && portraitFile.good());
    std::vector<uint8_t> uniformBytes((std::istreambuf_iterator<char>(uniformFile)), {});
    const std::vector<uint8_t> portraitBytes((std::istreambuf_iterator<char>(portraitFile)), {});
    iprop_program *uniforms = nullptr, *portrait = nullptr;
    iprop_parameters* parameters = nullptr;
    CHECK(iprop_program_load_ex(uniformBytes.data(), uniformBytes.size(), &uniforms, nullptr) == IPROP_EX_OK);
    CHECK(iprop_program_load_ex(portraitBytes.data(), portraitBytes.size(), &portrait, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_create(uniforms, &parameters, nullptr) == IPROP_EX_OK);
    iprop_parameter_value_ex seven;
    CHECK(iprop_parameter_value_ex_init(&seven, sizeof(seven)) == IPROP_EX_OK);
    seven.type = IPROP_INPUT_U32; seven.u32 = 7;
    CHECK(iprop_parameters_set(parameters, 2, &seven, nullptr) == IPROP_EX_OK);

    iprop_error_ex error;
    char message[128];
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    iprop_generate_options_ex options;
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 7;

    // Every allocation of a real generation, with and without parameters and
    // on two programs, fails as E_OOM or E_INTERNAL without publishing a
    // result, changing parameter values or retaining program data.
    std::size_t faultCases = 0;
    for (unsigned operation = 0; operation < 3; ++operation) {
        const iprop_program* program = operation == 2 ? portrait : uniforms;
        const iprop_parameters* set = operation == 1 ? parameters : nullptr;
        const auto owners = program->data.use_count();
        const auto values = parameters->values;
        std::size_t measured = 0;
        for (std::size_t ordinal = 0; ordinal <= measured; ++ordinal) {
            iprop_result* sentinel = reinterpret_cast<iprop_result*>(&measured);
            iprop_result* out = sentinel;
            iprop_status_ex status;
            { AllocationGuard guard(ordinal); status = iprop_generate_ex(program, &options, set, &out, &error); }
            if (ordinal == 0) {
                measured = count;
                CHECK(status == IPROP_EX_OK && out != sentinel && injected == 0);
                iprop_result_free(out);
                continue;
            }
            ++faultCases;
            CHECK(injected == 1 && count == ordinal && status == IPROP_EX_E_OOM && out == sentinel);
            CHECK(error.reason == IPROP_REASON_OUT_OF_MEMORY && std::strcmp(message, "out of memory") == 0);
            CHECK(error.stage == IPROP_STAGE_ARGUMENTS || error.stage == IPROP_STAGE_EXECUTE);
            CHECK(program->data.use_count() == owners);
            CHECK(std::memcmp(values.data(), parameters->values.data(), values.size() * sizeof(values[0])) == 0);
        }
        CHECK(measured > 0);
        for (const auto kind : {Failure::Standard, Failure::Unknown}) {
            for (std::size_t ordinal = 1; ordinal <= measured; ++ordinal) {
                iprop_result* sentinel = reinterpret_cast<iprop_result*>(&measured);
                iprop_result* out = sentinel;
                iprop_status_ex status;
                { AllocationGuard guard(ordinal, kind); status = iprop_generate_ex(program, &options, set, &out, &error); }
                CHECK(injected == 1 && status == IPROP_EX_E_INTERNAL && out == sentinel);
                CHECK(error.reason == IPROP_REASON_INTERNAL && error.location_flags == 0);
                CHECK(std::strcmp(message, kind == Failure::Standard ? Unexpected{}.what() : "unexpected internal exception") == 0);
                CHECK(program->data.use_count() == owners);
            }
        }
        std::printf("generate_ex operation %u: every one of %zu allocations injected\n", operation, measured);
    }
    // Argument rejections allocate nothing at all.
    {
        options.variant = 1;
        iprop_result* out = nullptr;
        AllocationGuard guard(1);
        CHECK(iprop_generate_ex(uniforms, &options, parameters, &out, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_generate_ex(portrait, &options, parameters, &out, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(count == 0 && out == nullptr && error.reason == IPROP_REASON_FOREIGN_PARAMETERS);
        options.variant = 0;
    }

    // Values travel by root-input ID. A stored name with an embedded NUL is
    // unreachable through legacy NUL-terminated names, yet its exact view
    // resolves once when the value is set, and generation needs no name.
    {
        const char exact[] = {'b', 'a', '\0', 'e'};
        CHECK(renameInput(uniformBytes, "base", exact, 4));
        iprop_program* renamed = nullptr;
        iprop_parameters* set = nullptr;
        CHECK(iprop_program_load_ex(uniformBytes.data(), uniformBytes.size(), &renamed, nullptr) == IPROP_EX_OK);
        CHECK(iprop_parameters_create(renamed, &set, nullptr) == IPROP_EX_OK);
        iprop_parameter_value_ex white;
        CHECK(iprop_parameter_value_ex_init(&white, sizeof(white)) == IPROP_EX_OK);
        white.type = IPROP_INPUT_COLOR; white.color = {0xff, 0xff, 0xff, 0xff};
        CHECK(iprop_parameters_set_named(set, {"base", 4}, &white, nullptr) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_set_named(set, {exact, 4}, &white, nullptr) == IPROP_EX_OK);
        iprop_uniform named[2] = {};
        named[0].name = "base"; named[0].type = IPROP_UNIFORM_COLOR; named[0].r = named[0].g = named[0].b = named[0].a = 0xff;
        named[1] = named[0]; named[1].name = exact; // "ba" after NUL termination
        iprop_generate_options old{};
        old.uniforms = named; old.uniform_count = 2;
        iprop_result *legacy = nullptr, *checked = nullptr;
        CHECK(iprop_generate(renamed, &old, &legacy, nullptr, 0) == IPROP_OK);
        options.seed = 0;
        CHECK(iprop_generate_ex(renamed, &options, set, &checked, &error) == IPROP_EX_OK);
        iprop_image legacyImage, checkedImage;
        iprop_result_image(legacy, &legacyImage); iprop_result_image(checked, &checkedImage);
        CHECK(legacyImage.pixels[0] == 0x33 && legacyImage.pixels[1] == 0x55 && legacyImage.pixels[2] == 0xaa);
        CHECK(checkedImage.pixels[0] == 0xff && checkedImage.pixels[1] == 0xff && checkedImage.pixels[2] == 0xff);
        iprop_result_free(legacy); iprop_result_free(checked);
        iprop_parameters_free(set); iprop_program_free(renamed);
    }

    // The resolved-value VM constructor: argument checks keep their order and
    // legacy Trap, mismatched resolved values are caller bugs, and matching
    // values render exactly as the named path.
    {
        const auto& file = *uniforms->data;
        iprop::GenerationInputs inputs;
        inputs.seed = 3;
        std::vector<iprop::UniformValue> resolved(parameters->values);
        iprop::VM byId(file, inputs, resolved);
        byId.run();
        iprop::GenerationInputs named = inputs;
        iprop::UniformValue ux;
        ux.type = iprop::InputType::U32; ux.u = 7;
        named.uniforms.emplace("ux", ux);
        iprop::VM byName(file, named);
        byName.run();
        const auto& a = byId.canvas().pixels;
        const auto& b = byName.canvas().pixels;
        CHECK(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
        bool threw = false;
        try { iprop::VM vm(file, inputs, std::span(resolved).first(2)); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { iprop::VM vm(file, named, resolved); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw);
        threw = false;
        auto mistyped = resolved;
        mistyped[0].type = iprop::InputType::F32;
        try { iprop::VM vm(file, inputs, mistyped); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw);
        iprop::GenerationInputs badVariant = inputs;
        badVariant.variant = 1;
        iprop::ErrorReason reason = iprop::ErrorReason::None;
        try { iprop::VM vm(file, badVariant, std::span(resolved).first(1)); } catch (const iprop::Trap& trap) { reason = trap.reason; }
        CHECK(reason == iprop::ErrorReason::VariantRange);
    }

    iprop_parameters_free(parameters);
    iprop_program_free(uniforms); iprop_program_free(portrait);
    std::printf("generate_ex: %zu actual OOM points plus internal exceptions without publication, allocation-free "
                "argument errors, ID-only parameter values and resolved VM contract: PASS\n", faultCases);
    return 0;
}
