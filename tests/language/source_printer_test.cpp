#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

TEST_CASE("canonical source formatting normalizes literals names and typed functions") {
    const auto program = ips::parse(R"(asset t { size 8X8 variants 0x2
      input color c=#AbcD output list point trace palette p=[0X1,0x2]
      draw { rect(color=c,h=4,w=4,y=0,x=0) x=0.10 }
      helper(n:i32)->i32 { return n+0x1 }
      noop() { return; pixel(0,0,#fff) }
    })");
    const std::string expected = R"(asset t {
  size 8x8
  variants 2
  input color c = #aabbccdd
  output list point trace
  palette p = [1, 2]

  draw {
    rect(x = 0, y = 0, w = 4, h = 4, color = c)
    x = 0.1
  }

  helper(n: i32) -> i32 {
    return (n + 1)
  }

  noop() {
    return;
    pixel(0, 0, #ffffff)
  }
}
)";
    const auto formatted = ips::formatProgram(program);
    CHECK(formatted == expected);
    const auto reparsed = ips::parse(formatted);
    CHECK(ips::formatProgram(reparsed) == formatted);
    ips::checkAsset(program, {});
    ips::checkAsset(reparsed, {});
    CHECK(stripDebug(compileProgram(program, {})) == stripDebug(compileProgram(reparsed, {})));
}

TEST_CASE("source formatting preserves every control and expression branch") {
    const auto source = R"(import tile from "dir\\tile\"ż.ips"
      asset all { size 16x16 origin 1,2 variants 3 archetypes 4
        input u32 offset=0x10 input f32 opacity=0.5
        output rect r output circle c output list point trace
        palette colors=[#f00,#0f0]
        draw {
          q = {1,2} x=q.x y=q.y
          r = {1,2,3,4} c={1,2,3}
          for i in (v+1)..(v+4) { if (i==2) { continue } else if(i!=3) { break } }
          while ((x<y) && (x<=y) || (x>y) && (x>=y)) { x=x+1 }
          x=((1+2)*3-4)/5 y=(6%3)^(7&3)|(8<<1)>>(~2)
          x=-sqrt(x) flag=!false
          pixel(0,0,colors[v])
          tile.draw(seed,v,a, color=#fff,at=q,rot=1)
          return;
        }
        helper(p:vec2,c:color) -> vec2 { return p*2.0 }
      })";
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    CHECK(ips::parse(formatted).imports[0].path == "dir\\tile\"ż.ips");
    CHECK(formatted.find("at = q, color = #ffffff, rot = 1") != std::string::npos);
    CHECK(formatted.find("helper(p: vec2, c: color) -> vec2") != std::string::npos);
}

TEST_CASE("composed examples and backend patterns roundtrip to identical executable bytes") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const auto read = [&](const std::filesystem::path& path) {
        std::ifstream file(root / path, std::ios::binary);
        REQUIRE(file);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    for (const auto path : {"examples/portrait.ips", "examples/plant.ips",
            "backends/html5/test/fixtures/aliases.ips", "backends/html5/test/fixtures/random-sugar.ips",
            "backends/html5/test/fixtures/named-arguments.ips", "backends/html5/test/fixtures/direct-seed.ips"}) {
        INFO(path);
        auto original = ips::parse(read(path));
        auto roundtrip = ips::parse(ips::formatProgram(original));
        CHECK(ips::formatProgram(roundtrip) == ips::formatProgram(original));
        std::map<std::string, ips::Program> originals, formatted;
        std::map<std::string, const ips::Program*> originalParts, formattedParts;
        for (const auto& import : original.imports) {
            auto child = ips::parse(read(std::filesystem::path(path).parent_path() / import.path));
            formatted.emplace(import.alias, ips::parse(ips::formatProgram(child)));
            originals.emplace(import.alias, std::move(child));
            originalParts.emplace(import.alias, &originals.at(import.alias));
            formattedParts.emplace(import.alias, &formatted.at(import.alias));
        }
        CHECK(stripDebug(compileProgram(original, originalParts)) == stripDebug(compileProgram(roundtrip, formattedParts)));
    }
}
