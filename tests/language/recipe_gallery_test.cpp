#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

TEST_CASE("recipe gallery compiles renders and retains canonical executable roundtrips") {
    const auto directory = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "examples/recipes";
    std::vector<std::filesystem::path> sources;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".ips") sources.push_back(entry.path());
    std::sort(sources.begin(), sources.end());
    REQUIRE(sources.size() == 24);
    for (const auto& source : sources) {
        INFO(source.filename().string());
        std::ifstream file(source);
        REQUIRE(file);
        const auto sourceText = std::string(std::istreambuf_iterator<char>(file), {});
        const auto program = ips::parse(sourceText);
        ips::checkAsset(program, {});
        const auto bytes = compileProgram(program, {});
        const auto formatted = ips::formatProgram(program);
        CHECK(sourceText == formatted);
        const auto roundtrip = ips::parse(formatted);
        CHECK(ips::formatProgram(roundtrip) == formatted);
        CHECK(stripDebug(bytes) == stripDebug(compileProgram(roundtrip, {})));
        const auto result = compiler_test::execute(bytes, 7);
        CHECK(result.canvas.width == 32);
        CHECK(result.canvas.height == 32);
        std::set<u32> colors;
        for (const auto color : result.canvas.pixels)
            colors.insert(static_cast<u32>(color.r) | (static_cast<u32>(color.g) << 8u) |
                          (static_cast<u32>(color.b) << 16u) | (static_cast<u32>(color.a) << 24u));
        CHECK(colors.size() >= 2);
        const auto repeated = compiler_test::execute(bytes, 7);
        CHECK(result.canvas.pixels == repeated.canvas.pixels);
    }
}
