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
    if (result.outputs.size() != 7 || result.packed.size() != 13 || result.ownerCount != 4) return 3;
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
    // Public accessors must not reconstruct names/typed tables or allocate on
    // any success/expected argument error, including after source data died.
    iprop_result_output_view_ex outputView{};
    iprop_error_ex error{};
    char message[64];
    uint32_t count = UINT32_MAX, index = UINT32_MAX;
    iprop_point_value point{};
    iprop_rect_value rect{};
    iprop_circle_value circle{};
    if (iprop_error_ex_init(&error, sizeof(error)) != IPROP_EX_OK) return 9;
    error.message_buffer = message; error.message_capacity = sizeof(message);
    allocations = 0;
    armed = true;
    bool success = iprop_result_output_count(&result, &count, &error) == IPROP_EX_OK && count == 7;
    for (uint32_t i = 0; i < count; ++i) {
        success &= iprop_result_output_view_ex_init(&outputView, sizeof(outputView)) == IPROP_EX_OK;
        success &= iprop_result_output_get(&result, i, &outputView, &error) == IPROP_EX_OK;
        success &= outputView.values == result.outputs[i].values && outputView.name.data == result.outputs[i].name;
        success &= outputView.name.byte_count == names[i].size() && outputView.owner == owners[i];
        success &= iprop_result_output_find(&result, owners[i], outputView.name, &index, &error) == IPROP_EX_OK && index == i;
    }
    success &= iprop_result_output_point(&result, 0, 0, &point, sizeof(point), &error) == IPROP_EX_OK;
    success &= bits(point.x, 0x80000000) && bits(point.y, 1);
    success &= iprop_result_output_rect(&result, 1, 0, &rect, sizeof(rect), &error) == IPROP_EX_OK;
    success &= bits(rect.x, 0x7fc12345) && rect.y == 4 && rect.w == 5 && rect.h == 6;
    success &= iprop_result_output_circle(&result, 2, 0, &circle, sizeof(circle), &error) == IPROP_EX_OK;
    success &= circle.x == 7 && circle.y == 8 && circle.radius == 9;
    success &= iprop_result_output_point(&result, 3, 0, &point, sizeof(point), &error) == IPROP_EX_E_ARGUMENT;
    success &= iprop_result_output_point(&result, 1, 0, &point, sizeof(point), &error) == IPROP_EX_E_ARGUMENT;
    success &= iprop_result_output_point(&result, 0, 0, &point, sizeof(point) - 1, &error) == IPROP_EX_E_ARGUMENT;
    success &= iprop_result_output_get(&result, UINT32_MAX, &outputView, &error) == IPROP_EX_E_ARGUMENT;
    success &= iprop_result_output_find(&result, 1, {nullptr,0}, &index, &error) == IPROP_EX_E_ARGUMENT;
    success &= iprop_result_output_find(&result, 4, {nullptr,0}, &index, &error) == IPROP_EX_E_ARGUMENT;
    success &= iprop_result_output_count(nullptr, &count, &error) == IPROP_EX_E_ARGUMENT;
    armed = false;
    if (!success || allocations != 0) return 10;
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
        !empty.names.empty() || !empty.packed.empty() || !empty.outputs.empty() || empty.ownerCount != 1) return 8;
    std::puts("C result storage: moved data, stable packed views, empty transfer and noalloc result accessors passed");
}
