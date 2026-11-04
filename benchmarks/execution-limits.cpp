// Benchmark-only observations through the public generation entry. No
// production clock instrumentation or new runtime dependency is required.
#include "iprop.h"
#include "../tools/json.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using Json = iprop::tools::Json;
using Result = std::unique_ptr<iprop_result, decltype(&iprop_result_free)>;
struct Probe {
    Clock::time_point previous;
    std::uint64_t polls = 0, maximumNs = 0;
    void record(Clock::time_point now) {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now-previous).count();
        maximumNs = std::max(maximumNs, static_cast<std::uint64_t>(ns));
        previous = now;
    }
    static std::uint32_t poll(void* data) {
        auto& self = *static_cast<Probe*>(data);
        self.record(Clock::now()); ++self.polls;
        return 0;
    }
};
Json sample(const iprop_program* program, std::uint64_t workLimit, bool observe) {
    iprop_generate_options_ex options;
    iprop_generate_options_ex_init(&options,sizeof(options));
    options.flags=IPROP_GENERATE_PART_OUTPUTS; options.seed=7;
    iprop_execution_limits_ex limits;
    iprop_execution_limits_ex_init(&limits,sizeof(limits)); limits.work_limit=workLimit;
    iprop_error_ex error;
    iprop_error_ex_init(&error,sizeof(error));
    Probe probe{};
    if(observe) { limits.poll=Probe::poll; limits.poll_context=&probe; }
    iprop_result* raw=nullptr;
    const auto cpuStart=std::clock();
    const auto start=Clock::now(); probe.previous=start;
    const auto status=iprop_generate_controlled(program,&options,nullptr,&limits,&raw,&error);
    const auto end=Clock::now();
    const auto cpuEnd=std::clock();
    const auto tailNs=std::chrono::duration_cast<std::chrono::nanoseconds>(end-probe.previous).count();
    if(cpuStart == std::clock_t(-1) || cpuEnd == std::clock_t(-1))
        throw std::runtime_error("process CPU clock unavailable");
    Result result(raw,iprop_result_free);
    if((status == IPROP_EX_OK) != (raw != nullptr)) throw std::runtime_error("partial result publication");
    auto output=Json::object({{"status",status},{"reason",error.reason},{"stage",error.stage},
        {"wall_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()},
        {"cpu_ns",static_cast<std::uint64_t>(double(cpuEnd-cpuStart)*1e9/CLOCKS_PER_SEC)}});
    if(observe) {
        output["polls"]=probe.polls; output["max_poll_gap_ns"]=probe.maximumNs;
        output["post_poll_ns"]=tailNs;
    }
    if(result) {
        iprop_work_stats_ex work; iprop_work_stats_ex_init(&work,sizeof(work));
        iprop_result_stats_ex fuel; iprop_result_stats_ex_init(&fuel,sizeof(fuel));
        iprop_image_view_ex image; iprop_image_view_ex_init(&image,sizeof(image));
        if(iprop_result_query_work(raw,&work,nullptr) != IPROP_EX_OK ||
           iprop_result_query_stats(raw,&fuel,nullptr) != IPROP_EX_OK ||
           iprop_result_query_image(raw,&image,nullptr) != IPROP_EX_OK) throw std::runtime_error("result query");
        std::uint64_t hash=UINT64_C(14695981039346656037);
        for(std::uint64_t i=0;i<image.byte_count;++i) hash=(hash^image.pixels[i])*UINT64_C(1099511628211);
        output["work_budget"]=work.work_budget; output["work_used"]=work.work_used;
        output["fuel_used"]=fuel.fuel_used; output["rgba_fnv1a64"]=std::to_string(hash);
    }
    return output;
}
std::uint64_t number(const char* text) {
    const std::string_view input(text); std::uint64_t value=0;
    const auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);
    if(parsed.ec != std::errc{} || parsed.ptr != input.data()+input.size()) throw std::runtime_error("invalid number");
    return value;
}
}
int main(int argc,char** argv) {
    try {
        if(argc != 4) throw std::runtime_error("usage: execution-limits-benchmark file.iprop iterations work-limit");
        const auto iterations=number(argv[2]), limit=number(argv[3]);
        if(!iterations || iterations > 100) throw std::runtime_error("iterations must be in 1..100");
        std::ifstream input(argv[1],std::ios::binary);
        if(!input) throw std::runtime_error("cannot read program");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        iprop_program* raw=nullptr;
        if(iprop_program_load_ex(bytes.data(),bytes.size(),&raw,nullptr) != IPROP_EX_OK) throw std::runtime_error("load");
        const std::unique_ptr<iprop_program,decltype(&iprop_program_free)> program(raw,iprop_program_free);
        auto plain=Json::array(), probed=Json::array();
        for(std::uint64_t i=0;i<iterations+3;++i) {
            auto a=sample(program.get(),limit,false), b=sample(program.get(),limit,true);
            if(i >= 3) { plain.push_back(std::move(a)); probed.push_back(std::move(b)); }
        }
        std::cout << Json::object({{"warmup",3},{"iterations",iterations},{"seed",7},
            {"plain",std::move(plain)},{"probed",std::move(probed)}}).dump() << '\n';
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
