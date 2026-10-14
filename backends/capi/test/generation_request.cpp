#include "../Operations.hpp"
#include "check.h"

#include "iprop/Constants.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <vector>

namespace {
bool counting = false;
std::size_t allocations = 0;
}

void* operator new(std::size_t size) {
    if (counting) ++allocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

using namespace iprop;
using namespace iprop::capi;

namespace {
// The verdict of the strict check, or None when it accepts the request.
ErrorReason verdict(const iprop_program& program, const iprop_generate_options_ex* options,
                    const iprop_parameters* parameters) {
    GenerateOptions out;
    const auto outcome = checkGenerationRequest(program, options, parameters, out);
    return outcome.succeeded ? ErrorReason::None : outcome.argumentReason;
}

// The reason the VM constructor itself reports for the same scalar request.
ErrorReason vmVerdict(const LoadedFile& file, const iprop_generate_options_ex& options) {
    GenerationInputs inputs;
    inputs.seed = options.seed;
    inputs.sizeX = options.size_x;
    inputs.sizeY = options.size_y;
    try {
        VM vm(file, inputs);
        return ErrorReason::None;
    } catch (const Trap& trap) {
        return trap.stage == ErrorStage::Arguments ? trap.reason : ErrorReason::Internal;
    }
}
} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::ifstream fixture(argv[1], std::ios::binary);
    CHECK(fixture.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fixture)), {});
    iprop_program *program = nullptr, *reloaded = nullptr;
    iprop_parameters *own = nullptr, *foreign = nullptr;
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &program, nullptr) == IPROP_EX_OK);
    CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &reloaded, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_create(program, &own, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_create(reloaded, &foreign, nullptr) == IPROP_EX_OK);

    iprop_generate_options_ex options;
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 26;
    GenerateOptions out;
    CHECK(checkGenerationRequest(*program, &options, nullptr, out).succeeded);
    CHECK(verdict(*program, &options, own) == ErrorReason::None);
    CHECK(verdict(*program, &options, foreign) == ErrorReason::ForeignParameters);
    CHECK(verdict(*reloaded, &options, foreign) == ErrorReason::None);

    // Descriptor shape: null, smaller than four bytes and complete nonzero flags.
    // A four-byte prefix is a complete request for the source defaults.
    CHECK(verdict(*program, nullptr, nullptr) == ErrorReason::InvalidArgument);
    for (uint32_t size = 0; size < 4; ++size) {
        options.struct_size = size;
        CHECK(verdict(*program, &options, nullptr) == ErrorReason::InvalidArgument);
    }
    options.struct_size = sizeof(uint32_t);
    CHECK(checkGenerationRequest(*program, &options, nullptr, out).succeeded && out.seed == 0 && out.sizeX == 0 && out.sizeY == 0);
    options.struct_size = offsetof(iprop_generate_options_ex, size_y);
    options.size_y = UINT32_MAX; // absent field defaults to zero
    options.struct_size = sizeof(options); options.size_y = 0; options.flags = 1;
    CHECK(verdict(*program, &options, nullptr) == ErrorReason::InvalidArgument);
    options.struct_size = offsetof(iprop_generate_options_ex, flags) + 3; // incomplete flags are absent
    CHECK(verdict(*program, &options, nullptr) == ErrorReason::None);
    options.struct_size = sizeof(options); options.flags = 0;

    // Program-dependent ranges, including the exact canvas cap on each side.
    const struct { uint32_t sizeX, sizeY; ErrorReason reason; } cases[] = {
        {kMaxCanvasSide, kMaxCanvasSide, ErrorReason::None},
        {kMaxCanvasSide + 1, 0, ErrorReason::CanvasSize},
        {0, kMaxCanvasSide + 1, ErrorReason::CanvasSize},
        {UINT32_MAX, 1, ErrorReason::CanvasSize},
    };
    for (const auto& item : cases) {
        options.size_x = item.sizeX; options.size_y = item.sizeY;
        CHECK(verdict(*program, &options, own) == item.reason);
    }
    options.size_x = UINT32_MAX;
    CHECK(verdict(*program, &options, foreign) == ErrorReason::ForeignParameters);
    options.flags = 1;
    CHECK(verdict(*program, &options, foreign) == ErrorReason::InvalidArgument);
    options.flags = 0;

    // Failure leaves the previous output untouched.
    GenerateOptions sentinel;
    sentinel.seed = 1; sentinel.sizeX = 4; sentinel.sizeY = 5;
    out = sentinel;
    CHECK(!checkGenerationRequest(*program, &options, nullptr, out).succeeded);
    CHECK(std::memcmp(&out, &sentinel, sizeof(out)) == 0);

    // The strict verdict equals the VM's own setup check wherever the VM can
    // be built cheaply; it never needs a VM, a trap or an allocation.
    const uint32_t sizes[] = {0, 1, kMaxCanvasSide + 1, UINT32_MAX};
    std::size_t compared = 0;
    for (const auto sizeX : sizes) for (const auto sizeY : sizes) {
            options.size_x = sizeX; options.size_y = sizeY;
            const auto expected = vmVerdict(*program->data, options);
            allocations = 0; counting = true;
            const auto actual = verdict(*program, &options, own);
            counting = false;
            CHECK(actual == expected && allocations == 0);
            ++compared;
        }
    CHECK(compared == 4 * 4);

    // Through the checked boundary, each verdict is a published E_ARGUMENT at
    // ARGUMENTS stage without location; the legacy entry keeps E_TRAP.
    iprop_error_ex error;
    char message[64];
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    options.size_x = UINT32_MAX; options.size_y = 0;
    ErrorContext context{ErrorStage::Arguments, program->data.get()};
    allocations = 0; counting = true;
    const auto status = errorBoundary(&error, context,
        [&] { return checkGenerationRequest(*program, &options, own, out); });
    counting = false;
    CHECK(status == IPROP_EX_E_ARGUMENT && allocations == 0);
    CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_CANVAS_SIZE);
    CHECK(error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
    options.size_x = 0;
    CHECK(errorBoundary(&error, context, [&] { return checkGenerationRequest(*program, &options, foreign, out); }) ==
          IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_FOREIGN_PARAMETERS);
    iprop_generate_options legacy{};
    legacy.size_x = UINT32_MAX;
    iprop_result* result = nullptr;
    CHECK(iprop_generate(program, &legacy, &result, nullptr, 0) == IPROP_E_TRAP && result == nullptr);

    iprop_parameters_free(foreign); iprop_parameters_free(own);
    iprop_program_free(reloaded); iprop_program_free(program);
    std::printf("generation request: options/parameters/ranges rejected before VM with stable reasons, "
                "%zu VM oracle cases, zero allocations: PASS\n", compared);
    return 0;
}
