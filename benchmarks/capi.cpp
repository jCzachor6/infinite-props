#include "iprop.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
struct AllocationCount { uint64_t calls = 0, bytes = 0; };
thread_local AllocationCount allocations;
thread_local bool measuring = false;
void countAllocation(std::size_t size) noexcept {
    if (measuring) { ++allocations.calls; allocations.bytes += size; }
}
void* allocate(std::size_t size) {
    void* result = std::malloc(size == 0 ? 1 : size);
    if (!result) throw std::bad_alloc{};
    countAllocation(size);
    return result;
}
void* allocateAligned(std::size_t size, std::size_t alignment) {
    void* result = nullptr;
#ifdef _WIN32
    result = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    if (posix_memalign(&result, alignment, size == 0 ? 1 : size) != 0) result = nullptr;
#endif
    if (!result) throw std::bad_alloc{};
    countAllocation(size);
    return result;
}
void freeAligned(void* value) noexcept {
#ifdef _WIN32
    _aligned_free(value);
#else
    std::free(value);
#endif
}
using Clock = std::chrono::steady_clock;
struct Sample { double nanoseconds; AllocationCount allocations; };
template<class Function> Sample measure(Function&& function) {
    allocations = {};
    const auto start = Clock::now();
    measuring = true;
    try { function(); } catch (...) { measuring = false; throw; }
    measuring = false;
    const auto end = Clock::now();
    return {std::chrono::duration<double, std::nano>(end - start).count(), allocations};
}
void require(iprop_status status, const char* message) {
    if (status != IPROP_OK) throw std::runtime_error(message);
}
uint64_t hashResult(const iprop_image& image) {
    uint64_t hash = 14695981039346656037ull;
    const auto byte = [&](uint8_t value) { hash = (hash ^ value) * 1099511628211ull; };
    const auto integer = [&](uint32_t value) {
        for (unsigned shift = 0; shift != 32; shift += 8) byte(static_cast<uint8_t>(value >> shift));
    };
    integer(image.width); integer(image.height);
    for (uint64_t i = 0; i < uint64_t(image.width) * image.height * 4; ++i) byte(image.pixels[i]);
    integer(image.output_count);
    for (uint32_t i = 0; i < image.output_count; ++i) {
        const auto& output = image.outputs[i];
        const std::string_view name(output.name);
        integer(static_cast<uint32_t>(name.size()));
        for (unsigned char c : name) byte(c);
        integer(output.owner); integer(output.kind); integer(output.is_list); integer(output.count);
        const unsigned stride = output.kind == IPROP_OUTPUT_POINT ? 2 : output.kind == IPROP_OUTPUT_RECT ? 4 : 3;
        for (uint64_t j = 0; j < uint64_t(output.count) * stride; ++j)
            integer(std::bit_cast<uint32_t>(output.values[j]));
    }
    return hash;
}
void printSamples(std::string_view name, const std::vector<Sample>& samples) {
    std::cout << '"' << name << "\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i != 0) std::cout << ',';
        const auto& sample = samples[i];
        std::cout << "{\"ns\":" << sample.nanoseconds << ",\"cpp_new_calls\":" << sample.allocations.calls
                  << ",\"cpp_requested_bytes\":" << sample.allocations.bytes << '}';
    }
    std::cout << ']';
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, std::size_t(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, std::size_t(alignment)); }
void operator delete(void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: capi-benchmark file.iprop iterations\n"; return 2; }
    unsigned iterations = 0;
    const std::string_view count(argv[2]);
    const auto parsed = std::from_chars(count.data(), count.data() + count.size(), iterations);
    if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() || iterations == 0 || iterations > 10000)
        return 2;
    iprop_program* program = nullptr;
    iprop_result* result = nullptr;
    try {
        // Observe a deliberate allocation before trusting the counter.
        const auto control = measure([] { void* p = ::operator new(4096); ::operator delete(p); });
        if (control.allocations.calls != 1 || control.allocations.bytes != 4096)
            throw std::runtime_error("allocation counter control failed");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("cannot read fixture");
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        char error[512]{};
        std::vector<Sample> loads(iterations), generates(iterations);
        std::vector<uint64_t> hashes(iterations);
        std::vector<uint32_t> seeds(iterations);
        constexpr uint32_t seedSet[] = {0, 7, 26, 0xffffffffu};
        for (unsigned i = 0; i < iterations + 2; ++i) {
            const auto sample = measure([&] { require(iprop_program_load(bytes.data(), bytes.size(), &program, error, sizeof(error)), error); });
            iprop_program_free(program); program = nullptr;
            if (i >= 2) loads[i - 2] = sample;
        }
        require(iprop_program_load(bytes.data(), bytes.size(), &program, error, sizeof(error)), error);
        for (unsigned i = 0; i < iterations + 2; ++i) {
            iprop_generate_options options{};
            options.seed = seedSet[i >= 2 ? (i - 2) % 4 : i % 4];
            const auto sample = measure([&] { require(iprop_generate(program, &options, &result, error, sizeof(error)), error); });
            iprop_image image{};
            iprop_result_image(result, &image);
            if (i >= 2) { generates[i - 2] = sample; hashes[i - 2] = hashResult(image); seeds[i - 2] = options.seed; }
            iprop_result_free(result); result = nullptr;
        }
        iprop_meta meta{};
        iprop_program_meta(program, &meta);
        iprop_program_free(program); program = nullptr;
        std::cout << std::setprecision(17) << "{\"schema_version\":1,\"warmup\":2,\"iterations\":" << iterations
                  << ",\"width\":" << meta.size_x << ",\"height\":" << meta.size_y << ",\"fuel_budget\":" << meta.fuel << ',';
        printSamples("load", loads); std::cout << ','; printSamples("generate", generates);
        std::cout << ",\"results\":[";
        for (unsigned i = 0; i < iterations; ++i) {
            if (i != 0) std::cout << ',';
            std::cout << "{\"seed\":" << seeds[i] << ",\"fnv1a64\":\"" << std::hex << hashes[i] << std::dec << "\"}";
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        iprop_result_free(result); iprop_program_free(program);
        std::cerr << error.what() << '\n'; return 1;
    }
}
