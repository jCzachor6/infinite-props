#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>

#include "ips/Imports.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ips/Types.hpp"
#include "support/semantic_support.hpp"

// Semantic analysis: name resolution, type checking, integer-only
// conditions, part-call arity and slot kinds, import-DAG resolution.

using namespace iprop::ips;

using namespace semantic_test;

namespace {

// A valid asset using the VM builtins (the rock sketch with stone_mass
// replaced by rect, which is a real VM primitive).
const std::string kValidRock = R"(
asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect collider

  draw {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    rect(-w * 0.10, 0, w * 0.87, h, palette)
    if (index(3) == 0) {
      rect(-w * 0.67, 2, w * 0.39, h * 0.43, shade(palette, -4))
    }
  }
})";

const std::string kHairSource = R"(
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  input f32   volume = 1.0
  output point top

  draw {
    top = {32, 4}
  }
})";

const std::string kPortraitSource = R"(
import hair from "hair.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  input  color hairColor = #33291f
  output point pupilCenter

  draw {
    rect(0, 0, 64, 64, skin)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    pupilCenter = hair.outputs.top
  }
})";

std::map<std::string, PartInfo> hairParts() {
    const Program hair = parse(kHairSource);
    return {{"hair", partInfoOf(hair.asset)}};
}

} // namespace

TEST_CASE("valid programs check cleanly") {
    CHECK_NOTHROW(checkAsset(parse(kValidRock), {}));
    CHECK_NOTHROW(checkAsset(parse(kPortraitSource), hairParts()));
}

TEST_CASE("part calls are validated against the part surface") {
    CHECK_NOTHROW(checkAsset(parse(kPortraitSource), hairParts()));

    // Wrong positional arity.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw { hair.draw(1, 2, at = {0, 0}) } }",
        hairParts(), "exactly three positional");
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw { hair.draw(1, 2, 3, 4, at = {0, 0}) } }",
        hairParts(), "exactly three positional");

    // Missing `at`.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw { hair.draw(1, 2, 3) } }",
        hairParts(), "requires an 'at");

    // Wrong `at` type.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw { hair.draw(1, 2, 3, at = 5) } }",
        hairParts(), "'at' must be a vec2");

    // Unknown uniform.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw { hair.draw(1, 2, 3, at = {0, 0}, bogus = 1) } }",
        hairParts(), "has no input 'bogus'");

    // Uniform of the wrong type.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw { hair.draw(1, 2, 3, at = {0, 0}, volume = #ffffff) } }",
        hairParts(), "must be f32, got color");

    // Unknown part.
    expectCheckError(
        "import nope from \"nope.ips\"\nasset t { size 8x8 "
        "draw { nope.draw(1, 2, 3, at = {0, 0}) } }",
        "was not provided");
}

TEST_CASE("part transform argument names cannot be declared as uniforms") {
    for (const std::string name : {"rot", "mirror_x", "mirror_y"}) {
        expectCheckError("asset t { size 8x8 input i32 " + name +
                             " = 0 draw { } }",
                         "reserved name");
    }
}

TEST_CASE("part transforms are fully checked before compilation") {
    const auto source = [](const std::string& transform) {
        return "import hair from \"hair.ips\"\nasset t { size 8x8 draw { "
               "hair.draw(1,2,3,at={0,0}," + transform + ") } }";
    };
    CHECK_NOTHROW(checkAsset(parse(source("rot=45+45,mirror_x=1,mirror_y=0")),hairParts()));
    for (const std::string value : {"45", "-90", "360"})
        expectCheckError(source("rot="+value),hairParts(),"rot transform must be");
    for (const std::string name : {"rot", "mirror_x", "mirror_y"}) {
        expectCheckError(source(name+"=v"),hairParts(),"constant integers");
        expectCheckError(source(name+"=0,"+name+"=0"),hairParts(),"duplicate");
        expectCheckError(source(name+"=0.0"),hairParts(),"must be an integer");
    }
    expectCheckError(source("mirror_x=2"),hairParts(),"mirror_x transform must be");
    expectCheckError(source("mirror_y=-1"),hairParts(),"mirror_y transform must be");
}

TEST_CASE("part output reads are validated") {
    CHECK_NOTHROW(checkAsset(parse(kPortraitSource), hairParts()));

    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 output point p "
        "draw { hair.draw(1, 2, 3, at = {0, 0}) p = hair.outputs.nope } }",
        hairParts(), "has no output 'nope'");
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 output rect r "
        "draw { hair.draw(1, 2, 3, at = {0, 0}) r = hair.outputs.top } }",
        hairParts(), "expects rect, got vec2");
    expectCheckError(
        "asset t { size 8x8 draw { x = bogus.outputs.p } }",
        "not an imported part");

    // List outputs cannot be read.
    const std::string listPart = R"(
asset lp {
  size 8x8
  output list point pts
  draw {
    pts = {1, 2}
  }
})";
    const Program listProgram = parse(listPart);
    const std::map<std::string, PartInfo> listParts = {
        {"lp", partInfoOf(listProgram.asset)}};
    expectCheckError(
        "import lp from \"lp.ips\"\nasset t { size 8x8 output point p "
        "draw { lp.draw(1, 2, 3, at = {0, 0}) p = lp.outputs.pts } }",
        listParts, "cannot read list output");
}

TEST_CASE("partInfoOf builds a part surface") {
    const Program hair = parse(kHairSource);
    const PartInfo info = partInfoOf(hair.asset);
    REQUIRE(info.inputs.size() == 2);
    REQUIRE(info.inputs[0].name == "color");
    REQUIRE(info.inputs[0].type == InputType::Color);
    REQUIRE(info.inputs[1].name == "volume");
    REQUIRE(info.inputs[1].type == InputType::F32);
    REQUIRE(info.outputs.size() == 1);
    REQUIRE(info.outputs[0].name == "top");
    REQUIRE(info.outputs[0].kind == OutputKind::Point);
    REQUIRE(!info.outputs[0].isList);
}

TEST_CASE("import cycles are rejected with the cycle path") {
    const std::map<std::string, std::string> files = {
        {"a.ips", "import b from \"b.ips\"\nasset a { size 8x8 draw { } }"},
        {"b.ips", "import a from \"a.ips\"\nasset b { size 8x8 draw { } }"},
    };
    const auto loader = [&](std::string_view path) {
        const auto found = files.find(std::string(path));
        if (found == files.end()) {
            throw std::runtime_error("no such file");
        }
        return found->second;
    };

    bool threw = false;
    try {
        (void)resolveImports("a.ips", loader);
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("import cycle") !=
                std::string::npos);
        REQUIRE(std::string(error.message).find("a.ips") != std::string::npos);
        REQUIRE(std::string(error.message).find("b.ips") != std::string::npos);
    }
    REQUIRE(threw);

    // A three-file cycle not passing through the entry.
    const std::map<std::string, std::string> three = {
        {"x.ips", "import y from \"y.ips\"\nasset x { size 8x8 draw { } }"},
        {"y.ips", "import z from \"z.ips\"\nasset y { size 8x8 draw { } }"},
        {"z.ips", "import y from \"y.ips\"\nasset z { size 8x8 draw { } }"},
    };
    const auto loader3 = [&](std::string_view path) {
        const auto found = three.find(std::string(path));
        if (found == three.end()) {
            throw std::runtime_error("no such file");
        }
        return found->second;
    };
    bool threw3 = false;
    try {
        (void)resolveImports("x.ips", loader3);
    } catch (const SourceError& error) {
        threw3 = true;
        REQUIRE(std::string(error.message).find("y.ips -> z.ips -> y.ips") !=
                std::string::npos);
    }
    REQUIRE(threw3);
}

TEST_CASE("import resolution returns dependencies first and reports bad files") {
    const std::map<std::string, std::string> files = {
        {"a.ips", "import b from \"b.ips\"\nimport c from \"c.ips\"\n"
                  "asset a { size 8x8 draw { } }"},
        {"b.ips", "import c from \"c.ips\"\nasset b { size 8x8 draw { } }"},
        {"c.ips", "asset c { size 8x8 draw { } }"},
    };
    const auto loader = [&](std::string_view path) {
        const auto found = files.find(std::string(path));
        if (found == files.end()) {
            throw std::runtime_error("no such file");
        }
        return found->second;
    };

    const auto order = resolveImports("a.ips", loader);
    REQUIRE(order == std::vector<std::string>{"c.ips", "b.ips", "a.ips"});

    const auto badLoader = [](std::string_view) -> std::string {
        throw std::runtime_error("boom");
    };
    bool threw = false;
    try {
        (void)resolveImports("missing.ips", badLoader);
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("cannot load") !=
                std::string::npos);
        REQUIRE(std::string(error.message).find("boom") != std::string::npos);
    }
    REQUIRE(threw);
}
