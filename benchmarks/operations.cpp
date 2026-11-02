#include "iprop/VM.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

namespace {
// Benchmark-only allocation headers: track requested C++ bytes, including
// buffers that die during a native operation. This is neither RSS nor the
// runtime's logical resource charge. No production instrumentation is added.
struct Header { void* base; std::size_t size; std::uint64_t epoch; };
struct Counter { std::uint64_t calls = 0, bytes = 0, live = 0, peak = 0; };
thread_local Counter counter;
thread_local bool measuring = false;
thread_local std::uint64_t epoch = 0;

void* allocate(std::size_t size, std::size_t alignment) {
    alignment = std::max(alignment, alignof(Header));
    const auto overhead = sizeof(Header) + alignment - 1;
    if (size > std::numeric_limits<std::size_t>::max() - overhead) throw std::bad_alloc{};
    void* base = std::malloc(std::max<std::size_t>(size, 1) + overhead);
    if (!base) throw std::bad_alloc{};
    const auto address = (reinterpret_cast<std::uintptr_t>(base) + overhead) & ~(alignment - 1);
    auto* header = reinterpret_cast<Header*>(address) - 1;
    *header = {base, size, measuring ? epoch : 0};
    if (measuring) {
        ++counter.calls; counter.bytes += size; counter.live += size;
        counter.peak = std::max(counter.peak, counter.live);
    }
    return reinterpret_cast<void*>(address);
}
void release(void* value) noexcept {
    if (!value) return;
    const auto* header = static_cast<Header*>(value) - 1;
    if (measuring && header->epoch == epoch) counter.live -= header->size;
    std::free(header->base);
}
using Clock = std::chrono::steady_clock;
struct Sample { double wallNs, cpuNs; Counter memory; iprop::u64 fuel, hash; };
template<class Function> Sample measure(Function&& function) {
    counter = {}; ++epoch;
    const auto cpuStart = std::clock();
    const auto start = Clock::now();
    measuring = true;
    try { function(); } catch (...) { measuring = false; throw; }
    measuring = false;
    const auto end = Clock::now();
    const auto cpuEnd = std::clock();
    if (cpuStart == std::clock_t(-1) || cpuEnd == std::clock_t(-1))
        throw std::runtime_error("process CPU clock unavailable");
    return {std::chrono::duration<double, std::nano>(end - start).count(),
        double(cpuEnd - cpuStart) * 1e9 / CLOCKS_PER_SEC, counter, 0, 0};
}
iprop::u64 hashImage(const iprop::Canvas& image) {
    iprop::u64 hash = 14695981039346656037ull;
    for (const auto& c : image.pixels)
        for (const auto b : {c.r, c.g, c.b, c.a}) hash = (hash ^ b) * 1099511628211ull;
    return hash;
}
}

void* operator new(std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, std::size_t(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, std::size_t(a)); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: operations-benchmark file.iprop iterations\n"; return 2; }
    unsigned iterations = 0;
    const std::string_view count(argv[2]);
    const auto parsed = std::from_chars(count.data(), count.data() + count.size(), iterations);
    if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() || iterations == 0 || iterations > 10000)
        return 2;
    try {
        const auto control = measure([] {
            void* first = ::operator new(4096);
            void* second = ::operator new(1024, std::align_val_t{64});
            if (reinterpret_cast<std::uintptr_t>(second) % 64 != 0)
                throw std::runtime_error("allocation alignment control failed");
            ::operator delete(first);
            ::operator delete(second, std::align_val_t{64});
        });
        if (control.memory.calls != 2 || control.memory.bytes != 5120 || control.memory.peak != 5120 || control.memory.live != 0)
            throw std::runtime_error("allocation counter control failed");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("cannot read fixture");
        const std::vector<iprop::u8> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto file = iprop::loadFile(bytes);
        iprop::validate(file);
        std::vector<Sample> samples(iterations);
        constexpr unsigned warmup = 3;
        // std::clock can have a 1 us quantum. Amortize it across identical
        // generations, so inexpensive operations do not report zero CPU.
        constexpr unsigned batch = 16;
        for (unsigned i = 0; i < iterations + warmup; ++i) {
            iprop::GenerationResult result;
            iprop::u64 fuel = 0;
            auto sample = measure([&] {
                for (unsigned generation = 0; generation < batch; ++generation) {
                    result = {};
                    iprop::VM vm(file, iprop::GenerationInputs{7, 0, 0, {}});
                    vm.run();
                    const auto spent = vm.fuelBudget() - vm.fuel();
                    if (generation && spent != fuel) throw std::runtime_error("fuel changed within a sample");
                    fuel = spent;
                    result = vm.takeResult();
                }
            });
            sample.wallNs /= batch; sample.cpuNs /= batch;
            if (sample.memory.calls % batch || sample.memory.bytes % batch)
                throw std::runtime_error("allocation traffic changed within a sample");
            sample.memory.calls /= batch; sample.memory.bytes /= batch;
            sample.fuel = fuel;
            sample.hash = hashImage(result.image); // outside measured CPU/memory
            if (i >= warmup) samples[i - warmup] = sample;
        }
        std::cout << std::setprecision(17) << "{\"schema_version\":1,\"warmup\":" << warmup
                  << ",\"seed\":7,\"generations_per_sample\":" << batch << ",\"cpu_clock_ticks_per_second\":" << CLOCKS_PER_SEC
                  << ",\"iterations\":" << iterations << ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (i) std::cout << ',';
            const auto& s = samples[i];
            std::cout << "{\"wall_ns\":" << s.wallNs << ",\"cpu_ns\":" << s.cpuNs
                      << ",\"cpp_new_calls\":" << s.memory.calls << ",\"cpp_requested_bytes\":" << s.memory.bytes
                      << ",\"cpp_peak_live_bytes\":" << s.memory.peak << ",\"cpp_live_at_return_bytes\":" << s.memory.live
                      << ",\"fuel_total\":" << s.fuel << ",\"rgba_fnv1a64\":\"" << std::hex << s.hash << std::dec << "\"}";
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
