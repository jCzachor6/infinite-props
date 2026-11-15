// Calibration of the normal map tariff (spec normal maps v1.1, Accounting):
// CPU time per fuel unit of surface.normal against blur, and against other
// raster kernels, on the same surfaces. Each row is the median generation of
// a program minus the median of a control without the operation.
//
//   cmake --build build-perf --target iprop_normal_map_tariff
//   build-perf/benchmarks/iprop_normal_map_tariff
//
// The output is a table for people; nothing asserts a timing.
#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace iprop;

struct Sample { double ns; u64 fuel; };

static Sample measure(const std::string& body, u32 side) {
    const auto source = ips::parse("asset B { size " + std::to_string(side) + "x" + std::to_string(side) +
        " draw { layer image(alpha=surface.source_over) { oval(" + std::to_string(side / 2) + "," + std::to_string(side / 2) + "," +
        std::to_string(side / 3) + "," + std::to_string(side / 3) + ",#c0a060) } " + body + " } }");
    ips::checkAsset(source, {});
    auto file = loadFile(compileProgram(source, {}, false));
    validate(file);
    file.meta.fuel = 4'000'000'000ull;
    GenerationInputs inputs; inputs.limits.workLimit = 4'000'000'000ull;
    u64 fuel = 0;
    std::vector<double> times;
    const int rounds = side >= 256 ? 9 : 15, batch = side >= 256 ? 8 : side >= 128 ? 24 : 96;
    for (int round = 0; round < rounds + 2; ++round) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < batch; ++i) {
            VM vm(file, inputs); vm.run();
            fuel = vm.fuelBudget() - vm.fuel();
        }
        const auto stop = std::chrono::steady_clock::now();
        if (round >= 2) times.push_back(std::chrono::duration<double, std::nano>(stop - start).count() / batch);
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], fuel};
}

int main() {
    std::printf("%-5s %-6s %-22s %12s %12s %10s\n", "side", "sigma", "operation", "ns/op", "fuel/op", "ns/fuel");
    for (const u32 side : {64u, 128u, 256u}) {
        const auto control = measure("m = mask.from(image)", side);
        for (const char* sigma : {"0.0", "1.5", "4.0", "12.0"}) {
            const std::string s = sigma;
            const std::pair<const char*, std::string> cases[] = {
                {"layer.blur", "m = mask.from(image) b = image.blur(" + s + ")"},
                {"mask.blur", "m = mask.from(image) b = m.blur(" + s + ")"},
                {"layer.normal", "m = mask.from(image) n = image.normal(4.0, " + s + ")"},
                {"mask.normal", "m = mask.from(image) n = m.normal(4.0, " + s + ")"},
            };
            for (const auto& [name, body] : cases) {
                const auto sample = measure(body, side);
                const double ns = sample.ns - control.ns;
                const double fuel = static_cast<double>(sample.fuel) - static_cast<double>(control.fuel);
                std::printf("%-5u %-6s %-22s %12.0f %12.0f %10.3f\n", side, sigma, name, ns, fuel, fuel > 0 ? ns / fuel : 0.0);
            }
        }
    }
    // Reference points from other raster kernels at the same size.
    for (const u32 side : {128u}) {
        const auto control = measure("m = mask.from(image)", side);
        const std::pair<const char*, std::string> cases[] = {
            {"mask.expand r=4", "m = mask.from(image) e = mask.expand(m, 4)"},
            {"surface.paint layer", "m = mask.from(image) surface.paint(image)"},
            {"noise.mask fbm", "m = mask.from(image) n = noise.mask({0,0},{128,128},noise.mode_fbm,{0.1,0.1},0,seed)"},
        };
        for (const auto& [name, body] : cases) {
            const auto sample = measure(body, side);
            const double ns = sample.ns - control.ns, fuel = static_cast<double>(sample.fuel) - static_cast<double>(control.fuel);
            std::printf("%-5u %-6s %-22s %12.0f %12.0f %10.3f\n", side, "-", name, ns, fuel, ns / fuel);
        }
    }
}
