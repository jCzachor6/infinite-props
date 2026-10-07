#include "../Operations.hpp"
#include "check.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <new>
#include <string>
#include <vector>

// Any allocation inside a guarded call fails that call. A mutating operation
// that allocated midway could otherwise leave a partial update behind.
namespace {
bool armed = false;
std::size_t allocations = 0;
struct AllocationGuard {
    AllocationGuard() { allocations = 0; armed = true; }
    ~AllocationGuard() { armed = false; }
};
}

void* operator new(std::size_t size) {
    if (armed) { ++allocations; throw std::bad_alloc{}; }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using Bytes = std::vector<unsigned char>;

Bytes rawValues(const iprop_parameters& parameters) {
    Bytes bytes(parameters.values.size() * sizeof(iprop::UniformValue));
    if (!bytes.empty()) std::memcpy(bytes.data(), parameters.values.data(), bytes.size());
    return bytes;
}

iprop_parameter_value_ex value(iprop_input_type type, uint32_t raw) {
    iprop_parameter_value_ex out;
    (void)iprop_parameter_value_ex_init(&out, sizeof(out));
    out.type = type;
    if (type == IPROP_INPUT_COLOR) std::memcpy(&out.color, &raw, 4);
    else if (type == IPROP_INPUT_F32) std::memcpy(&out.f32, &raw, 4);
    else out.u32 = raw;
    return out;
}

iprop_parameter_override_ex byId(uint32_t id, iprop_input_type type, uint32_t raw) {
    iprop_parameter_override_ex out;
    (void)iprop_parameter_override_ex_init(&out, sizeof(out));
    out.target = IPROP_PARAMETER_TARGET_ID; out.input_id = id; out.type = type;
    if (type == IPROP_INPUT_COLOR) std::memcpy(&out.color, &raw, 4);
    else if (type == IPROP_INPUT_F32) std::memcpy(&out.f32, &raw, 4);
    else out.u32 = raw;
    return out;
}

iprop_parameter_override_ex byName(const char* name, iprop_input_type type, uint32_t raw) {
    auto out = byId(UINT32_MAX, type, raw);
    out.target = IPROP_PARAMETER_TARGET_NAME;
    out.name = {name, static_cast<uint32_t>(std::strlen(name))};
    return out;
}

struct Failure {
    const char* what;
    std::function<iprop_status_ex(iprop_parameters*, iprop_error_ex*)> call;
    iprop_error_reason reason; // NONE: the boundary rejects without reporting
};
} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::ifstream fixture(argv[1], std::ios::binary);
    CHECK(fixture.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fixture)), {});
    iprop_program *program = nullptr, *other = nullptr;
    iprop_parameters *parameters = nullptr, *foreign = nullptr;
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &program, nullptr) == IPROP_EX_OK);
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &other, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_create(program, &parameters, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_create(other, &foreign, nullptr) == IPROP_EX_OK);
    CHECK(parameters->values.size() == 8);
    const uint32_t nan = UINT32_C(0x7fc12345), one = UINT32_C(0x3f800000), two = UINT32_C(0x40000000);
    // Start from non-default values so reset is observable as a mutation.
    {
        const std::array list{byId(1, IPROP_INPUT_F32, nan), byName("high", IPROP_INPUT_U32, 3),
                              byName("tint", IPROP_INPUT_COLOR, UINT32_C(0x04030201))};
        CHECK(iprop_parameters_set_overrides(parameters, list.data(), 3, sizeof(list[0]), nullptr) == IPROP_EX_OK);
    }

    iprop_error_ex error;
    char message[128];
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    char zeroName[] = "zero";
    const auto f32Value = value(IPROP_INPUT_F32, one);
    const std::array validThenBad{byId(1, IPROP_INPUT_F32, two), byName("high", IPROP_INPUT_U32, 9),
                                  byName("fraction", IPROP_INPUT_F32, two), byId(8, IPROP_INPUT_U32, 1)};
    const std::array validThenDuplicate{byId(4, IPROP_INPUT_U32, 9), byId(2, IPROP_INPUT_F32, two),
                                        byName("high", IPROP_INPUT_U32, 10)};
    const std::array validThenMistyped{byId(4, IPROP_INPUT_U32, 9), byName("tint", IPROP_INPUT_U32, 1)};
    auto malformed = validThenMistyped;
    malformed[1].type = IPROP_INPUT_COLOR; malformed[1].struct_size = offsetof(iprop_parameter_override_ex, color) + 3;

    // Every failed mutating request, from argument validation through error
    // storage defects and aliases, leaves the value bytes unchanged.
    const std::vector<Failure> failures = {
        {"set null handle", [&](iprop_parameters*, iprop_error_ex* e) { return iprop_parameters_set(nullptr, 1, &f32Value, e); }, IPROP_REASON_INVALID_ARGUMENT},
        {"set unknown ID", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set(p, 8, &f32Value, e); }, IPROP_REASON_UNKNOWN_PARAMETER},
        {"set mistyped", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set(p, 0, &f32Value, e); }, IPROP_REASON_UNIFORM_TYPE},
        {"set incomplete value", [&](iprop_parameters* p, iprop_error_ex* e) {
            auto partial = f32Value; partial.struct_size = offsetof(iprop_parameter_value_ex, f32) + 3;
            return iprop_parameters_set(p, 1, &partial, e); }, IPROP_REASON_INVALID_ARGUMENT},
        {"set_named unknown", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set_named(p, {"Zero", 4}, &f32Value, e); }, IPROP_REASON_UNKNOWN_PARAMETER},
        {"set_named malformed", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set_named(p, {"\xc0\xaf", 2}, &f32Value, e); }, IPROP_REASON_INVALID_ARGUMENT},
        {"bind unknown last", [&](iprop_parameters* p, iprop_error_ex* e) {
            return iprop_parameters_set_overrides(p, validThenBad.data(), 4, sizeof(validThenBad[0]), e); }, IPROP_REASON_UNKNOWN_PARAMETER},
        {"bind duplicate last", [&](iprop_parameters* p, iprop_error_ex* e) {
            return iprop_parameters_set_overrides(p, validThenDuplicate.data(), 3, sizeof(validThenDuplicate[0]), e); }, IPROP_REASON_DUPLICATE_PARAMETER},
        {"bind mistyped last", [&](iprop_parameters* p, iprop_error_ex* e) {
            return iprop_parameters_set_overrides(p, validThenMistyped.data(), 2, sizeof(validThenMistyped[0]), e); }, IPROP_REASON_UNIFORM_TYPE},
        {"bind malformed last", [&](iprop_parameters* p, iprop_error_ex* e) {
            return iprop_parameters_set_overrides(p, malformed.data(), 2, sizeof(malformed[0]), e); }, IPROP_REASON_INVALID_ARGUMENT},
        {"bind short stride", [&](iprop_parameters* p, iprop_error_ex* e) {
            return iprop_parameters_set_overrides(p, validThenBad.data(), 1, 3, e); }, IPROP_REASON_INVALID_ARGUMENT},
        {"reset null handle", [&](iprop_parameters*, iprop_error_ex* e) { return iprop_parameters_reset(nullptr, e); }, IPROP_REASON_INVALID_ARGUMENT},
    };
    // Valid requests become failures when error storage is unusable or would
    // land in protected storage; the operation must not run at all.
    const std::array valid{byId(4, IPROP_INPUT_U32, 11), byName("zero", IPROP_INPUT_F32, two)};
    const std::vector<std::pair<const char*, std::function<iprop_status_ex(iprop_parameters*, iprop_error_ex*)>>> validCalls = {
        {"set", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set(p, 1, &f32Value, e); }},
        {"set_named", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set_named(p, {zeroName, 4}, &f32Value, e); }},
        {"bind", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_set_overrides(p, valid.data(), 2, sizeof(valid[0]), e); }},
        {"reset", [&](iprop_parameters* p, iprop_error_ex* e) { return iprop_parameters_reset(p, e); }},
    };

    std::size_t cases = 0;
    const auto unchanged = [&](const Bytes& before) { return rawValues(*parameters) == before; };
    for (const auto& failure : failures) {
        const auto before = rawValues(*parameters);
        iprop_status_ex status;
        { AllocationGuard guard; status = failure.call(parameters, &error); }
        CHECK(status == IPROP_EX_E_ARGUMENT && allocations == 0 && unchanged(before));
        CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == failure.reason && error.stage == IPROP_STAGE_ARGUMENTS);
        ++cases;
    }
    for (const auto& [name, call] : validCalls) {
        // Malformed error descriptors: every short size and a NULL message
        // buffer with capacity. Neither the request nor error text is applied.
        for (uint32_t size = 0; size < 4; ++size) {
            iprop_error_ex shortError = error; shortError.struct_size = size;
            const auto before = rawValues(*parameters);
            iprop_status_ex status;
            { AllocationGuard guard; status = call(parameters, &shortError); }
            CHECK(status == IPROP_EX_E_ARGUMENT && allocations == 0 && unchanged(before));
            ++cases;
        }
        iprop_error_ex nullBuffer = error; nullBuffer.message_buffer = nullptr; nullBuffer.message_capacity = 4;
        auto before = rawValues(*parameters);
        iprop_status_ex status;
        { AllocationGuard guard; status = call(parameters, &nullBuffer); }
        CHECK(status == IPROP_EX_E_ARGUMENT && allocations == 0 && unchanged(before));
        CHECK(nullBuffer.reason == IPROP_REASON_INVALID_ARGUMENT && nullBuffer.message_buffer == nullptr);
        // Error descriptor or message inside protected handle/value/name/list
        // storage: rejected before binding, with every byte left as it was.
        std::vector<std::pair<iprop_error_ex*, char*>> aliases = {
            {reinterpret_cast<iprop_error_ex*>(parameters), message},
            {reinterpret_cast<iprop_error_ex*>(parameters->values.data()), message},
            {&error, reinterpret_cast<char*>(parameters->values.data())},
            {&error, reinterpret_cast<char*>(parameters)}};
        if (std::string(name) == "set_named") aliases.push_back({&error, zeroName});
        if (std::string(name) == "bind") aliases.push_back({&error, const_cast<char*>(reinterpret_cast<const char*>(&valid[1]))});
        for (const auto& [descriptor, buffer] : aliases) {
            const Bytes handle(reinterpret_cast<const unsigned char*>(parameters),
                               reinterpret_cast<const unsigned char*>(parameters) + sizeof(*parameters));
            before = rawValues(*parameters);
            iprop_error_ex savedError = error;
            if (descriptor == &error) { error.message_buffer = buffer; error.message_capacity = 4; savedError = error; }
            { AllocationGuard guard; status = call(parameters, descriptor); }
            CHECK(status == IPROP_EX_E_ARGUMENT && allocations == 0 && unchanged(before));
            CHECK(std::memcmp(parameters, handle.data(), handle.size()) == 0);
            CHECK(std::memcmp(&error, &savedError, sizeof(error)) == 0);
            CHECK(std::strcmp(zeroName, "zero") == 0 && valid[1].name.data[0] == 'z');
            error.message_buffer = message; error.message_capacity = sizeof(message);
            ++cases;
        }
        // The same request then succeeds, allocation-free.
        { AllocationGuard guard; status = call(parameters, &error); }
        CHECK(status == IPROP_EX_OK && allocations == 0 && error.status == IPROP_EX_OK && message[0] == '\0');
    }

    // Snapshots: a clone copies the values at that moment and owns them. A
    // failed bind on the source leaves it equal to the snapshot; later changes
    // on either side stay local. Separate configurations share only identity.
    CHECK(iprop_parameters_set_overrides(parameters, validThenBad.data(), 3, sizeof(validThenBad[0]), nullptr) == IPROP_EX_OK);
    iprop_parameters* snapshot = nullptr;
    CHECK(iprop_parameters_clone(parameters, &snapshot, nullptr) == IPROP_EX_OK);
    CHECK(rawValues(*snapshot) == rawValues(*parameters) && snapshot->values.data() != parameters->values.data());
    CHECK(iprop_parameters_set_overrides(parameters, validThenBad.data(), 4, sizeof(validThenBad[0]), nullptr) == IPROP_EX_E_ARGUMENT);
    CHECK(rawValues(*snapshot) == rawValues(*parameters));
    std::array<iprop_parameters*, 3> configurations{};
    for (uint32_t index = 0; index < configurations.size(); ++index) {
        CHECK(iprop_parameters_clone(snapshot, &configurations[index], nullptr) == IPROP_EX_OK);
        const std::array list{byName("high", IPROP_INPUT_U32, 100 + index), byId(6, IPROP_INPUT_U32, index)};
        CHECK(iprop_parameters_set_overrides(configurations[index], list.data(), 2, sizeof(list[0]), nullptr) == IPROP_EX_OK);
    }
    CHECK(iprop_parameters_reset(parameters, nullptr) == IPROP_EX_OK);
    iprop_parameter_value_ex got;
    CHECK(iprop_parameter_value_ex_init(&got, sizeof(got)) == IPROP_EX_OK);
    for (uint32_t index = 0; index < configurations.size(); ++index) {
        CHECK(iprop_parameters_get(configurations[index], 4, &got, nullptr) == IPROP_EX_OK && got.u32 == 100 + index);
        CHECK(iprop_parameters_get(configurations[index], 6, &got, nullptr) == IPROP_EX_OK && got.u32 == index);
        CHECK(iprop_parameters_get(configurations[index], 2, &got, nullptr) == IPROP_EX_OK && std::memcmp(&got.f32, &two, 4) == 0);
        CHECK(iprop::capi::parametersCompatible(*program, *configurations[index]));
        CHECK(!iprop::capi::parametersCompatible(*other, *configurations[index]));
    }
    CHECK(iprop_parameters_get(snapshot, 4, &got, nullptr) == IPROP_EX_OK && got.u32 == 9);
    CHECK(iprop_parameters_get(parameters, 4, &got, nullptr) == IPROP_EX_OK && got.u32 == UINT32_MAX);

    // Preparing a generation only reads a parameter set: the shared request
    // check takes it as const and leaves the handle and its values unchanged,
    // whether it accepts the request or rejects it.
    iprop_generate_options_ex options;
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    iprop::capi::GenerateOptions request;
    for (const uint32_t variant : {0u, 1u}) {
        options.variant = variant;
        for (const auto* candidate : {static_cast<const iprop_parameters*>(configurations[0]),
                                      static_cast<const iprop_parameters*>(foreign)}) {
            const auto values = rawValues(*candidate);
            const Bytes handle(reinterpret_cast<const unsigned char*>(candidate),
                               reinterpret_cast<const unsigned char*>(candidate) + sizeof(*candidate));
            bool accepted;
            { AllocationGuard guard; accepted = iprop::capi::checkGenerationRequest(*program, &options, candidate, request).succeeded; }
            CHECK(allocations == 0 && accepted == (variant == 0 && candidate == configurations[0]));
            CHECK(rawValues(*candidate) == values && std::memcmp(candidate, handle.data(), handle.size()) == 0);
        }
    }

    // Snapshots keep their values and identity after the program handle and
    // the source parameters are released.
    iprop_program_free(program); iprop_parameters_free(parameters);
    CHECK(iprop_parameters_get(snapshot, 4, &got, nullptr) == IPROP_EX_OK && got.u32 == 9);
    CHECK(iprop_parameters_get(configurations[2], 4, &got, nullptr) == IPROP_EX_OK && got.u32 == 102);
    for (auto* configuration : configurations) iprop_parameters_free(configuration);
    iprop_parameters_free(snapshot); iprop_parameters_free(foreign); iprop_program_free(other);
    std::printf("parameter transactions: %zu failed mutations left values unchanged without allocation; "
                "snapshots, separate configurations and read-only request checks: PASS\n", cases);
    return 0;
}
