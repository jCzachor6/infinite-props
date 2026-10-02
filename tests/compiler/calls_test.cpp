#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

TEST_CASE("resolved calls preserve argument evaluation order and the RNG stream") {
    const auto part = ips::parse(R"(asset child { size 8x8
      input f32 first = 1.0
      input f32 second = 2.0
      draw(v:i32,a:i32) { }
    })");
    const auto root = ips::parse(R"(import child from "child.ips"
      asset root { size 8x8 output list point trace palette p = [1.0]
        draw(v:i32,a:i32) {
          sink(note(1), note(2))
          lerp(note(3), note(4), note(5))
          round(note(6))
          poly(tint(), note(7), note(8), note(9), note(10), note(11), note(12))
          child.draw(note(15), note(16), note(17), at={note(18),note(19)},
                     second=note(14), first=note(13))
          ramp(p, note(20))
        }
        note(tag:i32) -> i32 { trace = {tag, unit()} return 0 }
        tint() -> color { trace = {70, unit()} return #000000 }
        sink(x:f32,y:f32) { trace = {30, unit()} }
      })");
    ips::checkAsset(part, {});
    ips::checkAsset(root, {{"child",partInfoOf(part.asset)}});
    const auto actual = execute(compileProgram(root, {{"child",&part}}));
    const std::vector<i32> order{1,2,30,3,4,5,6,7,8,9,10,11,12,70,13,14,15,16,17,18,19,20};
    std::string body;
    for (const auto tag : order) body += "trace = {" + std::to_string(tag) + ", unit()}\n";
    const auto reference = execute(compileChecked("asset t { size 8x8 output list point trace "
        "draw(v:i32,a:i32) {" + body + "} }"));
    REQUIRE(actual.outputs[0].points.size() == order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        INFO("effect " << i);
        CHECK(actual.outputs[0].points[i].x == reference.outputs[0].points[i].x);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[i].y) ==
              std::bit_cast<u32>(reference.outputs[0].points[i].y));
    }
}

