// Keep allocation and lifetime observations outside the test framework.
#include "../Operations.hpp"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>

namespace {
bool armed = false;
std::size_t allocations = 0;
std::array<const void*, 5> watched{};
std::array<bool, 5> released{};

void observeDelete(void* pointer) noexcept {
    for (std::size_t i = 0; i < watched.size(); ++i)
        if (pointer != nullptr && pointer == watched[i]) released[i] = true;
}
float value(uint32_t bits) { return std::bit_cast<float>(bits); }
bool bits(float actual, uint32_t expected) { return std::bit_cast<uint32_t>(actual) == expected; }
}

void* operator new(std::size_t size) {
    if (armed) ++allocations;
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { observeDelete(pointer); std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

int main() {
    iprop_result result;
    std::array<std::string, 7> names;
    std::array<const char*, 7> nameBuffers{};
    const iprop::Color* pixels = nullptr;
    {
        iprop::LoadedFile file;
        file.meta.outputs.resize(5);
        file.code.parts.resize(3);
        // Empty part zero must still consume owner 1.
        file.code.parts[1].outputs.resize(1);
        file.code.parts[2].outputs.resize(1);
        iprop::GenerationResult generation;
        generation.image.resize(2, 1);
        generation.image.pixels[0] = {1, 2, 3, 4};
        generation.image.pixels[1] = {255, 254, 253, 252};
        pixels = generation.image.pixels.data();
        generation.outputs.resize(7);
        const iprop::OutputKind kinds[] = {
            iprop::OutputKind::Point, iprop::OutputKind::Rect, iprop::OutputKind::Circle,
            iprop::OutputKind::Point, iprop::OutputKind::Circle,
            iprop::OutputKind::Point, iprop::OutputKind::Rect};
        for (std::size_t i = 0; i < generation.outputs.size(); ++i) {
            names[i] = std::string(48, static_cast<char>('a' + i)) + "\xc5\x82";
            names[i].push_back('\0');
            names[i] += "suffix";
            auto& output = generation.outputs[i];
            output.name = names[i];
            nameBuffers[i] = output.name.data();
            output.kind = kinds[i];
            output.isList = i != 1 && i != 2 && i != 3;
        }
        generation.outputs[0].points = {{value(0x80000000), value(1)}, {2, 3}};
        generation.outputs[1].rects = {{value(0x7fc12345), 4, 5, 6}};
        generation.outputs[2].circles = {{7, 8, 9}};
        generation.outputs[5].points = {{10, 11}};
        watched = {generation.outputs[0].points.data(), generation.outputs[1].rects.data(),
                   generation.outputs[2].circles.data(), generation.outputs[5].points.data(),
                   generation.outputs.data()};
        armed = true;
        iprop::capi::packResult(result, std::move(generation), file);
        armed = false;
    }
    // Only destination name, packed-value and C-slot tables allocate. Long
    // name copies would each allocate again. Payloads and the C++ slot table
    // have already been released, while every published C view stays valid.
    if (allocations != 3 || result.image.pixels.data() != pixels) return 1;
    for (bool freed : released) if (!freed) return 2;
    if (result.outputs.size() != 7 || result.packed.size() != 13) return 3;
    const uint32_t owners[] = {0, 0, 0, 0, 0, 2, 3};
    const uint32_t counts[] = {2, 1, 1, 0, 0, 1, 0};
    const iprop_output_kind kinds[] = {IPROP_OUTPUT_POINT, IPROP_OUTPUT_RECT,
        IPROP_OUTPUT_CIRCLE, IPROP_OUTPUT_POINT, IPROP_OUTPUT_CIRCLE,
        IPROP_OUTPUT_POINT, IPROP_OUTPUT_RECT};
    const std::size_t starts[] = {0, 4, 8, 11, 11, 11, 13};
    for (std::size_t i = 0; i < result.outputs.size(); ++i) {
        const auto& output = result.outputs[i];
        if (result.names[i] != names[i] || result.names[i].data() != nameBuffers[i] ||
            output.name != result.names[i].data() || output.owner != owners[i] ||
            output.kind != kinds[i] || output.is_list != (i != 1 && i != 2 && i != 3) ||
            output.count != counts[i] || output.values != result.packed.data() + starts[i]) return 4;
    }
    if (!bits(result.packed[0], 0x80000000) || !bits(result.packed[1], 1) ||
        !bits(result.packed[4], 0x7fc12345)) return 5;
    const float expected[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const std::size_t indices[] = {2, 3, 5, 6, 7, 8, 9, 10, 11, 12};
    for (std::size_t i = 0; i < std::size(expected); ++i)
        if (result.packed[indices[i]] != expected[i]) return 6;
    const auto view = iprop::capi::resultImage(result);
    if (view.width != 2 || view.height != 1 || view.output_count != 7 ||
        view.pixels[0] != 1 || view.pixels[3] != 4 || view.pixels[4] != 255 ||
        view.pixels[7] != 252) return 7;

    iprop_result empty;
    iprop::GenerationResult noOutputs;
    noOutputs.image.resize(1, 1);
    const auto* emptyPixels = noOutputs.image.pixels.data();
    allocations = 0;
    armed = true;
    iprop::capi::packResult(empty, std::move(noOutputs), {});
    armed = false;
    if (allocations != 0 || empty.image.pixels.data() != emptyPixels ||
        !empty.names.empty() || !empty.packed.empty() || !empty.outputs.empty()) return 8;
    std::puts("C result storage: moved pixels/names, released typed buffers, stable packed views and empty transfer passed");
}
