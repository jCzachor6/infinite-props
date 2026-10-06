// Isolate allocator replacement from Catch2 and count only result transfer.
#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
bool armed = false;
std::size_t allocations = 0, failAt = std::numeric_limits<std::size_t>::max();
}
void* operator new(std::size_t size) {
    if (armed && allocations++ >= failAt) throw std::bad_alloc{};
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    const auto program = iprop::ips::parse(R"(asset Transfer { size 16x16;
      output list point long_point_output_name_with_allocated_storage;
      output list rect long_rectangle_output_name_with_allocated_storage;
      output list circle long_circle_output_name_with_allocated_storage;
      draw { for i in 0..64 {
        long_point_output_name_with_allocated_storage = {i, i + 1};
        long_rectangle_output_name_with_allocated_storage = {i, i + 1, 2, 3};
        long_circle_output_name_with_allocated_storage = {i, i + 1, 4};
      } rect(0,0,16,16,#fc8); } })");
    iprop::ips::checkAsset(program, {});
    const auto file = iprop::loadFile(iprop::compileProgram(program, {}));
    iprop::validate(file);
    iprop::VM vm(file, {});
    vm.run();
    const auto expected = vm.result();
    const auto* pixels = vm.canvas().pixels.data();
    const auto remaining = vm.fuel();
    failAt = 0; allocations = 0; armed = true;
    bool failed = false;
    try { auto result = vm.takeResult(); armed = false; }
    catch (const std::bad_alloc&) { armed = false; failed = true; }
    catch (...) { armed = false; return 1; }
    if (!failed || allocations != 1 || vm.canvas().pixels.data() != pixels || vm.fuel() != remaining) return 2;
    const auto preserved = vm.result();
    if (preserved.image.pixels != expected.image.pixels || preserved.outputs.size() != 3) return 3;
    for (std::size_t i = 0; i < 3; ++i)
        if (preserved.outputs[i].name != expected.outputs[i].name ||
            preserved.outputs[i].points.size() != expected.outputs[i].points.size() ||
            preserved.outputs[i].rects.size() != expected.outputs[i].rects.size() ||
            preserved.outputs[i].circles.size() != expected.outputs[i].circles.size()) return 4;
    // Keep failure armed at the *second* allocation. Payload or name copies
    // would allocate and fail; only the destination slot table may allocate.
    failAt = 1; allocations = 0; armed = true;
    auto moved = vm.takeResult();
    armed = false;
    if (allocations != 1 || moved.image.pixels.data() != pixels ||
        moved.image.pixels != expected.image.pixels || moved.outputs.size() != 3) return 5;
    for (std::size_t i = 0; i < 3; ++i)
        if (moved.outputs[i].name != expected.outputs[i].name) return 6;
    if (moved.outputs[0].points.size() != 64 || moved.outputs[1].rects.size() != 64 ||
        moved.outputs[2].circles.size() != 64) return 7;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto x = static_cast<iprop::f32>(i);
        const auto& p = moved.outputs[0].points[i];
        const auto& r = moved.outputs[1].rects[i];
        const auto& c = moved.outputs[2].circles[i];
        if (p.x != x || p.y != x + 1 || r.x != x || r.y != x + 1 || r.w != 2 || r.h != 3 ||
            c.x != x || c.y != x + 1 || c.r != 4) return 7;
    }
    const auto empty = iprop::ips::parse("asset Empty { size 16x16; draw {} }");
    iprop::ips::checkAsset(empty, {});
    const auto emptyFile = iprop::loadFile(iprop::compileProgram(empty, {}));
    iprop::validate(emptyFile);
    iprop::VM emptyVm(emptyFile, {});
    emptyVm.run();
    allocations = 0; failAt = 0; armed = true;
    auto emptyResult = emptyVm.takeResult();
    armed = false;
    if (allocations != 0 || !emptyResult.outputs.empty() || emptyResult.image.pixels.size() != 256) return 8;
    std::puts("VM result transfer: one slot-table allocation, zero payload/name copies; OOM retry and no-output transfer passed");
}
