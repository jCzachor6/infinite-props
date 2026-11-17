// Calibration of the model tariff (spec models v1.2, Accounting): CPU time
// per fuel unit of the model page's primitives, placements and skin calls,
// against plain instructions and the raster kernels of the 2D tariff, on the
// same machine in the same run. Each row is the median generation of a
// program with N copies of one statement minus a control without them,
// divided by N; packing the result is inside the timed interval, so a
// vertex's price covers its share of the result too.
//
//   cmake --build build-perf --target iprop_model_tariff
//   build-perf/benchmarks/iprop_model_tariff
//
// The output is a table for people; nothing asserts a timing.
#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
#include "iprop/model/Model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iprop;

namespace {

struct Sample { double ns; u64 fuel; u32 vertices; u32 triangles; };

const std::map<std::string, std::string> kSkins = {
    // A tiny skin: what a texture costs is charged to its own program.
    {"tile.ips", "asset tile {\n  size 8x8\n  input color tint = #a3713d\n  draw { rect(0, 0, 8, 8, tint) }\n}\n"},
    // A part of one box (M3).
    {"peg.ips", "import tile from \"tile.ips\"\nasset peg {\n  size 2x4x2\n  output point3d tip\n"
                "  render {\n    box({0, 0, 0}, {2, 3, 4}, tile.draw(1))\n    tip = {0, 3, 0}\n  }\n}\n"},
};

std::vector<u8> compile(const std::string& source) {
    auto files = kSkins;
    files["bench.ips"] = source;
    const auto compiled = compileSource("bench.ips", [&](std::string_view path) {
        const auto found = files.find(std::string(path));
        if (found == files.end()) throw std::runtime_error("no source " + std::string(path));
        return found->second;
    });
    if (!compiled.succeeded())
        throw std::runtime_error(compiled.diagnostics.empty() ? "compilation failed" : compiled.diagnostics.front().message);
    return compiled.bytes;
}

template<class Run> std::vector<double> time(Run&& run, int rounds, int batch) {
    std::vector<double> times;
    for (int round = 0; round < rounds + 2; ++round) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < batch; ++i) run();
        const auto stop = std::chrono::steady_clock::now();
        if (round >= 2) times.push_back(std::chrono::duration<double, std::nano>(stop - start).count() / batch);
    }
    std::sort(times.begin(), times.end());
    return times;
}

std::string repeated(const std::string& statement, int count) {
    std::string text;
    for (int i = 0; i < count; ++i) text += "    " + statement + "\n";
    return text;
}

// A model whose render block makes one skin and then runs `body`.
Sample solid(const std::string& body) {
    auto loaded = model::loadModel(compile("import tile from \"tile.ips\"\nimport peg from \"peg.ips\"\nasset bench {\n"
                                           "  size 10x10x10\n  output point3d p\n  output list box3d b\n  render {\n"
                                           "    s = tile.draw(1)\n    v = {0.5, 0.25, 0.125}\n    x = 0.0\n" + body + "  }\n}\n"));
    model::validateModel(loaded);
    loaded.file.meta.fuel = 4'000'000'000ull;
    model::ModelInputs inputs;
    inputs.limits.workLimit = 4'000'000'000ull;
    model::ModelResult result;
    const auto times = time([&] { result = model::generateModel(loaded, inputs); }, 9, 24);
    return {times[times.size() / 2], result.fuelUsed, result.vertexCount, result.triangleCount};
}

// The same for a 2D asset, as the reference band.
Sample flat(const std::string& body) {
    auto file = loadFile(compile("asset bench {\n  size 64x64\n  draw {\n    x = 0.0\n" + body + "  }\n}\n"));
    validate(file);
    file.meta.fuel = 4'000'000'000ull;
    GenerationInputs inputs;
    inputs.limits.workLimit = 4'000'000'000ull;
    u64 fuel = 0;
    const auto times = time([&] { VM vm(file, inputs); vm.run(); fuel = vm.fuelBudget() - vm.fuel(); }, 9, 24);
    return {times[times.size() / 2], fuel, 0, 0};
}

void row(const char* name, int count, const Sample& sample, const Sample& control) {
    const double ns = (sample.ns - control.ns) / count;
    const double fuel = (static_cast<double>(sample.fuel) - static_cast<double>(control.fuel)) / count;
    const double vertices = static_cast<double>(sample.vertices - control.vertices) / count;
    const double triangles = static_cast<double>(sample.triangles - control.triangles) / count;
    std::printf("%-34s %5d %8.0f %8.0f %10.0f %10.0f %8.2f\n", name, count, vertices, triangles, ns, fuel,
                fuel > 0 ? ns / fuel : 0.0);
}

} // namespace

int main() {
    std::printf("%-34s %5s %8s %8s %10s %10s %8s\n", "statement", "N", "v/call", "t/call", "ns/call", "fuel/call", "ns/fuel");

    std::printf("-- 2D references\n");
    const auto flatControl = flat("");
    row("x = x + 1.0", 512, flat(repeated("x = x + 1.0", 512)), flatControl);
    row("rect 2x2", 256, flat(repeated("rect(1, 1, 2, 2, #ffffff)", 256)), flatControl);
    row("rect 16x16", 64, flat(repeated("rect(8, 8, 16, 16, #ffffff)", 64)), flatControl);
    row("oval r=8 (75%)", 64, flat(repeated("oval(32, 32, 8, 8, #ffffff)", 64)), flatControl);

    std::printf("-- model program\n");
    const auto control = solid("");
    row("x = x + 1.0", 512, solid(repeated("x = x + 1.0", 512)), control);
    row("v = v + v * 0.5", 512, solid(repeated("v = v + v * 0.5", 512)), control);
    row("vec3.normalize", 512, solid(repeated("v = vec3.normalize(v)", 512)), control);
    row("tri", 512, solid(repeated("tri({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, s)", 512)), control);
    row("quad", 512, solid(repeated("quad({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, s)", 512)), control);
    row("box", 256, solid(repeated("box({0, 0, 0}, {2, 3, 4}, s)", 256)), control);
    row("box with bevel", 128, solid(repeated("box({0, 0, 0}, {2, 3, 4}, 0.25, s)", 128)), control);
    for (const int sides : {8, 16, 32, 64}) {
        const auto name = "cylinder, " + std::to_string(sides) + " sides";
        const int count = 2048 / sides;
        row(name.c_str(), count, solid(repeated("cylinder({0, 0, 0}, 1.0, 2.0, " + std::to_string(sides) + ", s)", count)), control);
    }
    row("cone, 16 sides", 128, solid(repeated("cone({0, 0, 0}, 1.0, 0.5, 2.0, 16, s)", 128)), control);
    row("band, 16 sides", 128, solid(repeated("band({0, 0, 0}, 1.0, 0.5, 2.0, 16, s)", 128)), control);
    row("sphere, 16x8", 64, solid(repeated("sphere({0, 0, 0}, 1.0, 16, 8, s)", 64)), control);
    row("sphere, 32x16", 32, solid(repeated("sphere({0, 0, 0}, 1.0, 32, 16, s)", 32)), control);
    const std::string profile = "[{0, 0}, {0.55, 0}, {0.85, 0.2}, {0.5, 0.5}, {0.64, 1.6}, {0.98, 3.1}, {0.58, 3.5}, {1, 3.75}, {1, 5}]";
    row("lathe, 9 points, 12 sides", 64, solid(repeated("lathe({0, 0, 0}, " + profile + ", 12, s)", 64)), control);
    row("lathe, crease 40", 64, solid(repeated("lathe({0, 0, 0}, " + profile + ", 12, 40.0, s)", 64)), control);
    const std::string house = "[{-5, 0}, {5, 0}, {5, 6}, {0, 11}, {-5, 6}]";
    row("extrude, 5 points", 256, solid(repeated("extrude({0, 0, 0}, " + house + ", 4.0, s)", 256)), control);
    row("extrude, 5 points, bevel", 128, solid(repeated("extrude({0, 0, 0}, " + house + ", 4.0, 0.5, s)", 128)), control);
    // A concave star: the triangulation and the contact checks grow faster
    // than the vertex count.
    for (const int points : {16, 64, 128, 256}) {
        std::string star = "[";
        for (int i = 0; i < points; ++i) {
            const double angle = 6.283185307179586 * i / points;
            const double radius = i % 2 == 0 ? 10.0 : 6.0;
            char point[64];
            std::snprintf(point, sizeof(point), "%s{%.4f, %.4f}", i ? ", " : "", radius * std::cos(angle), radius * std::sin(angle));
            star += point;
        }
        star += "]";
        const int count = std::max(1, 2048 / points);
        const auto name = "extrude, star of " + std::to_string(points);
        row(name.c_str(), count, solid(repeated("extrude({0, 0, 0}, " + star + ", 4.0, s)", count)), control);
        const auto bevelled = name + ", bevel";
        row(bevelled.c_str(), count, solid(repeated("extrude({0, 0, 0}, " + star + ", 4.0, 0.3, s)", count)), control);
    }
    // A comb of 256 points, from the bottom left: the ear search starts at
    // the base every time and walks the teeth, the triangulation's worst
    // ordering among the outlines measured here.
    {
        std::string comb = "[{0, 0}, {253, 0}";
        for (int i = 62; i >= 0; --i) {
            const int x = i * 4;
            comb += ", {" + std::to_string(x + 2) + ", 12}, {" + std::to_string(x) + ", 12}, {" + std::to_string(x) + ", 1}";
            if (i > 0) comb += ", {" + std::to_string(x - 1) + ", 1}";
        }
        comb += "]";
        row("extrude, comb of 253", 8, solid(repeated("extrude({0, 0, 0}, " + comb + ", 4.0, s)", 8)), control);
        row("extrude, comb of 253, bevel", 8, solid(repeated("extrude({0, 0, 0}, " + comb + ", 4.0, 0.3, s)", 8)), control);
    }
    // The transforms of a scope: quarter turns are exact, other angles
    // and a mirror with a non-uniform scale take the general path.
    row("place at", 256, solid(repeated("place(at = {1, 2, 3}) { }", 256)), control);
    row("box in place at", 256, solid(repeated("place(at = {1, 2, 3}) { box({0, 0, 0}, {2, 3, 4}, s) }", 256)), control);
    row("box in place yaw 90", 256, solid(repeated("place(yaw = 90.0) { box({0, 0, 0}, {2, 3, 4}, s) }", 256)), control);
    row("box in place general", 256,
        solid(repeated("place(yaw = 37.0, pitch = 11.0, scale = {1.5, 0.7, 1.2}, mirror_x = 1) { box({0, 0, 0}, {2, 3, 4}, s) }", 256)),
        control);
    // A skin call: the record of a texture already made, and new textures.
    row("skin call, same texture", 256, solid(repeated("t = tile.draw(1)", 256)), control);
    std::string fresh;
    for (int i = 0; i < 31; ++i) fresh += "    t = tile.draw(" + std::to_string(i + 2) + ")\n";
    row("skin call, new 8x8 texture", 31, solid(fresh), control);
    row("skin.roughness", 256, solid(repeated("t = skin.roughness(s, 0.5)", 256)), control);

    std::printf("-- milestone M3\n");
    row("tube, 16 sides, 12 segments", 32, solid(repeated("tube({0, 0, 0}, {0, 5, 0}, {5, 8, 0}, {9, 6, 0}, 1.0, 0.5, 16, 12, s)", 32)),
        control);
    row("sweep, 4 x 4 points", 128,
        solid(repeated("sweep([{0, 0, 0}, {0, 5, 0}, {4, 6, 1}, {8, 4, 2}], [{-1, -1}, {1, -1}, {1, 1}, {-1, 1}], s)", 128)), control);
    row("box in refine(1)", 256, solid(repeated("refine(1) { box({0, 0, 0}, {2, 3, 4}, s) }", 256)), control);
    const auto refined = solid(repeated("refine(4) { box({0, 0, 0}, {2, 3, 4}, s) }", 64));
    row("box in refine(4)", 64, refined, control);
    row("  then smooth(30)", 64, solid(repeated("smooth(30.0) { refine(4) { box({0, 0, 0}, {2, 3, 4}, s) } }", 64)), refined);
    row("  then bend(60)", 64, solid(repeated("bend(60.0) { refine(4) { box({0, 0, 0}, {2, 3, 4}, s) } }", 64)), refined);
    row("  then twist(60)", 64, solid(repeated("twist(60.0) { refine(4) { box({0, 0, 0}, {2, 3, 4}, s) } }", 64)), refined);
    row("  then taper(0.5)", 64, solid(repeated("taper(0.5) { refine(4) { box({0, 0, 0}, {2, 3, 4}, s) } }", 64)), refined);
    row("  then displace", 64,
        solid(repeated("displace(0.2, 0.7, 5) { refine(4) { box({0, 0, 0}, {2, 3, 4}, s) } }", 64)), refined);
    row("part call of a box", 128, solid(repeated("peg.render(1)", 128)), control);
    row("point3d write", 512, solid(repeated("p = v", 512)), control);
    row("box3d write", 256, solid(repeated("b = {v, {1, 1, 1}}", 256)), control);
    return 0;
}
