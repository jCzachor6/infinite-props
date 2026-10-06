#include "iprop.h"
#include "iprop/VM.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>

namespace { bool armed = false; std::size_t allocationAttempts = 0; }
void* operator new(std::size_t size) {
    if (armed) { ++allocationAttempts; throw std::bad_alloc{}; }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(c) do { if (!(c)) { armed = false; std::fprintf(stderr, "stats oracle: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 6);
    std::size_t cases = 0;
    for (int index = 1; index < argc; ++index) {
        std::ifstream input(argv[index], std::ios::binary);
        CHECK(input.good());
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto file = iprop::loadFile(bytes);
        iprop::validate(file);
        iprop_program* program = nullptr;
        CHECK(iprop_program_load_ex(bytes.data(), bytes.size(), &program, nullptr) == IPROP_EX_OK);
        for (uint32_t seed : {0u, 7u, 26u, UINT32_MAX}) {
            for (bool scaled : {false, true}) {
                iprop::GenerationInputs inputs;
                inputs.seed = seed;
                inputs.sizeX = scaled ? 13 : 0;
                inputs.sizeY = scaled ? 11 : 0;
                iprop::VM vm(file, inputs);
                vm.run();
                const auto fuelBudget = vm.fuelBudget();
                const auto fuelRemaining = vm.fuel();
                const auto generation = vm.takeResult();
                uint64_t elements = 0;
                for (const auto& slot : generation.outputs) {
                    switch (slot.kind) {
                    case iprop::OutputKind::Point: elements += slot.points.size(); break;
                    case iprop::OutputKind::Rect: elements += slot.rects.size(); break;
                    case iprop::OutputKind::Circle: elements += slot.circles.size(); break;
                    }
                }
                iprop_generate_options options{};
                options.seed = inputs.seed;
                options.size_x = inputs.sizeX;
                options.size_y = inputs.sizeY;
                iprop_result* result = nullptr;
                CHECK(iprop_generate(program, &options, &result, nullptr, 0) == IPROP_OK);
                iprop_result_stats_ex stats{};
                iprop_error_ex error{};
                char message[128];
                CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
                error.message_buffer = message; error.message_capacity = sizeof(message);
                allocationAttempts = 0;
                armed = true;
                CHECK(iprop_result_stats_ex_init(&stats, sizeof(stats)) == IPROP_EX_OK);
                CHECK(iprop_result_query_stats(result, &stats, &error) == IPROP_EX_OK);
                CHECK(stats.fuel_budget == fuelBudget && stats.fuel_remaining == fuelRemaining);
                CHECK(stats.fuel_used == fuelBudget - fuelRemaining && stats.output_element_count == elements);
                CHECK(stats.image_byte_count == uint64_t{generation.image.pixels.size()} * 4);
                CHECK(error.status == IPROP_EX_OK && message[0] == '\0');
                for (uint32_t capacity = 0; capacity <= sizeof(stats); ++capacity) {
                    unsigned char* storage = static_cast<unsigned char*>(std::malloc(capacity < 4 ? 4 : capacity));
                    CHECK(storage != nullptr);
                    // malloc is outside the C++ counter, while every query
                    // allocation would throw and be observed immediately.
                    std::memset(storage, 0, capacity < 4 ? 4 : capacity);
                    std::memcpy(storage, &capacity, 4);
                    CHECK(iprop_result_query_stats(result, reinterpret_cast<iprop_result_stats_ex*>(storage), &error) ==
                          (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
                    std::free(storage);
                }
                stats.flags = 1;
                CHECK(iprop_result_query_stats(result, &stats, &error) == IPROP_EX_E_ARGUMENT);
                stats.flags = 0;
                CHECK(iprop_result_query_stats(nullptr, &stats, &error) == IPROP_EX_E_ARGUMENT);
                CHECK(iprop_result_query_stats(result, nullptr, &error) == IPROP_EX_E_ARGUMENT);
                CHECK(iprop_result_query_stats(result, &stats, &error) == IPROP_EX_OK);
                armed = false;
                CHECK(allocationAttempts == 0);
                iprop_result_free(result);
                ++cases;
            }
        }
        iprop_program_free(program);
    }
    std::printf("%zu actual C generations match independent VM fuel/image/typed-element counters; all stats paths allocate nothing: PASS\n", cases);
}
