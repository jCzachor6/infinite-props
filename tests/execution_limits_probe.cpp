// The same public C entry used by WASM, for work/fuel/pixel parity gates.
#include "iprop.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
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
    const auto status = iprop_generate_controlled(program.get(), &options, nullptr, &limits, &result, &error);
    std::cout << "{\"status\":" << status << ",\"reason\":" << error.reason << ",\"stage\":" << error.stage
        << ",\"pc\":" << error.pc << ",\"locationFlags\":" << error.location_flags;
    if (result) {
        iprop_work_stats_ex work{}; iprop_work_stats_ex_init(&work, sizeof(work));
        iprop_result_stats_ex fuel{}; iprop_result_stats_ex_init(&fuel, sizeof(fuel));
        iprop_image_view_ex image{}; iprop_image_view_ex_init(&image, sizeof(image));
        if (iprop_result_query_work(result, &work, nullptr) != IPROP_EX_OK ||
            iprop_result_query_stats(result, &fuel, nullptr) != IPROP_EX_OK ||
            iprop_result_query_image(result, &image, nullptr) != IPROP_EX_OK) { iprop_result_free(result); return 4; }
        uint64_t hash = UINT64_C(14695981039346656037);
        for (uint64_t i = 0; i < image.byte_count; ++i) hash = (hash ^ image.pixels[i]) * UINT64_C(1099511628211);
        std::cout << ",\"workBudget\":" << work.work_budget << ",\"workUsed\":" << work.work_used
            << ",\"fuelUsed\":" << fuel.fuel_used << ",\"rgbaHash\":\"" << std::hex << hash << std::dec << "\"";
        iprop_result_free(result);
    }
    std::cout << "}\n";
    return 0;
}
