#include "../Handles.hpp"

#include <array>
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
    const char* what() const noexcept override { return "parameter allocation failure is internal"; }
};
struct AllocationGuard {
    explicit AllocationGuard(std::size_t ordinal, Failure kind = Failure::Oom) {
        count = 0; injected = 0; failAt = ordinal; failure = kind; armed = true;
    }
    ~AllocationGuard() { armed = false; }
};
uint32_t bits(const float& value) noexcept {
    uint32_t result; std::memcpy(&result, &value, sizeof(result)); return result;
}
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

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "parameter errors: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 3);
    std::ifstream fixture(argv[1], std::ios::binary), emptyFixture(argv[2], std::ios::binary);
    CHECK(fixture.good() && emptyFixture.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fixture)), {});
    const std::vector<uint8_t> emptyBytes((std::istreambuf_iterator<char>(emptyFixture)), {});
    iprop_program *program = nullptr, *reloaded = nullptr, *empty = nullptr;
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &program, nullptr) == IPROP_EX_OK);
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &reloaded, nullptr) == IPROP_EX_OK);
    CHECK(iprop_program_load_ex(emptyBytes.data(), emptyBytes.size(), &empty, nullptr) == IPROP_EX_OK);
    iprop_parameters *parameters = nullptr, *emptyParameters = nullptr, *clone = nullptr;
    CHECK(iprop_parameters_create(program, &parameters, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_create(empty, &emptyParameters, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_clone(parameters, &clone, nullptr) == IPROP_EX_OK);
    CHECK(iprop::capi::parametersCompatible(*program, *parameters));
    CHECK(iprop::capi::parametersCompatible(*program, *clone));
    CHECK(!iprop::capi::parametersCompatible(*reloaded, *parameters));
    CHECK(parameters->values.data() != clone->values.data());
    std::weak_ptr<const iprop::LoadedFile> retained = program->data;

    iprop_error_ex error{};
    char message[128];
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    std::size_t faultCases = 0;
    // Sweep every allocation in actual create/clone adapters, including cleanup
    // after shared data has been retained but the value vector cannot allocate.
    for (unsigned operation = 0; operation < 4; ++operation) {
        const auto* sourceProgram = operation < 2 ? program : empty;
        const auto* sourceParameters = operation < 2 ? parameters : emptyParameters;
        const auto owners = sourceProgram->data.use_count();
        std::size_t measured = 0;
        for (std::size_t ordinal = 0; ordinal <= measured + 1; ++ordinal) {
            auto* candidate = parameters;
            iprop_status_ex status;
            std::strcpy(message, "SENTINEL");
            {
                AllocationGuard guard(ordinal);
                status = operation % 2 == 0 ? iprop_parameters_create(sourceProgram, &candidate, &error) :
                    iprop_parameters_clone(sourceParameters, &candidate, &error);
            }
            if (ordinal == 0) measured = count;
            if (ordinal != 0 && ordinal <= measured) {
                ++faultCases;
                CHECK(injected == 1 && count == ordinal && status == IPROP_EX_E_OOM && candidate == parameters);
                CHECK(error.status == status && error.reason == IPROP_REASON_OUT_OF_MEMORY);
                CHECK(error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
                CHECK(error.message_required_bytes == 13 && std::strcmp(message, "out of memory") == 0);
                CHECK(sourceProgram->data.use_count() == owners);
            } else {
                CHECK(injected == 0 && status == IPROP_EX_OK && candidate != nullptr && candidate != parameters);
                CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && message[0] == '\0');
                CHECK(error.message_required_bytes == 0 && error.location_flags == 0);
                CHECK(iprop::capi::parametersCompatible(*sourceProgram, *candidate));
                iprop_parameters_free(candidate);
                CHECK(sourceProgram->data.use_count() == owners);
            }
        }
        CHECK(measured == (operation < 2 ? 2 : 1));
        std::printf("parameter operation %u: every one of %zu allocations injected\n", operation, measured);
        for (const auto kind : {Failure::Standard, Failure::Unknown}) {
            for (std::size_t ordinal = 1; ordinal <= measured; ++ordinal) {
                auto* candidate = parameters;
                iprop_status_ex status;
                {
                    AllocationGuard guard(ordinal, kind);
                    status = operation % 2 == 0 ? iprop_parameters_create(sourceProgram, &candidate, &error) :
                        iprop_parameters_clone(sourceParameters, &candidate, &error);
                }
                CHECK(injected == 1 && count == ordinal && status == IPROP_EX_E_INTERNAL && candidate == parameters);
                CHECK(error.reason == IPROP_REASON_INTERNAL && error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
                CHECK(std::strcmp(message, kind == Failure::Standard ? Unexpected{}.what() : "unexpected internal exception") == 0);
                CHECK(sourceProgram->data.use_count() == owners);
            }
        }
    }

    iprop_parameter_value_ex value{}, got{}, before{};
    CHECK(iprop_parameter_value_ex_init(&value, sizeof(value)) == IPROP_EX_OK);
    CHECK(iprop_parameter_value_ex_init(&got, sizeof(got)) == IPROP_EX_OK);
    CHECK(iprop_parameter_value_ex_init(&before, sizeof(before)) == IPROP_EX_OK);
    value.type = IPROP_INPUT_F32;
    const uint32_t nan = UINT32_C(0x7fc12345);
    std::memcpy(&value.f32, &nan, sizeof(nan));
    // Reporting, updates, reset and all argument paths allocate nothing.
    {
        AllocationGuard guard(1);
        CHECK(iprop_parameter_value_ex_init(&got, sizeof(got)) == IPROP_EX_OK);
        CHECK(iprop_parameters_set(parameters, 1, &value, &error) == IPROP_EX_OK);
        CHECK(iprop_parameters_get_named(parameters, {"zero", 4}, &got, &error) == IPROP_EX_OK && bits(got.f32) == nan);
        CHECK(iprop_parameters_set_named(parameters, {"zero", 4}, &value, &error) == IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters, 1, &before, nullptr) == IPROP_EX_OK);
        CHECK(iprop_parameters_set(parameters, UINT32_MAX, &value, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_set_named(parameters, {"unknown", 7}, &value, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_set_named(parameters, {nullptr, 1}, &value, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_get_named(parameters, {"\xc0\xaf", 2}, &got, &error) == IPROP_EX_E_ARGUMENT);
        value.type = IPROP_INPUT_U32;
        CHECK(iprop_parameters_set(parameters, 1, &value, &error) == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_UNIFORM_TYPE);
        value.type = UINT32_MAX;
        CHECK(iprop_parameters_set(parameters, 1, &value, &error) == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_INVALID_ARGUMENT);
        value.type = IPROP_INPUT_F32; value.flags = 1;
        CHECK(iprop_parameters_set(parameters, 1, &value, &error) == IPROP_EX_E_ARGUMENT);
        value.flags = 0; value.struct_size = 19;
        CHECK(iprop_parameters_set(parameters, 1, &value, &error) == IPROP_EX_E_ARGUMENT);
        value.struct_size = sizeof(value);
        got.flags = 1;
        CHECK(iprop_parameters_get(parameters, 1, &got, &error) == IPROP_EX_E_ARGUMENT);
        got.flags = 0;
        CHECK(iprop_parameters_get(parameters, UINT32_MAX, &got, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_get(parameters, 1, &got, nullptr) == IPROP_EX_OK && std::memcmp(&got, &before, sizeof(got)) == 0);
        CHECK(iprop_parameters_get(nullptr, 0, &got, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_set(nullptr, 0, &value, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_create(nullptr, &clone, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_create(program, nullptr, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_clone(nullptr, &clone, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_clone(parameters, nullptr, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_reset(nullptr, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_reset(emptyParameters, &error) == IPROP_EX_OK);
        CHECK(iprop_parameters_reset(parameters, &error) == IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters, 1, &got, &error) == IPROP_EX_OK && bits(got.f32) == UINT32_C(0x80000000));
    }
    CHECK(count == 0 && injected == 0 && message[0] == '\0' && error.status == IPROP_EX_OK);

    // Protected handle/value storage is rejected before even interpreting an
    // overlapping error descriptor's optional fields (ASan checks exact sizes).
    std::array<unsigned char, sizeof(iprop_parameters)> handleBefore;
    std::array<iprop::UniformValue, 8> valuesBefore;
    std::memcpy(handleBefore.data(), parameters, sizeof(*parameters));
    std::memcpy(valuesBefore.data(), parameters->values.data(), sizeof(valuesBefore));
    {
        AllocationGuard guard(1);
        CHECK(iprop_parameters_reset(parameters, reinterpret_cast<iprop_error_ex*>(parameters)) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_reset(parameters, reinterpret_cast<iprop_error_ex*>(parameters->values.data())) == IPROP_EX_E_ARGUMENT);
        auto* candidate = clone;
        CHECK(iprop_parameters_create(program, &candidate, reinterpret_cast<iprop_error_ex*>(program)) == IPROP_EX_E_ARGUMENT && candidate == clone);
        CHECK(iprop_parameters_clone(parameters, reinterpret_cast<iprop_parameters**>(parameters), &error) == IPROP_EX_E_ARGUMENT);
        error.message_buffer = reinterpret_cast<char*>(parameters->values.data()); error.message_capacity = 1;
        iprop_error_ex errorBefore = error;
        CHECK(iprop_parameters_reset(parameters, &error) == IPROP_EX_E_ARGUMENT && std::memcmp(&error, &errorBefore, sizeof(error)) == 0);
        CHECK(iprop_parameters_get(parameters, 0, &got, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_set(parameters, 1, &value, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_clone(parameters, &candidate, &error) == IPROP_EX_E_ARGUMENT && candidate == clone);
        error.message_buffer = nullptr;
        CHECK(iprop_parameters_create(program, &candidate, &error) == IPROP_EX_E_ARGUMENT && candidate == clone);
        CHECK(iprop_parameters_reset(parameters, &error) == IPROP_EX_E_ARGUMENT);
        error.message_buffer = message; error.message_capacity = sizeof(message);
    }
    CHECK(count == 0 && injected == 0);
    CHECK(std::memcmp(handleBefore.data(), parameters, sizeof(*parameters)) == 0);
    CHECK(std::memcmp(valuesBefore.data(), parameters->values.data(), sizeof(valuesBefore)) == 0);
    for (uint32_t size = 0; size <= sizeof(error) + 8; ++size) {
        const auto allocation = size < 4 ? 4 : size;
        auto* storage = static_cast<unsigned char*>(std::malloc(allocation));
        CHECK(storage != nullptr);
        std::memset(storage, 0xa5, allocation);
        std::memcpy(storage, &size, sizeof(size));
        iprop_status_ex status;
        {
            AllocationGuard guard(1);
            CHECK(iprop_error_ex_init(reinterpret_cast<iprop_error_ex*>(storage), size) == (size < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
            status = iprop_parameters_get(parameters, 1, &got, reinterpret_cast<iprop_error_ex*>(storage));
        }
        CHECK(status == (size < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK) && count == 0 && injected == 0);
        std::free(storage);
    }

    // Clone preserves NaN payload independently. Reset uses stored defaults
    // without allocation; a separate immutable fixture proves NaN defaults too.
    CHECK(iprop_parameters_set(parameters, 1, &value, nullptr) == IPROP_EX_OK);
    iprop_parameters_free(clone); clone = nullptr;
    CHECK(iprop_parameters_clone(parameters, &clone, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_reset(parameters, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_get(clone, 1, &got, nullptr) == IPROP_EX_OK && bits(got.f32) == nan);
    auto customFile = iprop::loadFile(bytes);
    std::memcpy(&customFile.meta.inputs[1].f32Default, &nan, sizeof(nan));
    const iprop_program custom{std::make_shared<const iprop::LoadedFile>(std::move(customFile))};
    iprop_parameters* customParameters = nullptr;
    CHECK(iprop_parameters_create(&custom, &customParameters, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_get(customParameters, 1, &got, nullptr) == IPROP_EX_OK && bits(got.f32) == nan);
    {
        AllocationGuard guard(1);
        CHECK(iprop_parameters_reset(customParameters, nullptr) == IPROP_EX_OK);
        CHECK(iprop_parameters_get(customParameters, 1, &got, nullptr) == IPROP_EX_OK && bits(got.f32) == nan);
    }
    CHECK(count == 0 && injected == 0);
    iprop_parameters_free(customParameters);

    iprop_program_free(program);
    CHECK(!retained.expired());
    CHECK(iprop_parameters_get_named(clone, {"zero", 4}, &got, nullptr) == IPROP_EX_OK && bits(got.f32) == nan);
    iprop_parameters_free(parameters);
    CHECK(!retained.expired());
    CHECK(iprop_parameters_reset(clone, &error) == IPROP_EX_OK);
    CHECK(iprop_parameters_get(clone, 1, &got, &error) == IPROP_EX_OK && bits(got.f32) == UINT32_C(0x80000000));
    iprop_parameters_free(clone);
    CHECK(retained.expired());
    iprop_parameters_free(emptyParameters); iprop_program_free(empty); iprop_program_free(reloaded);
    std::printf("%zu actual create/clone OOM points, internal exceptions, allocation-free updates/errors, identity and final-owner lifetime: PASS\n", faultCases);
    return 0;
}
