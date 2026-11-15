// The same public C entry used by WASM, for work/fuel/pixel parity gates.
// A fourth argument `derive` asks for a derived normal image with defaults.
#include "iprop.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

namespace {
uint64_t fnv(const uint8_t* bytes, uint64_t count) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint64_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4 && !(argc == 5 && std::string_view(argv[4]) == "derive")) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    iprop_program* raw = nullptr;
    if (iprop_program_load_ex(bytes.data(), bytes.size(), &raw, nullptr) != IPROP_EX_OK) return 3;
    std::unique_ptr<iprop_program, decltype(&iprop_program_free)> program(raw, iprop_program_free);
    iprop_execution_limits_ex limits{}; iprop_execution_limits_ex_init(&limits, sizeof(limits));
    const auto parse = [](const char* text, uint64_t& out) {
        const std::string_view source(text);
        const auto p = std::from_chars(source.data(), source.data() + source.size(), out);
        return p.ec == std::errc{} && p.ptr == source.data() + source.size();
    };
    if (!parse(argv[2], limits.work_limit) || !parse(argv[3], limits.timeout_ns)) return 2;
    iprop_generate_options_ex options{}; iprop_generate_options_ex_init(&options, sizeof(options));
    options.flags = IPROP_GENERATE_PART_OUTPUTS; options.seed = 26;
    iprop_error_ex error{}; iprop_error_ex_init(&error, sizeof(error));
    iprop_result* result = nullptr;
    iprop_derived_normal_ex derived{}; iprop_derived_normal_ex_init(&derived, sizeof(derived));
    const auto status = argc == 5
        ? iprop_generate_derived_normal(program.get(), &options, nullptr, &limits, &derived, &result, &error)
        : iprop_generate_controlled(program.get(), &options, nullptr, &limits, &result, &error);
    std::cout << "{\"status\":" << status << ",\"reason\":" << error.reason << ",\"stage\":" << error.stage
        << ",\"pc\":" << error.pc << ",\"locationFlags\":" << error.location_flags;
    if (result) {
        iprop_work_stats_ex work{}; iprop_work_stats_ex_init(&work, sizeof(work));
        iprop_result_stats_ex fuel{}; iprop_result_stats_ex_init(&fuel, sizeof(fuel));
        iprop_image_view_ex image{}; iprop_image_view_ex_init(&image, sizeof(image));
        if (iprop_result_query_work(result, &work, nullptr) != IPROP_EX_OK ||
            iprop_result_query_stats(result, &fuel, nullptr) != IPROP_EX_OK ||
            iprop_result_query_image(result, &image, nullptr) != IPROP_EX_OK) { iprop_result_free(result); return 4; }
        std::cout << ",\"workBudget\":" << work.work_budget << ",\"workUsed\":" << work.work_used
            << ",\"fuelUsed\":" << fuel.fuel_used << ",\"rgbaHash\":\"" << std::hex
            << fnv(image.pixels, image.byte_count) << std::dec << "\"";
        // Auxiliary images are reported only when a result carries one.
        uint32_t images = 0, index = 0;
        iprop_aux_image_view_ex normal{}; iprop_aux_image_view_ex_init(&normal, sizeof(normal));
        if (iprop_result_aux_image_count(result, &images, nullptr) != IPROP_EX_OK) { iprop_result_free(result); return 4; }
        if (images != 0) {
            if (iprop_result_aux_image_find(result, IPROP_AUX_IMAGE_NORMAL, &index, nullptr) != IPROP_EX_OK ||
                iprop_result_aux_image_get(result, index, &normal, nullptr) != IPROP_EX_OK) { iprop_result_free(result); return 4; }
            std::cout << ",\"auxImages\":" << images << ",\"normalSource\":" << normal.source << ",\"normalHash\":\""
                << std::hex << fnv(normal.pixels, normal.byte_count) << std::dec << "\"";
        }
        iprop_result_free(result);
    }
    std::cout << "}\n";
    return 0;
}
